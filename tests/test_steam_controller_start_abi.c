#include "Limelight.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The Steam Controller (2026) callbacks are registered through
// LiStartConnectionWithSteamController() rather than through
// CONNECTION_LISTENER_CALLBACKS, so the V1 callback layout stays frozen
// (see test_connection_listener_callbacks_v1_layout.c). This checks that the
// opt-in start retains them, that the other start variants clear them, and
// that the speaker PCM callback still travels alongside.

extern ConnListenerControllerSpeakerPcm ControllerSpeakerPcmCallback;
extern ConnListenerSteamHaptic SteamHapticCallback;
extern ConnListenerSteamSettings SteamSettingsCallback;

static void speakerCallback(uint16_t controllerNumber, uint64_t speakerOwner, uint32_t generation,
                            uint32_t sequence, uint64_t samplePosition,
                            uint16_t flags, const uint8_t* pcm, uint16_t frames) {
    (void)controllerNumber; (void)speakerOwner; (void)generation; (void)sequence;
    (void)samplePosition; (void)flags; (void)pcm; (void)frames;
}

static void steamHapticCallback(uint16_t controllerNumber, uint8_t length, const uint8_t* report) {
    (void)controllerNumber; (void)length; (void)report;
}

static void steamSettingsCallback(uint16_t controllerNumber, uint8_t command, uint8_t count, const uint8_t* triples) {
    (void)controllerNumber; (void)command; (void)count; (void)triples;
}

enum StartVariant { START_LEGACY, START_SPEAKER, START_STEAM };

static int invokeStart(enum StartVariant variant) {
    CONNECTION_LISTENER_CALLBACKS callbacks;
    SERVER_INFORMATION serverInfo;
    STREAM_CONFIGURATION streamConfig;
    int err;

    LiInitializeConnectionCallbacks(&callbacks);
    memset(&serverInfo, 0, sizeof(serverInfo));
    memset(&streamConfig, 0, sizeof(streamConfig));
    serverInfo.address = "127.0.0.1";
    serverInfo.serverInfoAppVersion = "7.1.431.0";
    serverInfo.rtspSessionUrl = "";
    serverInfo.serverCodecModeSupport = SCM_H264;

    switch (variant) {
    case START_STEAM:
        err = LiStartConnectionWithSteamController(&serverInfo, &streamConfig, &callbacks,
            NULL, NULL, NULL, 0, NULL, 0, speakerCallback, steamHapticCallback, steamSettingsCallback);
        if (ControllerSpeakerPcmCallback != speakerCallback ||
                SteamHapticCallback != steamHapticCallback ||
                SteamSettingsCallback != steamSettingsCallback) {
            fprintf(stderr, "Steam Controller start API did not retain its separate callbacks\n");
            return 1;
        }
        break;
    case START_SPEAKER:
        err = LiStartConnectionWithSpeakerPcm(&serverInfo, &streamConfig, &callbacks,
            NULL, NULL, NULL, 0, NULL, 0, speakerCallback);
        if (ControllerSpeakerPcmCallback != speakerCallback) {
            fprintf(stderr, "speaker start API did not retain its callback\n");
            return 1;
        }
        if (SteamHapticCallback != NULL || SteamSettingsCallback != NULL) {
            fprintf(stderr, "speaker start API did not clear the Steam Controller callbacks\n");
            return 1;
        }
        break;
    default:
        err = LiStartConnection(&serverInfo, &streamConfig, &callbacks,
            NULL, NULL, NULL, 0, NULL, 0);
        if (ControllerSpeakerPcmCallback != NULL || SteamHapticCallback != NULL || SteamSettingsCallback != NULL) {
            fprintf(stderr, "legacy start API did not clear the optional callbacks\n");
            return 1;
        }
        break;
    }

    if (err == 0) {
        fprintf(stderr, "invalid stream configuration unexpectedly started\n");
        return 1;
    }
    return 0;
}

int main(void) {
    if (invokeStart(START_STEAM) != 0) {
        return 1;
    }
    // Neither of the other start variants may inherit the Steam callbacks.
    if (invokeStart(START_SPEAKER) != 0) {
        return 1;
    }
    if (invokeStart(START_STEAM) != 0) {
        return 1;
    }
    if (invokeStart(START_LEGACY) != 0) {
        return 1;
    }
    return 0;
}
