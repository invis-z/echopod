#include <math.h>
#include <stdlib.h>

#include "check.h"
#include "dsp/upsampler.h"

enum {
    INPUT_FRAMES = 4096,
    OUTPUT_CAPACITY = INPUT_FRAMES * UPSAMPLER_MAX_FACTOR,
    SKIP_FRAMES = 512,
    BITS = 24
};

static const double PI = 3.14159265358979323846;
static const double AMPLITUDE = 4194304.0;
static const uint32_t RATES[] = {8000, 11025, 12000, 16000, 22050, 24000};

static Upsampler upsampler;
static int32_t input[INPUT_FRAMES];
static int32_t output[2][OUTPUT_CAPACITY + UPSAMPLER_MAX_FACTOR];

static uint32_t run(uint32_t rate, const int32_t *left, const int32_t *right, uint32_t frames)
{
    upsampler_start(&upsampler, rate, BITS);
    uint32_t produced = 0;
    for (uint32_t index = 0; index < frames + UPSAMPLER_DELAY; index++) {
        bool real = index < frames;
        upsampler_put(&upsampler, 0, real ? left[index] : 0);
        upsampler_put(&upsampler, 1, real ? right[index] : 0);
        uint32_t count = upsampler_render(&upsampler, &output[0][produced], &output[1][produced]);
        produced += count;
        upsampler_advance(&upsampler);
    }
    return produced;
}

static void fill_sine(double cycles_per_sample)
{
    for (uint32_t index = 0; index < INPUT_FRAMES; index++) {
        input[index] = (int32_t)lround(AMPLITUDE * sin(2 * PI * cycles_per_sample * index));
    }
}

static double amplitude_at(const int32_t *samples, uint32_t first, uint32_t count,
                           double cycles_per_sample)
{
    double real = 0;
    double imaginary = 0;
    double weights = 0;
    for (uint32_t index = 0; index < count; index++) {
        double angle = 2 * PI * cycles_per_sample * (double)index;
        double phase = 2 * PI * (double)index / (double)(count - 1);
        double weight =
            0.35875 - 0.48829 * cos(phase) + 0.14128 * cos(2 * phase) - 0.01168 * cos(3 * phase);
        real += weight * samples[first + index] * cos(angle);
        imaginary += weight * samples[first + index] * sin(angle);
        weights += weight;
    }
    return 2 * sqrt(real * real + imaginary * imaginary) / weights;
}

static double decibels(double ratio)
{
    return 20 * log10(ratio);
}

static void rates_below_32_khz_are_raised_to_44_1_or_48(void)
{
    CHECK_EQUAL(6u, upsampler_factor(8000));
    CHECK_EQUAL(4u, upsampler_factor(11025));
    CHECK_EQUAL(4u, upsampler_factor(12000));
    CHECK_EQUAL(3u, upsampler_factor(16000));
    CHECK_EQUAL(2u, upsampler_factor(22050));
    CHECK_EQUAL(2u, upsampler_factor(24000));
    CHECK_EQUAL(1u, upsampler_factor(32000));
    CHECK_EQUAL(1u, upsampler_factor(44100));
    CHECK_EQUAL(1u, upsampler_factor(37800));
    upsampler_start(&upsampler, 44100, BITS);
    CHECK(!upsampler_active(&upsampler));
    upsampler_start(&upsampler, 22050, BITS);
    CHECK(upsampler_active(&upsampler));
}

static void every_input_frame_becomes_factor_output_frames(void)
{
    fill_sine(0.1);
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        uint32_t frames = 1000 + (uint32_t)index;
        CHECK_EQUAL(frames * upsampler_factor(RATES[index]),
                    run(RATES[index], input, input, frames));
    }
    CHECK_EQUAL(20u * 3, run(16000, input, input, 20));
}

static void a_steady_level_passes_unchanged(void)
{
    for (uint32_t index = 0; index < INPUT_FRAMES; index++) {
        input[index] = 1234567;
    }
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        uint32_t factor = upsampler_factor(RATES[index]);
        run(RATES[index], input, input, INPUT_FRAMES);
        bool steady = true;
        for (uint32_t at = SKIP_FRAMES * factor; at < (INPUT_FRAMES - SKIP_FRAMES) * factor; at++) {
            steady = steady && abs(output[0][at] - 1234567) <= 1;
        }
        CHECK(steady);
    }
}

static void the_passband_is_flat_to_0_41_of_the_source_rate(void)
{
    static const double TONES[] = {0.05, 0.2, 0.3, 0.38, 0.41};
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        uint32_t factor = upsampler_factor(RATES[rate]);
        for (size_t tone = 0; tone < sizeof TONES / sizeof TONES[0]; tone++) {
            fill_sine(TONES[tone]);
            run(RATES[rate], input, input, INPUT_FRAMES);
            uint32_t count = (INPUT_FRAMES - 2 * SKIP_FRAMES) * factor;
            double gain =
                amplitude_at(output[0], SKIP_FRAMES * factor, count, TONES[tone] / factor) /
                amplitude_at(input, SKIP_FRAMES, count / factor, TONES[tone]);
            CHECK(fabs(decibels(gain)) < 0.01);
        }
    }
}

static void images_above_the_source_band_are_90_db_down(void)
{
    static const double TONES[] = {0.1, 0.3, 0.4};
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        uint32_t factor = upsampler_factor(RATES[rate]);
        for (size_t tone = 0; tone < sizeof TONES / sizeof TONES[0]; tone++) {
            fill_sine(TONES[tone]);
            run(RATES[rate], input, input, INPUT_FRAMES);
            uint32_t first = SKIP_FRAMES * factor;
            uint32_t count = (INPUT_FRAMES - 2 * SKIP_FRAMES) * factor;
            double wanted = amplitude_at(output[0], first, count, TONES[tone] / factor);
            for (uint32_t image = 1; image < factor; image++) {
                double below = (image - TONES[tone]) / factor;
                double above = (image + TONES[tone]) / factor;
                CHECK(decibels(amplitude_at(output[0], first, count, below) / wanted) < -88);
                CHECK(decibels(amplitude_at(output[0], first, count, above) / wanted) < -88);
            }
        }
    }
}

static void a_click_comes_out_at_its_own_time(void)
{
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        uint32_t factor = upsampler_factor(RATES[rate]);
        for (uint32_t index = 0; index < 400; index++) {
            input[index] = index == 200 ? 4000000 : 0;
        }
        uint32_t produced = run(RATES[rate], input, input, 400);
        uint32_t peak = 0;
        for (uint32_t at = 1; at < produced; at++) {
            peak = output[0][at] > output[0][peak] ? at : peak;
        }
        CHECK(peak == 200 * factor || peak + 1 == 200 * factor);
    }
}

static void one_channel_alone_matches_the_pair(void)
{
    static int32_t single[OUTPUT_CAPACITY + UPSAMPLER_MAX_FACTOR];
    fill_sine(0.23);
    uint32_t produced = run(12000, input, input, 600);
    upsampler_start(&upsampler, 12000, BITS);
    uint32_t alone = 0;
    for (uint32_t index = 0; index < 600 + UPSAMPLER_DELAY; index++) {
        upsampler_put(&upsampler, 0, index < 600 ? input[index] : 0);
        alone += upsampler_render(&upsampler, &single[alone], NULL);
        upsampler_advance(&upsampler);
    }
    bool same = alone == produced;
    for (uint32_t at = 0; same && at < produced; at++) {
        same = single[at] == output[0][at];
    }
    CHECK(same);
}

static void loud_input_is_held_within_24_bits(void)
{
    for (uint32_t index = 0; index < INPUT_FRAMES; index++) {
        input[index] = (index / 7) % 2 == 0 ? 8388607 : -8388608;
    }
    uint32_t produced = run(22050, input, input, INPUT_FRAMES);
    bool inside = true;
    bool reached = false;
    for (uint32_t at = 0; at < produced; at++) {
        inside = inside && output[0][at] <= 8388607 && output[0][at] >= -8388608;
        reached = reached || output[0][at] == 8388607;
    }
    CHECK(inside);
    CHECK(reached);
}

static void the_channels_stay_apart(void)
{
    static int32_t silence[INPUT_FRAMES];
    fill_sine(0.2);
    uint32_t produced = run(16000, input, silence, INPUT_FRAMES);
    bool quiet = true;
    for (uint32_t at = 0; at < produced; at++) {
        quiet = quiet && output[1][at] == 0;
    }
    CHECK(quiet);
    CHECK(amplitude_at(output[0], SKIP_FRAMES * 3, 3000, 0.2 / 3) > AMPLITUDE * 0.99);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(rates_below_32_khz_are_raised_to_44_1_or_48),
        TEST_CASE(every_input_frame_becomes_factor_output_frames),
        TEST_CASE(a_steady_level_passes_unchanged),
        TEST_CASE(the_passband_is_flat_to_0_41_of_the_source_rate),
        TEST_CASE(images_above_the_source_band_are_90_db_down),
        TEST_CASE(a_click_comes_out_at_its_own_time),
        TEST_CASE(one_channel_alone_matches_the_pair),
        TEST_CASE(loud_input_is_held_within_24_bits),
        TEST_CASE(the_channels_stay_apart),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
