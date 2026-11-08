#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codec/flac.h"

typedef struct WavFormat {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits;
    uint16_t frame_bytes;
    uint32_t data_offset;
    uint32_t frames;
} WavFormat;

bool wav_parse(FlacInput input, WavFormat *format);
int32_t wav_sample(const uint8_t *bytes, uint32_t bits);
