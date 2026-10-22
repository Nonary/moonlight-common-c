#include "Limelight.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ConnListenerStageStarting LegacyConnListenerStageStarting
#define ConnListenerStageComplete LegacyConnListenerStageComplete
#define ConnListenerStageFailed LegacyConnListenerStageFailed
#define ConnListenerConnectionStarted LegacyConnListenerConnectionStarted
#define ConnListenerConnectionTerminated LegacyConnListenerConnectionTerminated
#define ConnListenerLogMessage LegacyConnListenerLogMessage
#define ConnListenerRumble LegacyConnListenerRumble
#define ConnListenerConnectionStatusUpdate LegacyConnListenerConnectionStatusUpdate
#define ConnListenerSetHdrMode LegacyConnListenerSetHdrMode
#define ConnListenerRumbleTriggers LegacyConnListenerRumbleTriggers
#define ConnListenerSetMotionEventState LegacyConnListenerSetMotionEventState
#define ConnListenerSetControllerLED LegacyConnListenerSetControllerLED
#define ConnListenerSetAdaptiveTriggers LegacyConnListenerSetAdaptiveTriggers
#define ConnListenerControllerHaptics LegacyConnListenerControllerHaptics
#define _CONNECTION_LISTENER_CALLBACKS _Legacy_CONNECTION_LISTENER_CALLBACKS
#define CONNECTION_LISTENER_CALLBACKS LegacyConnectionListenerCallbacks
#define PCONNECTION_LISTENER_CALLBACKS PLegacyConnectionListenerCallbacks
#define LiInitializeConnectionCallbacks LiInitializeConnectionCallbacksLegacy
#include "legacy_abi/Limelight-legacy.h"
#undef ConnListenerStageStarting
#undef ConnListenerStageComplete
#undef ConnListenerStageFailed
#undef ConnListenerConnectionStarted
#undef ConnListenerConnectionTerminated
#undef ConnListenerLogMessage
#undef ConnListenerRumble
#undef ConnListenerConnectionStatusUpdate
#undef ConnListenerSetHdrMode
#undef ConnListenerRumbleTriggers
#undef ConnListenerSetMotionEventState
#undef ConnListenerSetControllerLED
#undef ConnListenerSetAdaptiveTriggers
#undef ConnListenerControllerHaptics
#undef _CONNECTION_LISTENER_CALLBACKS
#undef CONNECTION_LISTENER_CALLBACKS
#undef PCONNECTION_LISTENER_CALLBACKS
#undef LiInitializeConnectionCallbacks

extern ConnListenerControllerSpeakerPcm ControllerSpeakerPcmCallback;

static void speakerCallback(uint16_t controllerNumber, uint32_t generation,
                            uint32_t sequence, uint64_t samplePosition,
                            uint16_t flags, const uint8_t* pcm, uint16_t frames) {
    (void)controllerNumber;
    (void)generation;
    (void)sequence;
    (void)samplePosition;
    (void)flags;
    (void)pcm;
    (void)frames;
}

static int invokeStart(int withSpeakerCallback) {
    LegacyConnectionListenerCallbacks* callbacks;
    SERVER_INFORMATION serverInfo;
    STREAM_CONFIGURATION streamConfig;
    int err;

    callbacks = calloc(1, sizeof(*callbacks));
    if (callbacks == NULL) {
        fprintf(stderr, "failed to allocate legacy callback table\n");
        return 1;
    }
    memset(&serverInfo, 0, sizeof(serverInfo));
    memset(&streamConfig, 0, sizeof(streamConfig));
    serverInfo.address = "127.0.0.1";
    serverInfo.serverInfoAppVersion = "7.1.431.0";
    serverInfo.rtspSessionUrl = "";
    serverInfo.serverCodecModeSupport = SCM_H264;

    if (withSpeakerCallback) {
        err = LiStartConnectionWithSpeakerPcm(
            &serverInfo, &streamConfig,
            (PCONNECTION_LISTENER_CALLBACKS)callbacks,
            NULL, NULL, NULL, 0, NULL, 0, speakerCallback);
        if (ControllerSpeakerPcmCallback != speakerCallback) {
            fprintf(stderr, "speaker start API did not retain its separate callback\n");
            free(callbacks);
            return 1;
        }
    } else {
        err = LiStartConnection(
            &serverInfo, &streamConfig,
            (PCONNECTION_LISTENER_CALLBACKS)callbacks,
            NULL, NULL, NULL, 0, NULL, 0);
        if (ControllerSpeakerPcmCallback != NULL) {
            fprintf(stderr, "legacy start API did not clear the optional speaker callback\n");
            free(callbacks);
            return 1;
        }
    }

    if (err == 0) {
        fprintf(stderr, "invalid stream configuration unexpectedly started\n");
        free(callbacks);
        return 1;
    }
    free(callbacks);
    return 0;
}

int main(void) {
    if (invokeStart(0) != 0) {
        return 1;
    }
    if (invokeStart(1) != 0) {
        return 1;
    }
    // A subsequent legacy connection must not retain the opt-in callback.
    if (invokeStart(0) != 0) {
        return 1;
    }
    return 0;
}
