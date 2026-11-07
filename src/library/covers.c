#include "library/covers.h"

#include "library/store_layout.h"

static uint32_t u32_at(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

uint32_t covers_offset(uint32_t slot)
{
    return slot / COVERS_PER_SEGMENT * SEGMENT_COVERS_SIZE +
           slot % COVERS_PER_SEGMENT * COVER_RECORD_BYTES;
}

bool covers_open(Covers *covers, const SegmentStore *store)
{
    SegmentRegion region = segment_store_region(store, LIBRARY_REGION_ALBUM_COVERS);
    uint32_t stream = segment_store_value(store, LIBRARY_VALUE_COVERS);
    *covers = (Covers){.store = store, .stream = (uint8_t)stream, .albums = 0, .slots = 0};
    if (region.stream != LIBRARY_STREAM_ALBUM_COVERS || region.offset != 0 ||
        (stream != LIBRARY_STREAM_COVERS && stream != LIBRARY_STREAM_OTHER_COVERS)) {
        return false;
    }
    covers->albums = region.length / COVER_ROW_BYTES;
    covers->slots = segment_store_value(store, LIBRARY_VALUE_COVER_SLOTS);
    return covers->albums > 0;
}

bool covers_slot(const Covers *covers, uint32_t rank, uint32_t *slot)
{
    uint8_t bytes[4];
    if (rank >= covers->albums ||
        !segment_store_read(covers->store, LIBRARY_STREAM_ALBUM_COVERS, rank * COVER_ROW_BYTES,
                            bytes, sizeof bytes)) {
        return false;
    }
    *slot = u32_at(bytes);
    return *slot < covers->slots;
}

bool covers_read(const Covers *covers, uint32_t rank, CoverSize size, uint16_t *pixels)
{
    uint32_t slot = 0;
    if (!covers_slot(covers, rank, &slot)) {
        return false;
    }
    bool full = size == COVER_FULL;
    uint32_t offset = covers_offset(slot) + (full ? 0 : COVER_FULL_BYTES);
    uint32_t length = full ? COVER_FULL_BYTES : COVER_HALF_BYTES;
    if (!segment_store_read(covers->store, covers->stream, offset, pixels, length)) {
        return false;
    }
    uint8_t *bytes = (uint8_t *)pixels;
    for (uint32_t index = 0; index < length / 2; index++) {
        pixels[index] = (uint16_t)(bytes[index * 2] | bytes[index * 2 + 1] << 8);
    }
    return true;
}
