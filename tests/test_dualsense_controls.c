// Regression test for DualSense extended output controls (0x5506) and audio status (0x5500000A):
// wire build/parse validation, truncation rejection, opt-in callback gate, and in-place coalescing.
#include "../src/ControlStream.c"

#include <stdio.h>
#include <string.h>

static int callbackInvocations = 0;
static void dummyDualSenseCallback(uint16_t controllerNumber, uint16_t validMask,
                                   uint8_t playerLeds, uint8_t ledBrightness, uint8_t micLed,
                                   uint8_t speakerVolume, uint8_t headphoneVolume) {
    (void)controllerNumber;
    (void)validMask;
    (void)playerLeds;
    (void)ledBrightness;
    (void)micLed;
    (void)speakerVolume;
    (void)headphoneVolume;
    callbackInvocations++;
}

static int fail(const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void) {
    ML_DUALSENSE_OUTPUT in = {0};
    ML_DUALSENSE_OUTPUT parsed = {0};
    uint8_t payload[ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE];
    uint8_t statusPacket[ML_DUALSENSE_AUDIO_STATUS_SIZE];

    in.controllerNumber = 2;
    in.validMask = ML_DS_OUT_ALL_MASK;
    in.playerLeds = 0x15;
    in.ledBrightness = 2;
    in.micLed = 1;
    in.speakerVolume = 85;
    in.headphoneVolume = 110;

    if (!MlDualSenseOutputBuild(payload, sizeof(payload), &in)) {
        return fail("MlDualSenseOutputBuild rejected valid payload");
    }
    if (!MlDualSenseOutputParse(payload, sizeof(payload), &parsed)) {
        return fail("MlDualSenseOutputParse rejected valid payload");
    }
    if (parsed.controllerNumber != 2 || parsed.validMask != ML_DS_OUT_ALL_MASK ||
            parsed.playerLeds != 0x15 || parsed.ledBrightness != 2 ||
            parsed.micLed != 1 || parsed.speakerVolume != 85 || parsed.headphoneVolume != 110) {
        return fail("MlDualSenseOutputParse round-trip mismatch");
    }

    // Reject wrong version, unknown validMask bit, out-of-range fields, bad size.
    uint8_t bad[ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE];
    memcpy(bad, payload, sizeof(bad));
    bad[0] = 2;
    if (MlDualSenseOutputParse(bad, sizeof(bad), &parsed)) return fail("accepted bad version");

    memcpy(bad, payload, sizeof(bad));
    bad[4] = 0x20; // unknown bit
    if (MlDualSenseOutputParse(bad, sizeof(bad), &parsed)) return fail("accepted unknown validMask bit");

    memcpy(bad, payload, sizeof(bad));
    bad[7] = 3; // brightness > 2
    if (MlDualSenseOutputParse(bad, sizeof(bad), &parsed)) return fail("accepted brightness > 2");

    memcpy(bad, payload, sizeof(bad));
    bad[8] = 3; // micLed > 2
    if (MlDualSenseOutputParse(bad, sizeof(bad), &parsed)) return fail("accepted micLed > 2");

    memcpy(bad, payload, sizeof(bad));
    bad[10] = 0x80; // headphoneVolume > 0x7F
    if (MlDualSenseOutputParse(bad, sizeof(bad), &parsed)) return fail("accepted headphoneVolume > 0x7F");

    if (MlDualSenseOutputParse(payload, sizeof(payload) - 1, &parsed)) return fail("accepted truncated payload");

    // Audio status builder checks
    if (!MlDualSenseAudioStatusBuild(statusPacket, sizeof(statusPacket), 1,
            ML_DS_AUDIO_HEADPHONES_CONNECTED | ML_DS_AUDIO_HEADSET_MIC_CONNECTED)) {
        return fail("MlDualSenseAudioStatusBuild rejected valid status");
    }
    if (statusPacket[3] != 8 || statusPacket[8] != 1 || statusPacket[9] != 0x03) {
        return fail("MlDualSenseAudioStatusBuild bad wire bytes");
    }
    if (MlDualSenseAudioStatusBuild(statusPacket, sizeof(statusPacket), 16, 0x01)) {
        return fail("MlDualSenseAudioStatusBuild accepted controller >= 16");
    }
    if (MlDualSenseAudioStatusBuild(statusPacket, sizeof(statusPacket), 0, 0x08)) {
        return fail("MlDualSenseAudioStatusBuild accepted unknown audio status bit");
    }

    // Test ControlStream queueAsyncCallback: opt-in gate, truncation, and in-place coalescing
    packetTypes = (short*)packetTypesGen7Enc;
    if (LbqInitializeLinkedBlockingQueue(&asyncCallbackQueue, 256) != 0) return fail("lbq init failed");

    union {
        NVCTL_ENET_PACKET_HEADER_V1 header;
        uint8_t bytes[64];
    } pkt;
    pkt.header.type = packetTypes[IDX_DUALSENSE_OUTPUT];
    memcpy(pkt.bytes + sizeof(pkt.header), payload, sizeof(payload));

    // Without callback registered, packets are silently dropped.
    DualSenseOutputCallback = NULL;
    queueAsyncCallback(&pkt.header, (int)(sizeof(pkt.header) + sizeof(payload)));
    void* item = NULL;
    if (LbqPollQueueElement(&asyncCallbackQueue, &item) == LBQ_SUCCESS) {
        free(item);
        return fail("queued DualSense output while DualSenseOutputCallback == NULL");
    }

    // With callback registered, valid packet queues; same validMask coalesces in place,
    // distinct validMask stays separate.
    DualSenseOutputCallback = dummyDualSenseCallback;

    ML_DUALSENSE_OUTPUT leds1 = { .controllerNumber = 0, .validMask = ML_DS_OUT_PLAYER_LEDS, .playerLeds = 0x04 };
    ML_DUALSENSE_OUTPUT leds2 = { .controllerNumber = 0, .validMask = ML_DS_OUT_PLAYER_LEDS, .playerLeds = 0x1F };
    ML_DUALSENSE_OUTPUT mic = { .controllerNumber = 0, .validMask = ML_DS_OUT_MIC_LED, .micLed = 2 };

    MlDualSenseOutputBuild(pkt.bytes + sizeof(pkt.header), ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE, &leds1);
    queueAsyncCallback(&pkt.header, (int)(sizeof(pkt.header) + ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE));

    MlDualSenseOutputBuild(pkt.bytes + sizeof(pkt.header), ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE, &mic);
    queueAsyncCallback(&pkt.header, (int)(sizeof(pkt.header) + ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE));

    // Coalesce onto leds1
    MlDualSenseOutputBuild(pkt.bytes + sizeof(pkt.header), ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE, &leds2);
    queueAsyncCallback(&pkt.header, (int)(sizeof(pkt.header) + ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE));

    // Truncated packet must not displace queued state
    queueAsyncCallback(&pkt.header, (int)(sizeof(pkt.header) + 5));

    PQUEUED_ASYNC_CALLBACK cb1 = NULL, cb2 = NULL, cb3 = NULL;
    if (LbqPollQueueElement(&asyncCallbackQueue, (void**)&cb1) != LBQ_SUCCESS) return fail("missing first queued item");
    if (LbqPollQueueElement(&asyncCallbackQueue, (void**)&cb2) != LBQ_SUCCESS) return fail("missing second queued item");
    if (LbqPollQueueElement(&asyncCallbackQueue, (void**)&cb3) == LBQ_SUCCESS) {
        free(cb3);
        return fail("expected coalescing to leave only 2 items");
    }

    int gotLeds = 0, gotMic = 0;
    PQUEUED_ASYNC_CALLBACK cbs[2] = { cb1, cb2 };
    for (int i = 0; i < 2; i++) {
        if (cbs[i]->typeIndex != IDX_DUALSENSE_OUTPUT) return fail("wrong typeIndex");
        if (cbs[i]->data.dualSenseOutput.validMask == ML_DS_OUT_PLAYER_LEDS) {
            if (cbs[i]->data.dualSenseOutput.playerLeds != 0x1F) return fail("playerLeds did not coalesce to latest");
            gotLeds = 1;
        } else if (cbs[i]->data.dualSenseOutput.validMask == ML_DS_OUT_MIC_LED) {
            if (cbs[i]->data.dualSenseOutput.micLed != 2) return fail("micLed mismatch");
            gotMic = 1;
        }
        free(cbs[i]);
    }
    if (!gotLeds || !gotMic) return fail("missing expected coalesced items");

    LbqSignalQueueShutdown(&asyncCallbackQueue);
    LbqDestroyLinkedBlockingQueue(&asyncCallbackQueue);
    printf("PASS: DualSense extended controls build, parse, opt-in gate, truncation, and coalescing\n");
    return 0;
}
