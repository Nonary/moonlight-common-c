#include "Limelight.h"

#include <stddef.h>

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

#define ASSERT_CALLBACK_OFFSET(field) \
    _Static_assert(offsetof(CONNECTION_LISTENER_CALLBACKS, field) == \
                       offsetof(LegacyConnectionListenerCallbacks, field), \
                   "CONNECTION_LISTENER_CALLBACKS V1 field offset changed: " #field)

_Static_assert(sizeof(CONNECTION_LISTENER_CALLBACKS) == sizeof(LegacyConnectionListenerCallbacks),
    "CONNECTION_LISTENER_CALLBACKS V1 size changed");
ASSERT_CALLBACK_OFFSET(stageStarting);
ASSERT_CALLBACK_OFFSET(stageComplete);
ASSERT_CALLBACK_OFFSET(stageFailed);
ASSERT_CALLBACK_OFFSET(connectionStarted);
ASSERT_CALLBACK_OFFSET(connectionTerminated);
ASSERT_CALLBACK_OFFSET(logMessage);
ASSERT_CALLBACK_OFFSET(rumble);
ASSERT_CALLBACK_OFFSET(connectionStatusUpdate);
ASSERT_CALLBACK_OFFSET(setHdrMode);
ASSERT_CALLBACK_OFFSET(rumbleTriggers);
ASSERT_CALLBACK_OFFSET(setMotionEventState);
ASSERT_CALLBACK_OFFSET(setControllerLED);
ASSERT_CALLBACK_OFFSET(setAdaptiveTriggers);
ASSERT_CALLBACK_OFFSET(controllerHaptics);

int main(void) {
    return 0;
}
