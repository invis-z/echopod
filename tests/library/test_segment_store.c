#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fake_card.h"
#include "library/segment_store.h"

enum {
    RECORDS = 0,
    INDEX = 1,
    SCRATCH = 2,
    COVERS = 3,
    MEBIBYTE = 1024 * 1024,
    GIGABYTE = 1024 * MEBIBYTE,
    CONTENT_CAPACITY = 4 * MEBIBYTE,
    TORN_SIZES = 3
};

typedef struct Content {
    uint8_t *bytes[SEGMENT_STREAM_COUNT];
    uint32_t length[SEGMENT_STREAM_COUNT];
    SegmentRegion region;
    uint32_t value;
} Content;

static FakeCard card;
static FakeCard saved;
static SegmentStore store;
static SegmentStore reopened;

static uint8_t pattern(uint32_t seed, uint32_t index)
{
    uint32_t mixed = seed * 2654435761u + index * 40503u + (index >> 11);
    return (uint8_t)(mixed >> 13);
}

static void content_init(Content *content)
{
    memset(content, 0, sizeof *content);
    for (int stream = 0; stream < SEGMENT_STREAM_COUNT; stream++) {
        content->bytes[stream] = malloc(CONTENT_CAPACITY);
    }
}

static void content_free(Content *content)
{
    for (int stream = 0; stream < SEGMENT_STREAM_COUNT; stream++) {
        free(content->bytes[stream]);
    }
}

static void content_copy(Content *to, const Content *from)
{
    for (int stream = 0; stream < SEGMENT_STREAM_COUNT; stream++) {
        memcpy(to->bytes[stream], from->bytes[stream], from->length[stream]);
        to->length[stream] = from->length[stream];
    }
    to->region = from->region;
    to->value = from->value;
}

static SegmentResult append_pattern(Content *content, uint8_t stream, uint32_t seed,
                                    uint32_t length)
{
    uint8_t *to = content->bytes[stream] + content->length[stream];
    for (uint32_t index = 0; index < length; index++) {
        to[index] = pattern(seed, index);
    }
    content->length[stream] += length;
    return segment_store_append(&store, stream, to, length);
}

static bool store_matches(const SegmentStore *opened, const Content *content)
{
    static uint8_t buffer[CONTENT_CAPACITY];
    for (uint8_t stream = 0; stream < SEGMENT_STREAM_COUNT; stream++) {
        uint32_t length = content->length[stream];
        if (segment_store_length(opened, stream) != length) {
            return false;
        }
        if (length > 0 && (!segment_store_read(opened, stream, 0, buffer, length) ||
                           memcmp(buffer, content->bytes[stream], length) != 0)) {
            return false;
        }
    }
    SegmentRegion region = segment_store_region(opened, 0);
    return region.stream == content->region.stream && region.offset == content->region.offset &&
           region.length == content->region.length &&
           segment_store_value(opened, 0) == content->value;
}

static void reopen(void)
{
    segment_store_open(&reopened, fake_card_files(&card));
}

static void fresh_card(uint64_t free_bytes)
{
    fake_card_free(&card);
    fake_card_init(&card, free_bytes);
    segment_store_open(&store, fake_card_files(&card));
}

static void an_empty_card_has_no_library(void)
{
    fresh_card(GIGABYTE);
    CHECK(!segment_store_found(&store));
    CHECK_EQUAL(0, segment_store_length(&store, RECORDS));
    CHECK_EQUAL(0, card.reserves);
}

static void a_commit_survives_reopening(void)
{
    Content content;
    content_init(&content);
    fresh_card(GIGABYTE);
    segment_store_begin(&store);
    segment_store_start(&store, RECORDS, SEGMENT_POOL_LIBRARY);
    CHECK_EQUAL(SEGMENT_OK, append_pattern(&content, RECORDS, 1, 5000));
    content.region = (SegmentRegion){.stream = RECORDS, .offset = 100, .length = 200};
    content.value = 77;
    segment_store_set_region(&store, 0, content.region);
    segment_store_set_value(&store, 0, content.value);
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    reopen();
    CHECK(segment_store_found(&reopened));
    CHECK(store_matches(&reopened, &content));
    CHECK(fake_card_find(&card, "/.rippl/library/L0000.BIN") != NULL);
    CHECK(fake_card_find(&card, SEGMENT_MANIFEST_PATH) != NULL);
    content_free(&content);
}

static void streams_span_segments(void)
{
    Content content;
    content_init(&content);
    fresh_card(GIGABYTE);
    segment_store_begin(&store);
    segment_store_start(&store, RECORDS, SEGMENT_POOL_LIBRARY);
    CHECK_EQUAL(SEGMENT_OK, append_pattern(&content, RECORDS, 2, MEBIBYTE - 10));
    CHECK_EQUAL(SEGMENT_OK, append_pattern(&content, RECORDS, 3, MEBIBYTE + 20));
    CHECK_EQUAL(SEGMENT_OK, append_pattern(&content, RECORDS, 4, MEBIBYTE / 2));
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    CHECK_EQUAL(3, store.committed.streams[RECORDS].count);
    reopen();
    CHECK(store_matches(&reopened, &content));
    uint8_t across[32];
    CHECK(segment_store_read(&reopened, RECORDS, MEBIBYTE - 16, across, sizeof across));
    CHECK(memcmp(across, content.bytes[RECORDS] + MEBIBYTE - 16, sizeof across) == 0);
    CHECK(!segment_store_read(&reopened, RECORDS, content.length[RECORDS] - 4, across, 8));
    content_free(&content);
}

static void uncommitted_work_is_not_seen(void)
{
    Content content;
    content_init(&content);
    fresh_card(GIGABYTE);
    segment_store_begin(&store);
    segment_store_start(&store, RECORDS, SEGMENT_POOL_LIBRARY);
    CHECK_EQUAL(SEGMENT_OK, append_pattern(&content, RECORDS, 5, 3000));
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    Content extra;
    content_init(&extra);
    content_copy(&extra, &content);
    segment_store_begin(&store);
    CHECK_EQUAL(SEGMENT_OK, append_pattern(&extra, RECORDS, 6, 4000));
    CHECK_EQUAL(7000, segment_store_working_length(&store, RECORDS));
    CHECK_EQUAL(3000, segment_store_length(&store, RECORDS));
    reopen();
    CHECK(store_matches(&reopened, &content));
    segment_store_cancel(&store);
    CHECK(store_matches(&store, &content));
    CHECK_EQUAL(3000, segment_store_working_length(&store, RECORDS));
    content_free(&extra);
    content_free(&content);
}

static void rebuilt_streams_reuse_freed_segments(void)
{
    Content content;
    content_init(&content);
    fresh_card(GIGABYTE);
    for (uint32_t round = 0; round < 10; round++) {
        segment_store_begin(&store);
        segment_store_start(&store, INDEX, SEGMENT_POOL_LIBRARY);
        content.length[INDEX] = 0;
        CHECK_EQUAL(SEGMENT_OK, append_pattern(&content, INDEX, round, MEBIBYTE + MEBIBYTE / 2));
        segment_store_start(&store, SCRATCH, SEGMENT_POOL_LIBRARY);
        CHECK_EQUAL(SEGMENT_OK,
                    segment_store_append(&store, SCRATCH, content.bytes[INDEX], MEBIBYTE + 1));
        segment_store_drop(&store, SCRATCH);
        CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    }
    CHECK(store.committed.reserved[SEGMENT_POOL_LIBRARY] <= 6);
    reopen();
    CHECK(store_matches(&reopened, &content));
    content_free(&content);
}

static void committed_bytes_cannot_be_overwritten(void)
{
    uint8_t bytes[16] = {1, 2, 3};
    fresh_card(GIGABYTE);
    segment_store_begin(&store);
    segment_store_start(&store, RECORDS, SEGMENT_POOL_LIBRARY);
    CHECK_EQUAL(SEGMENT_OK, segment_store_append(&store, RECORDS, bytes, sizeof bytes));
    CHECK_EQUAL(SEGMENT_OK, segment_store_write(&store, RECORDS, 4, bytes, 4));
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    segment_store_begin(&store);
    CHECK_EQUAL(SEGMENT_OK, segment_store_append(&store, RECORDS, bytes, sizeof bytes));
    CHECK_EQUAL(SEGMENT_REFUSED, segment_store_write(&store, RECORDS, 4, bytes, 4));
    CHECK_EQUAL(SEGMENT_REFUSED, segment_store_write(&store, RECORDS, 14, bytes, 4));
    CHECK_EQUAL(SEGMENT_OK, segment_store_write(&store, RECORDS, 16, bytes, 4));
    CHECK_EQUAL(SEGMENT_REFUSED, segment_store_write(&store, RECORDS, 30, bytes, 4));
    segment_store_start(&store, RECORDS, SEGMENT_POOL_LIBRARY);
    CHECK_EQUAL(SEGMENT_OK, segment_store_append(&store, RECORDS, bytes, sizeof bytes));
    CHECK_EQUAL(SEGMENT_OK, segment_store_write(&store, RECORDS, 4, bytes, 4));
}

static void a_full_card_keeps_the_old_library(void)
{
    Content content;
    content_init(&content);
    fresh_card(2 * MEBIBYTE + SEGMENT_MANIFEST_SIZE);
    segment_store_begin(&store);
    segment_store_start(&store, RECORDS, SEGMENT_POOL_LIBRARY);
    CHECK_EQUAL(SEGMENT_OK, append_pattern(&content, RECORDS, 7, 1000));
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    segment_store_begin(&store);
    segment_store_start(&store, INDEX, SEGMENT_POOL_LIBRARY);
    uint8_t *big = malloc(2 * MEBIBYTE);
    memset(big, 9, 2 * MEBIBYTE);
    CHECK_EQUAL(SEGMENT_NO_SPACE, segment_store_append(&store, INDEX, big, 2 * MEBIBYTE));
    segment_store_cancel(&store);
    reopen();
    CHECK(store_matches(&reopened, &content));
    free(big);
    content_free(&content);
}

static void a_pool_has_a_fixed_capacity(void)
{
    fresh_card(UINT64_MAX);
    segment_store_begin(&store);
    segment_store_start(&store, COVERS, SEGMENT_POOL_COVERS);
    static uint8_t chunk[1];
    for (int segment = 0; segment < SEGMENT_COVERS_CAPACITY; segment++) {
        CHECK_EQUAL(SEGMENT_OK, segment_store_append(&store, COVERS, chunk, 1));
        store.working.streams[COVERS].length = (uint32_t)(segment + 1) * SEGMENT_COVERS_SIZE;
    }
    CHECK_EQUAL(SEGMENT_FULL, segment_store_append(&store, COVERS, chunk, 1));
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    reopen();
    CHECK_EQUAL((uint32_t)SEGMENT_COVERS_CAPACITY * SEGMENT_COVERS_SIZE,
                segment_store_length(&reopened, COVERS));
}

static void build_generation_a(Content *content)
{
    segment_store_begin(&store);
    segment_store_start(&store, RECORDS, SEGMENT_POOL_LIBRARY);
    segment_store_start(&store, INDEX, SEGMENT_POOL_LIBRARY);
    segment_store_start(&store, COVERS, SEGMENT_POOL_COVERS);
    append_pattern(content, RECORDS, 10, MEBIBYTE + 3000);
    append_pattern(content, INDEX, 11, 70000);
    append_pattern(content, COVERS, 12, 31360 * 3);
    content->region = (SegmentRegion){.stream = INDEX, .offset = 0, .length = 70000};
    content->value = 1;
    segment_store_set_region(&store, 0, content->region);
    segment_store_set_value(&store, 0, content->value);
    segment_store_commit(&store);
}

static bool build_generation(Content *content, uint32_t seed)
{
    segment_store_begin(&store);
    bool ok = append_pattern(content, RECORDS, seed, 400000) == SEGMENT_OK;
    segment_store_start(&store, SCRATCH, SEGMENT_POOL_LIBRARY);
    ok = ok && append_pattern(content, SCRATCH, seed + 1, MEBIBYTE + 5) == SEGMENT_OK;
    segment_store_start(&store, INDEX, SEGMENT_POOL_LIBRARY);
    content->length[INDEX] = 0;
    ok = ok && append_pattern(content, INDEX, seed + 2, MEBIBYTE + MEBIBYTE / 4) == SEGMENT_OK;
    ok = ok && append_pattern(content, COVERS, seed + 3, 31360) == SEGMENT_OK;
    segment_store_drop(&store, SCRATCH);
    content->length[SCRATCH] = 0;
    content->region = (SegmentRegion){.stream = INDEX, .offset = 10, .length = 5000};
    content->value = seed;
    segment_store_set_region(&store, 0, content->region);
    segment_store_set_value(&store, 0, content->value);
    return ok && segment_store_commit(&store) == SEGMENT_OK;
}

static void power_loss_leaves_the_old_or_the_new_library(void)
{
    static const uint32_t torn[TORN_SIZES] = {0, 100, 700};
    Content first;
    Content second;
    Content third;
    content_init(&first);
    content_init(&second);
    content_init(&third);
    fresh_card(GIGABYTE);
    build_generation_a(&first);
    fake_card_copy(&saved, &card);
    int cuts = 0;
    int finished = 0;
    for (int cut = 0; finished < TORN_SIZES; cut++) {
        for (int size = 0; size < TORN_SIZES; size++) {
            fake_card_free(&card);
            fake_card_copy(&card, &saved);
            segment_store_open(&store, fake_card_files(&card));
            content_copy(&second, &first);
            fake_card_cut(&card, cut, torn[size]);
            bool committed = build_generation(&second, 20);
            bool was_cut = card.dead;
            fake_card_restore_power(&card);
            if (!was_cut) {
                CHECK(committed);
                finished++;
            }
            cuts++;
            segment_store_open(&store, fake_card_files(&card));
            bool old = store_matches(&store, &first);
            bool new = store_matches(&store, &second);
            CHECK(old || new);
            CHECK(!committed || new);
            const Content *now = new ? &second : &first;
            content_copy(&third, now);
            CHECK(build_generation(&third, 40));
            reopen();
            CHECK(store_matches(&reopened, &third));
        }
    }
    CHECK(cuts > 30);
    content_free(&third);
    content_free(&second);
    content_free(&first);
    fake_card_free(&saved);
}

static void a_torn_manifest_falls_back_to_the_other_copy(void)
{
    Content content;
    content_init(&content);
    fresh_card(GIGABYTE);
    build_generation_a(&content);
    Content later;
    content_init(&later);
    content_copy(&later, &content);
    CHECK(build_generation(&later, 50));
    uint8_t junk[64];
    memset(junk, 0x5A, sizeof junk);
    SegmentFiles files = fake_card_files(&card);
    files.write(files.context, SEGMENT_MANIFEST_PATH,
                store.slot * (uint32_t)SEGMENT_MANIFEST_SLOT_SIZE + 600u, junk, sizeof junk);
    reopen();
    CHECK(store_matches(&reopened, &content));
    files.write(files.context, SEGMENT_MANIFEST_PATH,
                (store.slot ^ 1u) * SEGMENT_MANIFEST_SLOT_SIZE + 8, junk, sizeof junk);
    reopen();
    CHECK(!segment_store_found(&reopened));
    CHECK_EQUAL(0, segment_store_length(&reopened, RECORDS));
    content_free(&later);
    content_free(&content);
}

static void the_newer_copy_wins_across_wraparound(void)
{
    fresh_card(GIGABYTE);
    store.committed.sequence = UINT32_MAX - 1;
    segment_store_begin(&store);
    segment_store_set_value(&store, 0, 1);
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    segment_store_begin(&store);
    segment_store_set_value(&store, 0, 2);
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    CHECK_EQUAL(0, segment_store_sequence(&store));
    reopen();
    CHECK_EQUAL(0, segment_store_sequence(&reopened));
    CHECK_EQUAL(2, segment_store_value(&reopened, 0));
}

static void regions_must_fit_their_stream(void)
{
    fresh_card(GIGABYTE);
    segment_store_begin(&store);
    segment_store_start(&store, RECORDS, SEGMENT_POOL_LIBRARY);
    uint8_t bytes[10] = {0};
    segment_store_append(&store, RECORDS, bytes, sizeof bytes);
    segment_store_set_region(&store, 1,
                             (SegmentRegion){.stream = RECORDS, .offset = 5, .length = 6});
    CHECK_EQUAL(SEGMENT_REFUSED, segment_store_commit(&store));
    segment_store_set_region(&store, 1,
                             (SegmentRegion){.stream = RECORDS, .offset = 5, .length = 5});
    CHECK_EQUAL(SEGMENT_OK, segment_store_commit(&store));
    CHECK_EQUAL(1, segment_store_sequence(&store));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_empty_card_has_no_library),
        TEST_CASE(a_commit_survives_reopening),
        TEST_CASE(streams_span_segments),
        TEST_CASE(uncommitted_work_is_not_seen),
        TEST_CASE(rebuilt_streams_reuse_freed_segments),
        TEST_CASE(committed_bytes_cannot_be_overwritten),
        TEST_CASE(a_full_card_keeps_the_old_library),
        TEST_CASE(a_pool_has_a_fixed_capacity),
        TEST_CASE(power_loss_leaves_the_old_or_the_new_library),
        TEST_CASE(a_torn_manifest_falls_back_to_the_other_copy),
        TEST_CASE(the_newer_copy_wins_across_wraparound),
        TEST_CASE(regions_must_fit_their_stream),
    };
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    fake_card_free(&card);
    return result;
}
