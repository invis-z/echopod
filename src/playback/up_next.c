#include "playback/up_next.h"

#include "core/hash.h"

enum {
    VERSION = 1,
    MAGIC_FIELD = 0,
    VERSION_FIELD = 4,
    COUNT_FIELD = 6,
    IDS_FIELD = UP_NEXT_HEADER_SIZE,
    CHECKSUM_FIELD = UP_NEXT_STATE_SIZE - UP_NEXT_CHECKSUM_SIZE,
    MAGIC_SIZE = 4,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'M', 'U', 'N'};

static void put_u16(uint8_t *bytes, int offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> BITS_PER_BYTE);
}

static void put_u32(uint8_t *bytes, int offset, uint32_t value)
{
    put_u16(bytes, offset, value & 0xFFFFu);
    put_u16(bytes, offset + 2, value >> 16);
}

static uint16_t get_u16(const uint8_t *bytes, int offset)
{
    return (uint16_t)(bytes[offset] | bytes[offset + 1] << BITS_PER_BYTE);
}

static uint32_t get_u32(const uint8_t *bytes, int offset)
{
    return (uint32_t)get_u16(bytes, offset) | (uint32_t)get_u16(bytes, offset + 2) << 16;
}

static uint32_t checksum_of(const uint8_t *bytes)
{
    return hash_update(HASH_START, bytes, CHECKSUM_FIELD);
}

void up_next_clear(UpNext *up_next)
{
    up_next->count = 0;
}

bool up_next_add(UpNext *up_next, uint32_t track)
{
    if (up_next->count == UP_NEXT_CAPACITY) {
        return false;
    }
    up_next->tracks[up_next->count++] = track;
    return true;
}

bool up_next_play_next(UpNext *up_next, uint32_t track)
{
    if (up_next->count == UP_NEXT_CAPACITY) {
        return false;
    }
    for (uint32_t index = up_next->count; index > 0; index--) {
        up_next->tracks[index] = up_next->tracks[index - 1];
    }
    up_next->tracks[0] = track;
    up_next->count++;
    return true;
}

bool up_next_first(const UpNext *up_next, uint32_t *track)
{
    if (up_next->count == 0) {
        return false;
    }
    *track = up_next->tracks[0];
    return true;
}

void up_next_drop(UpNext *up_next, uint32_t count)
{
    if (count >= up_next->count) {
        up_next->count = 0;
        return;
    }
    for (uint32_t index = count; index < up_next->count; index++) {
        up_next->tracks[index - count] = up_next->tracks[index];
    }
    up_next->count = up_next->count - count;
}

void up_next_encode(const UpNext *up_next, const Library *library, uint8_t *bytes)
{
    for (int index = 0; index < UP_NEXT_STATE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
    put_u16(bytes, VERSION_FIELD, VERSION);
    uint32_t count = 0;
    for (uint32_t index = 0; index < up_next->count; index++) {
        uint64_t id = 0;
        if (library_track_id(library, up_next->tracks[index], &id)) {
            int field = IDS_FIELD + UP_NEXT_ID_SIZE * (int)count++;
            put_u32(bytes, field, (uint32_t)id);
            put_u32(bytes, field + 4, (uint32_t)(id >> 32));
        }
    }
    put_u16(bytes, COUNT_FIELD, count);
    put_u32(bytes, CHECKSUM_FIELD, checksum_of(bytes));
}

static bool header_valid(const uint8_t *bytes)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (bytes[MAGIC_FIELD + index] != MAGIC[index]) {
            return false;
        }
    }
    return get_u16(bytes, VERSION_FIELD) == VERSION &&
           get_u16(bytes, COUNT_FIELD) <= UP_NEXT_CAPACITY &&
           get_u32(bytes, CHECKSUM_FIELD) == checksum_of(bytes);
}

bool up_next_decode(const uint8_t *bytes, uint32_t length, const Library *library, UpNext *up_next)
{
    if (length < UP_NEXT_STATE_SIZE || !header_valid(bytes)) {
        return false;
    }
    uint32_t count = get_u16(bytes, COUNT_FIELD);
    up_next->count = 0;
    for (uint32_t index = 0; index < count; index++) {
        int field = IDS_FIELD + UP_NEXT_ID_SIZE * (int)index;
        uint64_t id = (uint64_t)get_u32(bytes, field + 4) << 32 | get_u32(bytes, field);
        uint32_t track = 0;
        if (library_find_track(library, id, &track)) {
            up_next->tracks[up_next->count++] = track;
        }
    }
    return true;
}
