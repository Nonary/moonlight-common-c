// Drive the real input producer and sender with LiSendSteamControllerAttributesEvent(): the
// packet layout on the wire, its channel and ordering after the arrival event, the string
// sanitising, the controller number wrap, and the host gates.
#include "../src/Limelight-internal.h"
static int hookSend(unsigned char*, int, uint8_t, uint32_t, bool);
#define sendInputPacketOnControlStream hookSend
#include "../src/InputStream.c"
#undef sendInputPacketOnControlStream
#include <stdio.h>
#include <stddef.h>

// Layout pinned at compile time: the host decodes this packet by exact size.
#define STATIC_CHECK(cond, name) typedef char static_check_##name[(cond) ? 1 : -1]
STATIC_CHECK(sizeof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET) == sizeof(NV_INPUT_HEADER) + 4 + 24 + 48, packet_size);
STATIC_CHECK(sizeof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET) == 84, packet_size_84);
STATIC_CHECK(offsetof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET, controllerNumber) == 8, off_controller);
STATIC_CHECK(offsetof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET, transport) == 9, off_transport);
STATIC_CHECK(offsetof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET, validMask) == 10, off_valid);
STATIC_CHECK(offsetof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET, firmwareBuildTime) == 12, off_fw);
STATIC_CHECK(offsetof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET, dongleFirmwareBuildTime) == 32, off_dongle);
STATIC_CHECK(offsetof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET, buildId) == 36, off_build);
STATIC_CHECK(offsetof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET, unitSerial) == 52, off_unit);
STATIC_CHECK(offsetof(SS_STEAM_CONTROLLER_ATTRIBUTES_PACKET, boardSerial) == 68, off_board);
STATIC_CHECK(sizeof(LI_STEAM_CONTROLLER_ATTRIBUTES) == 4 + 24 + 48, api_struct_size);
STATIC_CHECK(SS_STEAM_CONTROLLER_ATTRIBUTES_MAGIC == 0x55000008, magic_value);
STATIC_CHECK(LI_CCAP_STEAM_ATTRIBUTES == 0x4000, ccap_value);
STATIC_CHECK((LI_SCATTR_FIRMWARE_BUILD_TIME | LI_SCATTR_BOOTLOADER_BUILD_TIME | LI_SCATTR_BOARD_REVISION |
              LI_SCATTR_CAPABILITIES | LI_SCATTR_RADIO_FW_BUILD_TIME | LI_SCATTR_DONGLE_FW_BUILD_TIME |
              LI_SCATTR_BUILD_ID | LI_SCATTR_SERIALS) == 0xFF, valid_bits_cover_a_byte);

typedef struct {
    unsigned char data[128];
    int length;
    uint8_t channel;
    uint32_t flags;
} SENT;
static SENT sent[16];
static int sends, checks, failures;

static int hookSend(unsigned char* data, int length, uint8_t channel, uint32_t flags, bool more) {
    (void)more;
    if (sends < 16 && length <= (int)sizeof(sent[0].data)) {
        memcpy(sent[sends].data, data, length);
        sent[sends].length = length;
        sent[sends].channel = channel;
        sent[sends].flags = flags;
        sends++;
    }
    return 0;
}

static void check(bool ok, const char* message) {
    ++checks;
    failures += !ok;
    printf("%s %s\n", ok ? "PASS" : "FAIL", message);
}

static void drain(void) {
    sends = 0;
    memset(sent, 0, sizeof(sent));
    LbqSignalQueueDrain(&packetQueue);
    inputSendThreadProc(NULL);
    LbqDestroyLinkedBlockingQueue(&packetQueue);
    LbqInitializeLinkedBlockingQueue(&packetQueue, MAX_QUEUED_INPUT_PACKETS);
}

static uint32_t le32at(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t be32at(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// The wired unit's capture (libvirtualgamepad captures/sc26-steam-handshake.txt).
static LI_STEAM_CONTROLLER_ATTRIBUTES realUnit(void) {
    LI_STEAM_CONTROLLER_ATTRIBUTES a;
    memset(&a, 0, sizeof(a));
    a.transport = LI_SCATTR_TRANSPORT_USB;
    a.validMask = LI_SCATTR_FIRMWARE_BUILD_TIME | LI_SCATTR_BOOTLOADER_BUILD_TIME | LI_SCATTR_BOARD_REVISION |
                  LI_SCATTR_CAPABILITIES | LI_SCATTR_BUILD_ID | LI_SCATTR_SERIALS;
    a.firmwareBuildTime = 0x6A4D85E3;
    a.bootloaderBuildTime = 0x68D2F92E;
    a.boardRevision = 0x4A;
    a.capabilities = 0;
    strcpy(a.buildId, "26e1f7ef1955");
    strcpy(a.unitSerial, "FXA9962403787");
    strcpy(a.boardSerial, "MXA2G623065D4");
    return a;
}

int main(void) {
    LI_STEAM_CONTROLLER_ATTRIBUTES attrs;

    // A GeForce Experience host: the extension is refused before anything is queued.
    memcpy(AppVersionQuad, (int[]){7, 1, 431, 0}, sizeof(AppVersionQuad));
    if (initializeInputStream() != 0) return 2;
    initialized = true;
    attrs = realUnit();
    check(LiSendSteamControllerAttributesEvent(0, &attrs) == LI_ERR_UNSUPPORTED, "a non-Sunshine host gets LI_ERR_UNSUPPORTED");
    drain();
    check(sends == 0, "nothing is queued for a non-Sunshine host");

    // A Sunshine-derived host (AppVersionQuad[3] < 0).
    memcpy(AppVersionQuad, (int[]){7, 1, 431, -1}, sizeof(AppVersionQuad));
    check(LiSendSteamControllerAttributesEvent(3, NULL) == -1, "a NULL attribute block is refused");
    drain();
    check(sends == 0, "a refused call queues nothing");

    // The arrival and the attributes for the same controller share one reliable channel, in order.
    check(LiSendControllerArrivalEvent(3, 0x8, LI_CTYPE_STEAM, 0xFFFF, LI_CCAP_STEAM_ATTRIBUTES) == 0, "arrival accepted");
    check(LiSendSteamControllerAttributesEvent(3, &attrs) == 0, "attributes accepted after the arrival");
    drain();
    // The arrival also sends a legacy multi-controller event (unordered channel), so three packets.
    check(sends == 3, "arrival, legacy state and attributes are sent");
    check(le32at(sent[0].data + 4) == SS_CONTROLLER_ARRIVAL_MAGIC && le32at(sent[2].data + 4) == SS_STEAM_CONTROLLER_ATTRIBUTES_MAGIC,
          "the attributes follow the arrival");
    check(sent[2].channel == sent[0].channel && sent[2].channel == CTRL_CHANNEL_GAMEPAD_BASE + 3,
          "the attributes use the arrival's per-controller channel");
    check(sent[2].flags == ENET_PACKET_FLAG_RELIABLE, "the attributes are sent reliably");
    check(sent[2].length == 84, "the packet is 84 bytes on the wire");
    check(be32at(sent[2].data) == 80, "header.size is big-endian and excludes itself");
    check(sent[2].data[8] == 3 && sent[2].data[9] == LI_SCATTR_TRANSPORT_USB && sent[2].data[10] == attrs.validMask && sent[2].data[11] == 0,
          "controller number, transport, valid mask and the reserved zero are in place");
    check(le32at(sent[2].data + 12) == 0x6A4D85E3 && le32at(sent[2].data + 16) == 0x68D2F92E &&
          le32at(sent[2].data + 20) == 0x4A && le32at(sent[2].data + 24) == 0 &&
          le32at(sent[2].data + 28) == 0 && le32at(sent[2].data + 32) == 0,
          "the six u32 fields are little-endian in the documented order");
    check(memcmp(sent[2].data + 36, "26e1f7ef1955\0\0\0", 16) == 0 &&
          memcmp(sent[2].data + 52, "FXA9962403787\0\0", 16) == 0 &&
          memcmp(sent[2].data + 68, "MXA2G623065D4\0\0", 16) == 0,
          "build id, unit serial and board serial are NUL padded to 16 bytes");

    // Strings: at most 15 characters, always NUL terminated, non-printable bytes replaced.
    memset(&attrs, 0, sizeof(attrs));
    attrs.transport = LI_SCATTR_TRANSPORT_PUCK;
    attrs.validMask = LI_SCATTR_SERIALS | LI_SCATTR_BUILD_ID | LI_SCATTR_DONGLE_FW_BUILD_TIME;
    attrs.dongleFirmwareBuildTime = 0x12345678;
    memset(attrs.buildId, 'A', sizeof(attrs.buildId));          // no terminator inside the field
    memcpy(attrs.unitSerial, "FX\x01\x7f\xff Z\0", 8);          // control, DEL, high bit, space
    attrs.boardSerial[0] = '\0';
    check(LiSendSteamControllerAttributesEvent(16 + 5, &attrs) == 0, "attributes with hostile strings accepted");
    drain();
    check(sends == 1 && sent[0].data[8] == 5 && sent[0].channel == CTRL_CHANNEL_GAMEPAD_BASE + 5,
          "the controller number wraps at MAX_GAMEPADS like the other controller packets");
    check(sent[0].data[9] == LI_SCATTR_TRANSPORT_PUCK && le32at(sent[0].data + 32) == 0x12345678,
          "the puck transport and dongle firmware build time pass through");
    check(memcmp(sent[0].data + 36, "AAAAAAAAAAAAAAA\0", 16) == 0, "an unterminated 16-byte build id is cut to 15 characters plus NUL");
    check(memcmp(sent[0].data + 52, "FX??? Z\0\0\0\0\0\0\0\0\0", 16) == 0, "non-printable serial bytes become '?' and the space survives");
    check(memcmp(sent[0].data + 68, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0, "an empty serial stays all zero");

    // Not initialised: the usual -2.
    initialized = false;
    check(LiSendSteamControllerAttributesEvent(0, &attrs) == -2, "an uninitialised stream returns -2");
    initialized = true;

    printf("Steam attributes: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
