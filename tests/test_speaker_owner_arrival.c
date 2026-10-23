// Exercise the production input API, real queue and actual input sender.
// Only network delivery and selected allocation/interleaving boundaries are replaced.
// Including this translation unit exposes only queue inspection, not a mock path.
#include <stdlib.h>
#include <stdatomic.h>
#include "../src/Limelight-internal.h"
static int testControlSend(unsigned char*, int, uint8_t, uint32_t, bool);
#define sendInputPacketOnControlStream testControlSend
static int inputTestOffer(PLINKED_BLOCKING_QUEUE queue, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry);
static int observeUnadmittedAlias;
static atomic_int pauseControllerOffer;
static PLT_EVENT offerPaused, releaseOffer;
static int sawUnadmittedAlias;
static int competeAfterArrival;
static int contenderResult;
#define LbqOfferQueueItem inputTestOffer
static int failAllocationAfter = -1;
static void* inputTestMalloc(size_t bytes) {
    if (failAllocationAfter == 0) { failAllocationAfter = -1; return NULL; }
    if (failAllocationAfter > 0) --failAllocationAfter;
    return malloc(bytes);
}
#define malloc inputTestMalloc
#include "../src/InputStream.c"
#undef malloc
#undef sendInputPacketOnControlStream
#undef LbqOfferQueueItem
static int inputTestOffer(PLINKED_BLOCKING_QUEUE queue, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry) {
    if (observeUnadmittedAlias && queue == &packetQueue) {
        PPACKET_HOLDER holder = data;
        if (LE32(holder->packet.header.magic) == MULTI_CONTROLLER_MAGIC_GEN5)
            sawUnadmittedAlias |= currentQueuedControllerPacket[3] == holder;
        if (LE32(holder->packet.header.magic) == MULTI_CONTROLLER_MAGIC_GEN5 &&
            atomic_exchange(&pauseControllerOffer, 0)) {
            PltSetEvent(&offerPaused);
            PltWaitForEvent(&releaseOffer);
        }
    }
    int result = LbqOfferQueueItem(queue, data, entry);
    if (competeAfterArrival && queue == &packetQueue && result == LBQ_SUCCESS &&
        LE32(((PPACKET_HOLDER)data)->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC) {
        competeAfterArrival = 0;
        contenderResult = LiSendKeyboardEvent(0x41, KEY_ACTION_DOWN, 0);
    }
    return result;
}
#include <stdio.h>

static void speaker(uint16_t slot, uint64_t owner, uint32_t generation,
                    uint32_t sequence, uint64_t position, uint16_t flags,
                    const uint8_t* pcm, uint16_t frames) {
    (void)slot; (void)owner; (void)generation; (void)sequence;
    (void)position; (void)flags; (void)pcm; (void)frames;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)

static PPACKET_HOLDER take(void) {
    PPACKET_HOLDER packet = NULL;
    LbqPollQueueElement(&packetQueue, (void**)&packet);
    return packet;
}
typedef struct { int slot; int failed; } producer_context;
static void concurrentArrivals(void* opaque) {
    producer_context* c = opaque;
    for (int i = 0; i < 40; ++i) {
        int result = LiSendControllerSpeakerArrivalEvent(c->slot, 15, LI_CTYPE_PS,
            i, LI_CCAP_PS_SPEAKER_PCM, (c->slot + 1) * 100 + i + 1);
        if (result != LBQ_SUCCESS && result != LBQ_BOUND_EXCEEDED) c->failed = 1;
    }
}
typedef struct { PLT_EVENT start; int result; } arrival_race;
static void arrivalDuringShutdown(void* opaque) {
    arrival_race* race = opaque;
    PltWaitForEvent(&race->start);
    race->result = LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0,
                        LI_CCAP_PS_SPEAKER_PCM, 9191);
}

static void clearFreeList(void) {
    void* holder;
    while (LbqPollQueueElement(&packetHolderFreeList, &holder) == LBQ_SUCCESS) free(holder);
}

typedef struct { PLT_EVENT started; int result; short axis; } controller_race;
static void controllerUpdate(void* opaque) {
    controller_race* race = opaque;
    PltSetEvent(&race->started);
    race->result = LiSendMultiControllerEvent(3, 8, 0, 0, 0, race->axis, 0, 0, 0);
}
static int testControllerAdmission(void) {
    controller_race first = {.axis = 11}, second = {.axis = 22};
    PLT_THREAD a, b;
    CHECK(PltCreateEvent(&offerPaused) == 0 && PltCreateEvent(&releaseOffer) == 0);
    CHECK(PltCreateEvent(&first.started) == 0 && PltCreateEvent(&second.started) == 0);
    for (int i = 0; i < 150; ++i) {
        PPACKET_HOLDER filler = allocatePacketHolder(0);
        CHECK(filler && LbqOfferQueueItem(&packetQueue, filler, &filler->entry) == LBQ_SUCCESS);
    }
    observeUnadmittedAlias = 1;
    atomic_store(&pauseControllerOffer, 1);
    CHECK(PltCreateThread("mc-first", controllerUpdate, &first, &a) == 0);
    PltWaitForEvent(&offerPaused);
    CHECK(PltCreateThread("mc-overlap", controllerUpdate, &second, &b) == 0);
    PltWaitForEvent(&second.started);
    // The deterministic assertion is made at the actual admission boundary.
    // This delay also exercises the second caller overlapping the paused offer.
    PltSleepMs(10);
    PltSetEvent(&releaseOffer);
    PltJoinThread(&a); PltJoinThread(&b);
    if (sawUnadmittedAlias) fprintf(stderr, "Overlapping rejected offers returned %d and %d\n", first.result, second.result);
    CHECK(!sawUnadmittedAlias);
    CHECK(first.result == LBQ_BOUND_EXCEEDED && second.result == LBQ_BOUND_EXCEEDED);
    CHECK(currentQueuedControllerPacket[3] == NULL);
    PPACKET_HOLDER packet;
    while ((packet = take()) != NULL) free(packet);
    // Successful admission remains batchable, with the latest axis retained.
    CHECK(LiSendMultiControllerEvent(3, 8, 0, 0, 0, 11, 0, 0, 0) == LBQ_SUCCESS);
    packet = currentQueuedControllerPacket[3];
    CHECK(packet && LbqGetItemCount(&packetQueue) == 1);
    CHECK(LiSendMultiControllerEvent(3, 8, 0, 0, 0, 22, 0, 0, 0) == LBQ_SUCCESS);
    CHECK(currentQueuedControllerPacket[3] == packet && LbqGetItemCount(&packetQueue) == 1);
    CHECK(LE16(packet->packet.multiController.leftStickX) == 22);
    for (int i = 0; i < 149; ++i) {
        PPACKET_HOLDER filler = allocatePacketHolder(0);
        CHECK(filler && LbqOfferQueueItem(&packetQueue, filler, &filler->entry) == LBQ_SUCCESS);
    }
    // A rejected new button batch must not erase an older admitted alias.
    CHECK(LiSendMultiControllerEvent(3, 8, 1, 0, 0, 99, 0, 0, 0) == LBQ_BOUND_EXCEEDED);
    CHECK(currentQueuedControllerPacket[3] == packet);
    CHECK(LiSendMultiControllerEvent(3, 8, 0, 0, 0, 33, 0, 0, 0) == LBQ_SUCCESS);
    CHECK(LE16(packet->packet.multiController.leftStickX) == 33);
    CHECK(take() == packet);
    currentQueuedControllerPacket[3] = NULL;
    free(packet);
    while ((packet = take()) != NULL) free(packet);
    PltCloseEvent(&offerPaused); PltCloseEvent(&releaseOffer);
    PltCloseEvent(&first.started); PltCloseEvent(&second.started);
    return 0;
}


typedef struct { unsigned char bytes[128]; int length; uint8_t channel; uint32_t flags; } wire_record;
static wire_record sent[512];
static int sentCount, failSend, termination;
static int testControlSend(unsigned char* data, int length, uint8_t channel, uint32_t flags, bool moreData) {
    (void)moreData;
    if (sentCount >= 512 || length > 128 || length < 8) abort();
    wire_record* r = &sent[sentCount++];
    memcpy(r->bytes, data, length); r->length = length; r->channel = channel; r->flags = flags;
    return failSend == sentCount ? -55 : 0;
}
static void terminated(int error) { termination = error; }
static int startCase(void) {
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 431; AppVersionQuad[3] = -1;
    CHECK(initializeInputStream() == 0);
    initialized = true; encryptedControlStream = true;
    ControllerSpeakerPcmCallback = speaker;
    ListenerCallbacks.connectionTerminated = terminated;
    sentCount = failSend = termination = contenderResult = competeAfterArrival = 0;
    return 0;
}
static void endCase(void) {
    initialized = false;
    LbqSignalQueueShutdown(&packetQueue);
    LbqSignalQueueShutdown(&packetHolderFreeList);
    destroyInputStream();
}
static int sendQueued(void) {
    LbqSignalQueueDrain(&packetQueue);
    CHECK(PltCreateThread("actual-input-sender", inputSendThreadProc, NULL, &inputSendThread) == 0);
    PltJoinThread(&inputSendThread);
    return 0;
}
static uint32_t magic(int index) { return MlSpeakerRead32(sent[index].bytes + 4); }
static int checkPrefix(uint64_t owner) {
    CHECK(sentCount >= 2 && magic(0) == ML_SPEAKER_BIND_MAGIC && magic(1) == SS_CONTROLLER_ARRIVAL_MAGIC);
    CHECK(sent[0].length == ML_SPEAKER_BIND_SIZE && MlSpeakerRead64(sent[0].bytes + 12) == owner);
    CHECK(sent[0].channel == CTRL_CHANNEL_GAMEPAD_BASE + 3 && sent[1].channel == sent[0].channel);
    CHECK(sent[0].flags == ENET_PACKET_FLAG_RELIABLE && sent[1].flags == ENET_PACKET_FLAG_RELIABLE);
    return 0;
}
int main(void) {
    CHECK(startCase() == 0);
    CHECK(testControllerAdmission() == 0);
    endCase();
    // Historical Sunshine v0.20.0 advertises 7.1.431.-1 but ignores arrival;
    // its legacy MC fallback must retain baseline capacity under pressure.
    for (int occupied = 147; occupied <= 150; ++occupied) {
        CHECK(startCase() == 0);
        for (int i = 0; i < occupied; ++i) {
            PPACKET_HOLDER filler = allocatePacketHolder(0);
            CHECK(filler && LbqOfferQueueItem(&packetQueue, filler, &filler->entry) == LBQ_SUCCESS);
        }
        int result = LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, UINT64_MAX);
        CHECK(result == (occupied <= 148 ? LBQ_SUCCESS : LBQ_BOUND_EXCEEDED));
        CHECK(LbqGetItemCount(&packetQueue) == (occupied == 147 ? 149 : 150));
        for (int i = 0; i < occupied; ++i) free(take());
        CHECK(sendQueued() == 0);
        if (occupied < 150) CHECK(checkPrefix(UINT64_MAX) == 0);
        CHECK(sentCount == (occupied <= 148 ? 3 : occupied == 149 ? 2 : 0));
        if (occupied <= 148) CHECK(magic(2) == MULTI_CONTROLLER_MAGIC_GEN5);
        CHECK(currentQueuedControllerPacket[3] == NULL);
        endCase();
    }
    // Exact same schedule as baseline: arrival admission, ONE keyboard offer,
    // then MC. All three queue items must fit the three remaining slots.
    CHECK(startCase() == 0);
    for (int i = 0; i < 147; ++i) {
        PPACKET_HOLDER filler = allocatePacketHolder(0);
        CHECK(filler && LbqOfferQueueItem(&packetQueue, filler, &filler->entry) == LBQ_SUCCESS);
    }
    competeAfterArrival = 1;
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, 9191) == LBQ_SUCCESS);
    CHECK(contenderResult == LBQ_SUCCESS && LbqGetItemCount(&packetQueue) == 150);
    for (int i = 0; i < 147; ++i) free(take());
    CHECK(sendQueued() == 0 && sentCount == 4 && checkPrefix(9191) == 0);
    CHECK(magic(3) == MULTI_CONTROLLER_MAGIC_GEN5);
    endCase();
    // No optional binding allocation exists. Arrival and MC failures retain
    // baseline behavior, and a rejected arrival never emits a binding.
    for (int failure = 0; failure < 2; ++failure) {
        CHECK(startCase() == 0);
        clearFreeList(); failAllocationAfter = failure;
        CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, 77) == -1);
        failAllocationAfter = -1;
        CHECK(sendQueued() == 0 && sentCount == (failure == 0 ? 0 : 2));
        if (failure) CHECK(checkPrefix(77) == 0);
        endCase();
    }
    // Real sender fails closed if either ordered wire operation fails.
    for (int failure = 1; failure <= 2; ++failure) {
        CHECK(startCase() == 0);
        CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, 88) == 0);
        failSend = failure;
        CHECK(sendQueued() == 0 && sentCount == failure && termination == -55);
        endCase();
    }
    for (int legacy = 0; legacy < 4; ++legacy) {
        CHECK(startCase() == 0);
        uint8_t type = LI_CTYPE_PS;
        if (legacy == 0) ControllerSpeakerPcmCallback = NULL;
        if (legacy == 1) AppVersionQuad[3] = 0;
        if (legacy == 2) type = 4; // Steam's type-scoped 0x4000 remains intact.
        int result = legacy == 3 ? LiSendControllerArrivalEvent(3, 8, type, 0, LI_CCAP_PS_SPEAKER_PCM) :
            LiSendControllerSpeakerArrivalEvent(3, 8, type, 0, LI_CCAP_PS_SPEAKER_PCM, 1);
        CHECK(result == 0 && sendQueued() == 0);
        CHECK(sentCount == (legacy == 1 ? 1 : 2));
        CHECK(magic(0) != ML_SPEAKER_BIND_MAGIC);
        endCase();
    }
    CHECK(startCase() == 0);
    PLT_THREAD producers[4]; producer_context contexts[4];
    for (int i = 0; i < 4; ++i) {
        contexts[i] = (producer_context){i, 0};
        CHECK(PltCreateThread("arrivals", concurrentArrivals, &contexts[i], &producers[i]) == 0);
    }
    for (int i = 0; i < 4; ++i) { PltJoinThread(&producers[i]); CHECK(!contexts[i].failed); }
    CHECK(LbqGetItemCount(&packetQueue) <= 150 && sendQueued() == 0);
    for (int i = 0; i < sentCount; ++i) if (magic(i) == ML_SPEAKER_BIND_MAGIC) {
        CHECK(i + 1 < sentCount && magic(i + 1) == SS_CONTROLLER_ARRIVAL_MAGIC);
        CHECK(sent[i].channel == sent[i + 1].channel);
    }
    endCase();
    for (int i = 0; i < 30; ++i) {
        CHECK(startCase() == 0);
        arrival_race race; PLT_THREAD thread;
        CHECK(PltCreateEvent(&race.start) == 0);
        CHECK(PltCreateThread("shutdown", arrivalDuringShutdown, &race, &thread) == 0);
        PltSetEvent(&race.start); LbqSignalQueueShutdown(&packetQueue); PltJoinThread(&thread);
        CHECK(race.result == LBQ_SUCCESS || race.result == LBQ_INTERRUPTED);
        PltCloseEvent(&race.start); endCase();
    }
    // Undelivered compound arrival/MC holders are safely discarded on teardown.
    CHECK(startCase() == 0);
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, 99) == 0);
    endCase();
    CHECK(startCase() == 0 && currentQueuedControllerPacket[3] == NULL);
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, 100) == 0);
    CHECK(sendQueued() == 0 && sentCount == 3 && checkPrefix(100) == 0);
    endCase();
    puts("Compound speaker arrival: baseline queue capacity, ordered actual sender, lifetime and failure paths passed");
    return 0;
}
