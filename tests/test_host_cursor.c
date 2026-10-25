// Host cursor extension: state parsing, shape reassembly and receive-thread dispatch,
// including every truncation. The byte vectors match Vibeshine's test_host_cursor.cpp.
#include "../src/ControlStream.c"

#include <stdio.h>

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); ++failures; } \
} while (0)

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

// The host's encode_state() output for its StateIsExactLittleEndian test, minus the header.
static const uint8_t kState[ML_HOST_CURSOR_STATE_SIZE] = {
    1, 0x05, 0, 0,
    0x04, 0x03, 0x02, 0x01,
    0x0D, 0x0C, 0x0B, 0x0A,
    0x80, 0x07, 0x38, 0x04,
    0x00, 0x80, 0x01, 0x00,
    0xFE, 0xFF, 0xFF, 0xFF,
    0x44, 0x33, 0x22, 0x11,
    1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0,
};

static size_t makeChunk(uint8_t* out, uint32_t id, uint16_t w, uint16_t h, uint32_t offset, uint16_t length, uint8_t fill) {
    memset(out, 0, ML_HOST_CURSOR_SHAPE_HEADER_SIZE);
    out[0] = ML_HOST_CURSOR_VERSION;
    out[1] = ML_HOST_CURSOR_FORMAT_BGRA8;
    put32(out + 4, id);
    put16(out + 8, w);
    put16(out + 10, h);
    put16(out + 12, 1);
    put16(out + 14, 2);
    put32(out + 16, (uint32_t)w * h * 4);
    put32(out + 20, offset);
    put16(out + 24, length);
    memset(out + ML_HOST_CURSOR_SHAPE_HEADER_SIZE, fill, length);
    return ML_HOST_CURSOR_SHAPE_HEADER_SIZE + (size_t)length;
}

static void testStateParsing(void) {
    LI_HOST_CURSOR_STATE s;
    CHECK(MlHostCursorParseState(kState, sizeof(kState), &s), "valid state rejected");
    CHECK(s.flags == (LI_HOST_CURSOR_VISIBLE | LI_HOST_CURSOR_HAS_POSITION) && s.sequence == 0x01020304 &&
          s.shapeId == 0x0A0B0C0D && s.referenceWidth == 1920 && s.referenceHeight == 1080 &&
          s.scaleQ16 == 0x18000 && s.x == -2 && s.y == 0x11223344 && s.clipLeft == 1 && s.clipTop == 2 &&
          s.clipRight == 3 && s.clipBottom == 4, "state fields");

    uint8_t bytes[ML_HOST_CURSOR_STATE_SIZE + 4];
    for (size_t size = 0; size <= sizeof(bytes); ++size) {
        memset(bytes, 0, sizeof(bytes));
        memcpy(bytes, kState, size < sizeof(kState) ? size : sizeof(kState));
        CHECK(MlHostCursorParseState(bytes, size, &s) == (size == sizeof(kState)), "state of %d bytes", (int)size);
    }
    CHECK(!MlHostCursorParseState(NULL, sizeof(kState), &s), "NULL state");

    struct { size_t offset; uint8_t value; const char* what; } bad[] = {
        {0, 2, "version"}, {1, 0x40, "unknown flag"}, {2, 1, "reserved"}, {3, 1, "reserved"},
        {12, 0, "zero width"}, {14, 0, "zero height"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memcpy(bytes, kState, sizeof(kState));
        bytes[bad[i].offset] = bad[i].value;
        if (bad[i].offset == 12 || bad[i].offset == 14) bytes[bad[i].offset + 1] = 0;
        CHECK(!MlHostCursorParseState(bytes, sizeof(kState), &s), "%s accepted", bad[i].what);
    }
    memcpy(bytes, kState, sizeof(kState));
    memset(bytes + 16, 0, 4);
    CHECK(!MlHostCursorParseState(bytes, sizeof(kState), &s), "zero scale accepted");
    memcpy(bytes, kState, sizeof(kState));
    bytes[1] |= LI_HOST_CURSOR_HAS_CLIP;
    put32(bytes + 28, 10);  // left > right
    CHECK(!MlHostCursorParseState(bytes, sizeof(kState), &s), "inverted clip accepted");
    bytes[1] &= ~LI_HOST_CURSOR_HAS_CLIP;
    CHECK(MlHostCursorParseState(bytes, sizeof(kState), &s), "clip fields must be ignored without HAS_CLIP");
}

static void testShapeAssembly(void) {
    static uint8_t chunk[ML_HOST_CURSOR_SHAPE_HEADER_SIZE + ML_HOST_CURSOR_MAX_CHUNK + 8];
    ML_HOST_CURSOR_ASSEMBLY a;
    LI_HOST_CURSOR_SHAPE shape;
    memset(&a, 0, sizeof(a));

    // One chunk.
    size_t size = makeChunk(chunk, 5, 4, 4, 0, 64, 0x11);
    CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == 1, "single chunk");
    CHECK(shape.shapeId == 5 && shape.width == 4 && shape.height == 4 && shape.hotspotX == 1 &&
          shape.hotspotY == 2 && shape.pixels[0] == 0x11 && shape.pixels[63] == 0x11, "single chunk fields");

    // A 256x256 shape in 16 chunks, with distinct fill per chunk.
    const uint32_t total = 256 * 256 * 4;
    int complete = 0;
    for (uint32_t offset = 0; offset < total; offset += ML_HOST_CURSOR_MAX_CHUNK) {
        size = makeChunk(chunk, 6, 256, 256, offset, ML_HOST_CURSOR_MAX_CHUNK, (uint8_t)(offset / ML_HOST_CURSOR_MAX_CHUNK));
        int ret = MlHostCursorFeedShape(&a, chunk, size, &shape);
        CHECK(ret == (offset + ML_HOST_CURSOR_MAX_CHUNK == total ? 1 : 0), "chunk at %u", offset);
        complete += ret == 1;
    }
    CHECK(complete == 1 && shape.pixels[0] == 0 && shape.pixels[total - 1] == 15 &&
          shape.pixels[ML_HOST_CURSOR_MAX_CHUNK] == 1, "reassembled 256x256");

    // A chunk that does not continue the shape in flight discards it.
    size = makeChunk(chunk, 7, 128, 128, 0, ML_HOST_CURSOR_MAX_CHUNK, 1);
    CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == 0, "first of four");
    size = makeChunk(chunk, 7, 128, 128, 2 * ML_HOST_CURSOR_MAX_CHUNK, ML_HOST_CURSOR_MAX_CHUNK, 1);
    CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == -1, "skipped chunk accepted");
    size = makeChunk(chunk, 7, 128, 128, ML_HOST_CURSOR_MAX_CHUNK, ML_HOST_CURSOR_MAX_CHUNK, 1);
    CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == -1, "continuation after a discard accepted");
    size = makeChunk(chunk, 8, 128, 128, 0, ML_HOST_CURSOR_MAX_CHUNK, 1);
    CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == 0, "fresh shape after a discard");
    size = makeChunk(chunk, 9, 128, 128, ML_HOST_CURSOR_MAX_CHUNK, ML_HOST_CURSOR_MAX_CHUNK, 1);
    CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == -1, "other id mid-shape accepted");
    size = makeChunk(chunk, 10, 4, 4, 32, 32, 1);
    CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == -1, "continuation with nothing in flight accepted");

    // Header validation.
    struct { size_t offset; uint32_t value; int width; const char* what; } bad[] = {
        {0, 2, 1, "version"}, {1, 2, 1, "format"}, {2, 1, 2, "flags"}, {26, 1, 2, "reserved"},
        {4, 0, 4, "zero id"}, {8, 0, 2, "zero width"}, {8, 257, 2, "width over 256"}, {10, 0, 2, "zero height"},
        {12, 4, 2, "hotspot x"}, {14, 4, 2, "hotspot y"}, {16, 63, 4, "total size"}, {20, 64, 4, "offset past total"},
        {24, 0, 2, "zero length"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        size = makeChunk(chunk, 11, 4, 4, 0, 64, 1);
        if (bad[i].width == 1) chunk[bad[i].offset] = (uint8_t)bad[i].value;
        else if (bad[i].width == 2) put16(chunk + bad[i].offset, (uint16_t)bad[i].value);
        else put32(chunk + bad[i].offset, bad[i].value);
        CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == -1, "%s accepted", bad[i].what);
    }
    size = makeChunk(chunk, 12, 128, 128, 0, ML_HOST_CURSOR_MAX_CHUNK, 1);
    put16(chunk + 24, ML_HOST_CURSOR_MAX_CHUNK + 1);
    CHECK(MlHostCursorFeedShape(&a, chunk, size + 1, &shape) == -1, "oversized chunk accepted");
    size = makeChunk(chunk, 12, 4, 4, 32, 64, 1);  // 32 + 64 > 64
    CHECK(MlHostCursorFeedShape(&a, chunk, size, &shape) == -1, "chunk past the end accepted");

    // Every truncation and extension of a valid chunk.
    for (size_t len = 0; len <= ML_HOST_CURSOR_SHAPE_HEADER_SIZE + 64 + 4; ++len) {
        makeChunk(chunk, 13, 4, 4, 0, 64, 1);
        int ret = MlHostCursorFeedShape(&a, chunk, len, &shape);
        CHECK(ret == (len == ML_HOST_CURSOR_SHAPE_HEADER_SIZE + 64 ? 1 : -1), "chunk of %d bytes", (int)len);
    }
    CHECK(MlHostCursorFeedShape(&a, NULL, 100, &shape) == -1, "NULL chunk");

    MlHostCursorResetAssembly(&a);
    CHECK(a.pixels == NULL && a.received == 0, "reset frees");
}

static int statesSeen, shapesSeen;
static LI_HOST_CURSOR_STATE lastState;
static LI_HOST_CURSOR_SHAPE lastShape;

static void stateSink(const LI_HOST_CURSOR_STATE* state) { ++statesSeen; lastState = *state; }
static void shapeSink(const LI_HOST_CURSOR_SHAPE* shape) { ++shapesSeen; lastShape = *shape; }

static void dispatch(uint16_t type, const uint8_t* payload, size_t size) {
    static uint8_t packet[sizeof(NVCTL_ENET_PACKET_HEADER_V1) + ML_HOST_CURSOR_SHAPE_HEADER_SIZE + ML_HOST_CURSOR_MAX_CHUNK];
    ((PNVCTL_ENET_PACKET_HEADER_V1)packet)->type = type;
    memcpy(packet + sizeof(NVCTL_ENET_PACKET_HEADER_V1), payload, size);
    handleHostCursorPacket((PNVCTL_ENET_PACKET_HEADER_V1)packet, (int)(sizeof(NVCTL_ENET_PACKET_HEADER_V1) + size));
}

static void testDispatch(void) {
    uint8_t chunk[ML_HOST_CURSOR_SHAPE_HEADER_SIZE + 64];
    size_t size = makeChunk(chunk, 21, 4, 4, 0, 64, 0x33);
    MlHostCursorResetAssembly(&hostCursorAssembly);

    // A host that did not advertise the extension is never interpreted.
    SunshineFeatureFlags = LI_FF_PEN_TOUCH_EVENTS;
    LiSetHostCursorCallbacks(stateSink, shapeSink);
    dispatch(ML_HOST_CURSOR_STATE_TYPE, kState, sizeof(kState));
    dispatch(ML_HOST_CURSOR_SHAPE_TYPE, chunk, size);
    CHECK(statesSeen == 0 && shapesSeen == 0, "messages from a host without LI_FF_HOST_CURSOR were delivered");

    SunshineFeatureFlags = LI_FF_HOST_CURSOR;
    dispatch(ML_HOST_CURSOR_STATE_TYPE, kState, sizeof(kState));
    dispatch(ML_HOST_CURSOR_SHAPE_TYPE, chunk, size);
    CHECK(statesSeen == 1 && lastState.shapeId == 0x0A0B0C0D, "state not delivered");
    CHECK(shapesSeen == 1 && lastShape.shapeId == 21 && lastShape.width == 4, "shape not delivered");

    // Malformed messages are dropped without a callback, at every truncation.
    for (size_t len = 0; len < sizeof(kState); ++len) dispatch(ML_HOST_CURSOR_STATE_TYPE, kState, len);
    for (size_t len = 0; len < size; ++len) dispatch(ML_HOST_CURSOR_SHAPE_TYPE, chunk, len);
    handleHostCursorPacket((PNVCTL_ENET_PACKET_HEADER_V1)chunk, 1);
    CHECK(statesSeen == 1 && shapesSeen == 1, "malformed message delivered");

    // Without callbacks nothing is delivered (and nothing crashes).
    LiSetHostCursorCallbacks(NULL, NULL);
    dispatch(ML_HOST_CURSOR_STATE_TYPE, kState, sizeof(kState));
    dispatch(ML_HOST_CURSOR_SHAPE_TYPE, chunk, size);
    CHECK(statesSeen == 1 && shapesSeen == 1, "delivered without callbacks");
    MlHostCursorResetAssembly(&hostCursorAssembly);
}

static void testControlEncoding(void) {
    uint8_t out[ML_HOST_CURSOR_CONTROL_SIZE];
    const uint8_t expected[ML_HOST_CURSOR_CONTROL_SIZE] = {1, 3, 0, 0, 0x11, 0x22, 0x33, 0x44};
    MlHostCursorEncodeControl(out, LI_HOST_CURSOR_CONTROL_SUBSCRIBE | LI_HOST_CURSOR_CONTROL_CLIENT_DRAWS, 0x44332211);
    CHECK(memcmp(out, expected, sizeof(out)) == 0, "control encoding");
    MlHostCursorEncodeControl(out, 0xFF, 0);
    CHECK(out[1] == ML_HOST_CURSOR_CONTROL_FLAGS_ALL, "unknown control flags are not sent");

    // The send entry point refuses hosts without the extension and unknown flags.
    SunshineFeatureFlags = 0;
    CHECK(LiSendHostCursorControl(LI_HOST_CURSOR_CONTROL_SUBSCRIBE, 0) != 0, "sent to a host without the extension");
    SunshineFeatureFlags = LI_FF_HOST_CURSOR;
    CHECK(LiSendHostCursorControl(0x80, 0) != 0, "unknown flag sent");
    CHECK(LiSendHostCursorControl(LI_HOST_CURSOR_CONTROL_SUBSCRIBE, 0) != 0, "sent without a control stream");
}

int main(void) {
    testStateParsing();
    testShapeAssembly();
    testDispatch();
    testControlEncoding();
    printf("%s host cursor (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
