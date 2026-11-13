#include "AudioConfig.h"
#include "AudioTestAdapter.h"
#ifndef LI_AUDIO_PARSER_ONLY
#include "Limelight-internal.h"
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/foundation_audio_sdp.h"

static int failures, assertions, cases;
static void check(bool result, const char* expression, int line) {
    assertions++;
    if (!result) { failures++; printf("FAIL protocol line %d: %s\n", line, expression); }
}
#define CHECK(x) check((x), #x, __LINE__)
#define N12 "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10,11"
#define H12 "a=fmtp:97 surround-params=12,12,0,0,1,2,3,4,5,6,7,8,9,10,11"

static int parse(const char* sdp, size_t length, int channels, POPUS_MULTISTREAM_CONFIGURATION normal,
                 POPUS_MULTISTREAM_CONFIGURATION high, bool* supported) {
    return LiParseAudioConfigurations(sdp, length, channels, normal, high, supported);
}

static void expectInvalid(const char* sdp, size_t length) {
    OPUS_MULTISTREAM_CONFIGURATION n, h;
    bool supported = true;
    cases++;
    CHECK(parse(sdp, length, 12, &n, &h, &supported) != 0);
    CHECK(!supported);
}

#ifndef LI_AUDIO_PARSER_ONLY
static void testGeneratedSdp(int channels, int bitrate, int capabilities, bool advertisedHq, bool expectedHq, int duration) {
    int length = 0;
    cases++;
    LiInitializeStreamConfiguration(&StreamConfig);
    StreamConfig.width = 3840;
    StreamConfig.height = 2160;
    StreamConfig.fps = 120;
    StreamConfig.bitrate = bitrate;
    StreamConfig.packetSize = 1024;
    StreamConfig.streamingRemotely = STREAM_CFG_LOCAL;
    StreamConfig.audioConfiguration = (int)(channels == 12 ? AUDIO_CONFIGURATION_714_SURROUND :
        channels == 8 ? AUDIO_CONFIGURATION_71_SURROUND : channels == 6 ? AUDIO_CONFIGURATION_51_SURROUND : AUDIO_CONFIGURATION_STEREO);
    AudioCallbacks.capabilities = capabilities;
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 500; AppVersionQuad[3] = -1;
    HighQualitySurroundSupported = advertisedHq;
    RemoteAddr.ss_family = AF_INET;
    VideoPortNumber = 47998;
    NegotiatedVideoFormat = VIDEO_FORMAT_H264;
    char* sdp = getSdpPayloadForStreamConfig(14, &length);
    CHECK(sdp != NULL);
    if (sdp == NULL) return;
    CHECK(length > 0 && (size_t)length == strlen(sdp));
    CHECK(HighQualitySurroundEnabled == expectedHq);
    CHECK(AudioPacketDuration == duration);
    CHECK(strstr(sdp, expectedHq ? "x-nv-audio.surround.AudioQuality:1 " : "x-nv-audio.surround.AudioQuality:0 ") != NULL);
    if (channels == 12) {
        CHECK(strstr(sdp, "x-nv-audio.surround.numChannels:12 ") != NULL);
        CHECK(strstr(sdp, "x-nv-audio.surround.channelMask:63039 ") != NULL);
        CHECK(strstr(sdp, "x-nv-aqos.packetDuration:5 ") != NULL);
    }
    free(sdp);
}
#endif

int runAudioProtocolTests(void) {
    OPUS_MULTISTREAM_CONFIGURATION n, h;
    bool supported;
    failures = assertions = cases = 0;
    cases++;
    CHECK(AUDIO_CONFIGURATION_MAX_CHANNEL_COUNT == 12);
    CHECK(AUDIO_CONFIGURATION_714_SURROUND == UINT32_C(0xF63F0CCA));
    CHECK(CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION((int32_t)AUDIO_CONFIGURATION_714_SURROUND) == 12);
    CHECK(CHANNEL_MASK_FROM_AUDIO_CONFIGURATION((int32_t)AUDIO_CONFIGURATION_714_SURROUND) == 0xF63F);
    CHECK(MAKE_AUDIO_CONFIGURATION(12, 0x12D63F) == UINT32_C(0xD63F0CCA));
    CHECK(SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(AUDIO_CONFIGURATION_714_SURROUND) == UINT32_C(0xF63F000C));
    char wireText[24];
    snprintf(wireText, sizeof(wireText), "%" PRIu32, SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(AUDIO_CONFIGURATION_714_SURROUND));
    CHECK(strcmp(wireText, "4131323916") == 0);

    const char* valid12[] = { N12, "v=0\r\n" N12 "\r\n", N12 "\n" H12 "\n", N12 "\n" N12 "\n" N12 "\n" H12 "\n" H12 };
    for (unsigned j = 0; j < sizeof(valid12) / sizeof(valid12[0]); j++) {
        cases++;
        CHECK(parse(valid12[j], strlen(valid12[j]), 12, &n, &h, &supported) == 0);
        CHECK(n.channelCount == 12 && n.streams == 8 && n.coupledStreams == 4 && n.sampleRate == 48000);
        CHECK(!supported);
        for (int i = 0; i < 12; i++) CHECK(n.mapping[i] == i); // LFE/heights must never rotate.
        if (j >= 2) CHECK(h.channelCount == 12 && h.streams == 12 && h.coupledStreams == 0);
    }
    // Every incomplete prefix is passed with its real allocation boundary.
    for (size_t length = 0; length < strlen(N12); length++) {
        char* bounded = malloc(length ? length : 1);
        if (length) memcpy(bounded, N12, length);
        expectInvalid(bounded, length);
        free(bounded);
    }
    const char* invalid[] = {
        "", H12, H12 "\n" N12,
        "x" N12, "a=fmtp:97 surround-params=120,8,4,0,1,2,3,4,5,6,7,8,9,10,11",
        "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10,10",
        "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10,255",
        "a=fmtp:97 surround-params=12,9,3,0,1,2,3,4,5,6,7,8,9,10,11",
        "a=fmtp:97 surround-params=12,8,9,0,1,2,3,4,5,6,7,8,9,10,11",
        "a=fmtp:97 surround-params=12,0,0,0,1,2,3,4,5,6,7,8,9,10,11",
        "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10,-1",
        "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10,256",
        "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10,999999999999999999999",
        N12 ",0", N12 ",", N12 "x", N12 "\rjunk", N12 "\n" H12 ",0",
        "a=fmtp:97 surround-params=12,,4,0,1,2,3,4,5,6,7,8,9,10,11",
        "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10, 11"
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) expectInvalid(invalid[i], strlen(invalid[i]));
    char embedded[] = N12 "\nignored";
    embedded[sizeof(N12)] = '\0';
    expectInvalid(embedded, sizeof(embedded) - 1);
    char longLine[400];
    memset(longLine, '0', sizeof(longLine));
    memcpy(longLine, "a=fmtp:97 surround-params=", 26);
    expectInvalid(longLine, sizeof(longLine));

    const struct { int channels; const char* sdp; const unsigned char expected[8]; } legacy[] = {
        { 2, "v=0\r\n", {0,1} },
        { 6, "", {0,4,1,5,2,3} },
        { 6, "a=fmtp:97 surround-params=642012345\r\na=fmtp:97 surround-params=660012345\r\n", {0,1,2,5,3,4} },
        { 8, "a=fmtp:97 surround-params=85301234567\na=fmtp:97 surround-params=88001234567\n", {0,1,2,7,3,4,5,6} },
        { 8, "a=fmtp:97 surround-params=8,5,3,0,1,2,3,4,5,6,7\na=fmtp:97 surround-params=8,8,0,0,1,2,3,4,5,6,7\n", {0,1,2,7,3,4,5,6} },
        { 6, "a=fmtp:97 surround-params=6,4,2,0,0,255,3,4,5", {0,0,255,5,3,4} }
    };
    for (unsigned i = 0; i < sizeof(legacy) / sizeof(legacy[0]); i++) {
        cases++;
        CHECK(parse(legacy[i].sdp, strlen(legacy[i].sdp), legacy[i].channels, &n, &h, &supported) == 0);
        CHECK(n.sampleRate == 48000 && n.channelCount == legacy[i].channels);
        CHECK(memcmp(n.mapping, legacy[i].expected, (size_t)legacy[i].channels) == 0);
        CHECK(supported == (i >= 2 && i <= 4));
        if (supported) for (int j = 0; j < h.channelCount; j++) CHECK(h.mapping[j] == j);
    }
    const int profileChannels[] = { 2, 6, 8, 12 };
    for (unsigned i = 0; i < sizeof(profileChannels) / sizeof(profileChannels[0]); i++) {
        cases++;
        int channels = profileChannels[i];
        CHECK(parse(foundationAudioSdp, strlen(foundationAudioSdp), channels, &n, &h, &supported) == 0);
        CHECK(n.channelCount == channels && n.streams + n.coupledStreams == channels);
        CHECK(supported == (channels == 6 || channels == 8));
        for (int j = 0; j < channels; j++) CHECK(n.mapping[j] == j);
        if (channels > 2) for (int j = 0; j < channels; j++) CHECK(h.mapping[j] == j);
    }
    char custom[1024];
    snprintf(custom, sizeof(custom), N12 "\n" N12 "\n%s", foundationAudioSdp);
    cases++;
    CHECK(parse(custom, strlen(custom), 12, &n, &h, &supported) == 0);
    CHECK(!supported && n.streams == 8 && n.coupledStreams == 4 && h.streams == 12 && h.coupledStreams == 0);
    snprintf(custom, sizeof(custom), "a=fmtp:97 surround-params=642012345\na=fmtp:97 surround-params=642012345\n%s", foundationAudioSdp);
    cases++;
    CHECK(parse(custom, strlen(custom), 6, &n, &h, &supported) == 0);
    CHECK(supported && n.streams == 4 && h.streams == 4 && n.mapping[3] == 5 && h.mapping[3] == 3);
    const char* unrelated[] = {
        "a=fmtp:97 surround-params=12,8,4,0,0,2,3,4,5,6,7,8,9,10,11\n",
        "a=fmtp:97 surround-params=12,8,4,0,1,2,3,4,5,6,7,8,9,10,255\n",
        "a=fmtp:97 surround-params=12,9,3,0,1,2,3,4,5,6,7,8,9,10,11\n",
        "a=fmtp:97 surround-params=16,16,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n",
        "a=fmtp:97 surround-params=16,malformed\n",
        "a=fmtp:97 surround-params=12,8,4,broken\n",
        "a=fmtp:97 surround-params=120,malformed\n"
    };
    // Legacy profiles consume the first two matching offers. Extra offers
    // must not invalidate a session after that selection has finished.
    for (int channels = 6; channels <= 8; channels += 2) {
        snprintf(custom, sizeof(custom), "%s\na=fmtp:97 surround-params=%d,malformed\n",
                 foundationAudioSdp, channels);
        cases++;
        CHECK(parse(custom, strlen(custom), channels, &n, &h, &supported) == 0);
        CHECK(supported && n.channelCount == channels && h.channelCount == channels);
    }
    for (int channels = 6; channels <= 8; channels += 2) {
        for (unsigned i = 0; i < sizeof(unrelated) / sizeof(unrelated[0]); i++) {
            cases++;
            snprintf(custom, sizeof(custom), "%s%s%s", unrelated[i], foundationAudioSdp, unrelated[i]);
            CHECK(parse(custom, strlen(custom), channels, &n, &h, &supported) == 0);
            CHECK(supported && n.channelCount == channels && h.channelCount == channels);
            for (int j = 0; j < channels; j++) { CHECK(n.mapping[j] == j); CHECK(h.mapping[j] == j); }
        }
    }
    cases++;
    snprintf(custom, sizeof(custom), N12 "\na=fmtp:97 surround-params=120,malformed\n");
    CHECK(parse(custom, strlen(custom), 12, &n, &h, &supported) == 0);
    CHECK(n.channelCount == 12 && n.streams == 8 && !supported);
#ifndef LI_AUDIO_PARSER_ONLY
    testGeneratedSdp(12, 150000, 0, true, false, 5);
    testGeneratedSdp(12, 1000, CAPABILITY_SLOW_OPUS_DECODER | CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION, true, false, 5);
    testGeneratedSdp(12, 150000, 0, false, false, 5);
    for (int channels = 2; channels <= 8; channels += channels == 2 ? 4 : 2) {
        testGeneratedSdp(channels, 150000, 0, true, channels > 2, 5);
        testGeneratedSdp(channels, 1000, CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION, true, false, 10);
        testGeneratedSdp(channels, 150000, CAPABILITY_SLOW_OPUS_DECODER, true, false, 10);
        testGeneratedSdp(channels, 150000, 0, false, false, 5);
    }
#endif
    printf("Audio protocol: %d cases, %d assertions, %d failures, 0 SKIP\n", cases, assertions, failures);
    return failures;
}

#ifdef LI_AUDIO_PROTOCOL_TEST_MAIN
int main(void) {
#ifdef LI_AUDIO_PARSER_ONLY
    return runAudioProtocolTests() != 0;
#else
    PltTicksInit();
    return (runAudioProtocolTests() + runAudioReceiveTests()) != 0;
#endif
}
#endif
