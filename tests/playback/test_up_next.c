#include "check.h"
#include "core/hash.h"
#include "fixture_library.h"
#include "playback/up_next.h"

static UpNext with_tracks(const uint32_t *tracks, int count)
{
    UpNext up_next;
    up_next_clear(&up_next);
    for (int index = 0; index < count; index++) {
        CHECK(up_next_add(&up_next, tracks[index]));
    }
    return up_next;
}

static void a_new_list_is_empty(void)
{
    UpNext up_next;
    up_next_clear(&up_next);
    uint32_t track = 0;
    CHECK_EQUAL(0, up_next.count);
    CHECK(!up_next_first(&up_next, &track));
}

static void added_songs_keep_their_order(void)
{
    static const uint32_t TRACKS[] = {7, 3, 9};
    UpNext up_next = with_tracks(TRACKS, 3);
    CHECK_EQUAL(3, up_next.count);
    CHECK_EQUAL(7, up_next.tracks[0]);
    CHECK_EQUAL(3, up_next.tracks[1]);
    CHECK_EQUAL(9, up_next.tracks[2]);
}

static void play_next_goes_to_the_front(void)
{
    static const uint32_t TRACKS[] = {7, 3};
    UpNext up_next = with_tracks(TRACKS, 2);
    CHECK(up_next_play_next(&up_next, 11));
    uint32_t track = 0;
    CHECK(up_next_first(&up_next, &track));
    CHECK_EQUAL(11, track);
    CHECK_EQUAL(3, up_next.count);
    CHECK_EQUAL(7, up_next.tracks[1]);
    CHECK_EQUAL(3, up_next.tracks[2]);
}

static void a_full_list_refuses_more(void)
{
    UpNext up_next;
    up_next_clear(&up_next);
    for (int index = 0; index < UP_NEXT_CAPACITY; index++) {
        CHECK(up_next_add(&up_next, (uint32_t)index));
    }
    CHECK(!up_next_add(&up_next, 500));
    CHECK(!up_next_play_next(&up_next, 500));
    CHECK_EQUAL(UP_NEXT_CAPACITY, up_next.count);
    CHECK_EQUAL(UP_NEXT_CAPACITY - 1, up_next.tracks[UP_NEXT_CAPACITY - 1]);
}

static void dropping_removes_from_the_front(void)
{
    static const uint32_t TRACKS[] = {7, 3, 9, 4};
    UpNext up_next = with_tracks(TRACKS, 4);
    up_next_drop(&up_next, 2);
    CHECK_EQUAL(2, up_next.count);
    CHECK_EQUAL(9, up_next.tracks[0]);
    CHECK_EQUAL(4, up_next.tracks[1]);
    up_next_drop(&up_next, 5);
    CHECK_EQUAL(0, up_next.count);
}

static FixtureLibrary fixture;

static void rewrite_checksum(uint8_t *bytes)
{
    uint32_t checksum = hash_update(HASH_START, bytes, UP_NEXT_STATE_SIZE - UP_NEXT_CHECKSUM_SIZE);
    for (int index = 0; index < 4; index++) {
        bytes[UP_NEXT_STATE_SIZE - UP_NEXT_CHECKSUM_SIZE + index] =
            (uint8_t)(checksum >> (8 * index));
    }
}

static void a_list_survives_a_round_trip(void)
{
    const Library *library = fixture_library_open(&fixture);
    static const uint32_t TRACKS[] = {7, 3, 0, 3};
    UpNext saved = with_tracks(TRACKS, 4);
    uint8_t bytes[UP_NEXT_STATE_SIZE];
    up_next_encode(&saved, library, bytes);
    UpNext loaded;
    CHECK(up_next_decode(bytes, sizeof bytes, library, &loaded));
    CHECK_EQUAL(4, loaded.count);
    CHECK_EQUAL(7, loaded.tracks[0]);
    CHECK_EQUAL(3, loaded.tracks[1]);
    CHECK_EQUAL(0, loaded.tracks[2]);
    CHECK_EQUAL(3, loaded.tracks[3]);
}

static void songs_that_left_the_library_are_skipped(void)
{
    const Library *library = fixture_library_open(&fixture);
    static const uint32_t TRACKS[] = {7, 1200, 3, 5};
    UpNext saved = with_tracks(TRACKS, 4);
    uint8_t bytes[UP_NEXT_STATE_SIZE];
    up_next_encode(&saved, library, bytes);
    bytes[UP_NEXT_HEADER_SIZE + UP_NEXT_ID_SIZE] ^= 1;
    rewrite_checksum(bytes);
    UpNext loaded;
    CHECK(up_next_decode(bytes, sizeof bytes, library, &loaded));
    CHECK_EQUAL(2, loaded.count);
    CHECK_EQUAL(7, loaded.tracks[0]);
    CHECK_EQUAL(5, loaded.tracks[1]);
}

static void a_corrupted_or_blank_file_is_rejected(void)
{
    const Library *library = fixture_library_open(&fixture);
    static const uint32_t TRACKS[] = {7, 3};
    UpNext saved = with_tracks(TRACKS, 2);
    uint8_t bytes[UP_NEXT_STATE_SIZE];
    up_next_encode(&saved, library, bytes);
    bytes[12] ^= 1;
    UpNext loaded;
    CHECK(!up_next_decode(bytes, sizeof bytes, library, &loaded));
    uint8_t blank[UP_NEXT_STATE_SIZE] = {0};
    CHECK(!up_next_decode(blank, sizeof blank, library, &loaded));
    CHECK(!up_next_decode(bytes, UP_NEXT_STATE_SIZE - 1, library, &loaded));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_new_list_is_empty),
        TEST_CASE(added_songs_keep_their_order),
        TEST_CASE(play_next_goes_to_the_front),
        TEST_CASE(a_full_list_refuses_more),
        TEST_CASE(dropping_removes_from_the_front),
        TEST_CASE(a_list_survives_a_round_trip),
        TEST_CASE(songs_that_left_the_library_are_skipped),
        TEST_CASE(a_corrupted_or_blank_file_is_rejected),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
