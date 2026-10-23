// Minimal public callback declaration frozen from moonlight-common-c 9348def91b5bbe9f21ba5af4aa11fcadc85b5875.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ConnListenerStageStarting)(int stage);
typedef void (*ConnListenerStageComplete)(int stage);
typedef void (*ConnListenerStageFailed)(int stage, int errorCode);
typedef void (*ConnListenerConnectionStarted)(void);
typedef void (*ConnListenerConnectionTerminated)(int errorCode);
typedef void (*ConnListenerLogMessage)(const char* format, ...);
typedef void (*ConnListenerRumble)(unsigned short controllerNumber, unsigned short lowFreqMotor,
    unsigned short highFreqMotor);
typedef void (*ConnListenerConnectionStatusUpdate)(int connectionStatus);
typedef void (*ConnListenerSetHdrMode)(bool hdrEnabled);
typedef void (*ConnListenerRumbleTriggers)(uint16_t controllerNumber, uint16_t leftTriggerMotor,
    uint16_t rightTriggerMotor);
typedef void (*ConnListenerSetMotionEventState)(uint16_t controllerNumber, uint8_t motionType,
    uint16_t reportRateHz);
typedef void (*ConnListenerSetControllerLED)(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b);
typedef void (*ConnListenerSetAdaptiveTriggers)(uint16_t controllerNumber, uint8_t eventFlags, uint8_t typeLeft,
    uint8_t typeRight, uint8_t* left, uint8_t* right);
typedef void (*ConnListenerControllerHaptics)(uint16_t controllerNumber, uint32_t sequence,
    const uint8_t* pcm, uint16_t frames);

typedef struct _CONNECTION_LISTENER_CALLBACKS {
    ConnListenerStageStarting stageStarting;
    ConnListenerStageComplete stageComplete;
    ConnListenerStageFailed stageFailed;
    ConnListenerConnectionStarted connectionStarted;
    ConnListenerConnectionTerminated connectionTerminated;
    ConnListenerLogMessage logMessage;
    ConnListenerRumble rumble;
    ConnListenerConnectionStatusUpdate connectionStatusUpdate;
    ConnListenerSetHdrMode setHdrMode;
    ConnListenerRumbleTriggers rumbleTriggers;
    ConnListenerSetMotionEventState setMotionEventState;
    ConnListenerSetControllerLED setControllerLED;
    ConnListenerSetAdaptiveTriggers setAdaptiveTriggers;
    ConnListenerControllerHaptics controllerHaptics;
} CONNECTION_LISTENER_CALLBACKS, *PCONNECTION_LISTENER_CALLBACKS;

void LiInitializeConnectionCallbacks(PCONNECTION_LISTENER_CALLBACKS clCallbacks);

#ifdef __cplusplus
}
#endif
