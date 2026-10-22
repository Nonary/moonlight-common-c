// Exercise the production input API and real queue without starting its sender.
// Including this translation unit exposes only queue inspection, not a mock path.
#include "../src/InputStream.c"
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
int main(void) {
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
    initialized = false;
    LbqSignalQueueShutdown(&packetQueue);
    LbqSignalQueueShutdown(&packetHolderFreeList);
    destroyInputStream();
    puts("Speaker binding ordering, width, reliable channel and legacy arrival passed");
    return 0;
}
