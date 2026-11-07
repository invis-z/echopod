#include <stdbool.h>
#include <stdint.h>

#include "big_library.h"
#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "home_art.h"
#include "library_store.h"

enum {
    BIG_ALBUMS = 2500,
    TRACKS_PER_ALBUM = 2,
    UPDATES_FOR_A_LAP = BIG_ALBUMS,
    RED_ALBUM = 0,
    BLUE_ALBUM = 2
};

static FixtureLibrary fixture;
static BigLibrary big;
static LibraryStore store;
static SegmentStore segments;
static CoverCache covers;
static Player player;
static HomeArt art;

static HomeArt *art_over(const Library *library, bool with_covers)
{
    fake_hal_reset();
    segment_store_open(&segments, library_store_files());
    if (with_covers && library == &fixture.library) {
        CHECK(fixture_covers_write(&segments, library));
    } else if (with_covers) {
        CHECK(fixture_covers_none(&segments, library_count(library, LIBRARY_ALBUMS)));
    }
    store.library = *library;
    store.available = true;
    cover_cache_open(&covers, &store.library, &segments);
    player_init(&player, &store.library);
    home_art_init(&art, &covers, &store, &player, &DEFAULT_THEME);
    return &art;
}

static int reads_of_update(MemorySource *memory, uint32_t now_ms)
{
    memory->reads = 0;
    home_art_update(&art, now_ms);
    return memory->reads;
}

static void without_covers_home_art_reads_no_albums(void)
{
    art_over(fixture_library_open(&fixture), false);
    int reads = 0;
    for (uint32_t cycle = 0; cycle < 4; cycle++) {
        reads += reads_of_update(&fixture.memory, cycle * HOME_ART_CYCLE_MS);
    }
    CHECK_EQUAL(0, reads);
}

static void home_art_shows_the_first_album_with_a_cover(void)
{
    art_over(fixture_library_open(&fixture), true);
    home_art_update(&art, 0);
    Image image;
    CHECK(art.has_album);
    CHECK_EQUAL(RED_ALBUM, art.album);
    CHECK_EQUAL(COVER_LOADED, cover_cache_full(&covers, RED_ALBUM, &image));
    home_art_update(&art, HOME_ART_CYCLE_MS);
    CHECK_EQUAL(BLUE_ALBUM, art.album);
    CHECK_EQUAL(COVER_LOADED, cover_cache_full(&covers, BLUE_ALBUM, &image));
}

static uint32_t cover_reads_of_update(uint32_t now_ms)
{
    uint32_t before = fake_hal_file_read_count();
    home_art_update(&art, now_ms);
    return fake_hal_file_read_count() - before;
}

static void home_art_looks_at_only_a_few_albums_per_update(void)
{
    art_over(big_library_open(&big, BIG_ALBUMS * TRACKS_PER_ALBUM, TRACKS_PER_ALBUM), true);
    CHECK(cover_reads_of_update(0) <= HOME_ART_ALBUMS_PER_UPDATE);
    CHECK(cover_reads_of_update(1) <= HOME_ART_ALBUMS_PER_UPDATE);
    big_library_close(&big);
}

static void home_art_rests_after_a_lap_without_covers_until_the_next_change(void)
{
    art_over(big_library_open(&big, BIG_ALBUMS * TRACKS_PER_ALBUM, TRACKS_PER_ALBUM), true);
    for (uint32_t update = 0; update < UPDATES_FOR_A_LAP; update++) {
        home_art_update(&art, 0);
    }
    CHECK_EQUAL(0, reads_of_update(&big.memory, 1));
    CHECK(!art.has_album);
    CHECK(reads_of_update(&big.memory, HOME_ART_CYCLE_MS) > 0);
    big_library_close(&big);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(without_covers_home_art_reads_no_albums),
        TEST_CASE(home_art_shows_the_first_album_with_a_cover),
        TEST_CASE(home_art_looks_at_only_a_few_albums_per_update),
        TEST_CASE(home_art_rests_after_a_lap_without_covers_until_the_next_change),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
