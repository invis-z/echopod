#include "fixture_library.h"

#include "library/covers.h"
#include "library/store_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t fixture_data[FIXTURE_CAPACITY];
static uint32_t fixture_size;

static uint32_t load_fixture(const char *path, uint8_t *buffer, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "missing fixture %s\n", path);
        exit(1);
    }
    uint32_t size = (uint32_t)fread(buffer, 1, capacity, file);
    fclose(file);
    return size;
}

const uint8_t *fixture_bytes(uint32_t *size)
{
    if (fixture_size == 0) {
        fixture_size =
            load_fixture(RIPPL_FIXTURE_DIR "/small_library.db", fixture_data, sizeof fixture_data);
    }
    *size = fixture_size;
    return fixture_data;
}

Library *fixture_library_open(FixtureLibrary *fixture)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    memcpy(fixture->bytes, bytes, size);
    fixture->size = size;
    fixture->memory = (MemorySource){.bytes = fixture->bytes, .size = size};
    library_open(&fixture->library, memory_source(&fixture->memory), size);
    return &fixture->library;
}

static uint16_t fixture_color(uint32_t album)
{
    return album == FIXTURE_RED_ALBUM    ? FIXTURE_RED
           : album == FIXTURE_BLUE_ALBUM ? FIXTURE_BLUE
                                         : 0;
}

static bool append_cover(SegmentStore *store, uint16_t color)
{
    static uint8_t record[COVER_RECORD_BYTES];
    for (uint32_t index = 0; index < COVER_RECORD_BYTES; index += 2) {
        record[index] = (uint8_t)color;
        record[index + 1] = (uint8_t)(color >> 8);
    }
    return segment_store_append(store, LIBRARY_STREAM_COVERS, record, sizeof record) == SEGMENT_OK;
}

static bool append_row(SegmentStore *store, uint32_t slot)
{
    uint8_t row[COVER_ROW_BYTES] = {(uint8_t)slot,
                                    (uint8_t)(slot >> 8),
                                    (uint8_t)(slot >> 16),
                                    (uint8_t)(slot >> 24),
                                    1,
                                    0,
                                    0,
                                    0};
    return segment_store_append(store, LIBRARY_STREAM_ALBUM_COVERS, row, sizeof row) == SEGMENT_OK;
}

bool fixture_covers_none(SegmentStore *store, uint32_t albums)
{
    segment_store_begin(store);
    segment_store_start(store, LIBRARY_STREAM_ALBUM_COVERS, SEGMENT_POOL_LIBRARY);
    segment_store_start(store, LIBRARY_STREAM_COVERS, SEGMENT_POOL_COVERS);
    for (uint32_t rank = 0; rank < albums; rank++) {
        if (!append_row(store, COVER_NONE)) {
            return false;
        }
    }
    segment_store_set_region(store, LIBRARY_REGION_ALBUM_COVERS,
                             (SegmentRegion){.stream = LIBRARY_STREAM_ALBUM_COVERS,
                                             .offset = 0,
                                             .length = albums * COVER_ROW_BYTES});
    segment_store_set_value(store, LIBRARY_VALUE_COVERS, LIBRARY_STREAM_COVERS);
    segment_store_set_value(store, LIBRARY_VALUE_COVER_SLOTS, 0);
    return segment_store_commit(store) == SEGMENT_OK;
}

bool fixture_covers_write(SegmentStore *store, const Library *library)
{
    uint32_t albums = library_count(library, LIBRARY_ALBUMS);
    uint32_t slots = 0;
    segment_store_begin(store);
    segment_store_start(store, LIBRARY_STREAM_ALBUM_COVERS, SEGMENT_POOL_LIBRARY);
    segment_store_start(store, LIBRARY_STREAM_COVERS, SEGMENT_POOL_COVERS);
    for (uint32_t rank = 0; rank < albums; rank++) {
        uint32_t album = 0;
        uint32_t found = 0;
        while (album < albums && (!library_album_rank(library, album, &found) || found != rank)) {
            album++;
        }
        uint16_t color = fixture_color(album);
        bool written = color == 0 ? append_row(store, COVER_NONE)
                                  : append_cover(store, color) && append_row(store, slots++);
        if (!written) {
            return false;
        }
    }
    segment_store_set_region(store, LIBRARY_REGION_ALBUM_COVERS,
                             (SegmentRegion){.stream = LIBRARY_STREAM_ALBUM_COVERS,
                                             .offset = 0,
                                             .length = albums * COVER_ROW_BYTES});
    segment_store_set_value(store, LIBRARY_VALUE_COVERS, LIBRARY_STREAM_COVERS);
    segment_store_set_value(store, LIBRARY_VALUE_COVER_SLOTS, slots);
    return segment_store_commit(store) == SEGMENT_OK;
}
