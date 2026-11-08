#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/core1_stream.h"

typedef struct Core1Pcm {
    int32_t *left;
    int32_t *right;
    FlacInput input;
    const bool *starved;
    uint32_t position;
    uint32_t next_sample;
    uint32_t channels;
    uint32_t bits;
    uint32_t frame_bytes;
} Core1Pcm;

Core1Decoder core1_pcm_decoder(Core1Pcm *pcm, int32_t *left, int32_t *right);
