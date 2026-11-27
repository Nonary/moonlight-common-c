#include "AudioConfig.h"
#include <string.h>

#define MAX_SURROUND_LINE_LENGTH 256

static bool parseNumber(const char** cursor, const char* end, unsigned* number) {
    unsigned value = 0;
    const char* start = *cursor;
    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        unsigned digit = (unsigned)(*(*cursor)++ - '0');
        if (value > (255U - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    if (*cursor == start) return false;
    *number = value;
    return true;
}

static int parseOffer(const char* start, const char* end, POPUS_MULTISTREAM_CONFIGURATION config) {
    unsigned values[3 + AUDIO_CONFIGURATION_MAX_CHANNEL_COUNT];
    unsigned count = 0;
    const char* cursor = start;
    memset(config, 0, sizeof(*config));
    config->sampleRate = 48000;

    if (memchr(start, ',', (size_t)(end - start)) != NULL) {
        do {
            if (count == sizeof(values) / sizeof(values[0]) ||
                    !parseNumber(&cursor, end, &values[count++])) return -1;
            if (cursor == end) break;
            if (*cursor++ != ',' || cursor == end) return -1;
        } while (cursor < end);
        if (count < 3 || values[0] > AUDIO_CONFIGURATION_MAX_CHANNEL_COUNT || count != 3 + values[0]) return -1;
    }
    else {
        // Historical GFE uses one digit per field: channels, streams, coupled,
        // then exactly channels mapping entries. Keep that representation intact.
        if (end - start < 3 || *start < '1' || *start > '9') return -1;
        count = (unsigned)(end - start);
        if (count != 3U + (unsigned)(*start - '0') || count > sizeof(values) / sizeof(values[0])) return -1;
        for (unsigned i = 0; i < count; i++) {
            if (start[i] < '0' || start[i] > '9') return -1;
            values[i] = (unsigned)(start[i] - '0');
        }
    }

    if (values[0] == 0 || values[1] == 0 || values[2] > values[1] || values[1] + values[2] > 255) return -1;
    config->channelCount = (int)values[0];
    config->streams = (int)values[1];
    config->coupledStreams = (int)values[2];
    for (unsigned i = 0; i < values[0]; i++) {
        // Opus permits duplicate and silence mappings for legacy/custom profiles.
        if (values[3 + i] != 255 && values[3 + i] >= values[1] + values[2]) return -1;
        config->mapping[i] = (unsigned char)values[3 + i];
    }
    if (config->channelCount == 12) {
        if (!((config->streams == 8 && config->coupledStreams == 4) ||
                (config->streams == 12 && config->coupledStreams == 0))) return -1;
        for (unsigned i = 0; i < 12; i++) {
            if (config->mapping[i] != i) return -1;
        }
    }
    return 0;
}

int LiParseAudioConfigurations(const char* sdp, size_t length, int channels,
                              POPUS_MULTISTREAM_CONFIGURATION normal,
                              POPUS_MULTISTREAM_CONFIGURATION high,
                              bool* highSupported) {
    static const char prefix[] = "a=fmtp:97 surround-params=";
    OPUS_MULTISTREAM_CONFIGURATION offers[2];
    unsigned offerCount = 0;
    size_t offset = 0;
    memset(normal, 0, sizeof(*normal));
    memset(high, 0, sizeof(*high));
    *highSupported = false;
    if ((sdp == NULL && length != 0) || channels < 1 || channels > AUDIO_CONFIGURATION_MAX_CHANNEL_COUNT) return -1;

    // Stereo negotiation has never depended on surround attributes.
    if (channels == 2) {
        normal->sampleRate = 48000;
        normal->channelCount = 2;
        normal->streams = normal->coupledStreams = 1;
        normal->mapping[1] = 1;
        return 0;
    }

    while (offset < length) {
        size_t start = offset;
        while (offset < length && sdp[offset] != '\n') {
            if (sdp[offset] == '\0') return -1;
            offset++;
        }
        size_t end = offset;
        if (offset < length) offset++;
        if (end > start && sdp[end - 1] == '\r') end--;
        if (end - start >= sizeof(prefix) - 1 && memcmp(sdp + start, prefix, sizeof(prefix) - 1) == 0) {
            const char* value = sdp + start + sizeof(prefix) - 1;
            const char* lineEnd = sdp + end;
            unsigned advertisedChannels;
            if (memchr(value, ',', (size_t)(lineEnd - value)) != NULL) {
                const char* channelEnd = value;
                // Select on the complete bounded channel field before parsing
                // mappings. Unrequested future/custom profiles must not impose
                // the new discrete-12 policy on existing 5.1/7.1 sessions.
                if (!parseNumber(&channelEnd, lineEnd, &advertisedChannels)) continue;
            }
            else {
                // The historical packed representation has one digit per field.
                if (value == lineEnd || *value < '0' || *value > '9') continue;
                advertisedChannels = (unsigned)(*value - '0');
            }
            if (advertisedChannels != (unsigned)channels) continue;
            if (channels != 12 && offerCount == 2) continue;
            OPUS_MULTISTREAM_CONFIGURATION candidate;
            if (end - start > MAX_SURROUND_LINE_LENGTH ||
                    parseOffer(value, lineEnd, &candidate) != 0) return -1;
            if (channels == 12) {
                // Foundation advertises custom params twice before its standard
                // profiles. Identical repeats are harmless; different extra
                // profiles or a different quality order remain ambiguous.
                bool duplicate = false;
                for (unsigned i = 0; i < offerCount; i++) {
                    if (offers[i].streams == candidate.streams && offers[i].coupledStreams == candidate.coupledStreams &&
                            memcmp(offers[i].mapping, candidate.mapping, 12) == 0) duplicate = true;
                }
                if (duplicate) continue;
                if (offerCount == 2) return -1;
            }
            offers[offerCount++] = candidate;
        }
    }
    if (offerCount == 0) {
        if (channels != 6) return -4;
        // Old GFE versions may omit their 5.1 offer entirely.
        static const unsigned char mapping[] = { 0, 4, 1, 5, 2, 3 };
        normal->sampleRate = 48000;
        normal->channelCount = 6;
        normal->streams = 4;
        normal->coupledStreams = 2;
        memcpy(normal->mapping, mapping, sizeof(mapping));
        return 0;
    }
    if (channels == 12) {
        // Advertised HQ is parsed for consistency, but never selected: Foundation
        // does not yet prove matching sender settings or a safe HQ packet budget.
        if (offers[0].streams != 8 || offers[0].coupledStreams != 4 ||
                (offerCount == 2 && (offers[1].streams != 12 || offers[1].coupledStreams != 0))) return -1;
    }
    *normal = offers[0];
    if (channels == 6 || channels == 8) {
        // Only legacy normal quality puts LFE last. HQ and 7.1.4 are already
        // in FL FR FC LFE BL BR SL SR [TFL TFR TBL TBR] output order.
        normal->mapping[3] = offers[0].mapping[channels - 1];
        memcpy(normal->mapping + 4, offers[0].mapping + 3, (size_t)channels - 4);
    }
    if (offerCount == 2) {
        *high = offers[1];
        *highSupported = channels != 12;
    }
    return 0;
}
