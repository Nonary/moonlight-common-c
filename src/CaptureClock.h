#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t offsetUs;
    uint64_t delayUs;
} CAPTURE_CLOCK_SAMPLE;

// Client clock = host clock + offset. The measured round trip bounds the
// error introduced by unequal one-way paths.
static inline bool captureClockSample(uint64_t clientSent, uint64_t hostReceived,
                                      uint64_t hostSent, uint64_t clientReceived,
                                      uint64_t maximumRoundTrip, CAPTURE_CLOCK_SAMPLE* sample) {
    if (!hostReceived || hostSent < hostReceived || hostSent > INT64_MAX ||
            clientReceived > INT64_MAX || clientReceived < clientSent ||
            clientReceived - clientSent >= maximumRoundTrip) {
        return false;
    }

    const uint64_t elapsed = clientReceived - clientSent;
    const uint64_t hostElapsed = hostSent - hostReceived;
    if (hostElapsed > elapsed) {
        return false;
    }

    sample->delayUs = elapsed - hostElapsed;
    const int64_t lowerOffset = (int64_t)clientSent - (int64_t)hostReceived;
    sample->offsetUs = lowerOffset + (int64_t)(sample->delayUs / 2);
    return true;
}
