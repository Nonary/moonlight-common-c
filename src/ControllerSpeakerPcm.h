// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

// Separate wire contract for controller speaker output. 0x5601 remains the
// existing actuator-only stream.
#define ML_SPEAKER_PCM_PACKET_TYPE 0x5602
#define ML_SPEAKER_PCM_CHANNEL 0x08
#define ML_SPEAKER_PCM_HEADER_SIZE 24
#define ML_SPEAKER_PCM_MAX_FRAMES 240
#define ML_SPEAKER_PCM_FRAME_BYTES 4
#define ML_SPEAKER_PCM_MAX_PAYLOAD                                             \
  (ML_SPEAKER_PCM_HEADER_SIZE +                                                \
   ML_SPEAKER_PCM_MAX_FRAMES * ML_SPEAKER_PCM_FRAME_BYTES)
#define ML_SPEAKER_PCM_FLAG_DISCONTINUITY 0x0001
// Type-scoped alias: valid only for LI_CTYPE_PS (0x02). Steam controllers
// use the same bit for attributes; never interpret it without controller type.
#define LI_CCAP_PS_SPEAKER_PCM 0x4000
#define ML_FF_CONTROLLER_SPEAKER_PCM 0x08

static inline uint16_t MlSpeakerRead16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t MlSpeakerRead32(const uint8_t *p) {
  return (uint32_t)MlSpeakerRead16(p) |
         ((uint32_t)MlSpeakerRead16(p + 2) << 16);
}
static inline uint64_t MlSpeakerRead64(const uint8_t *p) {
  return (uint64_t)MlSpeakerRead32(p) |
         ((uint64_t)MlSpeakerRead32(p + 4) << 32);
}
static inline void MlSpeakerWrite16(uint8_t *p, uint16_t n) {
  p[0] = (uint8_t)n;
  p[1] = (uint8_t)(n >> 8);
}
static inline void MlSpeakerWrite32(uint8_t *p, uint32_t n) {
  MlSpeakerWrite16(p, (uint16_t)n);
  MlSpeakerWrite16(p + 2, (uint16_t)(n >> 16));
}
static inline void MlSpeakerWrite64(uint8_t *p, uint64_t n) {
  MlSpeakerWrite32(p, (uint32_t)n);
  MlSpeakerWrite32(p + 4, (uint32_t)(n >> 32));
}

// Payload: version=1, format=1 (48 kHz S16LE stereo), controller, generation,
// sequence, sample position, frame count, flags, then speaker samples. The
// sample position is measured in stereo frames within the nonzero generation.
static inline int MlSpeakerPcmValidate(const uint8_t *p, size_t size) {
  if (!p || size < ML_SPEAKER_PCM_HEADER_SIZE || p[0] != 1 || p[1] != 1 ||
      MlSpeakerRead16(p + 2) >= 16 || MlSpeakerRead32(p + 4) == 0 ||
      (MlSpeakerRead16(p + 22) & ~ML_SPEAKER_PCM_FLAG_DISCONTINUITY) != 0) {
    return 0;
  }
  const uint16_t frames = MlSpeakerRead16(p + 20);
  return frames > 0 && frames <= ML_SPEAKER_PCM_MAX_FRAMES &&
         size == ML_SPEAKER_PCM_HEADER_SIZE +
                     (size_t)frames * ML_SPEAKER_PCM_FRAME_BYTES;
}

static inline int MlSpeakerPcmCanSend(uint32_t session_flags,
                                      uint8_t controller_type,
                                      uint16_t controller_capabilities,
                                      int source_available) {
  return source_available && controller_type == 0x02 &&
         (session_flags & ML_FF_CONTROLLER_SPEAKER_PCM) != 0 &&
         (controller_capabilities & LI_CCAP_PS_SPEAKER_PCM) != 0;
}

// Shared by the Windows endpoint writer and its portable renderer test. The
// input planes are stereo S16LE; output is four interleaved float channels.
static inline void MlSpeakerPcmRouteFourChannel(const int16_t *speaker,
                                                const int16_t *actuators,
                                                float *output, size_t frames) {
  size_t i;
  for (i = 0; i < frames; ++i) {
    output[i * 4 + 0] = speaker ? speaker[i * 2] / 32768.0f : 0.0f;
    output[i * 4 + 1] = speaker ? speaker[i * 2 + 1] / 32768.0f : 0.0f;
    output[i * 4 + 2] = actuators ? actuators[i * 2] / 32768.0f : 0.0f;
    output[i * 4 + 3] = actuators ? actuators[i * 2 + 1] / 32768.0f : 0.0f;
  }
}
