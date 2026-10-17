// A malformed feedback message must neither invoke a callback nor replace a
// complete pending state update in the shared Steam/legacy callback queue.
#include "../src/ControlStream.c"

static bool expectedState(PQUEUED_ASYNC_CALLBACK cb, int kind) {
    if (cb->typeIndex != kind) return false;
    switch (kind) {
    case IDX_RUMBLE_DATA:
        return cb->data.rumble.controllerNumber == 7 &&
               cb->data.rumble.lowFreqRumble == 0x5555 && cb->data.rumble.highFreqRumble == 0x5555;
    case IDX_RUMBLE_TRIGGER_DATA:
        return cb->data.rumbleTriggers.controllerNumber == 7 &&
               cb->data.rumbleTriggers.leftTriggerMotor == 0x5555 && cb->data.rumbleTriggers.rightTriggerMotor == 0x5555;
    case IDX_SET_MOTION_EVENT:
        return cb->data.setMotionEventState.controllerNumber == 7 &&
               cb->data.setMotionEventState.reportRateHz == 0x5555 && cb->data.setMotionEventState.motionType == 1;
    case IDX_SET_RGB_LED:
        return cb->data.setControllerLed.controllerNumber == 7 &&
               cb->data.setControllerLed.r == 0x55 && cb->data.setControllerLed.g == 0x55 && cb->data.setControllerLed.b == 0x55;
    case IDX_DS_ADAPTIVE_TRIGGERS:
        if (cb->data.dsAdaptiveTrigger.controllerNumber != 7 ||
                cb->data.dsAdaptiveTrigger.eventFlags != DS_EFFECT_LEFT_TRIGGER ||
                cb->data.dsAdaptiveTrigger.typeLeft != 0x55 || cb->data.dsAdaptiveTrigger.typeRight != 0x55) return false;
        for (int i = 0; i < DS_EFFECT_PAYLOAD_SIZE; ++i) {
            if (cb->data.dsAdaptiveTrigger.left[i] != 0x55 || cb->data.dsAdaptiveTrigger.right[i] != 0x55) return false;
        }
        return true;
    default: return false;
    }
}

int main(void) {
    union {
        NVCTL_ENET_PACKET_HEADER_V1 header;
        unsigned char bytes[128];
    } packet;
    const struct { int kind, size, controllerOffset; } cases[] = {
        {IDX_RUMBLE_DATA, 10, 4}, {IDX_RUMBLE_TRIGGER_DATA, 6, 0},
        {IDX_SET_MOTION_EVENT, 5, 0}, {IDX_SET_RGB_LED, 5, 0},
        {IDX_DS_ADAPTIVE_TRIGGERS, 5 + 2 * DS_EFFECT_PAYLOAD_SIZE, 0}
    };
    int failures = 0, tested = 0;
    packetTypes = (short*)packetTypesGen7Enc;
    if (LbqInitializeLinkedBlockingQueue(&asyncCallbackQueue, 256) != 0) return 2;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int errors = 0;
        memset(packet.bytes, 0x55, sizeof(packet.bytes));
        packet.header.type = packetTypes[cases[i].kind];
        unsigned char* payload = packet.bytes + sizeof(packet.header);
        payload[cases[i].controllerOffset] = 7;
        payload[cases[i].controllerOffset + 1] = 0;
        if (cases[i].kind == IDX_SET_MOTION_EVENT) payload[4] = 1;
        if (cases[i].kind == IDX_DS_ADAPTIVE_TRIGGERS) payload[2] = DS_EFFECT_LEFT_TRIGGER;
        for (int size = 0; size <= cases[i].size + 4; ++size) {
            void* value = NULL;
            queueAsyncCallback(&packet.header, sizeof(packet.header) + size);
            bool received = LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS;
            errors += received != (size >= cases[i].size);
            if (received && size >= cases[i].size) errors += !expectedState(value, cases[i].kind);
            free(value);
            ++tested;
        }
        for (int size = 0; size < cases[i].size; ++size) {
            // Seed real feedback before the bad packet, then verify the queue
            // still contains exactly that complete state and no fabricated one.
            queueAsyncCallback(&packet.header, sizeof(packet.header) + cases[i].size);
            queueAsyncCallback(&packet.header, sizeof(packet.header) + size);
            void* value = NULL;
            bool received = LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS;
            errors += !received || !expectedState(value, cases[i].kind);
            free(value);
            while (LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS) {
                ++errors;
                free(value);
            }
            ++tested;
        }
        failures += errors;
        printf("%s feedback type %d rejects every short prefix, preserves pending state, and accepts complete/padded payloads (%d errors)\n",
               errors ? "FAIL" : "PASS", cases[i].kind, errors);
    }
    LbqSignalQueueShutdown(&asyncCallbackQueue);
    LbqDestroyLinkedBlockingQueue(&asyncCallbackQueue);
    printf("Feedback truncation: %d cases, %d failures\n", tested, failures);
    return failures != 0;
}
