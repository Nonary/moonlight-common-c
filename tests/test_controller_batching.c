// Drive the real input producer and sender with a queued trigger tap. A delayed
// sender must retain press/release transitions while still coalescing analog motion.
#include "../src/Limelight-internal.h"
static int hookSend(unsigned char*, int, uint8_t, uint32_t, bool);
#define sendInputPacketOnControlStream hookSend
#include "../src/InputStream.c"
#undef sendInputPacketOnControlStream
#include <stdio.h>

static NV_MULTI_CONTROLLER_PACKET sent[16];
static int sends, checks, failures;
static int hookSend(unsigned char* data, int length, uint8_t channel, uint32_t flags, bool more) {
    (void)channel; (void)flags; (void)more;
    if (length == sizeof(sent[0]) && sends < 16) {
        memcpy(&sent[sends++], data, length);
    }
    return 0;
}
static void check(bool ok, const char* message) {
    ++checks;
    failures += !ok;
    printf("%s %s\n", ok ? "PASS" : "FAIL", message);
}
static void sendState(unsigned char left, unsigned char right, short axis, int buttons) {
    check(LiSendMultiControllerEvent(0, 1, buttons, left, right, axis, 0, 0, 0) == 0,
          "controller state accepted");
}
static void drain(void) {
    sends = 0;
    memset(sent, 0, sizeof(sent));
    LbqSignalQueueDrain(&packetQueue);
    inputSendThreadProc(NULL);
    LbqDestroyLinkedBlockingQueue(&packetQueue);
    LbqInitializeLinkedBlockingQueue(&packetQueue, MAX_QUEUED_INPUT_PACKETS);
}
int main(void) {
    memcpy(AppVersionQuad, (int[]){7, 1, 431, -1}, sizeof(AppVersionQuad));
    if (initializeInputStream() != 0) return 2;
    initialized = true;

    sendState(0, 0, 0, 0);
    sendState(255, 0, 100, 0);
    sendState(0, 0, 200, 0);
    drain();
    check(sends == 3 && sent[0].leftTrigger == 0 && sent[1].leftTrigger == 255 &&
          sent[2].leftTrigger == 0, "left trigger tap retains rest, press, release");

    sendState(0, 0, 0, 0);
    sendState(0, 255, 100, 0);
    sendState(0, 0, 200, 0);
    drain();
    check(sends == 3 && sent[0].rightTrigger == 0 && sent[1].rightTrigger == 255 &&
          sent[2].rightTrigger == 0, "right trigger tap retains rest, press, release");

    sendState(80, 0, 100, 0);
    sendState(200, 0, 200, 0);
    sendState(220, 0, 300, 0);
    drain();
    check(sends == 1 && sent[0].leftTrigger == 220 && LE16(sent[0].leftStickX) == 300,
          "continuous travel still coalesces to the newest state");

    sendState(239, 0, 0, 0);
    sendState(240, 0, 0, 0);
    sendState(239, 0, 0, 0);
    drain();
    check(sends == 3 && sent[1].leftTrigger == 240 && sent[2].leftTrigger == 239,
          "left Steam Controller full-pull click survives without returning to rest");

    sendState(0, 239, 0, 0);
    sendState(0, 240, 0, 0);
    sendState(0, 239, 0, 0);
    drain();
    check(sends == 3 && sent[1].rightTrigger == 240 && sent[2].rightTrigger == 239,
          "right Steam Controller full-pull click survives without returning to rest");

    sendState(20, 0, 10, 0);
    sendState(20, 0, 20, A_FLAG);
    sendState(20, 0, 30, 0);
    drain();
    check(sends == 3 && LE16(sent[1].buttonFlags) == A_FLAG,
          "button transitions retain their existing ordering");

    // A holder the sender has already latched (currentQueuedControllerPacket cleared
    // under batchedInputMutex while the holder is being sent) must not be batched
    // into: the next state goes into a new holder even without a discrete edge.
    sendState(80, 0, 10, 0);
    PltLockMutex(&batchedInputMutex);
    currentQueuedControllerPacket[0] = NULL;
    PltUnlockMutex(&batchedInputMutex);
    sendState(90, 0, 20, 0);
    drain();
    check(sends == 2 && sent[0].leftTrigger == 80 && sent[1].leftTrigger == 90,
          "a holder the sender has latched is not coalesced into");

    // The same edge rules on the GFE branch of the comparison (buttonFlags2 forced
    // to zero, controller number modulo 4).
    memcpy(AppVersionQuad, (int[]){7, 1, 431, 0}, sizeof(AppVersionQuad));
    sendState(0, 0, 0, 0);
    sendState(255, 0, 100, 0);
    sendState(0, 0, 200, 0);
    drain();
    check(sends == 3 && sent[0].leftTrigger == 0 && sent[1].leftTrigger == 255 && sent[2].leftTrigger == 0,
          "GFE: left trigger tap retains rest, press, release");
    sendState(239, 0, 0, 0);
    sendState(240, 0, 0, 0);
    sendState(239, 0, 0, 0);
    drain();
    check(sends == 3 && sent[1].leftTrigger == 240, "GFE: the 0xF0 click edge is retained");
    sendState(80, 0, 100, 0);
    sendState(220, 0, 300, 0);
    drain();
    check(sends == 1 && sent[0].leftTrigger == 220, "GFE: continuous travel coalesces");
    sendState(20, 0, 10, 0);
    sendState(20, 0, 20, A_FLAG);
    sendState(20, 0, 30, 0);
    drain();
    check(sends == 3 && LE16(sent[1].buttonFlags) == A_FLAG && sent[1].buttonFlags2 == 0,
          "GFE: button transitions are retained with buttonFlags2 zero");

    // Physical SC2026 pad and trigger clicks ride in the high half of the button word
    // (LI_CCAP_SC26_BUTTONS). A click release is a button change, so it ends the batch
    // even though the analog triggers are unchanged.
    memcpy(AppVersionQuad, (int[]){7, 1, 431, -1}, sizeof(AppVersionQuad));
    {
        const int clicks = LEFT_PAD_CLICK_FLAG | RIGHT_TRIGGER_CLICK_FLAG;
        check(LiSendMultiControllerEvent(2, 4, clicks | TOUCHPAD_FLAG, 255, 0, 0, 0, 0, 0) == 0,
              "physical SC2026 clicks are queued");
        PPACKET_HOLDER pressed = currentQueuedControllerPacket[2];
        check(pressed && pressed->packet.multiController.buttonFlags2 == LE16((short)((clicks | TOUCHPAD_FLAG) >> 16)),
              "pad and trigger clicks survive the high button word");
        check(LiSendMultiControllerEvent(2, 4, 0, 255, 0, 0, 0, 0, 0) == 0 &&
              currentQueuedControllerPacket[2] != pressed && pressed->packet.multiController.buttonFlags2 != 0,
              "physical click release cannot coalesce away a press with unchanged analog triggers");
        drain();
        check(sends == 2 && sent[0].buttonFlags2 == LE16((short)((clicks | TOUCHPAD_FLAG) >> 16)) && sent[1].buttonFlags2 == 0,
              "press and release of a physical click are both sent");
    }

    initialized = false;
    LbqSignalQueueShutdown(&packetHolderFreeList);
    LbqSignalQueueShutdown(&packetQueue);
    destroyInputStream();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
