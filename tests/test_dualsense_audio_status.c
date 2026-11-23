// Exercise LiSendDualSenseAudioStatusEvent through the real InputStream queue and sender.
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "../src/Limelight-internal.h"

static int testControlSend(unsigned char*, int, uint8_t, uint32_t, bool);
#define sendInputPacketOnControlStream testControlSend
#include "../src/InputStream.c"
#undef sendInputPacketOnControlStream

typedef struct {
    unsigned char bytes[64];
    int length;
    uint8_t channel;
    uint32_t flags;
} wire_record;

static wire_record sent[16];
static int sentCount = 0;

static int testControlSend(unsigned char* data, int length, uint8_t channel, uint32_t flags, bool moreData) {
    (void)moreData;
    if (sentCount >= 16 || length < 0 || length > 64) {
        return -1;
    }
    memcpy(sent[sentCount].bytes, data, (size_t)length);
    sent[sentCount].length = length;
    sent[sentCount].channel = channel;
    sent[sentCount].flags = flags;
    sentCount++;
    return 0;
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    // Uninitialized returns -2.
    initialized = false;
    CHECK(LiSendDualSenseAudioStatusEvent(0, ML_DS_AUDIO_HEADPHONES_CONNECTED) == -2);

    // Initialize input stream with Sunshine server version.
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 431; AppVersionQuad[3] = -1;
    CHECK(initializeInputStream() == 0);
    initialized = true;
    encryptedControlStream = true;
    sentCount = 0;

    // Non-Sunshine host returns LI_ERR_UNSUPPORTED.
    AppVersionQuad[3] = 0;
    CHECK(LiSendDualSenseAudioStatusEvent(0, ML_DS_AUDIO_HEADPHONES_CONNECTED) == LI_ERR_UNSUPPORTED);
    AppVersionQuad[3] = -1;

    // Invalid controller index or unknown flags return -1.
    CHECK(LiSendDualSenseAudioStatusEvent(MAX_GAMEPADS, ML_DS_AUDIO_HEADPHONES_CONNECTED) == -1);
    CHECK(LiSendDualSenseAudioStatusEvent(0, 0x08) == -1);

    // Valid event queues and transmits 12-byte packet on the controller's reliable channel.
    uint8_t flags = ML_DS_AUDIO_HEADPHONES_CONNECTED | ML_DS_AUDIO_HEADSET_MIC_CONNECTED;
    CHECK(LiSendDualSenseAudioStatusEvent(2, flags) == 0);

    LbqSignalQueueDrain(&packetQueue);
    CHECK(PltCreateThread("audio-status-sender", inputSendThreadProc, NULL, &inputSendThread) == 0);
    PltJoinThread(&inputSendThread);

    CHECK(sentCount == 1);
    CHECK(sent[0].length == ML_DUALSENSE_AUDIO_STATUS_SIZE);
    CHECK(sent[0].channel == CTRL_CHANNEL_GAMEPAD_BASE + 2);
    CHECK(sent[0].flags == ENET_PACKET_FLAG_RELIABLE);
    // Size = BE32(8)
    CHECK(sent[0].bytes[0] == 0 && sent[0].bytes[1] == 0 && sent[0].bytes[2] == 0 && sent[0].bytes[3] == 8);
    // Magic = LE32(0x5500000A)
    uint32_t magic = (uint32_t)sent[0].bytes[4] |
                     ((uint32_t)sent[0].bytes[5] << 8) |
                     ((uint32_t)sent[0].bytes[6] << 16) |
                     ((uint32_t)sent[0].bytes[7] << 24);
    CHECK(magic == ML_DUALSENSE_AUDIO_STATUS_MAGIC);
    CHECK(sent[0].bytes[8] == 2);
    CHECK(sent[0].bytes[9] == flags);
    CHECK(sent[0].bytes[10] == 0 && sent[0].bytes[11] == 0);

    initialized = false;
    LbqSignalQueueShutdown(&packetQueue);
    LbqSignalQueueShutdown(&packetHolderFreeList);
    destroyInputStream();

    printf("PASS: LiSendDualSenseAudioStatusEvent validation, queue, and wire encoding\n");
    return 0;
}
