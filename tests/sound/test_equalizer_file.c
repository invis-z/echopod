#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "sound/equalizer_file.h"

enum {
    PRESET_FIELD = 5,
    NAME_LENGTH_FIELD = 6,
    NAME_FIELD = 7,
    CHECKSUM_FIELD = 71
};

static EqualizerChoice file_choice(void)
{
    EqualizerChoice choice = {.preset = EQUALIZER_PRESET_FILE, .preset_name = "AutoEQ.txt"};
    return choice;
}

static void rewrite_checksum(uint8_t *bytes)
{
    uint32_t checksum = hash_update(HASH_START, bytes, CHECKSUM_FIELD);
    for (int index = 0; index < 4; index++) {
        bytes[CHECKSUM_FIELD + index] = (uint8_t)(checksum >> (8 * index));
    }
}

static void the_default_is_off(void)
{
    EqualizerChoice choice = equalizer_file_defaults();
    CHECK_EQUAL(EQUALIZER_PRESET_OFF, choice.preset);
    CHECK_EQUAL('\0', choice.preset_name[0]);
}

static void a_choice_survives_a_round_trip(void)
{
    EqualizerChoice saved = file_choice();
    uint8_t bytes[EQUALIZER_FILE_SIZE];
    equalizer_file_encode(&saved, bytes);
    CHECK_EQUAL(75, EQUALIZER_FILE_SIZE);
    CHECK_EQUAL(1, bytes[4]);
    CHECK_EQUAL(EQUALIZER_PRESET_FILE, bytes[PRESET_FIELD]);
    CHECK_EQUAL(10, bytes[NAME_LENGTH_FIELD]);
    CHECK(memcmp("AutoEQ.txt", bytes + NAME_FIELD, 10) == 0);
    EqualizerChoice loaded = equalizer_file_defaults();
    CHECK(equalizer_file_decode(bytes, sizeof bytes, &loaded));
    CHECK_EQUAL(EQUALIZER_PRESET_FILE, loaded.preset);
    CHECK(strcmp(saved.preset_name, loaded.preset_name) == 0);
}

static void a_corrupted_or_short_file_is_rejected(void)
{
    EqualizerChoice choice = file_choice();
    uint8_t bytes[EQUALIZER_FILE_SIZE];
    equalizer_file_encode(&choice, bytes);
    CHECK(!equalizer_file_decode(bytes, EQUALIZER_FILE_SIZE - 1, &choice));
    bytes[NAME_FIELD] ^= 1;
    CHECK(!equalizer_file_decode(bytes, sizeof bytes, &choice));
    uint8_t zeros[EQUALIZER_FILE_SIZE] = {0};
    CHECK(!equalizer_file_decode(zeros, sizeof zeros, &choice));
}

static void out_of_range_values_are_rejected(void)
{
    EqualizerChoice choice = file_choice();
    uint8_t bytes[EQUALIZER_FILE_SIZE];
    equalizer_file_encode(&choice, bytes);
    bytes[PRESET_FIELD] = EQUALIZER_PRESET_COUNT;
    rewrite_checksum(bytes);
    CHECK(!equalizer_file_decode(bytes, sizeof bytes, &choice));
    equalizer_file_encode(&choice, bytes);
    bytes[NAME_LENGTH_FIELD] = EQUALIZER_PRESET_NAME_CAPACITY;
    rewrite_checksum(bytes);
    CHECK(!equalizer_file_decode(bytes, sizeof bytes, &choice));
}

static void another_version_is_rejected(void)
{
    EqualizerChoice choice = file_choice();
    uint8_t bytes[EQUALIZER_FILE_SIZE];
    equalizer_file_encode(&choice, bytes);
    for (uint8_t version = 2; version <= 3; version++) {
        bytes[4] = version;
        rewrite_checksum(bytes);
        EqualizerChoice loaded = equalizer_file_defaults();
        CHECK(!equalizer_file_decode(bytes, sizeof bytes, &loaded));
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_default_is_off),
        TEST_CASE(a_choice_survives_a_round_trip),
        TEST_CASE(a_corrupted_or_short_file_is_rejected),
        TEST_CASE(out_of_range_values_are_rejected),
        TEST_CASE(another_version_is_rejected),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
