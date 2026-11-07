#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fake_card.h"
#include "library/library.h"
#include "library/library_builder.h"
#include "library/segment_scratch.h"
#include "library/segment_stream.h"
#include "random_library.h"

enum {
    PLAYER_MEMORY = 360 * 1024,
    TRACKS = 250000,
    PLAYLISTS = 30,
    INDEX_STREAM = 1,
    SCRATCH_STREAM = 7,
    LIBRARY_REGION = 0,
    SAMPLES = 2000,
    TEXT_CAPACITY = 1024
};

static FakeCard card;
static SegmentStore store;
static SegmentStore reopened;
static SegmentScratch scratch;
static uint8_t memory[PLAYER_MEMORY];

static int by_text(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static BuilderResult add_library(LibraryBuilder *builder, const RandomLibrary *library)
{
    for (uint32_t index = 0; index < library->track_count; index++) {
        BuilderResult result = library_builder_add_track(builder, &library->tracks[index]);
        if (result != BUILDER_OK) {
            return result;
        }
    }
    for (uint32_t index = 0; index < library->playlist_count; index++) {
        const RandomPlaylist *playlist = &library->playlists[index];
        BuilderResult result =
            library_builder_begin_playlist(builder, playlist->name, playlist->folder);
        for (uint32_t entry = 0; result == BUILDER_OK && entry < playlist->count; entry++) {
            result = library_builder_playlist_entry(builder, playlist->entries[entry]);
        }
        if (result != BUILDER_OK) {
            return result;
        }
    }
    return BUILDER_OK;
}

static uint32_t build_on_card(const RandomLibrary *library)
{
    segment_store_begin(&store);
    segment_store_start(&store, INDEX_STREAM, SEGMENT_POOL_LIBRARY);
    segment_scratch_start(&scratch, &store, SCRATCH_STREAM);
    SegmentStreamIo output = {.store = &store, .stream = INDEX_STREAM};
    LibraryBuilder builder;
    library_builder_init(&builder, memory, sizeof memory, segment_scratch_io(&scratch));
    CHECK_EQUAL(BUILDER_OK, add_library(&builder, library));
    uint32_t length = 0;
    CHECK_EQUAL(BUILDER_OK,
                library_builder_finish_wide(&builder, segment_stream_sink(&output), 0, &length));
    segment_scratch_finish(&scratch);
    segment_store_set_region(
        &store, LIBRARY_REGION,
        (SegmentRegion){.stream = INDEX_STREAM, .offset = 0, .length = length});
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    return length;
}

static bool known_path(const char **paths, uint32_t count, const char *path)
{
    return bsearch(&path, paths, count, sizeof paths[0], by_text) != NULL;
}

static void check_sections(const Library *library)
{
    uint32_t tracks = library_count(library, LIBRARY_TRACKS);
    for (uint32_t album = 0; album < library_count(library, LIBRARY_ALBUMS); album++) {
        LibraryAlbum record;
        CHECK(library_read_album(library, album, &record));
        CHECK(record.track_count > 0 && record.first_track + record.track_count <= tracks);
    }
    LibraryFolder root;
    CHECK(library_read_folder(library, 0, &root));
    CHECK_EQUAL(LIBRARY_NONE, root.parent);
    uint32_t last = 0;
    CHECK(library_read_index(library, LIBRARY_TITLE_ORDER, tracks - 1, &last));
    CHECK(last < tracks);
}

static void a_quarter_million_tracks_build_in_the_players_memory(void)
{
    RandomLibrary library;
    random_library_make(&library, TRACKS, PLAYLISTS, 21);
    const char **paths = malloc(TRACKS * sizeof paths[0]);
    for (uint32_t index = 0; index < TRACKS; index++) {
        paths[index] = library.tracks[index].path;
    }
    qsort(paths, TRACKS, sizeof paths[0], by_text);
    fake_card_init(&card, UINT64_MAX);
    segment_store_open(&store, fake_card_files(&card));
    uint32_t length = build_on_card(&library);
    segment_store_open(&reopened, fake_card_files(&card));
    SegmentRegion region = segment_store_region(&reopened, LIBRARY_REGION);
    CHECK_EQUAL(length, region.length);
    SegmentStreamIo input = {.store = &reopened, .stream = region.stream};
    Library opened;
    CHECK_EQUAL(LIBRARY_OK, library_open(&opened, segment_stream_source(&input), region.length));
    CHECK_EQUAL(TRACKS, library_count(&opened, LIBRARY_TRACKS));
    CHECK_EQUAL(PLAYLISTS, library_count(&opened, LIBRARY_PLAYLISTS));
    check_sections(&opened);
    static char text[TEXT_CAPACITY];
    for (uint32_t sample = 0; sample < SAMPLES; sample++) {
        LibraryTrack track;
        uint32_t index = (uint32_t)(((uint64_t)sample * 2654435761u) % TRACKS);
        CHECK(library_read_track(&opened, index, &track));
        CHECK(library_read_string(&opened, track.path, text, sizeof text));
        CHECK(known_path(paths, TRACKS, text));
        uint64_t id = 0;
        uint32_t found = LIBRARY_NONE;
        CHECK(library_track_id(&opened, index, &id));
        CHECK(library_find_track(&opened, id, &found));
        CHECK_EQUAL(index, found);
    }
    CHECK_EQUAL(0, segment_store_length(&reopened, SCRATCH_STREAM));
    free(paths);
    fake_card_free(&card);
    random_library_free(&library);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_quarter_million_tracks_build_in_the_players_memory),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
