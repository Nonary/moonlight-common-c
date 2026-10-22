// Exercise the production input API and real queue without starting its sender.
// Including this translation unit exposes only queue inspection, not a mock path.
#include <stdlib.h>
static int failAllocationAfter = -1;
static void* inputTestMalloc(size_t bytes) {
    if (failAllocationAfter == 0) { failAllocationAfter = -1; return NULL; }
    if (failAllocationAfter > 0) --failAllocationAfter;
    return malloc(bytes);
}
#define malloc inputTestMalloc
#include "../src/InputStream.c"
#undef malloc
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

typedef struct {
    LINKED_BLOCKING_QUEUE queue;
    LINKED_BLOCKING_QUEUE_ENTRY entries[2];
    PLT_EVENT start;
    int result;
} offer_race;
static void offerPair(void* opaque) {
    offer_race* race = opaque;
    void* data[] = {&race->entries[0], &race->entries[1]};
    PLINKED_BLOCKING_QUEUE_ENTRY entries[] = {&race->entries[0], &race->entries[1]};
    PltWaitForEvent(&race->start);
    race->result = LbqOfferQueueItems(&race->queue, data, entries, 2);
}
static int listSize(PLINKED_BLOCKING_QUEUE_ENTRY entry) {
    int count = 0;
    while (entry) { ++count; entry = entry->flink; }
    return count;
}
static int testOfferRaces(void) {
    for (int mode = 0; mode < 3; ++mode) for (int i = 0; i < 30; ++i) {
        offer_race race;
        PLT_THREAD thread;
        CHECK(LbqInitializeLinkedBlockingQueue(&race.queue, 2) == 0);
        CHECK(PltCreateEvent(&race.start) == 0);
        CHECK(PltCreateThread("speaker-pair", offerPair, &race, &thread) == 0);
        PltSetEvent(&race.start);
        int removed = 0;
        if (mode == 0) LbqSignalQueueShutdown(&race.queue);
        else if (mode == 1) LbqSignalQueueDrain(&race.queue);
        else removed = listSize(LbqFlushQueueItems(&race.queue));
        PltJoinThread(&thread);
        int remaining = listSize(LbqFlushQueueItems(&race.queue));
        CHECK((removed == 0 || removed == 2) && (remaining == 0 || remaining == 2));
        CHECK(removed + remaining == (race.result == LBQ_SUCCESS ? 2 : 0));
        CHECK(race.result == LBQ_SUCCESS || (mode != 2 && race.result == LBQ_INTERRUPTED));
        LbqSignalQueueShutdown(&race.queue);
        CHECK(!LbqDestroyLinkedBlockingQueue(&race.queue));
        PltCloseEvent(&race.start);
    }
    return 0;
}

int main(void) {
    CHECK(testOfferRaces() == 0);
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 500; AppVersionQuad[3] = -1;
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, 1) == -2);
    CHECK(initializeInputStream() == 0);
    initialized = true;
    ControllerSpeakerPcmCallback = speaker;
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0,
          LI_CCAP_PS_SPEAKER_PCM, UINT64_MAX) == 0);
    PPACKET_HOLDER binding = take();
    PPACKET_HOLDER arrival = take();
    PPACKET_HOLDER fallback = take();
    CHECK(binding && arrival && fallback && !take());
    CHECK(binding->channelId == CTRL_CHANNEL_GAMEPAD_BASE + 3 &&
          arrival->channelId == binding->channelId);
    CHECK(binding->enetPacketFlags == ENET_PACKET_FLAG_RELIABLE &&
          arrival->enetPacketFlags == ENET_PACKET_FLAG_RELIABLE);
    CHECK(MlSpeakerRead32(binding->packet.controllerSpeakerBinding + 4) == ML_SPEAKER_BIND_MAGIC);
    CHECK(MlSpeakerRead64(binding->packet.controllerSpeakerBinding + 12) == UINT64_MAX);
    CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
    CHECK(BE32(arrival->packet.header.size) == sizeof(SS_CONTROLLER_ARRIVAL_PACKET) - 4);
    CHECK(LE16(arrival->packet.controllerArrival.capabilities) & LI_CCAP_PS_SPEAKER_PCM);
    free(binding); free(arrival); free(fallback);
    memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));

    // The legacy API still emits exactly the old arrival and MC fallback.
    CHECK(LiSendControllerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_RUMBLE) == 0);
    arrival = take(); fallback = take();
    CHECK(arrival && fallback && !take());
    CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
    free(arrival); free(fallback);
    memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));

    // At the queue boundary, ordinary controller arrival takes priority over
    // optional speaker binding. No binding may be left without its arrival.
    for (int occupied = 147; occupied <= 150; ++occupied) {
        for (int i = 0; i < occupied; ++i) {
            PPACKET_HOLDER filler = allocatePacketHolder(0);
            CHECK(filler);
            CHECK(LbqOfferQueueItem(&packetQueue, filler, &filler->entry) == LBQ_SUCCESS);
        }
        int result = LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0,
                        LI_CCAP_PS_SPEAKER_PCM, 100 + occupied);
        CHECK(result == (occupied == 147 ? LBQ_SUCCESS : LBQ_BOUND_EXCEEDED));
        CHECK(LbqGetItemCount(&packetQueue) == 150);
        for (int i = 0; i < occupied; ++i) free(take());
        if (occupied <= 148) {
            binding = take(); arrival = take();
            CHECK(binding && arrival);
            CHECK(MlSpeakerRead32(binding->packet.controllerSpeakerBinding + 4) == ML_SPEAKER_BIND_MAGIC);
            CHECK(MlSpeakerRead64(binding->packet.controllerSpeakerBinding + 12) == 100u + occupied);
            CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
            CHECK(LE16(arrival->packet.controllerArrival.capabilities) & LI_CCAP_PS_SPEAKER_PCM);
            free(binding); free(arrival);
            if (occupied == 147) free(take());
        } else if (occupied == 149) {
            arrival = take();
            CHECK(arrival && LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
            CHECK(!(LE16(arrival->packet.controllerArrival.capabilities) & LI_CCAP_PS_SPEAKER_PCM));
            free(arrival);
        }
        CHECK(!take());
        if (occupied >= 148) CHECK(currentQueuedControllerPacket[3] == NULL);
        memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));
        // Capacity recovery must not overwrite a new arrival via a cached,
        // rejected MC holder returned to the allocation free list.
        CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0,
              LI_CCAP_PS_SPEAKER_PCM, 777) == LBQ_SUCCESS);
        binding = take(); arrival = take(); fallback = take();
        CHECK(binding && arrival && fallback && !take());
        CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
        CHECK(MlSpeakerRead64(binding->packet.controllerSpeakerBinding + 12) == 777);
        free(binding); free(arrival); free(fallback);
        memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));
    }

    // Reset/reconnect after queue pressure and undelivered packets cannot leak
    // old bindings or freed MC batching aliases into the next connection.
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0,
          LI_CCAP_PS_SPEAKER_PCM, 998) == LBQ_SUCCESS);
    CHECK(currentQueuedControllerPacket[3] != NULL);
    initialized = false;
    LbqSignalQueueShutdown(&packetQueue);
    LbqSignalQueueShutdown(&packetHolderFreeList);
    destroyInputStream();
    CHECK(initializeInputStream() == 0);
    initialized = true;
    CHECK(currentQueuedControllerPacket[3] == NULL);
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0,
          LI_CCAP_PS_SPEAKER_PCM, 999) == LBQ_SUCCESS);
    binding = take(); arrival = take(); fallback = take();
    CHECK(binding && arrival && fallback && !take());
    CHECK(MlSpeakerRead64(binding->packet.controllerSpeakerBinding + 12) == 999);
    free(binding); free(arrival); free(fallback);
    memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));

    // Fail each allocation boundary in the actual input translation unit.
    // Arrival failure publishes nothing; binding failure keeps ordinary input;
    // MC fallback failure preserves the already published complete pair.
    for (int failure = 0; failure < 3; ++failure) {
        clearFreeList();
        failAllocationAfter = failure;
        int result = LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0,
                     LI_CCAP_PS_SPEAKER_PCM, 1234);
        failAllocationAfter = -1;
        CHECK(result == (failure == 1 ? LBQ_SUCCESS : -1));
        if (failure == 1) {
            arrival = take(); fallback = take();
            CHECK(arrival && fallback);
            CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
            CHECK(!(LE16(arrival->packet.controllerArrival.capabilities) & LI_CCAP_PS_SPEAKER_PCM));
            free(arrival); free(fallback);
        } else if (failure == 2) {
            binding = take(); arrival = take();
            CHECK(binding && arrival);
            CHECK(MlSpeakerRead32(binding->packet.controllerSpeakerBinding + 4) == ML_SPEAKER_BIND_MAGIC);
            CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
            free(binding); free(arrival);
        }
        CHECK(!take());
        memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));
    }

    // Concurrent real API producers cannot split a pair, duplicate an arrival,
    // or attach a binding to another producer's announcement.
    PLT_THREAD producers[4];
    producer_context contexts[4] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    for (int i = 0; i < 4; ++i)
        CHECK(PltCreateThread("speaker-producer", concurrentArrivals, &contexts[i], &producers[i]) == 0);
    for (int i = 0; i < 4; ++i) {
        PltJoinThread(&producers[i]);
        CHECK(!contexts[i].failed);
    }
    bool seen[4][40] = {{false}};
    while ((arrival = take()) != NULL) {
        uint64_t owner = 0;
        if (LE32(arrival->packet.header.magic) == ML_SPEAKER_BIND_MAGIC) {
            binding = arrival;
            owner = MlSpeakerRead64(binding->packet.controllerSpeakerBinding + 12);
            arrival = take();
            CHECK(arrival && arrival->channelId == binding->channelId);
            CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
            free(binding);
        }
        if (LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC) {
            unsigned slot = arrival->packet.controllerArrival.controllerNumber;
            unsigned serial = LE32(arrival->packet.controllerArrival.supportedButtonFlags);
            CHECK(slot < 4 && serial < 40 && !seen[slot][serial]);
            seen[slot][serial] = true;
            CHECK(owner == ((LE16(arrival->packet.controllerArrival.capabilities) & LI_CCAP_PS_SPEAKER_PCM)
                            ? (slot + 1) * 100 + serial + 1 : 0));
        }
        free(arrival);
    }
    memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));

    // Steam uses the same numerical capability bit with a different meaning.
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, 4, 0, 0x4000, 1234) == LBQ_SUCCESS);
    arrival = take(); fallback = take();
    CHECK(arrival && fallback && !take());
    CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
    CHECK(LE16(arrival->packet.controllerArrival.capabilities) & 0x4000);
    free(arrival); free(fallback);
    memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));

    ControllerSpeakerPcmCallback = NULL;
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, 42) == 0);
    arrival = take(); fallback = take();
    CHECK(arrival && fallback && !take());
    CHECK(!(LE16(arrival->packet.controllerArrival.capabilities) & LI_CCAP_PS_SPEAKER_PCM));
    free(arrival); free(fallback);
    memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));
    AppVersionQuad[3] = 0; // Non-Sunshine host retains only ordinary MC input.
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0, LI_CCAP_PS_SPEAKER_PCM, 42) == 0);
    fallback = take();
    CHECK(fallback && !take());
    free(fallback);
    memset(currentQueuedControllerPacket, 0, sizeof(currentQueuedControllerPacket));
    AppVersionQuad[3] = -1;
    ControllerSpeakerPcmCallback = speaker;
    LbqSignalQueueShutdown(&packetQueue);
    CHECK(LiSendControllerSpeakerArrivalEvent(3, 8, LI_CTYPE_PS, 0,
          LI_CCAP_PS_SPEAKER_PCM, 8888) == LBQ_INTERRUPTED);
    CHECK(LbqGetItemCount(&packetQueue) == 0);
    initialized = false;
    LbqSignalQueueShutdown(&packetHolderFreeList);
    destroyInputStream();
    // Shutdown racing the actual API may accept a complete pair (and possibly
    // MC fallback), or interrupt it entirely, but never leaves a lone binding.
    for (int i = 0; i < 30; ++i) {
        arrival_race race;
        PLT_THREAD thread;
        CHECK(initializeInputStream() == 0);
        initialized = true;
        CHECK(PltCreateEvent(&race.start) == 0);
        CHECK(PltCreateThread("speaker-shutdown", arrivalDuringShutdown, &race, &thread) == 0);
        PltSetEvent(&race.start);
        LbqSignalQueueShutdown(&packetQueue);
        PltJoinThread(&thread);
        CHECK(race.result == LBQ_SUCCESS || race.result == LBQ_INTERRUPTED);
        PLINKED_BLOCKING_QUEUE_ENTRY list = LbqFlushQueueItems(&packetQueue);
        int count = listSize(list);
        CHECK(count == 0 || count == 2 || count == 3);
        if (list) {
            binding = list->data;
            arrival = list->flink->data;
            CHECK(MlSpeakerRead32(binding->packet.controllerSpeakerBinding + 4) == ML_SPEAKER_BIND_MAGIC);
            CHECK(LE32(arrival->packet.header.magic) == SS_CONTROLLER_ARRIVAL_MAGIC);
        }
        while (list) {
            PLINKED_BLOCKING_QUEUE_ENTRY next = list->flink;
            free(list->data);
            list = next;
        }
        initialized = false;
        LbqSignalQueueShutdown(&packetHolderFreeList);
        destroyInputStream();
        PltCloseEvent(&race.start);
    }
    puts("Speaker binding ordering, width, reliable channel and legacy arrival passed");
    return 0;
}
