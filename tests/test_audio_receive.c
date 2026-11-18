#include "AudioTestAdapter.h"
#include "Limelight-internal.h"
#include <rs.h>

static int failures, assertions, cases, outputCount;
static int outputSizes[128];
static unsigned char output[128][RTPA_MAX_PACKET_SIZE];
static unsigned char key[16], initialIv[16];
static bool encrypted;
#define CHECK(x) do { assertions++; if (!(x)) { failures++; printf("FAIL receive line %d: %s\n", __LINE__, #x); } } while (0)

static void decoded(char* data, int length) {
    if (outputCount >= 128 || length > RTPA_MAX_PACKET_SIZE || length < 0) { failures++; return; }
    outputSizes[outputCount] = length;
    if (length) memcpy(output[outputCount], data, length);
    outputCount++;
}

static void begin(bool useEncryption) {
    cases++;
    encrypted = useEncryption;
    outputCount = 0;
    memset(outputSizes, 0, sizeof(outputSizes));
    memset(output, 0, sizeof(output));
    LiTestAudioBegin(useEncryption, key, initialIv, decoded);
}

static unsigned char value(uint16_t sequence, int index) {
    return (unsigned char)((sequence * 13 + index * 37 + (sequence >> 8)) ^ 0x55);
}

static int dataPacket(unsigned char* packet, uint16_t sequence, int plainLength) {
    PRTP_PACKET rtp = (PRTP_PACKET)packet;
    unsigned char plaintext[2048];
    memset(packet, 0, RTPA_MAX_PACKET_SIZE + 32);
    rtp->header = 0x80;
    rtp->packetType = 97;
    rtp->sequenceNumber = BE16(sequence);
    rtp->timestamp = BE32((uint32_t)sequence * 5);
    rtp->ssrc = BE32(0x12345678);
    for (int i = 0; i < plainLength; i++) plaintext[i] = value(sequence, i);
    int payloadLength = plainLength;
    if (encrypted) {
        unsigned char iv[16] = { 0 };
        uint32_t keyId;
        memcpy(&keyId, initialIv, 4);
        keyId = BE32(BE32(keyId) + sequence);
        memcpy(iv, &keyId, 4);
        PPLT_CRYPTO_CONTEXT ctx = PltCreateCryptoContext(0);
        CHECK(ctx != NULL);
        if (ctx == NULL) return -1;
        CHECK(PltEncryptMessage(ctx, ALGORITHM_AES_CBC, CIPHER_FLAG_RESET_IV | CIPHER_FLAG_FINISH,
                               key, 16, iv, 16, NULL, 0, plaintext, plainLength, packet + sizeof(*rtp), &payloadLength));
        PltDestroyCryptoContext(ctx);
    }
    else memcpy(packet + sizeof(*rtp), plaintext, plainLength);
    return (int)sizeof(*rtp) + payloadLength;
}

static void pushData(uint16_t sequence, int length) {
    unsigned char packet[RTPA_MAX_PACKET_SIZE + 32];
    int size = dataPacket(packet, sequence, length);
    CHECK(LiTestAudioPush(packet, size) == 0);
}

static void syncQueue(uint16_t first, int plainLength) {
    pushData(first, plainLength);
    CHECK(outputCount == 0); // Actual receiver discards the initial partial FEC block.
}

static void expectOutput(int index, uint16_t sequence, int length) {
    CHECK(index < outputCount);
    if (index >= outputCount) return;
    CHECK(outputSizes[index] == length);
    if (outputSizes[index] != length) return;
    for (int i = 0; i < length; i++) CHECK(output[index][i] == value(sequence, i));
}

static int createFec(unsigned char packets[4][RTPA_MAX_PACKET_SIZE + 32], int dataLength, uint16_t base,
                     unsigned char fec[2][RTPA_MAX_PACKET_SIZE + 32]) {
    uint8_t* shards[6];
    for (int i = 0; i < 4; i++) shards[i] = packets[i] + sizeof(RTP_PACKET);
    for (int i = 0; i < 2; i++) {
        memset(fec[i], 0, RTPA_MAX_PACKET_SIZE + 32);
        PRTP_PACKET rtp = (PRTP_PACKET)fec[i];
        rtp->header = 0x80;
        rtp->packetType = 127;
        rtp->sequenceNumber = BE16((uint16_t)(base + i));
        rtp->timestamp = BE32(base * 5U);
        rtp->ssrc = BE32(0x12345678);
        PAUDIO_FEC_HEADER header = (PAUDIO_FEC_HEADER)(rtp + 1);
        header->fecShardIndex = (uint8_t)i;
        header->payloadType = 97;
        header->baseSequenceNumber = BE16(base);
        header->baseTimestamp = BE32(base * 5U);
        header->ssrc = BE32(0x12345678);
        shards[4 + i] = (uint8_t*)(header + 1);
    }
    reed_solomon* rs = reed_solomon_new(4, 2);
    const unsigned char parity[] = { 0x77, 0x40, 0x38, 0x0e, 0xc7, 0xa7, 0x0d, 0x6c };
    memcpy(rs->p, parity, sizeof(parity));
    CHECK(reed_solomon_encode(rs, shards, 6, dataLength - sizeof(RTP_PACKET)) == 0);
    reed_solomon_release(rs);
    return dataLength + sizeof(AUDIO_FEC_HEADER);
}

static void testRecovery(bool useEncryption, int missing, int plainLength) {
    unsigned char data[4][RTPA_MAX_PACKET_SIZE + 32], fec[2][RTPA_MAX_PACKET_SIZE + 32];
    int size = 0;
    begin(useEncryption);
    syncQueue(0, plainLength);
    for (int i = 0; i < 4; i++) size = dataPacket(data[i], (uint16_t)(4 + i), plainLength);
    int fecSize = createFec(data, size, 4, fec);
    CHECK(fecSize <= RTPA_MAX_PACKET_SIZE);
    for (int i = 0; i < 4; i++) {
        if (i == 1 || (missing == 2 && i == 0)) continue;
        CHECK(LiTestAudioPush(data[i], size) == 0);
    }
    for (int i = 0; i < missing; i++) CHECK(LiTestAudioPush(fec[i], fecSize) == 0);
    CHECK(outputCount == 4);
    for (int i = 0; i < 4; i++) expectOutput(i, (uint16_t)(4 + i), plainLength);
    CHECK(LiTestAudioPush(fec[0], fecSize) == 0); // Duplicate parity never duplicates decoded output.
    CHECK(outputCount == 4);
    LiTestAudioEnd();
}

int runAudioReceiveTests(void) {
    failures = assertions = cases = 0;
    for (int i = 0; i < 16; i++) { key[i] = (unsigned char)(0x11 + i); initialIv[i] = (unsigned char)(0x21 + i); }
    for (int encryption = 0; encryption < 2; encryption++) {
        begin(encryption != 0);
        syncQueue(UINT16_MAX, 32);
        pushData(0, 32);
        pushData(2, 32);
        pushData(1, 32);
        pushData(3, 32);
        CHECK(outputCount == 4);
        for (int i = 0; i < 4; i++) expectOutput(i, (uint16_t)i, 32);
        LiTestAudioEnd();

        begin(encryption != 0);
        syncQueue(0, 64);
        for (uint16_t seq = 4; seq < 12; seq++) { pushData(seq, 64); pushData(seq, 64); }
        CHECK(outputCount == 8);
        for (int i = 0; i < 8; i++) expectOutput(i, (uint16_t)(4 + i), 64);
        LiTestAudioEnd();

        begin(encryption != 0);
        syncQueue(0, 64);
        pushData(6, 64); pushData(6, 64);
        CHECK(outputCount == 0);
        pushData(4, 64); pushData(7, 64); pushData(5, 64);
        CHECK(outputCount == 4);
        for (int i = 0; i < 4; i++) expectOutput(i, (uint16_t)(4 + i), 64);
        LiTestAudioEnd();

        testRecovery(encryption != 0, 1, 64);
        testRecovery(encryption != 0, 2, 64);

        begin(encryption != 0);
        syncQueue(0, 64);
        pushData(4, 64); pushData(6, 64); pushData(7, 64); pushData(8, 64);
        CHECK(outputCount == 5);
        expectOutput(0, 4, 64);
        CHECK(outputSizes[1] == 0); // Real decoder callback receives NULL,0 for PLC.
        expectOutput(2, 6, 64); expectOutput(3, 7, 64); expectOutput(4, 8, 64);
        pushData(5, 64);
        CHECK(outputCount == 5);
        LiTestAudioEnd();

        begin(encryption != 0);
        syncQueue(65528, 64);
        for (int i = 0; i < 8; i++) pushData((uint16_t)(65532 + i), 64);
        CHECK(outputCount == 8);
        for (int i = 0; i < 8; i++) expectOutput(i, (uint16_t)(65532 + i), 64);
        LiTestAudioEnd();
    }

    // Exact FEC datagram boundary, for both plaintext and PKCS7 ciphertext.
    testRecovery(false, 2, 1376);
    testRecovery(true, 2, 1375);

    unsigned char packet[RTPA_MAX_PACKET_SIZE + 32], bad[RTPA_MAX_PACKET_SIZE + 32];
    begin(false);
    syncQueue(0, 64);
    int size = dataPacket(packet, 4, 64);
    for (int length = 0; length < (int)sizeof(RTP_PACKET); length++) CHECK(LiTestAudioPush(packet, length) == -1);
    CHECK(LiTestAudioPush(packet, RTPA_MAX_PACKET_SIZE + 1) == -1);
    CHECK(LiTestAudioPush(NULL, 64) == -1);
    for (unsigned header = 0; header < 256; header++) {
        if (header == 0x80) continue;
        memcpy(bad, packet, size); bad[0] = (unsigned char)header;
        CHECK(LiTestAudioPush(bad, size) == 0);
    }
    memcpy(bad, packet, size); bad[1] = 98;
    CHECK(LiTestAudioPush(bad, size) == 0);
    CHECK(outputCount == 0);
    CHECK(LiTestAudioPush(packet, size) == 0);
    expectOutput(0, 4, 64);
    LiTestAudioEnd();

    // All short FEC lengths, invalid index/type and conflicting block identity.
    unsigned char data[4][RTPA_MAX_PACKET_SIZE + 32], fec[2][RTPA_MAX_PACKET_SIZE + 32];
    begin(false); syncQueue(0, 64);
    for (int i = 0; i < 4; i++) size = dataPacket(data[i], (uint16_t)(4 + i), 64);
    int fecSize = createFec(data, size, 4, fec);
    for (int length = 12; length <= 24; length++) CHECK(LiTestAudioPush(fec[0], length) == 0);
    memcpy(bad, fec[0], fecSize); bad[12] = 2; CHECK(LiTestAudioPush(bad, fecSize) == 0);
    memcpy(bad, fec[0], fecSize); bad[13] = 98; CHECK(LiTestAudioPush(bad, fecSize) == 0);
    CHECK(outputCount == 0);
    CHECK(LiTestAudioPush(data[0], size) == 0);
    memcpy(bad, fec[0], fecSize); bad[23] ^= 1; CHECK(LiTestAudioPush(bad, fecSize) == 0);
    memcpy(bad, fec[0], fecSize); bad[19] ^= 1; CHECK(LiTestAudioPush(bad, fecSize) == 0);
    CHECK(LiTestAudioPush(data[2], size) == 0); CHECK(LiTestAudioPush(data[3], size) == 0);
    CHECK(LiTestAudioPush(fec[0], fecSize) == 0);
    CHECK(outputCount == 4);
    for (int i = 0; i < 4; i++) expectOutput(i, (uint16_t)(4 + i), 64);
    LiTestAudioEnd();

    begin(false); syncQueue(0, 1388);
    size = dataPacket(packet, 4, 1388);
    CHECK(size == RTPA_MAX_PACKET_SIZE);
    CHECK(LiTestAudioPush(packet, size) == 0);
    CHECK(outputCount == 1); expectOutput(0, 4, 1388);
    LiTestAudioEnd();

    begin(true); syncQueue(0, 64);
    size = dataPacket(packet, 4, 64);
    CHECK(LiTestAudioPush(packet, size - 1) == 0); // Invalid CBC block length.
    CHECK(outputCount == 0);
    packet[sizeof(RTP_PACKET) + 63] ^= 0x10; // Deterministically turns last PKCS7 byte 16 into 0.
    CHECK(LiTestAudioPush(packet, size) == 0);
    CHECK(outputCount == 0); // Corrupt padding never reaches decoder.
    pushData(5, 64);
    CHECK(outputCount == 1); expectOutput(0, 5, 64); // Reset-IV decrypt recovers next packet.
    LiTestAudioEnd();

    begin(true); syncQueue(0, 64);
    size = dataPacket(packet, 4, 1376);
    CHECK(size == 1404 && LiTestAudioPush(packet, size) == -1);
    CHECK(outputCount == 0);
    LiTestAudioEnd();

    printf("Audio receive: %d cases, %d assertions, %d failures, 0 SKIP\n", cases, assertions, failures);
    return failures;
}
