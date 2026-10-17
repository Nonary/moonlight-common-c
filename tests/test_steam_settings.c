// Exercise the control callback parser for Steam Controller settings writes (0x5505),
// including every wire truncation, every command/count combination, and queue ordering.
#include "../src/ControlStream.c"

#define WIRE (sizeof(NVCTL_ENET_PACKET_HEADER_V1) + 4 + STEAM_SETTINGS_PAYLOAD_SIZE)

static void send_settings(unsigned controller, unsigned command, unsigned count, unsigned first_id) {
    unsigned char bytes[WIRE] = {0};
    PNVCTL_ENET_PACKET_HEADER_V1 hdr = (PNVCTL_ENET_PACKET_HEADER_V1)bytes;
    hdr->type = packetTypes[IDX_STEAM_SETTINGS];
    bytes[2] = (unsigned char)controller;
    bytes[4] = (unsigned char)command;
    bytes[5] = (unsigned char)count;
    for (unsigned i = 0; i < count && i < STEAM_SETTINGS_TRIPLES_MAX; ++i) {
        bytes[6 + i * 3] = (unsigned char)(first_id + i);
        bytes[7 + i * 3] = 0x34;
        bytes[8 + i * 3] = 0x12;
    }
    queueAsyncCallback(hdr, sizeof(bytes));
}

int main(void) {
    union {
        NVCTL_ENET_PACKET_HEADER_V1 header;
        unsigned char bytes[WIRE];
    } packet = {0};
    // A union's {0} initializer only covers its first member; the rest is unspecified.
    memset(&packet, 0, sizeof(packet));
    packetTypes = (short*)packetTypesGen7Enc;
    packet.header.type = packetTypes[IDX_STEAM_SETTINGS];
    packet.bytes[2] = 7;
    int failures = 0;
    if (LbqInitializeLinkedBlockingQueue(&asyncCallbackQueue, 256) != 0) return 2;

    // Every command byte, every count, every length: only a full-size packet carrying 0x87
    // with 1..9 triples or 0x8E with none is admitted, and what arrives is what was sent.
    int admitted = 0;
    static const int counts[] = {0, 1, 5, 9, 10, 255};
    for (int command = 0; command <= 255; ++command) {
        for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c) {
            const int count = counts[c];
            packet.bytes[4] = (unsigned char)command;
            packet.bytes[5] = (unsigned char)count;
            packet.bytes[6] = 48; packet.bytes[7] = 0x18; packet.bytes[8] = 0;
            // Every length for the two valid commands, the boundaries for the rest.
            const bool sweep = command == STEAM_SETTINGS_SET_VALUES || command == STEAM_SETTINGS_LOAD_DEFAULTS;
            for (size_t length = sizeof(packet.header); length <= sizeof(packet.bytes);
                 length = sweep ? length + 1 : (length == sizeof(packet.header) ? sizeof(packet.bytes) - 1 : length + 1)) {
                void* value = NULL;
                queueAsyncCallback(&packet.header, (int)length);
                bool received = LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS;
                bool expected = length == sizeof(packet.bytes) &&
                    ((command == STEAM_SETTINGS_SET_VALUES && count >= 1 && count <= STEAM_SETTINGS_TRIPLES_MAX) ||
                     (command == STEAM_SETTINGS_LOAD_DEFAULTS && count == 0));
                if (received != expected) failures++;
                if (received && expected) {
                    PQUEUED_ASYNC_CALLBACK cb = value;
                    ++admitted;
                    if (cb->typeIndex != IDX_STEAM_SETTINGS || cb->data.steamSettings.controllerNumber != 7 ||
                        cb->data.steamSettings.command != command || cb->data.steamSettings.count != count ||
                        cb->data.steamSettings.triples[0] != 48 || cb->data.steamSettings.triples[1] != 0x18) failures++;
                    if (isCallbackState(cb)) failures++;  // never coalesced
                }
                free(value);
            }
        }
    }
    printf("%s settings parser admits exactly the valid command/count pairs (%d admitted)\n",
           admitted == 4 ? "PASS" : "FAIL", admitted);
    failures += admitted != 4;  // 0x87 with 1, 5, 9 triples and 0x8E with none

    // A 20-triple write arrives as three chunks; they stay whole, in order, and are not
    // merged with each other or with a state callback for the same controller.
    {
        PQUEUED_ASYNC_CALLBACK rumble = calloc(1, sizeof(*rumble));
        if (rumble == NULL) return 2;
        rumble->typeIndex = IDX_RUMBLE_DATA; rumble->data.rumble.controllerNumber = 3;
        void* old = NULL;
        if (LbqOfferQueueItemPriority(&asyncCallbackQueue, rumble, &rumble->entry, sameCallbackState, isCallbackState, 32, &old) != LBQ_SUCCESS) { free(rumble); ++failures; }
        free(old);
        send_settings(3, STEAM_SETTINGS_SET_VALUES, 9, 0);
        send_settings(3, STEAM_SETTINGS_SET_VALUES, 9, 9);
        send_settings(3, STEAM_SETTINGS_SET_VALUES, 2, 18);
        send_settings(3, STEAM_SETTINGS_LOAD_DEFAULTS, 0, 0);
        int order[8] = {0}, n = 0;
        void* value = NULL;
        while (LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS) {
            PQUEUED_ASYNC_CALLBACK cb = value;
            if (n < 8) order[n] = cb->typeIndex == IDX_STEAM_SETTINGS ? (cb->data.steamSettings.command == STEAM_SETTINGS_LOAD_DEFAULTS ? 100 : cb->data.steamSettings.triples[0]) : -1;
            ++n; free(value);
        }
        bool ordered = n == 5 && order[0] == -1 && order[1] == 0 && order[2] == 9 && order[3] == 18 && order[4] == 100;
        printf("%s chunked settings write is delivered whole and in order after pending state (%d callbacks)\n", ordered ? "PASS" : "FAIL", n);
        failures += !ordered;
    }

    // Settings are transient events with their own bound: a flood of them keeps only the newest
    // STEAM_SETTINGS_QUEUE_LIMIT chunks, and a pending haptic stop (state) survives it.
    {
        unsigned char bytes[sizeof(NVCTL_ENET_PACKET_HEADER_V1) + 3 + STEAM_HAPTIC_REPORT_MAX] = {0};
        PNVCTL_ENET_PACKET_HEADER_V1 hdr = (PNVCTL_ENET_PACKET_HEADER_V1)bytes;
        hdr->type = packetTypes[IDX_STEAM_HAPTIC];
        bytes[2] = 1; bytes[4] = 8; bytes[5] = 0x81; bytes[6] = 0;
        queueAsyncCallback(hdr, sizeof(bytes));
        for (unsigned i = 0; i < 1000; ++i) send_settings(1, STEAM_SETTINGS_SET_VALUES, 1, i & 0x7F);
        int total = 0, states = 0, settings = 0, lastId = -1;
        void* value = NULL;
        while (LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS) {
            PQUEUED_ASYNC_CALLBACK cb = value;
            ++total; if (isCallbackState(value)) ++states;
            if (cb->typeIndex == IDX_STEAM_SETTINGS) { ++settings; lastId = cb->data.steamSettings.triples[0]; }
            free(value);
        }
        bool bounded = states == 1 && settings == STEAM_SETTINGS_QUEUE_LIMIT && total == STEAM_SETTINGS_QUEUE_LIMIT + 1 && lastId == (999 & 0x7F);
        printf("%s settings flood is bounded to its own limit (%d delivered, newest last) and keeps the pending haptic stop\n", bounded ? "PASS" : "FAIL", total);
        failures += !bounded;
    }

    // A burst of ordinary haptic events (pulses, not stops) beyond the 32-event bound evicts
    // only haptic events: a settings chunk queued before it is delivered whole, and first.
    {
        send_settings(2, STEAM_SETTINGS_SET_VALUES, 3, 20);
        unsigned char bytes[sizeof(NVCTL_ENET_PACKET_HEADER_V1) + 3 + STEAM_HAPTIC_REPORT_MAX] = {0};
        PNVCTL_ENET_PACKET_HEADER_V1 hdr = (PNVCTL_ENET_PACKET_HEADER_V1)bytes;
        hdr->type = packetTypes[IDX_STEAM_HAPTIC];
        // 81 <side> 90 01 00 00 01 00: Steam's 400 us slide pulse, an event the queue may evict.
        bytes[2] = 2; bytes[4] = 8; bytes[5] = 0x81; bytes[6] = 1; bytes[7] = 0x90; bytes[8] = 0x01; bytes[11] = 0x01;
        for (unsigned i = 0; i < 40; ++i) queueAsyncCallback(hdr, sizeof(bytes));
        int n = 0, haptics = 0, settings = 0, settingsAt = -1, count = -1;
        void* value = NULL;
        while (LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS) {
            PQUEUED_ASYNC_CALLBACK cb = value;
            if (cb->typeIndex == IDX_STEAM_SETTINGS) { ++settings; settingsAt = n; count = cb->data.steamSettings.count; }
            else if (cb->typeIndex == IDX_STEAM_HAPTIC) ++haptics;
            ++n; free(value);
        }
        bool kept = settings == 1 && settingsAt == 0 && count == 3 && haptics == 32 && n == 33;
        printf("%s a 40-pulse haptic burst evicts only haptic events (%d kept) and leaves the queued settings chunk intact\n",
               kept ? "PASS" : "FAIL", haptics);
        failures += !kept;
    }

    // The same burst arriving while several chunks of one write wait: all of them survive, in order.
    {
        send_settings(2, STEAM_SETTINGS_SET_VALUES, 9, 0);
        send_settings(2, STEAM_SETTINGS_SET_VALUES, 9, 9);
        send_settings(2, STEAM_SETTINGS_SET_VALUES, 2, 18);
        unsigned char bytes[sizeof(NVCTL_ENET_PACKET_HEADER_V1) + 3 + STEAM_HAPTIC_REPORT_MAX] = {0};
        PNVCTL_ENET_PACKET_HEADER_V1 hdr = (PNVCTL_ENET_PACKET_HEADER_V1)bytes;
        hdr->type = packetTypes[IDX_STEAM_HAPTIC];
        bytes[2] = 2; bytes[4] = 8; bytes[5] = 0x81; bytes[6] = 0; bytes[7] = 0x90; bytes[8] = 0x01; bytes[11] = 0x01;
        for (unsigned i = 0; i < 40; ++i) queueAsyncCallback(hdr, sizeof(bytes));
        int order[3] = {-1, -1, -1}, chunks = 0, haptics = 0;
        void* value = NULL;
        while (LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS) {
            PQUEUED_ASYNC_CALLBACK cb = value;
            if (cb->typeIndex == IDX_STEAM_SETTINGS) { if (chunks < 3) order[chunks] = cb->data.steamSettings.triples[0]; ++chunks; }
            else ++haptics;
            free(value);
        }
        bool whole = chunks == 3 && order[0] == 0 && order[1] == 9 && order[2] == 18 && haptics == 32;
        printf("%s a chunked write survives a haptic burst whole and in order (%d chunks, %d pulses)\n", whole ? "PASS" : "FAIL", chunks, haptics);
        failures += !whole;
    }

    // Fill the production-sized queue with the 241 supported state keys plus
    // fifteen settings chunks. No ordinary event is available for eviction.
    // An incoming pulse must be refused, not take the oldest settings chunk.
    {
        for (int id = 0; id < 16; ++id) {
            for (int kind = 0; kind < 15; ++kind) {
                PQUEUED_ASYNC_CALLBACK cb = calloc(1, sizeof(*cb));
                if (cb == NULL) return 2;
                if (kind < 7) {
                    cb->typeIndex = IDX_STEAM_HAPTIC;
                    cb->data.steamHaptic.controllerNumber = id;
                    cb->data.steamHaptic.length = 12;
                    cb->data.steamHaptic.report[0] = kind == 0 ? 0x80 : kind < 4 ? 0x81 : 0x82;
                    cb->data.steamHaptic.report[1] = kind == 0 ? 0 : (kind - 1) % 3;
                }
                else if (kind == 7) { cb->typeIndex = IDX_RUMBLE_DATA; cb->data.rumble.controllerNumber = id; }
                else if (kind == 8) { cb->typeIndex = IDX_RUMBLE_TRIGGER_DATA; cb->data.rumbleTriggers.controllerNumber = id; }
                else if (kind == 9) { cb->typeIndex = IDX_SET_RGB_LED; cb->data.setControllerLed.controllerNumber = id; }
                else if (kind < 12) { cb->typeIndex = IDX_SET_MOTION_EVENT; cb->data.setMotionEventState.controllerNumber = id; cb->data.setMotionEventState.motionType = kind - 9; }
                else { cb->typeIndex = IDX_DS_ADAPTIVE_TRIGGERS; cb->data.dsAdaptiveTrigger.controllerNumber = id; cb->data.dsAdaptiveTrigger.eventFlags = 4 * (kind - 11); }
                void* old = NULL;
                if (LbqOfferQueueItemClassed(&asyncCallbackQueue, cb, &cb->entry, sameCallbackState, isCallbackState, 32,
                        isSettingsCallback, STEAM_SETTINGS_QUEUE_LIMIT, &old) != LBQ_SUCCESS) { free(cb); ++failures; }
                free(old);
            }
        }
        PQUEUED_ASYNC_CALLBACK hdrState = calloc(1, sizeof(*hdrState));
        if (hdrState == NULL) return 2;
        hdrState->typeIndex = IDX_HDR_INFO;
        void* old = NULL;
        if (LbqOfferQueueItemClassed(&asyncCallbackQueue, hdrState, &hdrState->entry, sameCallbackState, isCallbackState, 32,
                isSettingsCallback, STEAM_SETTINGS_QUEUE_LIMIT, &old) != LBQ_SUCCESS) { free(hdrState); ++failures; }
        free(old);
        for (unsigned i = 0; i < 15; ++i) send_settings(2, STEAM_SETTINGS_SET_VALUES, 1, i);
        bool filled = LbqGetItemCount(&asyncCallbackQueue) == 256;
        printf("%s real callback capacity is filled with state and reserved settings\n", filled ? "PASS" : "FAIL");
        failures += !filled;

        unsigned char bytes[sizeof(NVCTL_ENET_PACKET_HEADER_V1) + 3 + STEAM_HAPTIC_REPORT_MAX] = {0};
        PNVCTL_ENET_PACKET_HEADER_V1 hdr = (PNVCTL_ENET_PACKET_HEADER_V1)bytes;
        hdr->type = packetTypes[IDX_STEAM_HAPTIC];
        bytes[2] = 2; bytes[4] = 8; bytes[5] = 0x81; bytes[7] = 0x90; bytes[8] = 0x01; bytes[11] = 0x01;
        queueAsyncCallback(hdr, sizeof(bytes));
        int states = 0, settings = 0, pulses = 0;
        bool ordered = true;
        void* value = NULL;
        while (LbqPollQueueElement(&asyncCallbackQueue, &value) == LBQ_SUCCESS) {
            PQUEUED_ASYNC_CALLBACK cb = value;
            if (isSettingsCallback(cb)) {
                ordered &= cb->data.steamSettings.triples[0] == settings;
                ++settings;
            }
            else if (isCallbackState(cb)) ++states;
            else ++pulses;
            free(value);
        }
        bool kept = states == 241 && settings == 15 && pulses == 0 && ordered;
        printf("%s at total capacity a pulse cannot evict reserved settings (%d states, %d settings, %d pulses)\n",
               kept ? "PASS" : "FAIL", states, settings, pulses);
        failures += !kept;
    }

    LbqSignalQueueShutdown(&asyncCallbackQueue);
    LbqDestroyLinkedBlockingQueue(&asyncCallbackQueue);
    printf("Steam settings parser: %d failures\n", failures);
    return failures != 0;
}
