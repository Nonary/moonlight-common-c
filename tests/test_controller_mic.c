// Unit tests for Client -> Host controller microphone audio (0x5603) wire format & guards.
#include "../src/ControllerMic.h"
#include "../src/Limelight-internal.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    uint8_t opusFrame[40];
    uint8_t packet[ML_CONTROLLER_MIC_MAX_PACKET_SIZE];
    ML_CONTROLLER_MIC_PACKET parsed = {0};

    for (size_t i = 0; i < sizeof(opusFrame); ++i) {
        opusFrame[i] = (uint8_t)(0xA0 + i);
    }

    // Valid round-trip build and parse.
    size_t built = MlMicBuildPacket(packet, sizeof(packet), 3, 0x12345678u, 960, opusFrame, sizeof(opusFrame));
    CHECK(built == ML_CONTROLLER_MIC_HEADER_SIZE + sizeof(opusFrame));
    CHECK(MlMicParsePacket(packet, built, &parsed) == 1);
    CHECK(parsed.controllerNumber == 3);
    CHECK(parsed.sequence == 0x12345678u);
    CHECK(parsed.sampleCount == 960);
    CHECK(parsed.payloadBytes == sizeof(opusFrame));
    CHECK(memcmp(parsed.payload, opusFrame, sizeof(opusFrame)) == 0);

    // Reject invalid build inputs.
    CHECK(MlMicBuildPacket(packet, sizeof(packet), 16, 1, 960, opusFrame, sizeof(opusFrame)) == 0);
    CHECK(MlMicBuildPacket(packet, sizeof(packet), 0, 1, 0, opusFrame, sizeof(opusFrame)) == 0);
    CHECK(MlMicBuildPacket(packet, sizeof(packet), 0, 1, ML_CONTROLLER_MIC_MAX_SAMPLES + 1, opusFrame, sizeof(opusFrame)) == 0);
    CHECK(MlMicBuildPacket(packet, sizeof(packet), 0, 1, 960, opusFrame, 0) == 0);
    CHECK(MlMicBuildPacket(packet, sizeof(packet), 0, 1, 960, opusFrame, ML_CONTROLLER_MIC_MAX_PAYLOAD + 1) == 0);
    CHECK(MlMicBuildPacket(packet, built - 1, 0, 1, 960, opusFrame, sizeof(opusFrame)) == 0);

    // Reject malformed packets during parse.
    uint8_t bad[ML_CONTROLLER_MIC_MAX_PACKET_SIZE];
    memcpy(bad, packet, built);
    bad[0] = 2; // bad version
    CHECK(MlMicParsePacket(bad, built, &parsed) == 0);

    memcpy(bad, packet, built);
    bad[1] = 2; // unknown codec
    CHECK(MlMicParsePacket(bad, built, &parsed) == 0);

    memcpy(bad, packet, built);
    bad[2] = 16; // controller >= 16
    CHECK(MlMicParsePacket(bad, built, &parsed) == 0);

    CHECK(MlMicParsePacket(packet, built - 1, &parsed) == 0);
    CHECK(MlMicParsePacket(packet, ML_CONTROLLER_MIC_HEADER_SIZE - 1, &parsed) == 0);

    // Opt-in API guards when disconnected or disabled.
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 431; AppVersionQuad[3] = -1;
    LiSetControllerMicEnabled(false);
    CHECK(LiSendControllerMicAudio(0, 1, 960, opusFrame, sizeof(opusFrame)) == LI_ERR_UNSUPPORTED);

    LiSetControllerMicEnabled(true);
    AppVersionQuad[3] = 0; // non-Sunshine host
    CHECK(LiSendControllerMicAudio(0, 1, 960, opusFrame, sizeof(opusFrame)) == LI_ERR_UNSUPPORTED);

    AppVersionQuad[3] = -1; // Sunshine host, but not connected
    CHECK(LiSendControllerMicAudio(0, 1, 960, opusFrame, sizeof(opusFrame)) == -2);

    LiSetControllerMicEnabled(false);
    printf("PASS: Controller mic wire build/parse and LiSendControllerMicAudio opt-in guards\n");
    return 0;
}
