#include "device/wav_format.h"

#include <stddef.h>

enum {
    RIFF_HEADER_BYTES = 12,
    CHUNK_HEADER_BYTES = 8,
    FORMAT_BYTES = 26,
    PCM_TAG = 1,
    EXTENSIBLE_TAG = 0xFFFE,
    EXTENSIBLE_MIN_BYTES = 26,
    MAX_CHUNKS = 64,
    MAX_CHANNELS = 2,
    BITS_PER_BYTE = 8,
    EIGHT_BIT_CENTRE = 128
};

static uint32_t read_u16(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return read_u16(bytes) | read_u16(bytes + 2) << 16;
}

static bool same_tag(const uint8_t *bytes, const char *tag)
{
    return bytes[0] == (uint8_t)tag[0] && bytes[1] == (uint8_t)tag[1] &&
           bytes[2] == (uint8_t)tag[2] && bytes[3] == (uint8_t)tag[3];
}

static bool supported_bits(uint32_t bits)
{
    return bits == 8 || bits == 16 || bits == 24 || bits == 32;
}

static bool read_format(FlacInput input, uint32_t offset, uint32_t size, WavFormat *format)
{
    uint8_t bytes[FORMAT_BYTES] = {0};
    uint32_t length = size < FORMAT_BYTES ? size : FORMAT_BYTES;
    if (size < 16 || !input.read(input.context, offset, bytes, length)) {
        return false;
    }
    uint32_t tag = read_u16(bytes);
    if (tag == EXTENSIBLE_TAG) {
        if (size < EXTENSIBLE_MIN_BYTES || read_u16(bytes + 24) != PCM_TAG) {
            return false;
        }
    } else if (tag != PCM_TAG) {
        return false;
    }
    uint32_t channels = read_u16(bytes + 2);
    uint32_t rate = read_u32(bytes + 4);
    uint32_t frame_bytes = read_u16(bytes + 12);
    uint32_t bits = read_u16(bytes + 14);
    if (channels < 1 || channels > MAX_CHANNELS || rate == 0 || !supported_bits(bits) ||
        frame_bytes != channels * bits / BITS_PER_BYTE) {
        return false;
    }
    format->sample_rate = rate;
    format->channels = (uint8_t)channels;
    format->bits = (uint8_t)bits;
    format->frame_bytes = (uint16_t)frame_bytes;
    return true;
}

bool wav_parse(FlacInput input, WavFormat *format)
{
    uint8_t header[RIFF_HEADER_BYTES];
    if (input.size < RIFF_HEADER_BYTES || !input.read(input.context, 0, header, sizeof header) ||
        !same_tag(header, "RIFF") || !same_tag(header + 8, "WAVE")) {
        return false;
    }
    bool have_format = false;
    uint32_t offset = RIFF_HEADER_BYTES;
    for (int chunk = 0; chunk < MAX_CHUNKS && input.size - offset >= CHUNK_HEADER_BYTES; chunk++) {
        uint8_t bytes[CHUNK_HEADER_BYTES];
        if (!input.read(input.context, offset, bytes, sizeof bytes)) {
            return false;
        }
        uint32_t size = read_u32(bytes + 4);
        uint32_t body = offset + CHUNK_HEADER_BYTES;
        if (same_tag(bytes, "fmt ")) {
            if (!read_format(input, body, size, format)) {
                return false;
            }
            have_format = true;
        } else if (same_tag(bytes, "data")) {
            if (!have_format) {
                return false;
            }
            uint32_t available = input.size - body;
            uint32_t length = size < available ? size : available;
            format->data_offset = body;
            format->frames = length / format->frame_bytes;
            return true;
        }
        uint32_t remaining = input.size - body;
        uint32_t step = size + (size & 1u);
        if (step >= remaining) {
            return false;
        }
        offset = body + step;
    }
    return false;
}

int32_t wav_sample(const uint8_t *bytes, uint32_t bits)
{
    switch (bits) {
    case 8:
        return (int32_t)bytes[0] - EIGHT_BIT_CENTRE;
    case 16:
        return (int32_t)(int16_t)read_u16(bytes);
    case 24: {
        uint32_t value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16;
        return (int32_t)(value ^ 0x800000u) - 0x800000;
    }
    default:
        return (int32_t)read_u32(bytes);
    }
}
