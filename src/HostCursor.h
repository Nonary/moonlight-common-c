// SPDX-License-Identifier: GPL-3.0-or-later
// Versioned Vibeshine extension: client-side cursor. All multibyte fields are LE.
// The host copy lives in Vibeshine's src/host_cursor_protocol.h; the design is in
// Vibeshine's docs/client-cursor-design.md.
#pragma once
#include "Limelight.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ML_HOST_CURSOR_SHAPE_TYPE 0x5610   // host -> client
#define ML_HOST_CURSOR_STATE_TYPE 0x5611   // host -> client
#define ML_HOST_CURSOR_CONTROL_TYPE 0x5612 // client -> host
#define ML_HOST_CURSOR_CHANNEL 0x09
#define ML_HOST_CURSOR_VERSION 1
#define ML_HOST_CURSOR_FORMAT_BGRA8 1
#define ML_HOST_CURSOR_SHAPE_HEADER_SIZE 28
#define ML_HOST_CURSOR_MAX_CHUNK 16384
#define ML_HOST_CURSOR_STATE_SIZE 44
#define ML_HOST_CURSOR_CONTROL_SIZE 8
#define ML_HOST_CURSOR_STATE_FLAGS_ALL 0x3F
#define ML_HOST_CURSOR_CONTROL_FLAGS_ALL 0x03

static inline uint16_t MlHostCursorRead16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t MlHostCursorRead32(const uint8_t* p) {
    return (uint32_t)MlHostCursorRead16(p) | ((uint32_t)MlHostCursorRead16(p + 2) << 16);
}

// CURSOR_STATE: version, flags, reserved16, seq, shape id, reference width/height,
// scale (16.16), x, y, clip left/top/right/bottom (signed). Returns 0 if invalid.
static inline int MlHostCursorParseState(const uint8_t* p, size_t size, LI_HOST_CURSOR_STATE* out) {
    if (!p || size != ML_HOST_CURSOR_STATE_SIZE || p[0] != ML_HOST_CURSOR_VERSION ||
        (p[1] & ~ML_HOST_CURSOR_STATE_FLAGS_ALL) || MlHostCursorRead16(p + 2) != 0) return 0;
    LI_HOST_CURSOR_STATE s;
    s.flags = p[1];
    s.sequence = MlHostCursorRead32(p + 4);
    s.shapeId = MlHostCursorRead32(p + 8);
    s.referenceWidth = MlHostCursorRead16(p + 12);
    s.referenceHeight = MlHostCursorRead16(p + 14);
    s.scaleQ16 = MlHostCursorRead32(p + 16);
    s.x = (int32_t)MlHostCursorRead32(p + 20);
    s.y = (int32_t)MlHostCursorRead32(p + 24);
    s.clipLeft = (int32_t)MlHostCursorRead32(p + 28);
    s.clipTop = (int32_t)MlHostCursorRead32(p + 32);
    s.clipRight = (int32_t)MlHostCursorRead32(p + 36);
    s.clipBottom = (int32_t)MlHostCursorRead32(p + 40);
    if (s.referenceWidth == 0 || s.referenceHeight == 0 || s.scaleQ16 == 0) return 0;
    if ((s.flags & LI_HOST_CURSOR_HAS_CLIP) && (s.clipLeft > s.clipRight || s.clipTop > s.clipBottom)) return 0;
    *out = s;
    return 1;
}

// One shape in flight at a time: chunks arrive in order on a reliable channel.
typedef struct _ML_HOST_CURSOR_ASSEMBLY {
    uint32_t shapeId;
    uint16_t width;
    uint16_t height;
    uint16_t hotspotX;
    uint16_t hotspotY;
    uint32_t total;
    uint32_t received;
    uint8_t* pixels;
} ML_HOST_CURSOR_ASSEMBLY;

static inline void MlHostCursorResetAssembly(ML_HOST_CURSOR_ASSEMBLY* a) {
    free(a->pixels);
    memset(a, 0, sizeof(*a));
}

// CURSOR_SHAPE chunk: version, format, flags16, shape id, width, height, hotspot x/y,
// total size, chunk offset, chunk length, reserved16, then the chunk's pixels.
static inline int MlHostCursorShapeChunkValid(const uint8_t* p, size_t size) {
    if (!p || size < ML_HOST_CURSOR_SHAPE_HEADER_SIZE || p[0] != ML_HOST_CURSOR_VERSION ||
        p[1] != ML_HOST_CURSOR_FORMAT_BGRA8 || MlHostCursorRead16(p + 2) != 0 || MlHostCursorRead16(p + 26) != 0) return 0;
    uint32_t id = MlHostCursorRead32(p + 4);
    uint32_t w = MlHostCursorRead16(p + 8), h = MlHostCursorRead16(p + 10);
    uint32_t total = MlHostCursorRead32(p + 16), offset = MlHostCursorRead32(p + 20);
    uint32_t length = MlHostCursorRead16(p + 24);
    return id != 0 && w != 0 && h != 0 && w <= LI_HOST_CURSOR_MAX_DIMENSION && h <= LI_HOST_CURSOR_MAX_DIMENSION &&
        MlHostCursorRead16(p + 12) < w && MlHostCursorRead16(p + 14) < h && total == w * h * 4 &&
        length != 0 && length <= ML_HOST_CURSOR_MAX_CHUNK && size == ML_HOST_CURSOR_SHAPE_HEADER_SIZE + (size_t)length &&
        offset <= total && length <= total - offset;
}

// Returns 1 when a shape is complete (*out borrows the assembly's pixels until the
// next feed or reset), 0 when more chunks are needed, and -1 for anything invalid,
// which also discards a partial shape.
static inline int MlHostCursorFeedShape(ML_HOST_CURSOR_ASSEMBLY* a, const uint8_t* p, size_t size, LI_HOST_CURSOR_SHAPE* out) {
    uint32_t id, total, offset, length;
    uint16_t w, h, hx, hy;

    if (!MlHostCursorShapeChunkValid(p, size)) {
        MlHostCursorResetAssembly(a);
        return -1;
    }
    id = MlHostCursorRead32(p + 4);
    w = MlHostCursorRead16(p + 8);
    h = MlHostCursorRead16(p + 10);
    hx = MlHostCursorRead16(p + 12);
    hy = MlHostCursorRead16(p + 14);
    total = MlHostCursorRead32(p + 16);
    offset = MlHostCursorRead32(p + 20);
    length = MlHostCursorRead16(p + 24);

    if (offset == 0) {
        uint8_t* pixels = (uint8_t*)realloc(a->pixels, total);
        if (!pixels) {
            MlHostCursorResetAssembly(a);
            return -1;
        }
        a->pixels = pixels;
        a->shapeId = id;
        a->width = w;
        a->height = h;
        a->hotspotX = hx;
        a->hotspotY = hy;
        a->total = total;
        a->received = 0;
    }
    else if (a->pixels == NULL || a->shapeId != id || a->width != w || a->height != h ||
             a->hotspotX != hx || a->hotspotY != hy || a->total != total || a->received != offset) {
        // Not the continuation of the shape in flight: a lost or reordered chunk.
        MlHostCursorResetAssembly(a);
        return -1;
    }

    memcpy(a->pixels + offset, p + ML_HOST_CURSOR_SHAPE_HEADER_SIZE, length);
    a->received += length;
    if (a->received < a->total) {
        return 0;
    }

    out->shapeId = a->shapeId;
    out->width = a->width;
    out->height = a->height;
    out->hotspotX = a->hotspotX;
    out->hotspotY = a->hotspotY;
    out->pixels = a->pixels;
    a->shapeId = 0;
    a->received = 0;
    return 1;
}

// CURSOR_CONTROL payload: version, flags, reserved16, resend shape id.
static inline void MlHostCursorEncodeControl(uint8_t out[ML_HOST_CURSOR_CONTROL_SIZE], uint8_t flags, uint32_t resendShapeId) {
    out[0] = ML_HOST_CURSOR_VERSION;
    out[1] = (uint8_t)(flags & ML_HOST_CURSOR_CONTROL_FLAGS_ALL);
    out[2] = 0;
    out[3] = 0;
    out[4] = (uint8_t)resendShapeId;
    out[5] = (uint8_t)(resendShapeId >> 8);
    out[6] = (uint8_t)(resendShapeId >> 16);
    out[7] = (uint8_t)(resendShapeId >> 24);
}
