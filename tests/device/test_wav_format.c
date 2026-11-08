#include <string.h>

#include "check.h"
#include "device/core1_pcm.h"
#include "device/wav_format.h"

enum {
    FILE_CAPACITY = 65536,
    PCM_TAG = 1,
    FLOAT_TAG = 3,
    EXTENSIBLE_TAG = 0xFFFE
};

typedef struct Builder {
    uint8_t bytes[FILE_CAPACITY];
    uint32_t size;
} Builder;

static Builder file;
static Core1Pcm pcm;
static int32_t left[STREAM_PCM_BLOCK_FRAMES];
static int32_t right[STREAM_PCM_BLOCK_FRAMES];
static uint32_t data_base;
static bool reads_fail;
static bool starved;

static void put_bytes(const void *data, uint32_t length)
{
    memcpy(file.bytes + file.size, data, length);
    file.size += length;
}

static void put_u16(uint32_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    put_bytes(bytes, 2);
}

static void put_u32(uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    put_bytes(bytes, 4);
}

static void put_sample(int32_t value, int bits)
{
    if (bits == 8) {
        uint8_t byte = (uint8_t)(value + 128);
        put_bytes(&byte, 1);
        return;
    }
    for (int shift = 0; shift < bits; shift += 8) {
        uint8_t byte = (uint8_t)((uint32_t)value >> shift);
        put_bytes(&byte, 1);
    }
}

static void put_fmt(uint32_t tag, int channels, uint32_t rate, int bits)
{
    uint32_t align = (uint32_t)(channels * bits / 8);
    put_bytes("fmt ", 4);
    put_u32(tag == EXTENSIBLE_TAG ? 40 : 16);
    put_u16(tag);
    put_u16((uint32_t)channels);
    put_u32(rate);
    put_u32(rate * align);
    put_u16(align);
    put_u16((uint32_t)bits);
    if (tag == EXTENSIBLE_TAG) {
        static const uint8_t PCM_GUID_TAIL[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                                  0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        put_u16(22);
        put_u16((uint32_t)bits);
        put_u32(channels == 1 ? 4u : 3u);
        put_u16(PCM_TAG);
        put_bytes(PCM_GUID_TAIL, sizeof PCM_GUID_TAIL);
    }
}

static int32_t sample_at(uint32_t frame, int channel, int bits)
{
    uint32_t mix = frame * 2654435761u + (uint32_t)channel * 40503u;
    if (bits == 32) {
        return (int32_t)mix;
    }
    uint32_t span = bits == 8 ? 128u : bits == 16 ? 32768u : 8388608u;
    return (int32_t)(mix % (2u * span)) - (int32_t)span;
}

static void build(uint32_t tag, int channels, uint32_t rate, int bits, uint32_t frames,
                  bool extra_chunk)
{
    file.size = 0;
    put_bytes("RIFF", 4);
    put_u32(0);
    put_bytes("WAVE", 4);
    if (extra_chunk) {
        put_bytes("LIST", 4);
        put_u32(3);
        put_bytes("abc", 3);
        put_bytes("\0", 1);
    }
    put_fmt(tag, channels, rate, bits);
    put_bytes("data", 4);
    put_u32(frames * (uint32_t)(channels * bits / 8));
    for (uint32_t frame = 0; frame < frames; frame++) {
        for (int channel = 0; channel < channels; channel++) {
            put_sample(sample_at(frame, channel, bits), bits);
        }
    }
    uint32_t riff = file.size - 8;
    memcpy(file.bytes + 4, &riff, 4);
}

static bool read_file(void *context, uint32_t offset, void *destination, uint32_t length)
{
    (void)context;
    if (offset > file.size || length > file.size - offset) {
        return false;
    }
    memcpy(destination, file.bytes + offset, length);
    return true;
}

static FlacInput whole_file(void)
{
    return (FlacInput){.read = read_file, .context = NULL, .size = file.size};
}

static bool read_data(void *context, uint32_t offset, void *destination, uint32_t length)
{
    return !reads_fail && read_file(context, data_base + offset, destination, length);
}

static bool parses(WavFormat *format)
{
    *format = (WavFormat){0};
    return wav_parse(whole_file(), format);
}

static Core1Decoder decoder_from(WavFormat format, uint32_t first_frame)
{
    Core1Decoder decoder = core1_pcm_decoder(&pcm, left, right);
    data_base = format.data_offset + first_frame * format.frame_bytes;
    reads_fail = false;
    starved = false;
    FlacInput input = {.read = read_data,
                       .context = NULL,
                       .size = (format.frames - first_frame) * format.frame_bytes};
    FlacInfo info = {.sample_rate = format.sample_rate,
                     .channels = format.channels,
                     .bits_per_sample = format.bits,
                     .total_samples = format.frames};
    decoder.start(decoder.context, input, info, first_frame, &starved);
    return decoder;
}

static bool block_matches(const Core1Block *block, int channels, int bits)
{
    for (uint32_t index = 0; index < block->samples; index++) {
        uint32_t at = block->first_sample + index;
        int32_t expected_left = sample_at(at, 0, bits);
        int32_t expected_right = channels == 1 ? expected_left : sample_at(at, 1, bits);
        if (block->channels[0][index] != expected_left ||
            block->channels[1][index] != expected_right) {
            return false;
        }
    }
    return true;
}

static bool decodes_through(int channels, int bits, uint32_t first_frame)
{
    WavFormat format;
    if (!parses(&format)) {
        return false;
    }
    Core1Decoder decoder = decoder_from(format, first_frame);
    Core1Block block;
    uint32_t expected = first_frame;
    Core1DecodeResult result;
    while ((result = decoder.decode(decoder.context, &block)) == CORE1_DECODED) {
        if (block.first_sample != expected || !block_matches(&block, channels, bits)) {
            return false;
        }
        expected += block.samples;
    }
    return result == CORE1_FINISHED && expected == format.frames;
}

static void sixteen_bit_stereo_is_described_and_decodes_every_frame(void)
{
    build(PCM_TAG, 2, 44100, 16, 5000, false);
    WavFormat format;
    CHECK(parses(&format));
    CHECK_EQUAL(44100u, format.sample_rate);
    CHECK_EQUAL(16, format.bits);
    CHECK_EQUAL(2, format.channels);
    CHECK_EQUAL(4, format.frame_bytes);
    CHECK_EQUAL(5000u, format.frames);
    CHECK_EQUAL(44u, format.data_offset);
    CHECK(decodes_through(2, 16, 0));
}

static uint32_t first_block_frames(void)
{
    WavFormat format;
    if (!parses(&format)) {
        return 0;
    }
    Core1Decoder decoder = decoder_from(format, 0);
    Core1Block block;
    return decoder.decode(decoder.context, &block) == CORE1_DECODED ? block.samples : 0;
}

static void blocks_hold_at_most_4096_frames_and_16_kb_of_file(void)
{
    build(PCM_TAG, 2, 48000, 16, 5000, false);
    CHECK_EQUAL((uint32_t)STREAM_PCM_BLOCK_FRAMES, first_block_frames());
    build(PCM_TAG, 2, 48000, 32, 5000, false);
    CHECK_EQUAL((uint32_t)STREAM_PCM_BLOCK_BYTES / 8u, first_block_frames());
}

static void twenty_four_bit_samples_keep_their_sign(void)
{
    build(PCM_TAG, 2, 96000, 24, 3000, false);
    CHECK(decodes_through(2, 24, 0));
}

static void thirty_two_bit_samples_stay_whole(void)
{
    build(PCM_TAG, 2, 44100, 32, 3000, false);
    WavFormat format;
    CHECK(parses(&format));
    CHECK_EQUAL(32, format.bits);
    CHECK(decodes_through(2, 32, 0));
}

static void mono_decodes_on_both_sides(void)
{
    build(PCM_TAG, 1, 48000, 16, 1000, false);
    WavFormat format;
    CHECK(parses(&format));
    CHECK_EQUAL(1, format.channels);
    CHECK(decodes_through(1, 16, 0));
}

static void eight_bit_samples_are_centred_on_zero(void)
{
    build(PCM_TAG, 2, 44100, 8, 600, false);
    CHECK(decodes_through(2, 8, 0));
}

static void the_extensible_header_and_other_chunks_are_understood(void)
{
    build(EXTENSIBLE_TAG, 2, 88200, 24, 700, true);
    WavFormat format;
    CHECK(parses(&format));
    CHECK_EQUAL(88200u, format.sample_rate);
    CHECK(decodes_through(2, 24, 0));
}

static void decoding_part_way_begins_at_that_frame(void)
{
    build(PCM_TAG, 2, 44100, 16, 9000, false);
    CHECK(decodes_through(2, 16, 4410));
}

static void a_data_chunk_longer_than_the_file_stops_at_the_file(void)
{
    build(PCM_TAG, 2, 44100, 16, 1000, false);
    file.size -= 400;
    WavFormat format;
    CHECK(parses(&format));
    CHECK_EQUAL(900u, format.frames);
    CHECK(decodes_through(2, 16, 0));
}

static void files_it_cannot_play_are_refused(void)
{
    WavFormat format;
    build(FLOAT_TAG, 2, 44100, 32, 100, false);
    CHECK(!parses(&format));
    build(PCM_TAG, 6, 48000, 16, 100, false);
    CHECK(!parses(&format));
    build(PCM_TAG, 2, 44100, 16, 100, false);
    memcpy(file.bytes, "fLaC", 4);
    CHECK(!parses(&format));
}

static void a_failed_read_fails_unless_the_ring_ran_dry(void)
{
    build(PCM_TAG, 2, 44100, 16, 100, false);
    WavFormat format;
    CHECK(parses(&format));
    Core1Decoder decoder = decoder_from(format, 0);
    Core1Block block;
    reads_fail = true;
    CHECK_EQUAL(CORE1_FAILED, decoder.decode(decoder.context, &block));
    starved = true;
    CHECK_EQUAL(CORE1_STARVED, decoder.decode(decoder.context, &block));
    reads_fail = false;
    CHECK_EQUAL(CORE1_DECODED, decoder.decode(decoder.context, &block));
    CHECK_EQUAL(0u, block.first_sample);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(sixteen_bit_stereo_is_described_and_decodes_every_frame),
        TEST_CASE(blocks_hold_at_most_4096_frames_and_16_kb_of_file),
        TEST_CASE(twenty_four_bit_samples_keep_their_sign),
        TEST_CASE(thirty_two_bit_samples_stay_whole),
        TEST_CASE(mono_decodes_on_both_sides),
        TEST_CASE(eight_bit_samples_are_centred_on_zero),
        TEST_CASE(the_extensible_header_and_other_chunks_are_understood),
        TEST_CASE(decoding_part_way_begins_at_that_frame),
        TEST_CASE(a_data_chunk_longer_than_the_file_stops_at_the_file),
        TEST_CASE(files_it_cannot_play_are_refused),
        TEST_CASE(a_failed_read_fails_unless_the_ring_ran_dry),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
