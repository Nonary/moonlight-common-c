// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

// Separate wire contract for controller speaker output. 0x5601 remains the
// existing actuator-only stream.
#define ML_SPEAKER_PCM_PACKET_TYPE 0x5602
#define ML_SPEAKER_PCM_CHANNEL 0x08
#define ML_SPEAKER_PCM_HEADER_SIZE 32
// Input magic08 is reserved by the pending Steam attributes extension.
#define ML_SPEAKER_BIND_MAGIC 0x55000009
#define ML_SPEAKER_BIND_SIZE 20
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

// Payload: version=2, format=1 (48 kHz S16LE stereo), controller, generation,
// sequence, sample position, frame count, flags, nonzero client owner64, then speaker samples. The
// sample position is measured in stereo frames within the nonzero generation.
static inline int MlSpeakerPcmValidate(const uint8_t *p, size_t size) {
  if (!p || size < ML_SPEAKER_PCM_HEADER_SIZE || p[0] != 2 || p[1] != 1 ||
      MlSpeakerRead16(p + 2) >= 16 || MlSpeakerRead32(p + 4) == 0 || MlSpeakerRead64(p + 24) == 0 ||
      (MlSpeakerRead16(p + 22) & ~ML_SPEAKER_PCM_FLAG_DISCONTINUITY) != 0) {
    return 0;
  }
  const uint16_t frames = MlSpeakerRead16(p + 20);
  return frames > 0 && frames <= ML_SPEAKER_PCM_MAX_FRAMES &&
         MlSpeakerRead64(p + 12) <= UINT64_MAX - frames &&
         size == ML_SPEAKER_PCM_HEADER_SIZE +
                     (size_t)frames * ML_SPEAKER_PCM_FRAME_BYTES;
}

// A reliable input packet, ordered before ordinary controller arrival on the
// same controller channel. Old hosts may ignore it and retain legacy arrival.
static inline int MlSpeakerPcmBuildBinding(uint8_t *p, size_t size,
                                          uint16_t controller, uint64_t owner) {
  if (!p || size != ML_SPEAKER_BIND_SIZE || controller >= 16 || !owner) return 0;
  p[0] = p[1] = p[2] = 0; p[3] = ML_SPEAKER_BIND_SIZE - 4;
  MlSpeakerWrite32(p + 4, ML_SPEAKER_BIND_MAGIC);
  MlSpeakerWrite16(p + 8, controller);
  MlSpeakerWrite16(p + 10, 1);
  MlSpeakerWrite64(p + 12, owner);
  return 1;
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
