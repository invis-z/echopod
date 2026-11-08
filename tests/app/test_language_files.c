#include "check.h"
#include "core/i18n.h"
#include "fake_hal.h"
#include "hal/language.h"
#include "language_files.h"

#include <string.h>

enum {
    TEXT_BASE = I18N_HEADER_BYTES + STRING_COUNT * 2,
    PACK_CAPACITY = 0x4000,
    FUZZ_ROUNDS = 3000
};

static const char GERMAN_PATH[] = "/:internal/Languages/de.bin";
static uint8_t pack[PACK_CAPACITY + 64];

static void put_u16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static uint32_t make_pack(const char *text)
{
    memset(pack, 0, sizeof pack);
    memcpy(pack, "ELNG", 4);
    put_u16(pack + 4, I18N_VERSION);
    put_u16(pack + 6, STRING_COUNT);
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        put_u16(pack + I18N_HEADER_BYTES + id * 2, 0);
    }
    size_t length = strlen(text) + 1;
    memcpy(pack + TEXT_BASE, text, length);
    uint32_t size = TEXT_BASE + (uint32_t)length;
    put_u16(pack + 8, size);
    put_u16(pack + 10, size >> 16);
    return size;
}

static void install(uint32_t size)
{
    fake_hal_reset();
    i18n_detach();
    fake_hal_add_file(GERMAN_PATH, pack, size);
}

static void a_valid_pack_on_the_internal_drive_is_listed_and_used(void)
{
    install(make_pack("Sprache"));
    CHECK(language_files_available(LANGUAGE_GERMAN));
    CHECK(language_files_attach(LANGUAGE_GERMAN));
    CHECK(strcmp("Sprache", i18n_text(STR_LANGUAGE_TITLE)) == 0);
    i18n_detach();
}

static void english_and_chinese_need_no_file(void)
{
    fake_hal_reset();
    CHECK(language_files_available(LANGUAGE_ENGLISH));
    CHECK(language_files_available(LANGUAGE_CHINESE));
    CHECK(!language_files_available(LANGUAGE_GERMAN));
    CHECK(!language_files_attach(LANGUAGE_GERMAN));
    CHECK(!language_files_attach(LANGUAGE_CHINESE));
}

static void the_path_is_the_languages_folder_and_the_code(void)
{
    char path[LANGUAGE_FILE_PATH_CAPACITY];
    CHECK(language_files_path(LANGUAGE_UKRAINIAN, path, sizeof path));
    CHECK(strcmp("/:internal/Languages/uk.bin", path) == 0);
    CHECK(!language_files_path((Language)LANGUAGE_COUNT, path, sizeof path));
    CHECK(!language_files_path(LANGUAGE_GERMAN, path, 10));
}

static void refused(uint32_t size)
{
    install(size);
    CHECK(!language_files_attach(LANGUAGE_GERMAN));
    CHECK(strcmp(i18n_english(STR_LANGUAGE_TITLE), i18n_text(STR_LANGUAGE_TITLE)) == 0);
}

static void broken_headers_are_refused(void)
{
    uint32_t size = make_pack("x");
    pack[0] = 'X';
    refused(size);
    size = make_pack("x");
    put_u16(pack + 4, I18N_VERSION + 1);
    refused(size);
    size = make_pack("x");
    put_u16(pack + 6, STRING_COUNT - 1);
    refused(size);
    size = make_pack("x");
    put_u16(pack + 8, size + 1);
    refused(size);
    make_pack("x");
    refused(I18N_HEADER_BYTES - 1);
    make_pack("x");
    refused(0);
}

static void a_pack_too_big_for_the_buffer_is_refused_not_cut(void)
{
    make_pack("x");
    refused(PACK_CAPACITY + 1);
    install(make_pack("x"));
    CHECK(language_files_attach(LANGUAGE_GERMAN));
    i18n_detach();
}

static void offsets_past_the_end_and_unended_strings_are_refused(void)
{
    uint32_t size = make_pack("x");
    put_u16(pack + I18N_HEADER_BYTES + 7 * 2, size);
    refused(size);
    size = make_pack("abc");
    pack[size - 1] = 'd';
    refused(size);
    size = make_pack("abc");
    refused(size - 1);
}

static void a_failed_load_never_leaves_the_old_text_pointing_at_the_buffer(void)
{
    install(make_pack("Sprache"));
    CHECK(language_files_attach(LANGUAGE_GERMAN));
    uint32_t capacity = 0;
    uint8_t *buffer = hal_language_buffer(&capacity);
    CHECK(buffer != NULL && capacity == PACK_CAPACITY);
    uint32_t size = make_pack("x");
    pack[size - 1] = 'y';
    fake_hal_reset();
    fake_hal_add_file(GERMAN_PATH, pack, size);
    CHECK(!language_files_attach(LANGUAGE_GERMAN));
    CHECK(strcmp(i18n_english(STR_LANGUAGE_TITLE), i18n_text(STR_LANGUAGE_TITLE)) == 0);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static void random_packs_never_read_outside_the_file(void)
{
    uint32_t state = 12345;
    int attached = 0;
    for (int round = 0; round < FUZZ_ROUNDS; round++) {
        uint32_t size = make_pack("seed");
        uint32_t length = TEXT_BASE + next_random(&state) % (PACK_CAPACITY - TEXT_BASE + 32);
        for (uint32_t at = round % 3 == 0 ? 0 : I18N_HEADER_BYTES; at < length; at++) {
            pack[at] = (uint8_t)next_random(&state);
        }
        if (round % 2 == 0) {
            put_u16(pack + 8, length);
            put_u16(pack + 10, 0);
            for (uint32_t id = 0; id < STRING_COUNT; id++) {
                put_u16(pack + I18N_HEADER_BYTES + id * 2,
                        next_random(&state) % (length - TEXT_BASE));
            }
        }
        (void)size;
        install(length);
        if (language_files_attach(LANGUAGE_GERMAN)) {
            attached++;
            for (uint32_t id = 0; id < STRING_COUNT; id++) {
                const char *text = i18n_text((StringId)id);
                const char *start = (const char *)hal_language_buffer(&(uint32_t){0});
                if (text >= start && text < start + PACK_CAPACITY) {
                    CHECK((size_t)(text - start) + strlen(text) < length);
                }
            }
        }
        (void)language_files_available(LANGUAGE_GERMAN);
    }
    CHECK(attached > 0);
    i18n_detach();
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_valid_pack_on_the_internal_drive_is_listed_and_used),
        TEST_CASE(english_and_chinese_need_no_file),
        TEST_CASE(the_path_is_the_languages_folder_and_the_code),
        TEST_CASE(broken_headers_are_refused),
        TEST_CASE(a_pack_too_big_for_the_buffer_is_refused_not_cut),
        TEST_CASE(offsets_past_the_end_and_unended_strings_are_refused),
        TEST_CASE(a_failed_load_never_leaves_the_old_text_pointing_at_the_buffer),
        TEST_CASE(random_packs_never_read_outside_the_file),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
