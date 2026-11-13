// Cover the upstream bounded/locked depacketizer together with PyroWave metadata.
// Compile the real production translation unit; replace only control notifications.
#define connectionDetectedFrameLoss testFrameLoss
#define connectionReceivedCompleteFrame testCompleteFrame
#define notifyKeyFrameReceived testKeyFrame
#define LiGetCurrentHostDisplayHdrMode testHdrMode
#define LiRequestIdrFrame testRequestIdr
#include "../src/VideoDepacketizer.c"
#include <stdio.h>

static int completeFrames, idrRequests;
void testFrameLoss(uint32_t start, uint32_t end) { (void)start; (void)end; }
void testCompleteFrame(uint32_t frame, bool ltr) { (void)frame; (void)ltr; ++completeFrames; }
void testKeyFrame(void) {}
bool testHdrMode(void) { return false; }
void testRequestIdr(void) { ++idrRequests; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)

static void packet(unsigned index, unsigned flags, bool lost, bool recordStart) {
    struct { NV_VIDEO_PACKET header; unsigned char payload[12]; } p = {0};
    p.header.streamPacketIndex = index << 8;
    p.header.frameIndex = 1;
    p.header.flags = flags | FLAG_CONTAINS_PIC_DATA;
    p.header.extraFlags = recordStart ? NV_VIDEO_PACKET_EXTRA_FLAG_PYROWAVE_RECORD_START : 0;
    p.payload[0] = 1; // 8-byte Sunshine header
    p.payload[3] = 2; // IDR frame
    p.payload[4] = 4; // four bytes in final packet
    p.payload[6] = 2; // two critical packets
    processRtpPayload(&p.header, sizeof(p.header) + ((flags & FLAG_SOF) ? 12 : 4),
                      100, 200, 900, lost, NULL);
}

int main(void) {
    NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE;
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 450;
    memset(&VideoCallbacks, 0, sizeof(VideoCallbacks));
    initializeVideoDepacketizer(1200);

    // Byte and allocation caps must not lose the fork's fragment classifications.
    char data[4] = {0};
    CHECK(queueFragment(NULL, data, 0, sizeof(data), true, false));
    CHECK(nalChainHead->bufferType == BUFFER_TYPE_LOST);
    CHECK(queueFragment(NULL, data, 0, sizeof(data), false, true));
    CHECK(nalChainTail->bufferType == BUFFER_TYPE_RECORD_START);
    CHECK(nalChainFragmentCount == 2 && nalChainDataLength == 8);
    CHECK(queueFragment(NULL, data, 0, 0, false, false));
    CHECK(nalChainFragmentCount == 2);
    CHECK(!queueFragment(NULL, data, 0, MAX_VIDEO_REASSEMBLED_FRAME_SIZE, false, false));
    CHECK(nalChainFragmentCount == 2 && nalChainDataLength == 8);
    CHECK(!canAppendVideoFrameFragment(0, 1, MAX_VIDEO_REASSEMBLED_FRAGMENTS));
    cleanupFrameState();
    CHECK(!isValidVideoFramePacket(false, false, 1, 1));
    CHECK(!isValidVideoFramePacket(false, true, 2, 1));
    CHECK(isValidVideoFramePacket(false, true, 1, 1));

    // An IDR with an interior loss remains deliverable, including a refresh
    // arriving mid-frame, and preserves metadata through the new lock wrapper.
    packet(0, FLAG_SOF, false, true);
    packet(1, 0, true, false);
    requestDecoderRefresh();
    packet(2, FLAG_EOF, false, true);
    VIDEO_FRAME_HANDLE handle;
    PDECODE_UNIT du;
    CHECK(LiPollNextVideoFrame(&handle, &du));
    CHECK(completeFrames == 1 && idrRequests == 1);
    CHECK(du->fullLength == 12 && du->pyrowaveCriticalPackets == 2);
    CHECK(du->bufferList->bufferType == BUFFER_TYPE_RECORD_START);
    CHECK(du->bufferList->next->bufferType == BUFFER_TYPE_LOST);
    CHECK(du->bufferList->next->next->bufferType == BUFFER_TYPE_RECORD_START);
    CHECK(du->bufferList->next->next->next == NULL);
    LiCompleteVideoFrame(handle, DR_OK);
    CHECK(atomic_load(&idrFrameProcessed));
    CHECK(nalChainFragmentCount == 0 && nalChainHead == NULL);
    stopVideoDepacketizer();
    destroyVideoDepacketizer();
    puts("video merge regressions passed");
    return 0;
}
