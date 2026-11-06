#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fake_card.h"
#include "library/library_builder.h"
#include "library/segment_scratch.h"
#include "random_library.h"

enum {
    PLAYER_MEMORY = 360 * 1024,
    MEMORY_SCRATCH = 32 * 1024 * 1024,
    OUTPUT_CAPACITY = 16 * 1024 * 1024,
    SCRATCH_STREAM = 7,
    GIGABYTE = 1024 * 1024 * 1024
};

typedef struct Buffer {
    uint8_t *bytes;
    uint32_t size;
    uint32_t length;
} Buffer;

static FakeCard card;
static SegmentStore store;
static SegmentScratch segments;

static bool buffer_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    Buffer *buffer = context;
    if (offset > buffer->size || length > buffer->size - offset) {
        return false;
    }
    memcpy(bytes, buffer->bytes + offset, length);
    return true;
}

static bool buffer_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Buffer *buffer = context;
    if (offset > buffer->size || length > buffer->size - offset) {
        return false;
    }
    memcpy(buffer->bytes + offset, bytes, length);
    buffer->length = offset + length > buffer->length ? offset + length : buffer->length;
    return true;
}

static Buffer make_buffer(uint32_t size)
{
    return (Buffer){.bytes = calloc(size, 1), .size = size, .length = 0};
}

static BuilderResult build(const RandomLibrary *library, BuilderScratch scratch, Buffer *output)
{
    static uint8_t memory[PLAYER_MEMORY];
    LibraryBuilder builder;
    library_builder_init(&builder, memory, sizeof memory, scratch);
    for (uint32_t index = 0; index < library->track_count; index++) {
        BuilderResult result = library_builder_add_track(&builder, &library->tracks[index]);
        if (result != BUILDER_OK) {
            return result;
        }
    }
    for (uint32_t index = 0; index < library->playlist_count; index++) {
        const RandomPlaylist *playlist = &library->playlists[index];
        BuilderResult result =
            library_builder_begin_playlist(&builder, playlist->name, playlist->folder);
        for (uint32_t entry = 0; result == BUILDER_OK && entry < playlist->count; entry++) {
            result = library_builder_playlist_entry(&builder, playlist->entries[entry]);
        }
        if (result != BUILDER_OK) {
            return result;
        }
    }
    uint32_t length = 0;
    return library_builder_finish(&builder, (ByteSink){buffer_write, output}, output->size,
                                  &length);
}

static BuilderResult build_on_segments(const RandomLibrary *library, Buffer *output)
{
    segment_store_begin(&store);
    segment_scratch_start(&segments, &store, SCRATCH_STREAM);
    BuilderResult result = build(library, segment_scratch_io(&segments), output);
    segment_scratch_finish(&segments);
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    return result;
}

static void a_build_on_segments_matches_a_build_in_memory(void)
{
    RandomLibrary library;
    random_library_make(&library, 3000, 20, 11);
    Buffer scratch = make_buffer(MEMORY_SCRATCH);
    Buffer expected = make_buffer(OUTPUT_CAPACITY);
    Buffer actual = make_buffer(OUTPUT_CAPACITY);
    CHECK_EQUAL(BUILDER_OK,
                build(&library, (BuilderScratch){buffer_read, buffer_write, &scratch, scratch.size},
                      &expected));
    fake_card_init(&card, GIGABYTE);
    segment_store_open(&store, fake_card_files(&card));
    CHECK_EQUAL(BUILDER_OK, build_on_segments(&library, &actual));
    CHECK_EQUAL(expected.length, actual.length);
    CHECK(memcmp(expected.bytes, actual.bytes, expected.length) == 0);
    CHECK_EQUAL(0, segment_store_length(&store, SCRATCH_STREAM));
    uint16_t reserved = store.committed.reserved[SEGMENT_POOL_LIBRARY];
    CHECK(reserved > 0);
    memset(actual.bytes, 0, actual.size);
    CHECK_EQUAL(BUILDER_OK, build_on_segments(&library, &actual));
    CHECK(memcmp(expected.bytes, actual.bytes, expected.length) == 0);
    CHECK_EQUAL(reserved, store.committed.reserved[SEGMENT_POOL_LIBRARY]);
    fake_card_free(&card);
    free(scratch.bytes);
    free(expected.bytes);
    free(actual.bytes);
    random_library_free(&library);
}

static void a_full_card_stops_the_build(void)
{
    RandomLibrary library;
    random_library_make(&library, 3000, 0, 12);
    Buffer output = make_buffer(OUTPUT_CAPACITY);
    fake_card_init(&card, 3u * SEGMENT_LIBRARY_SIZE);
    segment_store_open(&store, fake_card_files(&card));
    segment_store_begin(&store);
    segment_scratch_start(&segments, &store, SCRATCH_STREAM);
    BuilderResult result = build(&library, segment_scratch_io(&segments), &output);
    CHECK(result != BUILDER_OK);
    CHECK_EQUAL(SEGMENT_NO_SPACE, segments.result);
    segment_store_cancel(&store);
    fake_card_free(&card);
    free(output.bytes);
    random_library_free(&library);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_build_on_segments_matches_a_build_in_memory),
        TEST_CASE(a_full_card_stops_the_build),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
