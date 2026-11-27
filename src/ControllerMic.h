// SPDX-License-Identifier: GPL-3.0-or-later
// Versioned Client -> Host controller microphone audio wire contract (Opus 48 kHz mono).
// All multibyte header fields are little-endian.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Session feature flag in x-ml-general.featureFlags (SDP).
// Advertised when the client enables controller microphone streaming via LiSetControllerMicEnabled(true).
#define ML_FF_CONTROLLER_MIC_AUDIO 0x20

// Client -> Host control stream packet carrying an Opus-encoded controller microphone frame.
#define ML_CONTROLLER_MIC_PACKET_TYPE 0x5603
#define ML_CONTROLLER_MIC_VERSION 1
#define ML_CONTROLLER_MIC_CODEC_OPUS_48K_MONO 1
#define ML_CONTROLLER_MIC_HEADER_SIZE 12
#define ML_CONTROLLER_MIC_MAX_PAYLOAD 512
#define ML_CONTROLLER_MIC_MAX_PACKET_SIZE (ML_CONTROLLER_MIC_HEADER_SIZE + ML_CONTROLLER_MIC_MAX_PAYLOAD)
#define ML_CONTROLLER_MIC_MAX_SAMPLES 2880 // 60 ms at 48 kHz

typedef struct _ML_CONTROLLER_MIC_PACKET {
    uint16_t controllerNumber;
    uint32_t sequence;
    uint16_t sampleCount;
    uint16_t payloadBytes;
    const uint8_t* payload;
} ML_CONTROLLER_MIC_PACKET;

static inline uint16_t MlMicRead16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t MlMicRead32(const uint8_t* p) {
    return (uint32_t)MlMicRead16(p) | ((uint32_t)MlMicRead16(p + 2) << 16);
}

static inline void MlMicWrite16(uint8_t* p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static inline void MlMicWrite32(uint8_t* p, uint32_t n) {
    MlMicWrite16(p, (uint16_t)n);
    MlMicWrite16(p + 2, (uint16_t)(n >> 16));
}

static inline size_t MlMicBuildPacket(uint8_t* dst, size_t dstCap,
                                      uint8_t controllerNumber,
                                      uint32_t sequence,
                                      uint16_t sampleCount,
                                      const uint8_t* opusData,
                                      uint16_t opusLength) {
    size_t totalSize;

    if (!dst || !opusData) return 0;
    if (controllerNumber >= 16) return 0;
    if (sampleCount == 0 || sampleCount > ML_CONTROLLER_MIC_MAX_SAMPLES) return 0;
    if (opusLength == 0 || opusLength > ML_CONTROLLER_MIC_MAX_PAYLOAD) return 0;

    totalSize = (size_t)ML_CONTROLLER_MIC_HEADER_SIZE + opusLength;
    if (dstCap < totalSize) return 0;

    dst[0] = ML_CONTROLLER_MIC_VERSION;
    dst[1] = ML_CONTROLLER_MIC_CODEC_OPUS_48K_MONO;
    MlMicWrite16(dst + 2, controllerNumber);
    MlMicWrite32(dst + 4, sequence);
    MlMicWrite16(dst + 8, sampleCount);
    MlMicWrite16(dst + 10, opusLength);
    memcpy(dst + ML_CONTROLLER_MIC_HEADER_SIZE, opusData, opusLength);
    return totalSize;
}

static inline int MlMicParsePacket(const uint8_t* src, size_t srcSize, ML_CONTROLLER_MIC_PACKET* out) {
    uint16_t controller;
    uint32_t sequence;
    uint16_t sampleCount;
    uint16_t payloadBytes;

    if (!src || !out || srcSize < ML_CONTROLLER_MIC_HEADER_SIZE) return 0;
    if (src[0] != ML_CONTROLLER_MIC_VERSION || src[1] != ML_CONTROLLER_MIC_CODEC_OPUS_48K_MONO) return 0;

    controller = MlMicRead16(src + 2);
    sequence = MlMicRead32(src + 4);
    sampleCount = MlMicRead16(src + 8);
    payloadBytes = MlMicRead16(src + 10);

    if (controller >= 16) return 0;
    if (sampleCount == 0 || sampleCount > ML_CONTROLLER_MIC_MAX_SAMPLES) return 0;
    if (payloadBytes == 0 || payloadBytes > ML_CONTROLLER_MIC_MAX_PAYLOAD) return 0;
    if (srcSize != (size_t)ML_CONTROLLER_MIC_HEADER_SIZE + payloadBytes) return 0;

    out->controllerNumber = controller;
    out->sequence = sequence;
    out->sampleCount = sampleCount;
    out->payloadBytes = payloadBytes;
    out->payload = src + ML_CONTROLLER_MIC_HEADER_SIZE;
    return 1;
}
