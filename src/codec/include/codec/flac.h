#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    FLAC_MAX_CHANNELS = 2,
    FLAC_MAX_BLOCK = 16384,
    FLAC_SUBSET_BLOCK = 4608,
    FLAC_MAX_BITS = 32,
    FLAC_BUFFER_BYTES = 4096
};

typedef enum FlacJoin {
    FLAC_JOIN_NONE,
    FLAC_JOIN_LEFT_SIDE,
    FLAC_JOIN_SIDE_RIGHT,
    FLAC_JOIN_MID_SIDE
} FlacJoin;

typedef enum FlacResult {
    FLAC_OK,
    FLAC_END,
    FLAC_ERROR,
    FLAC_UNSUPPORTED
} FlacResult;

typedef struct FlacInput {
    bool (*read)(void *context, uint32_t offset, void *buffer, uint32_t length);
    void *context;
    uint32_t size;
} FlacInput;

typedef struct FlacInfo {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint16_t min_block;
    uint16_t max_block;
    uint32_t total_samples;
} FlacInfo;

typedef struct FlacBits {
    FlacInput input;
    uint8_t buffer[FLAC_BUFFER_BYTES];
    uint32_t buffer_offset;
    uint32_t buffer_length;
    uint32_t index;
    uint32_t crc_index;
    uint32_t word;
    uint32_t bits;
    uint16_t crc16;
    uint8_t crc8;
    bool crc8_done;
    bool failed;
} FlacBits;

typedef struct FlacLpcJob {
    const int32_t *coefficients;
    uint32_t block_size;
    uint32_t order;
    uint32_t shift;
    uint32_t width;
    uint32_t precision;
} FlacLpcJob;

typedef struct FlacLpcHook {
    bool (*restore)(void *context, int32_t *samples, FlacLpcJob job);
    void *context;
} FlacLpcHook;

typedef struct FlacDecoder {
    FlacBits bits;
    FlacInfo info;
    uint32_t audio_offset;
    uint32_t decoded_samples;
    uint32_t block_samples;
    uint32_t frame_first_sample;
    FlacLpcHook lpc_hook;
    int32_t *samples[FLAC_MAX_CHANNELS];
    uint32_t capacity;
    bool defer_join;
    uint8_t join;
} FlacDecoder;

typedef struct FlacBlockBuffer {
    int32_t samples[FLAC_MAX_CHANNELS][FLAC_SUBSET_BLOCK];
} FlacBlockBuffer;

FlacResult flac_open(FlacDecoder *decoder, FlacInput input);
FlacResult flac_read_header(FlacBits *bits, FlacInput input, FlacInfo *info,
                            uint32_t *audio_offset);
void flac_start(FlacDecoder *decoder, FlacInput input, FlacInfo info);
uint32_t flac_position(const FlacDecoder *decoder);
void flac_rewind(FlacDecoder *decoder, uint32_t position);
FlacResult flac_decode_frame(FlacDecoder *decoder);
FlacResult flac_seek(FlacDecoder *decoder, uint32_t sample);
const int32_t *flac_channel(const FlacDecoder *decoder, int channel);
void flac_set_lpc_hook(FlacDecoder *decoder, FlacLpcHook hook);
void flac_set_buffers(FlacDecoder *decoder, int32_t *first, int32_t *second, uint32_t capacity);
void flac_use_block_buffer(FlacDecoder *decoder, FlacBlockBuffer *buffer);
void flac_defer_join(FlacDecoder *decoder, bool defer);
void flac_join(FlacJoin join, const int32_t *first, const int32_t *second, int32_t *left,
               int32_t *right, uint32_t count);
void flac_join_pair(FlacJoin join, int32_t first, int32_t second, int32_t *left, int32_t *right);
