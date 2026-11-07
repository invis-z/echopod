#include "check.h"
#include "fake_card.h"
#include "fixture_library.h"
#include "library/covers.h"
#include "library/store_layout.h"

static FixtureLibrary fixture;
static FakeCard card;
static SegmentStore store;
static uint16_t pixels[COVER_FULL_PIXELS];

static const Library *with_fixture_covers(void)
{
    Library *library = fixture_library_open(&fixture);
    fake_card_init(&card, UINT64_MAX);
    segment_store_open(&store, fake_card_files(&card));
    CHECK(fixture_covers_write(&store, library));
    return library;
}

static uint32_t rank_of(const Library *library, uint32_t album)
{
    uint32_t rank = 0;
    CHECK(library_album_rank(library, album, &rank));
    return rank;
}

static bool all(const uint16_t *values, uint32_t count, uint16_t color)
{
    for (uint32_t index = 0; index < count; index++) {
        if (values[index] != color) {
            return false;
        }
    }
    return true;
}

static void covers_sit_in_whole_records_of_each_segment(void)
{
    CHECK_EQUAL(133, COVERS_PER_SEGMENT);
    CHECK_EQUAL(132 * COVER_RECORD_BYTES, covers_offset(132));
    CHECK_EQUAL(SEGMENT_COVERS_SIZE, covers_offset(133));
    CHECK_EQUAL(SEGMENT_COVERS_SIZE + COVER_RECORD_BYTES, covers_offset(134));
}

static void a_store_without_a_cover_table_has_no_covers(void)
{
    fake_card_init(&card, UINT64_MAX);
    segment_store_open(&store, fake_card_files(&card));
    Covers covers;
    CHECK(!covers_open(&covers, &store));
    fake_card_free(&card);
}

static void a_cover_reads_back_in_both_sizes(void)
{
    const Library *library = with_fixture_covers();
    Covers covers;
    CHECK(covers_open(&covers, &store));
    CHECK_EQUAL(library_count(library, LIBRARY_ALBUMS), covers.albums);
    CHECK_EQUAL(2, covers.slots);
    CHECK(covers_read(&covers, rank_of(library, FIXTURE_RED_ALBUM), COVER_FULL, pixels));
    CHECK(all(pixels, COVER_FULL_PIXELS, FIXTURE_RED));
    CHECK(covers_read(&covers, rank_of(library, FIXTURE_BLUE_ALBUM), COVER_HALF, pixels));
    CHECK(all(pixels, COVER_HALF_PIXELS, FIXTURE_BLUE));
    fake_card_free(&card);
}

static void an_album_without_art_has_no_cover(void)
{
    const Library *library = with_fixture_covers();
    Covers covers;
    CHECK(covers_open(&covers, &store));
    CHECK(!covers_read(&covers, rank_of(library, 1), COVER_FULL, pixels));
    CHECK(!covers_read(&covers, covers.albums, COVER_FULL, pixels));
    fake_card_free(&card);
}

static void a_slot_past_the_stored_covers_is_not_read(void)
{
    const Library *library = with_fixture_covers();
    segment_store_begin(&store);
    segment_store_set_value(&store, LIBRARY_VALUE_COVER_SLOTS, 1);
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    Covers covers;
    CHECK(covers_open(&covers, &store));
    uint32_t first = 0;
    CHECK(covers_slot(&covers, rank_of(library, FIXTURE_RED_ALBUM), &first) ||
          covers_slot(&covers, rank_of(library, FIXTURE_BLUE_ALBUM), &first));
    CHECK_EQUAL(0, first);
    CHECK(!covers_read(&covers, rank_of(library, FIXTURE_BLUE_ALBUM), COVER_FULL, pixels) ||
          !covers_read(&covers, rank_of(library, FIXTURE_RED_ALBUM), COVER_FULL, pixels));
    fake_card_free(&card);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(covers_sit_in_whole_records_of_each_segment),
        TEST_CASE(a_store_without_a_cover_table_has_no_covers),
        TEST_CASE(a_cover_reads_back_in_both_sizes),
        TEST_CASE(an_album_without_art_has_no_cover),
        TEST_CASE(a_slot_past_the_stored_covers_is_not_read),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
