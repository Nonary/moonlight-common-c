#pragma once

#include "Limelight.h"
#include <stdbool.h>
#include <stddef.h>

// Internal negotiation API. No NUL terminator is required or read beyond length.
int LiParseAudioConfigurations(const char* sdp, size_t length, int channels,
                              POPUS_MULTISTREAM_CONFIGURATION normal,
                              POPUS_MULTISTREAM_CONFIGURATION high,
                              bool* highSupported);
