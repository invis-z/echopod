#include <stdio.h>
#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "equalizer_store.h"
#include "fake_hal.h"
#include "screens/equalizer_screen.h"

enum {
    START_MS = 1000
};

static EqualizerStore store;
static Toast toast;
static EqualizerScreen equalizer;

static ScreenAction press(Screen *screen, Key key, KeyGesture gesture)
{
    return screen->handle_key(screen->context, (KeyEvent){.key = key, .gesture = gesture},
                              START_MS);
}

static ScreenAction tap(Screen *screen, Key key)
{
    return press(screen, key, KEY_GESTURE_PRESS);
}

static EqualizerChoice saved_choice(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes(EQUALIZER_STORE_PATH, &size);
    EqualizerChoice choice = equalizer_file_defaults();
    CHECK(bytes != NULL);
    CHECK(bytes != NULL && equalizer_file_decode(bytes, size, &choice));
    return choice;
}

static const char PRESET_A[] = "Preamp: -1 dB\n"
                               "Filter 1: ON PK Fc 1000 Hz Gain 2 dB Q 1.5\n";
static const char PRESET_B[] = "Preamp: -2 dB\n"
                               "Filter 1: ON PK Fc 2000 Hz Gain -3 dB Q 0.8\n";
static const char PRESET_BAD[] = "Filter 1: ON PK Fc 100 Hz Gain 13 dB Q 0.7\n";
static const char PRESET_SKIPPING[] = "Preamp: -2 dB\n"
                                      "Filter 1: ON LP Fc 18000 Hz Q 0.7\n"
                                      "Filter 2: ON PK Fc 1000 Hz Gain -2 dB Q 1\n"
                                      "Filter 3: ON HP Fc 20 Hz\n";

static void add_preset(const char *name, const char *text)
{
    char path[64];
    snprintf(path, sizeof path, "/EQ/%s", name);
    fake_hal_add_file(path, (const uint8_t *)text, (uint32_t)strlen(text));
}

static Screen *open_files(void)
{
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "A.txt", false, false);
    add_preset("A.txt", PRESET_A);
    equalizer_store_load(&store);
    toast = (Toast){0};
    Screen *picker = equalizer_screen_init(
        &equalizer, (EqualizerConfig){.store = &store, .toast = &toast, .theme = &DEFAULT_THEME});
    equalizer_screen_open(&equalizer);
    picker->enter(picker->context, START_MS);
    for (int step = 0; step < EQUALIZER_PRESET_FILE; step++) {
        tap(picker, KEY_VOLUME_UP);
    }
    return picker;
}

static void the_file_list_lists_txt_presets(void)
{
    open_files();
    CHECK_EQUAL(1, equalizer.files.count);
    CHECK(strcmp("A", equalizer.model.label(&equalizer, EQUALIZER_PRESET_FILE)) == 0);
    CHECK_EQUAL(EQUALIZER_PRESET_FILE, equalizer.list.selected);
}

static void the_settings_summary_uses_seven_characters_and_the_list_the_whole_name(void)
{
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "LongPresetName.txt", false, false);
    add_preset("LongPresetName.txt", PRESET_A);
    equalizer_store_load(&store);
    toast = (Toast){0};
    equalizer_screen_init(
        &equalizer, (EqualizerConfig){.store = &store, .toast = &toast, .theme = &DEFAULT_THEME});
    equalizer_screen_open(&equalizer);
    EqualizerTextError error;
    CHECK(equalizer_store_select_file(&store, "LongPresetName.txt", &error));
    CHECK_EQUAL(1, equalizer.files.count);
    CHECK(strcmp("LongPre", equalizer_screen_summary(&equalizer)) == 0);
    CHECK(strcmp("LongPresetName", equalizer.model.label(&equalizer, EQUALIZER_PRESET_FILE)) == 0);
    CHECK(equalizer.model.detail(&equalizer, EQUALIZER_PRESET_FILE) != NULL);
}

static void opening_the_picker_finds_files_added_since_the_last_time(void)
{
    Screen *picker = open_files();
    tap(picker, KEY_BACK);
    fake_hal_add_directory_entry("/EQ", "B.txt", false, false);
    add_preset("B.txt", PRESET_B);
    equalizer_screen_open(&equalizer);
    picker->enter(picker->context, START_MS);
    CHECK_EQUAL(2, equalizer.files.count);
    CHECK(strcmp("B", equalizer.model.label(&equalizer, EQUALIZER_PRESET_FILE + 1)) == 0);
}

static void a_rejected_file_says_why(void)
{
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "Bad.txt", false, false);
    add_preset("Bad.txt", PRESET_BAD);
    equalizer_store_load(&store);
    toast = (Toast){0};
    Screen *picker = equalizer_screen_init(
        &equalizer, (EqualizerConfig){.store = &store, .toast = &toast, .theme = &DEFAULT_THEME});
    equalizer_screen_open(&equalizer);
    picker->enter(picker->context, START_MS);
    for (int step = 0; step < EQUALIZER_PRESET_FILE; step++) {
        tap(picker, KEY_VOLUME_UP);
    }
    CHECK(toast.visible);
    CHECK(strcmp("Line 1: gain over 12 dB", equalizer.notice) == 0);
}

static void entering_the_picker_does_not_rescan_the_card(void)
{
    Screen *picker = open_files();
    int opens = fake_hal_dir_open_count();
    picker->enter(picker->context, START_MS);
    CHECK_EQUAL(opens, fake_hal_dir_open_count());
}

static void choosing_a_valid_file_applies_and_saves_it(void)
{
    Screen *picker = open_files();
    CHECK_EQUAL(SCREEN_ACTION_POP, tap(picker, KEY_PLAY).type);
    CHECK_EQUAL(EQUALIZER_PRESET_FILE, store.choice.preset);
    CHECK(strcmp("A.txt", store.choice.preset_name) == 0);
    CHECK_EQUAL(1, fake_hal_equalizer().band_count);
    CHECK_EQUAL(1000, fake_hal_equalizer().bands[0].frequency_hz);
    CHECK_EQUAL(20, fake_hal_equalizer().bands[0].gain_tenths);
}

static void highlighting_a_file_previews_it(void)
{
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "A.txt", false, false);
    fake_hal_add_directory_entry("/EQ", "B.txt", false, false);
    add_preset("A.txt", PRESET_A);
    add_preset("B.txt", PRESET_B);
    equalizer_store_load(&store);
    toast = (Toast){0};
    Screen *picker = equalizer_screen_init(
        &equalizer, (EqualizerConfig){.store = &store, .toast = &toast, .theme = &DEFAULT_THEME});
    equalizer_screen_open(&equalizer);
    picker->enter(picker->context, START_MS);
    for (int step = 0; step < EQUALIZER_PRESET_FILE; step++) {
        tap(picker, KEY_VOLUME_UP);
    }
    tap(picker, KEY_VOLUME_UP);
    CHECK_EQUAL(EQUALIZER_PRESET_FILE, store.choice.preset);
    CHECK(strcmp("B.txt", store.choice.preset_name) == 0);
    CHECK_EQUAL(2000, fake_hal_equalizer().bands[0].frequency_hz);
}

static void an_invalid_file_is_not_applied_or_saved(void)
{
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "Bad.txt", false, false);
    add_preset("Bad.txt", PRESET_BAD);
    equalizer_store_load(&store);
    toast = (Toast){0};
    Screen *picker = equalizer_screen_init(
        &equalizer, (EqualizerConfig){.store = &store, .toast = &toast, .theme = &DEFAULT_THEME});
    equalizer_screen_open(&equalizer);
    picker->enter(picker->context, START_MS);
    for (int step = 0; step < EQUALIZER_PRESET_FILE; step++) {
        tap(picker, KEY_VOLUME_UP);
    }
    CHECK_EQUAL(SCREEN_ACTION_NONE, tap(picker, KEY_PLAY).type);
    CHECK_EQUAL(EQUALIZER_PRESET_OFF, store.choice.preset);
    CHECK(toast.visible);
    CHECK_EQUAL(0, fake_hal_equalizer().band_count);
}

static void a_file_with_unsupported_filters_plays_the_rest_and_says_so(void)
{
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "Mixed.txt", false, false);
    add_preset("Mixed.txt", PRESET_SKIPPING);
    equalizer_store_load(&store);
    toast = (Toast){0};
    Screen *picker = equalizer_screen_init(
        &equalizer, (EqualizerConfig){.store = &store, .toast = &toast, .theme = &DEFAULT_THEME});
    equalizer_screen_open(&equalizer);
    picker->enter(picker->context, START_MS);
    for (int step = 0; step < EQUALIZER_PRESET_FILE; step++) {
        tap(picker, KEY_VOLUME_UP);
    }
    CHECK_EQUAL(EQUALIZER_PRESET_FILE, store.choice.preset);
    CHECK_EQUAL(1, fake_hal_equalizer().band_count);
    CHECK(toast.visible);
    CHECK(strcmp("2 filters skipped", equalizer.notice) == 0);
}

static Screen *open_two(void)
{
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "A.txt", false, false);
    fake_hal_add_directory_entry("/EQ", "B.txt", false, false);
    add_preset("A.txt", PRESET_A);
    add_preset("B.txt", PRESET_B);
    equalizer_store_load(&store);
    toast = (Toast){0};
    Screen *picker = equalizer_screen_init(
        &equalizer, (EqualizerConfig){.store = &store, .toast = &toast, .theme = &DEFAULT_THEME});
    ScreenAction open = equalizer_screen_open(&equalizer);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, open.type);
    CHECK(open.target == picker);
    picker->enter(picker->context, START_MS);
    return picker;
}

static void the_picker_lists_off_before_the_files(void)
{
    open_two();
    CHECK(strcmp("Off", equalizer_screen_summary(&equalizer)) == 0);
    CHECK(strcmp("Off", equalizer_screen_preset_name(EQUALIZER_PRESET_OFF)) == 0);
    CHECK_EQUAL(3, equalizer.model.count(&equalizer));
    CHECK(strcmp("Off", equalizer.model.label(&equalizer, 0)) == 0);
    CHECK(strcmp("A", equalizer.model.label(&equalizer, 1)) == 0);
}

static void moving_through_the_list_plays_each_preset(void)
{
    Screen *picker = open_two();
    tap(picker, KEY_VOLUME_UP);
    CHECK(strcmp("A.txt", store.choice.preset_name) == 0);
    CHECK_EQUAL(1000, fake_hal_equalizer().bands[0].frequency_hz);
    tap(picker, KEY_VOLUME_UP);
    CHECK(strcmp("B.txt", store.choice.preset_name) == 0);
    tap(picker, KEY_VOLUME_DOWN);
    tap(picker, KEY_VOLUME_DOWN);
    CHECK_EQUAL(EQUALIZER_PRESET_OFF, store.choice.preset);
    CHECK_EQUAL(0, fake_hal_equalizer().band_count);
}

static void leaving_the_picker_saves_what_is_playing(void)
{
    Screen *picker = open_two();
    tap(picker, KEY_VOLUME_UP);
    CHECK_EQUAL(SCREEN_ACTION_POP, tap(picker, KEY_BACK).type);
    EqualizerChoice choice = saved_choice();
    CHECK_EQUAL(EQUALIZER_PRESET_FILE, choice.preset);
    CHECK(strcmp("A.txt", choice.preset_name) == 0);
}

static void choosing_off_closes_the_picker(void)
{
    Screen *picker = open_two();
    tap(picker, KEY_VOLUME_UP);
    CHECK_EQUAL(SCREEN_ACTION_POP, tap(picker, KEY_BACK).type);
    picker->enter(picker->context, START_MS);
    tap(picker, KEY_VOLUME_DOWN);
    CHECK_EQUAL(SCREEN_ACTION_POP, tap(picker, KEY_PLAY).type);
    CHECK_EQUAL(EQUALIZER_PRESET_OFF, saved_choice().preset);
}

static void the_saved_choice_comes_back_at_start(void)
{
    Screen *picker = open_two();
    tap(picker, KEY_VOLUME_UP);
    tap(picker, KEY_BACK);
    EqualizerStore reloaded;
    equalizer_store_load(&reloaded);
    equalizer_store_apply(&reloaded);
    CHECK_EQUAL(EQUALIZER_PRESET_FILE, reloaded.choice.preset);
    CHECK_EQUAL(1, fake_hal_equalizer().band_count);
    CHECK_EQUAL(20, fake_hal_equalizer().bands[0].gain_tenths);
}

static void an_unchanged_choice_is_not_written_again(void)
{
    Screen *picker = open_two();
    tap(picker, KEY_VOLUME_UP);
    tap(picker, KEY_BACK);
    int writes = fake_hal_write_count();
    picker->enter(picker->context, START_MS);
    tap(picker, KEY_BACK);
    CHECK_EQUAL(writes, fake_hal_write_count());
}

static void without_presets_the_equalizer_does_not_open(void)
{
    fake_hal_reset();
    equalizer_store_load(&store);
    toast = (Toast){0};
    equalizer_screen_init(
        &equalizer, (EqualizerConfig){.store = &store, .toast = &toast, .theme = &DEFAULT_THEME});
    equalizer_screen_refresh(&equalizer);
    CHECK(!equalizer_screen_has_presets(&equalizer));
    CHECK_EQUAL(SCREEN_ACTION_NONE, equalizer_screen_open(&equalizer).type);
    CHECK(toast.visible);
    CHECK(strcmp("No presets in /EQ", toast.text) == 0);
    fake_hal_add_directory_entry("/EQ", "A.txt", false, false);
    add_preset("A.txt", PRESET_A);
    CHECK(!equalizer_screen_has_presets(&equalizer));
    equalizer_screen_refresh(&equalizer);
    CHECK(equalizer_screen_has_presets(&equalizer));
    CHECK_EQUAL(SCREEN_ACTION_PUSH, equalizer_screen_open(&equalizer).type);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_picker_lists_off_before_the_files),
        TEST_CASE(moving_through_the_list_plays_each_preset),
        TEST_CASE(leaving_the_picker_saves_what_is_playing),
        TEST_CASE(choosing_off_closes_the_picker),
        TEST_CASE(the_saved_choice_comes_back_at_start),
        TEST_CASE(an_unchanged_choice_is_not_written_again),
        TEST_CASE(without_presets_the_equalizer_does_not_open),
        TEST_CASE(the_file_list_lists_txt_presets),
        TEST_CASE(the_settings_summary_uses_seven_characters_and_the_list_the_whole_name),
        TEST_CASE(opening_the_picker_finds_files_added_since_the_last_time),
        TEST_CASE(a_rejected_file_says_why),
        TEST_CASE(entering_the_picker_does_not_rescan_the_card),
        TEST_CASE(choosing_a_valid_file_applies_and_saves_it),
        TEST_CASE(highlighting_a_file_previews_it),
        TEST_CASE(an_invalid_file_is_not_applied_or_saved),
        TEST_CASE(a_file_with_unsupported_filters_plays_the_rest_and_says_so),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
