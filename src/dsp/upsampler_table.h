#pragma once

#include <stdint.h>

enum {
    UPSAMPLER_TAPS = 64,
    UPSAMPLER_COEFFICIENT_BITS = 30,
    UPSAMPLER_FILTER_COUNT = 4
};

typedef struct UpsamplerFilter {
    uint32_t factor;
    const int32_t *coefficients;
} UpsamplerFilter;

extern const UpsamplerFilter UPSAMPLER_FILTERS[UPSAMPLER_FILTER_COUNT];
