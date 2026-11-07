#include "check.h"
#include "cover_cache.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "library_store.h"

enum {
    RED_ALBUM = FIXTURE_RED_ALBUM,
    BARE_ALBUM = 1,
    BLUE_ALBUM = FIXTURE_BLUE_ALBUM,
    RED = FIXTURE_RED,
    BLUE = FIXTURE_BLUE
};

static FixtureLibrary fixture;
static CoverCache cache;
static SegmentStore store;

static CoverCache *cache_with_covers(void)
{
    fake_hal_reset();
    Library *library = fixture_library_open(&fixture);
    segment_store_open(&store, library_store_files());
    CHECK(fixture_covers_write(&store, library));
    cover_cache_open(&cache, library, &store);
    return &cache;
}

static void without_covers_every_cover_is_missing(void)
{
    fake_hal_reset();
    segment_store_open(&store, library_store_files());
    cover_cache_open(&cache, fixture_library_open(&fixture), &store);
    CHECK(!cache.available);
    Image image;
    cover_cache_load_half(&cache, 0, RED_ALBUM);
    CHECK_EQUAL(COVER_MISSING, cover_cache_half(&cache, 0, RED_ALBUM, &image));
}

static void without_covers_loading_reads_nothing_from_the_library(void)
{
    fake_hal_reset();
    segment_store_open(&store, library_store_files());
    cover_cache_open(&cache, fixture_library_open(&fixture), &store);
    fixture.memory.reads = 0;
    cover_cache_load_full(&cache, RED_ALBUM);
    cover_cache_load_half(&cache, 0, RED_ALBUM);
    CHECK(!cover_cache_has_cover(&cache, RED_ALBUM));
    CHECK_EQUAL(0, fixture.memory.reads);
}

static void an_album_has_a_cover_only_when_the_store_holds_one(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(cover_cache_has_cover(covers, RED_ALBUM));
    CHECK(cover_cache_has_cover(covers, BLUE_ALBUM));
    CHECK(!cover_cache_has_cover(covers, BARE_ALBUM));
    CHECK_EQUAL(COVER_EMPTY, cover_cache_full(covers, RED_ALBUM, &image));
}

static void an_unloaded_cover_is_empty(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(covers->available);
    CHECK_EQUAL(COVER_EMPTY, cover_cache_half(covers, 0, RED_ALBUM, &image));
    CHECK_EQUAL(COVER_EMPTY, cover_cache_full(covers, RED_ALBUM, &image));
}

static void a_loaded_half_cover_has_its_pixels(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(cover_cache_load_half(covers, 3, BLUE_ALBUM));
    CHECK_EQUAL(COVER_LOADED, cover_cache_half(covers, 3, BLUE_ALBUM, &image));
    CHECK_EQUAL(COVER_HALF_SIZE, image.width);
    CHECK_EQUAL(BLUE, image.pixels[0]);
    CHECK(!cover_cache_load_half(covers, 3, BLUE_ALBUM));
}

static void a_loaded_full_cover_has_its_pixels(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(cover_cache_load_full(covers, RED_ALBUM));
    CHECK_EQUAL(COVER_LOADED, cover_cache_full(covers, RED_ALBUM, &image));
    CHECK_EQUAL(COVER_FULL_SIZE, image.height);
    CHECK_EQUAL(RED, image.pixels[COVER_FULL_PIXELS - 1]);
    CHECK_EQUAL(COVER_EMPTY, cover_cache_full(covers, BLUE_ALBUM, &image));
}

static void an_album_without_art_is_missing(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(cover_cache_load_half(covers, 1, BARE_ALBUM));
    CHECK_EQUAL(COVER_MISSING, cover_cache_half(covers, 1, BARE_ALBUM, &image));
}

static void a_position_reuses_the_slot_of_one_a_window_away(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    cover_cache_load_half(covers, 0, RED_ALBUM);
    cover_cache_load_half(covers, COVER_CACHE_SLOTS, BLUE_ALBUM);
    CHECK_EQUAL(COVER_EMPTY, cover_cache_half(covers, 0, RED_ALBUM, &image));
    CHECK_EQUAL(COVER_LOADED, cover_cache_half(covers, COVER_CACHE_SLOTS, BLUE_ALBUM, &image));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(without_covers_every_cover_is_missing),
        TEST_CASE(without_covers_loading_reads_nothing_from_the_library),
        TEST_CASE(an_album_has_a_cover_only_when_the_store_holds_one),
        TEST_CASE(an_unloaded_cover_is_empty),
        TEST_CASE(a_loaded_half_cover_has_its_pixels),
        TEST_CASE(a_loaded_full_cover_has_its_pixels),
        TEST_CASE(an_album_without_art_is_missing),
        TEST_CASE(a_position_reuses_the_slot_of_one_a_window_away),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
