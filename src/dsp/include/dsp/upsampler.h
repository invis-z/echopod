#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    UPSAMPLER_CHANNELS = 2,
    UPSAMPLER_HISTORY = 64,
    UPSAMPLER_DELAY = UPSAMPLER_HISTORY / 2,
    UPSAMPLER_MAX_FACTOR = 6
};

typedef struct Upsampler {
    const int32_t *coefficients;
    uint32_t factor;
    uint32_t position;
    uint32_t warmup;
    int32_t limit;
    int32_t history[UPSAMPLER_CHANNELS][UPSAMPLER_HISTORY];
} Upsampler;

uint32_t upsampler_factor(uint32_t sample_rate);
void upsampler_start(Upsampler *upsampler, uint32_t sample_rate, uint32_t bits);
bool upsampler_active(const Upsampler *upsampler);
void upsampler_put(Upsampler *upsampler, uint32_t channel, int32_t sample);
uint32_t upsampler_render(const Upsampler *upsampler, int32_t *left, int32_t *right);
void upsampler_advance(Upsampler *upsampler);
