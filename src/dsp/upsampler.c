#include "dsp/upsampler.h"

#include <stddef.h>

#include "upsampler_table.h"

_Static_assert((int)UPSAMPLER_TAPS == (int)UPSAMPLER_HISTORY,
               "the history holds one filter's taps");

typedef struct RateFactor {
    uint32_t rate;
    uint32_t factor;
} RateFactor;

static const RateFactor RATE_FACTORS[] = {{8000, 6},  {11025, 4}, {12000, 4},
                                          {16000, 3}, {22050, 2}, {24000, 2}};

uint32_t upsampler_factor(uint32_t sample_rate)
{
    for (size_t index = 0; index < sizeof RATE_FACTORS / sizeof RATE_FACTORS[0]; index++) {
        if (RATE_FACTORS[index].rate == sample_rate) {
            return RATE_FACTORS[index].factor;
        }
    }
    return 1;
}

static const int32_t *coefficients_for(uint32_t factor)
{
    for (size_t index = 0; index < UPSAMPLER_FILTER_COUNT; index++) {
        if (UPSAMPLER_FILTERS[index].factor == factor) {
            return UPSAMPLER_FILTERS[index].coefficients;
        }
    }
    return NULL;
}

void upsampler_start(Upsampler *upsampler, uint32_t sample_rate, uint32_t bits)
{
    uint32_t factor = upsampler_factor(sample_rate);
    upsampler->coefficients = coefficients_for(factor);
    upsampler->factor = upsampler->coefficients != NULL ? factor : 1;
    upsampler->position = 0;
    upsampler->warmup = UPSAMPLER_DELAY;
    upsampler->limit = bits > 0 && bits < 32 ? (int32_t)((1u << (bits - 1)) - 1u) : INT32_MAX;
    for (uint32_t channel = 0; channel < UPSAMPLER_CHANNELS; channel++) {
        for (uint32_t index = 0; index < UPSAMPLER_HISTORY; index++) {
            upsampler->history[channel][index] = 0;
        }
    }
}

bool upsampler_active(const Upsampler *upsampler)
{
    return upsampler->factor > 1;
}

void upsampler_put(Upsampler *upsampler, uint32_t channel, int32_t sample)
{
    upsampler->history[channel][upsampler->position] = sample;
}

static int32_t clamped(int64_t value, int32_t limit)
{
    return value > limit ? limit : value < -(int64_t)limit - 1 ? -limit - 1 : (int32_t)value;
}

static int64_t multiply_add(int64_t sum, const int32_t *coefficients, const int32_t *samples,
                            uint32_t count)
{
    uint32_t tap = 0;
    for (; tap + 4 <= count; tap += 4) {
        sum += (int64_t)coefficients[tap] * samples[tap];
        sum += (int64_t)coefficients[tap + 1] * samples[tap + 1];
        sum += (int64_t)coefficients[tap + 2] * samples[tap + 2];
        sum += (int64_t)coefficients[tap + 3] * samples[tap + 3];
    }
    for (; tap < count; tap++) {
        sum += (int64_t)coefficients[tap] * samples[tap];
    }
    return sum;
}

typedef struct PairSum {
    int64_t left;
    int64_t right;
} PairSum;

static PairSum multiply_add_pair(PairSum sum, const int32_t *coefficients, const int32_t *left,
                                 uint32_t count)
{
    const int32_t *right = left + UPSAMPLER_HISTORY;
    uint32_t tap = 0;
    for (; tap + 2 <= count; tap += 2) {
        int32_t first = coefficients[tap];
        int32_t second = coefficients[tap + 1];
        sum.left += (int64_t)first * left[tap];
        sum.right += (int64_t)first * right[tap];
        sum.left += (int64_t)second * left[tap + 1];
        sum.right += (int64_t)second * right[tap + 1];
    }
    for (; tap < count; tap++) {
        sum.left += (int64_t)coefficients[tap] * left[tap];
        sum.right += (int64_t)coefficients[tap] * right[tap];
    }
    return sum;
}

static int64_t rounding(void)
{
    return INT64_C(1) << (UPSAMPLER_COEFFICIENT_BITS - 1);
}

static void render_one(const Upsampler *upsampler, int32_t *out)
{
    const int32_t *newest = &upsampler->history[0][upsampler->position];
    const int32_t *oldest = upsampler->history[0];
    uint32_t first_run = UPSAMPLER_HISTORY - upsampler->position;
    const int32_t *coefficients = upsampler->coefficients;
    for (uint32_t phase = 0; phase < upsampler->factor; phase++) {
        int64_t sum = multiply_add(rounding(), coefficients, newest, first_run);
        sum = multiply_add(sum, coefficients + first_run, oldest, UPSAMPLER_TAPS - first_run);
        out[phase] = clamped(sum >> UPSAMPLER_COEFFICIENT_BITS, upsampler->limit);
        coefficients += UPSAMPLER_TAPS;
    }
}

static void render_pair(const Upsampler *upsampler, int32_t *left, int32_t *right)
{
    const int32_t *newest = &upsampler->history[0][upsampler->position];
    const int32_t *oldest = upsampler->history[0];
    uint32_t first_run = UPSAMPLER_HISTORY - upsampler->position;
    const int32_t *coefficients = upsampler->coefficients;
    for (uint32_t phase = 0; phase < upsampler->factor; phase++) {
        PairSum sum =
            multiply_add_pair((PairSum){rounding(), rounding()}, coefficients, newest, first_run);
        sum = multiply_add_pair(sum, coefficients + first_run, oldest, UPSAMPLER_TAPS - first_run);
        left[phase] = clamped(sum.left >> UPSAMPLER_COEFFICIENT_BITS, upsampler->limit);
        right[phase] = clamped(sum.right >> UPSAMPLER_COEFFICIENT_BITS, upsampler->limit);
        coefficients += UPSAMPLER_TAPS;
    }
}

uint32_t upsampler_render(const Upsampler *upsampler, int32_t *left, int32_t *right)
{
    if (upsampler->warmup > 0) {
        return 0;
    }
    if (right != NULL) {
        render_pair(upsampler, left, right);
    } else {
        render_one(upsampler, left);
    }
    return upsampler->factor;
}

void upsampler_advance(Upsampler *upsampler)
{
    if (upsampler->warmup > 0) {
        upsampler->warmup--;
    }
    upsampler->position =
        upsampler->position == 0 ? UPSAMPLER_HISTORY - 1 : upsampler->position - 1;
}
