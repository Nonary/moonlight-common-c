// SPDX-License-Identifier: GPL-3.0-or-later
// Versioned DualSense extended controls & audio status wire contract.
// All multibyte fields are little-endian unless noted (NV_INPUT_HEADER.size is BE32).
#pragma once

#include <stddef.h>
#include <stdint.h>

// Session feature flag in x-ml-general.featureFlags (SDP).
// Advertised when the client registers ConnListenerDualSenseOutput.
#define ML_FF_DUALSENSE_CONTROLS 0x10

// Host -> Client control stream packet carrying DualSense extended outputs.
#define ML_DUALSENSE_OUTPUT_PACKET_TYPE 0x5506
#define ML_DUALSENSE_OUTPUT_VERSION 1
#define ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE 12

// Bitmask of valid fields in ML_DUALSENSE_OUTPUT_PACKET_TYPE.
#define ML_DS_OUT_PLAYER_LEDS      0x0001
#define ML_DS_OUT_LED_BRIGHTNESS   0x0002
#define ML_DS_OUT_MIC_LED          0x0004
#define ML_DS_OUT_SPEAKER_VOLUME   0x0008
#define ML_DS_OUT_HEADPHONE_VOLUME 0x0010
#define ML_DS_OUT_ALL_MASK         0x001F

// Client -> Host reliable input packet reporting 3.5 mm jack & mic mute state.
#define ML_DUALSENSE_AUDIO_STATUS_MAGIC 0x5500000A
#define ML_DUALSENSE_AUDIO_STATUS_SIZE 12

// Bitmask of flags in ML_DUALSENSE_AUDIO_STATUS_MAGIC (matches DS5 USB input byte 54).
#define ML_DS_AUDIO_HEADPHONES_CONNECTED  0x01
#define ML_DS_AUDIO_HEADSET_MIC_CONNECTED 0x02
#define ML_DS_AUDIO_MIC_MUTED             0x04
#define ML_DS_AUDIO_STATUS_MASK           0x07

typedef struct _ML_DUALSENSE_OUTPUT {
    uint16_t controllerNumber;
    uint16_t validMask;
    uint8_t playerLeds;
    uint8_t ledBrightness;
    uint8_t micLed;
    uint8_t speakerVolume;
    uint8_t headphoneVolume;
} ML_DUALSENSE_OUTPUT;

static inline uint16_t MlDualSenseRead16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline void MlDualSenseWrite16(uint8_t* p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static inline void MlDualSenseWrite32(uint8_t* p, uint32_t n) {
    MlDualSenseWrite16(p, (uint16_t)n);
    MlDualSenseWrite16(p + 2, (uint16_t)(n >> 16));
}

static inline int MlDualSenseOutputBuild(uint8_t* p, size_t size, const ML_DUALSENSE_OUTPUT* out) {
    if (!p || !out || size != ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE) return 0;
    if (out->controllerNumber >= 16 || out->validMask == 0 || (out->validMask & ~ML_DS_OUT_ALL_MASK) != 0) return 0;
    if ((out->validMask & ML_DS_OUT_LED_BRIGHTNESS) && out->ledBrightness > 2) return 0;
    if ((out->validMask & ML_DS_OUT_MIC_LED) && out->micLed > 2) return 0;
    if ((out->validMask & ML_DS_OUT_HEADPHONE_VOLUME) && out->headphoneVolume > 0x7F) return 0;

    p[0] = ML_DUALSENSE_OUTPUT_VERSION;
    p[1] = 0;
    MlDualSenseWrite16(p + 2, out->controllerNumber);
    MlDualSenseWrite16(p + 4, out->validMask);
    p[6] = (out->validMask & ML_DS_OUT_PLAYER_LEDS) ? (uint8_t)(out->playerLeds & 0x1F) : 0;
    p[7] = (out->validMask & ML_DS_OUT_LED_BRIGHTNESS) ? out->ledBrightness : 0;
    p[8] = (out->validMask & ML_DS_OUT_MIC_LED) ? out->micLed : 0;
    p[9] = (out->validMask & ML_DS_OUT_SPEAKER_VOLUME) ? out->speakerVolume : 0;
    p[10] = (out->validMask & ML_DS_OUT_HEADPHONE_VOLUME) ? out->headphoneVolume : 0;
    p[11] = 0;
    return 1;
}

static inline int MlDualSenseOutputParse(const uint8_t* p, size_t size, ML_DUALSENSE_OUTPUT* out) {
    uint16_t controller;
    uint16_t validMask;
    uint8_t playerLeds;
    uint8_t ledBrightness;
    uint8_t micLed;
    uint8_t speakerVolume;
    uint8_t headphoneVolume;

    if (!p || !out || size != ML_DUALSENSE_OUTPUT_PAYLOAD_SIZE) return 0;
    if (p[0] != ML_DUALSENSE_OUTPUT_VERSION || p[1] != 0 || p[11] != 0) return 0;
    controller = MlDualSenseRead16(p + 2);
    validMask = MlDualSenseRead16(p + 4);
    if (controller >= 16 || validMask == 0 || (validMask & ~ML_DS_OUT_ALL_MASK) != 0) return 0;

    playerLeds = (uint8_t)(p[6] & 0x1F);
    ledBrightness = p[7];
    micLed = p[8];
    speakerVolume = p[9];
    headphoneVolume = p[10];

    if ((validMask & ML_DS_OUT_LED_BRIGHTNESS) && ledBrightness > 2) return 0;
    if ((validMask & ML_DS_OUT_MIC_LED) && micLed > 2) return 0;
    if ((validMask & ML_DS_OUT_HEADPHONE_VOLUME) && headphoneVolume > 0x7F) return 0;

    out->controllerNumber = controller;
    out->validMask = validMask;
    out->playerLeds = playerLeds;
    out->ledBrightness = ledBrightness;
    out->micLed = micLed;
    out->speakerVolume = speakerVolume;
    out->headphoneVolume = headphoneVolume;
    return 1;
}

static inline int MlDualSenseAudioStatusBuild(uint8_t* p, size_t size,
                                              uint8_t controllerNumber, uint8_t flags) {
    if (!p || size != ML_DUALSENSE_AUDIO_STATUS_SIZE || controllerNumber >= 16) return 0;
    if ((flags & ~ML_DS_AUDIO_STATUS_MASK) != 0) return 0;
    p[0] = p[1] = p[2] = 0;
    p[3] = ML_DUALSENSE_AUDIO_STATUS_SIZE - 4;
    MlDualSenseWrite32(p + 4, ML_DUALSENSE_AUDIO_STATUS_MAGIC);
    p[8] = controllerNumber;
    p[9] = flags;
    p[10] = 0;
    p[11] = 0;
    return 1;
}
