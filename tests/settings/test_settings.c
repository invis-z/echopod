#include "check.h"
#include "core/hash.h"
#include "settings/settings.h"

enum {
    VERSION_FIELD = 4,
    BRIGHTNESS_FIELD = 5,
    USB_MODE_FIELD = 14,
    DAC_VOLUME_FIELD = 15,
    ORIENTATION_FIELD = 16,
    LANGUAGE_FIELD = 17,
    DAC_FIELD = 18,
    OPTIONS_FIELD = 19,
    CHECKSUM_FIELD = OPTIONS_FIELD + SETTINGS_OPTION_COUNT,
    NOW_PLAYING_FACE_FIELD = OPTIONS_FIELD + OPTION_NOW_PLAYING_FACE,
    NEVER_OPTION = 6
};

static Settings custom_settings(void)
{
    Settings settings = settings_defaults();
    settings.brightness = 3;
    settings.screen_timeout = 4;
    settings.gain = GAIN_HIGH;
    settings.output = OUTPUT_BALANCED;
    settings.start_screen = START_FAVORITES;
    settings.shuffle = SHUFFLE_ALBUMS;
    settings.repeat = REPEAT_ONE;
    settings.volume = 42;
    settings.auto_off = 3;
    settings.usb_mode = USB_MODE_DAC;
    settings.dac_volume = 37;
    settings.orientation = ORIENTATION_PORTRAIT_FLIPPED;
    settings.language = LANGUAGE_KOREAN;
    return settings;
}

static void rewrite_checksum(uint8_t *bytes)
{
    uint32_t checksum = hash_update(HASH_START, bytes, CHECKSUM_FIELD);
    for (int index = 0; index < 4; index++) {
        bytes[CHECKSUM_FIELD + index] = (uint8_t)(checksum >> (8 * index));
    }
}

static void defaults_are_comfortable(void)
{
    Settings settings = settings_defaults();
    CHECK_EQUAL(7, settings.brightness);
    CHECK_EQUAL(30000, settings_screen_timeout_ms(&settings));
    CHECK_EQUAL(GAIN_LOW, settings.gain);
    CHECK_EQUAL(OUTPUT_AUTO, settings.output);
    CHECK_EQUAL(START_MAIN_MENU, settings.start_screen);
    CHECK_EQUAL(SHUFFLE_OFF, settings.shuffle);
    CHECK_EQUAL(REPEAT_OFF, settings.repeat);
    CHECK_EQUAL(40, settings.volume);
    CHECK_EQUAL(5u * 60000u, settings_auto_off_ms(&settings));
    CHECK_EQUAL(USB_MODE_STORAGE, settings.usb_mode);
    CHECK_EQUAL(60, settings.dac_volume);
    CHECK_EQUAL(ORIENTATION_LANDSCAPE, settings.orientation);
    CHECK_EQUAL(DRE_OFF, settings.options[OPTION_DRE]);
}

static void auto_off_options_are_stocks(void)
{
    CHECK_EQUAL(0, settings_auto_off_option_minutes(0));
    CHECK_EQUAL(1, settings_auto_off_option_minutes(1));
    CHECK_EQUAL(5, settings_auto_off_option_minutes(2));
    CHECK_EQUAL(10, settings_auto_off_option_minutes(AUTO_OFF_OPTION_COUNT - 1));
}

static void the_usb_mode_and_dac_volume_are_saved_at_bytes_fourteen_and_fifteen(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(1, bytes[VERSION_FIELD]);
    CHECK_EQUAL(USB_MODE_DAC, bytes[USB_MODE_FIELD]);
    CHECK_EQUAL(37, bytes[DAC_VOLUME_FIELD]);
}

static void the_orientation_is_saved_at_byte_sixteen(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(ORIENTATION_PORTRAIT_FLIPPED, bytes[ORIENTATION_FIELD]);
}

static void the_language_is_saved_at_byte_seventeen(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(LANGUAGE_KOREAN, bytes[LANGUAGE_FIELD]);
}

static void an_unknown_language_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[LANGUAGE_FIELD] = LANGUAGE_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void an_unknown_orientation_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[ORIENTATION_FIELD] = ORIENTATION_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void a_dac_volume_above_the_top_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[DAC_VOLUME_FIELD] = SETTINGS_VOLUME_MAX + 1;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void an_unknown_usb_mode_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[USB_MODE_FIELD] = USB_MODE_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void any_other_version_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[VERSION_FIELD] = 2;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
    bytes[VERSION_FIELD] = 0;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void the_filter_and_balance_share_byte_eighteen(void)
{
    Settings settings = custom_settings();
    settings.filter = 3;
    settings.balance = 20;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(3 << 5 | 20, bytes[DAC_FIELD]);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(3, decoded.filter);
    CHECK_EQUAL(20, decoded.balance);
}

static void an_unknown_filter_or_balance_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[DAC_FIELD] = DAC_FILTER_COUNT << 5;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
    bytes[DAC_FIELD] = SETTINGS_BALANCE_STEPS;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void timeout_options_end_with_never(void)
{
    CHECK_EQUAL(10, settings_timeout_option_seconds(0));
    CHECK_EQUAL(300, settings_timeout_option_seconds(5));
    CHECK_EQUAL(0, settings_timeout_option_seconds(NEVER_OPTION));
    Settings settings = settings_defaults();
    settings.screen_timeout = NEVER_OPTION;
    CHECK_EQUAL(0, settings_screen_timeout_ms(&settings));
}

static void sleep_options_start_with_off(void)
{
    CHECK_EQUAL(0, settings_sleep_option_minutes(0));
    CHECK_EQUAL(15, settings_sleep_option_minutes(1));
    CHECK_EQUAL(120, settings_sleep_option_minutes(SLEEP_TIMER_OPTION_COUNT - 1));
}

static void settings_survive_a_round_trip(void)
{
    Settings original = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings decoded;
    settings_encode(&original, bytes);
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(original.brightness, decoded.brightness);
    CHECK_EQUAL(original.screen_timeout, decoded.screen_timeout);
    CHECK_EQUAL(original.gain, decoded.gain);
    CHECK_EQUAL(original.output, decoded.output);
    CHECK_EQUAL(original.start_screen, decoded.start_screen);
    CHECK_EQUAL(original.shuffle, decoded.shuffle);
    CHECK_EQUAL(original.repeat, decoded.repeat);
    CHECK_EQUAL(original.volume, decoded.volume);
    CHECK_EQUAL(original.auto_off, decoded.auto_off);
    CHECK_EQUAL(original.usb_mode, decoded.usb_mode);
    CHECK_EQUAL(original.dac_volume, decoded.dac_volume);
    CHECK_EQUAL(original.orientation, decoded.orientation);
    CHECK_EQUAL(original.language, decoded.language);
}

static void a_corrupted_file_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[BRIGHTNESS_FIELD] ^= 0x02;
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
    CHECK(!settings_decode(bytes, SETTINGS_FILE_SIZE - 1, &settings));
}

static void out_of_range_values_are_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[BRIGHTNESS_FIELD] = SETTINGS_BRIGHTNESS_MAX + 1;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

static void the_new_options_are_saved_and_read_back(void)
{
    Settings settings = custom_settings();
    for (int option = 0; option < SETTINGS_OPTION_COUNT; option++) {
        settings.options[option] = (uint8_t)(settings_option_count((SettingOption)option) - 1);
    }
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(ACCENT_COUNT - 1, bytes[OPTIONS_FIELD + OPTION_ACCENT]);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    for (int option = 0; option < SETTINGS_OPTION_COUNT; option++) {
        CHECK_EQUAL(settings.options[option], decoded.options[option]);
    }
}

static void an_option_out_of_range_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[OPTIONS_FIELD + OPTION_ROCKER] = ROCKER_MODE_COUNT;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(!settings_decode(bytes, sizeof bytes, &decoded));
}

static Settings settings_with_every_option_at_its_top(void)
{
    Settings settings = custom_settings();
    settings.filter = DAC_FILTER_COUNT - 1;
    settings.balance = SETTINGS_BALANCE_STEPS - 1;
    for (int option = 0; option < SETTINGS_OPTION_COUNT; option++) {
        settings.options[option] = (uint8_t)(settings_option_count((SettingOption)option) - 1);
    }
    return settings;
}

static void check_same_values(const Settings *expected, const Settings *actual)
{
    CHECK_EQUAL(expected->brightness, actual->brightness);
    CHECK_EQUAL(expected->screen_timeout, actual->screen_timeout);
    CHECK_EQUAL(expected->gain, actual->gain);
    CHECK_EQUAL(expected->output, actual->output);
    CHECK_EQUAL(expected->start_screen, actual->start_screen);
    CHECK_EQUAL(expected->shuffle, actual->shuffle);
    CHECK_EQUAL(expected->repeat, actual->repeat);
    CHECK_EQUAL(expected->volume, actual->volume);
    CHECK_EQUAL(expected->auto_off, actual->auto_off);
    CHECK_EQUAL(expected->usb_mode, actual->usb_mode);
    CHECK_EQUAL(expected->dac_volume, actual->dac_volume);
    CHECK_EQUAL(expected->orientation, actual->orientation);
    CHECK_EQUAL(expected->language, actual->language);
    CHECK_EQUAL(expected->filter, actual->filter);
    CHECK_EQUAL(expected->balance, actual->balance);
}

static void the_new_options_default_to_their_first_choice(void)
{
    Settings settings = settings_defaults();
    for (int option = OPTION_COVER_FLOW_COVERS; option < SETTINGS_OPTION_COUNT; option++) {
        CHECK_EQUAL(0, settings.options[option]);
    }
    CHECK_EQUAL(COVER_FLOW_FIVE_COVERS, settings.options[OPTION_COVER_FLOW_COVERS]);
    CHECK_EQUAL(ANIMATION_NORMAL, settings.options[OPTION_ANIMATION_SPEED]);
    CHECK_EQUAL(USB_PROMPT_ON, settings.options[OPTION_USB_PROMPT]);
    CHECK_EQUAL(2, settings_option_count(OPTION_COVER_FLOW_COVERS));
    CHECK_EQUAL(3, settings_option_count(OPTION_ANIMATION_SPEED));
    CHECK_EQUAL(2, settings_option_count(OPTION_CLOCK));
    CHECK_EQUAL(2, settings_option_count(OPTION_USB_PROMPT));
    CHECK_EQUAL(CLOCK_OFF, settings.options[OPTION_CLOCK]);
    CHECK_EQUAL(TIME_ZONE_UNSET, settings.options[OPTION_TIME_ZONE]);
    CHECK_EQUAL(TIME_ZONE_OPTION_COUNT, settings_option_count(OPTION_TIME_ZONE));
    CHECK_EQUAL(106, TIME_ZONE_OPTION_COUNT);
}

static void the_animation_speed_scales_animation_times(void)
{
    Settings settings = settings_defaults();
    CHECK_EQUAL(240u, settings_animation_ms(settings.options, 240));
    settings.options[OPTION_ANIMATION_SPEED] = ANIMATION_SLOWER;
    CHECK_EQUAL(360u, settings_animation_ms(settings.options, 240));
    settings.options[OPTION_ANIMATION_SPEED] = ANIMATION_FASTER;
    CHECK_EQUAL(144u, settings_animation_ms(settings.options, 240));
    CHECK_EQUAL(42u, settings_animation_ms(settings.options, 70));
    CHECK_EQUAL(70u, settings_animation_ms(NULL, 70));
}

static void a_file_is_43_bytes_with_every_byte_in_use(void)
{
    Settings settings = settings_with_every_option_at_its_top();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(43, SETTINGS_FILE_SIZE);
    CHECK_EQUAL(1, bytes[VERSION_FIELD]);
    CHECK_EQUAL(3, bytes[BRIGHTNESS_FIELD]);
    CHECK_EQUAL(ANIMATION_FASTER, bytes[OPTIONS_FIELD + OPTION_ANIMATION_SPEED]);
    CHECK_EQUAL(SETTINGS_FILE_SIZE - 4, CHECKSUM_FIELD);
    CHECK_EQUAL(settings_option_count((SettingOption)(SETTINGS_OPTION_COUNT - 1)) - 1,
                bytes[CHECKSUM_FIELD - 1]);
    uint32_t checksum = hash_update(HASH_START, bytes, CHECKSUM_FIELD);
    CHECK_EQUAL(checksum, (uint32_t)bytes[CHECKSUM_FIELD] |
                              (uint32_t)bytes[CHECKSUM_FIELD + 1] << 8 |
                              (uint32_t)bytes[CHECKSUM_FIELD + 2] << 16 |
                              (uint32_t)bytes[CHECKSUM_FIELD + 3] << 24);
    Settings decoded;
    CHECK(!settings_decode(bytes, SETTINGS_FILE_SIZE - 1, &decoded));
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    check_same_values(&settings, &decoded);
    for (int option = 0; option < SETTINGS_OPTION_COUNT; option++) {
        CHECK_EQUAL(settings.options[option], decoded.options[option]);
    }
}

static void the_now_playing_face_is_saved_in_its_option_byte(void)
{
    Settings settings = custom_settings();
    settings.options[OPTION_NOW_PLAYING_FACE] = NP_FACE_TURNTABLE;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    CHECK_EQUAL(NP_FACE_TURNTABLE, bytes[NOW_PLAYING_FACE_FIELD]);
    Settings decoded;
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(NP_FACE_TURNTABLE, decoded.options[OPTION_NOW_PLAYING_FACE]);
    CHECK_EQUAL(NP_FACE_STANDARD, settings_defaults().options[OPTION_NOW_PLAYING_FACE]);
}

static void a_face_from_a_newer_firmware_is_accepted_up_to_the_eighth_slot(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings decoded;
    for (uint8_t face = NP_FACE_COUNT; face < NP_FACE_OPTION_SLOTS; face++) {
        settings_encode(&settings, bytes);
        bytes[NOW_PLAYING_FACE_FIELD] = face;
        rewrite_checksum(bytes);
        CHECK(settings_decode(bytes, sizeof bytes, &decoded));
        CHECK_EQUAL(LANGUAGE_KOREAN, decoded.language);
    }
    bytes[NOW_PLAYING_FACE_FIELD] = NP_FACE_OPTION_SLOTS;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &decoded));
}

static void the_checksum_covers_the_now_playing_face_byte(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[NOW_PLAYING_FACE_FIELD] = NP_FACE_LCD;
    Settings decoded;
    CHECK(!settings_decode(bytes, sizeof bytes, &decoded));
    rewrite_checksum(bytes);
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(NP_FACE_LCD, decoded.options[OPTION_NOW_PLAYING_FACE]);
}

static void an_unknown_dre_setting_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[OPTIONS_FIELD + OPTION_DRE] = DRE_MODE_COUNT;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(!settings_decode(bytes, sizeof bytes, &decoded));
}

static void a_new_option_out_of_range_is_rejected(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[OPTIONS_FIELD + OPTION_ANIMATION_SPEED] = ANIMATION_SPEED_COUNT;
    rewrite_checksum(bytes);
    Settings decoded;
    CHECK(!settings_decode(bytes, sizeof bytes, &decoded));
}

static void a_settings_file_with_the_last_theme_round_trips(void)
{
    Settings settings = custom_settings();
    settings.options[OPTION_THEME] = THEME_HIGH_CONTRAST;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    Settings decoded;
    settings_encode(&settings, bytes);
    CHECK(settings_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(THEME_HIGH_CONTRAST, decoded.options[OPTION_THEME]);
}

static void a_theme_past_the_last_one_rejects_the_file(void)
{
    Settings settings = custom_settings();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[OPTIONS_FIELD + OPTION_THEME] = THEME_COUNT;
    rewrite_checksum(bytes);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_new_options_are_saved_and_read_back),
        TEST_CASE(the_new_options_default_to_their_first_choice),
        TEST_CASE(the_animation_speed_scales_animation_times),
        TEST_CASE(a_file_is_43_bytes_with_every_byte_in_use),
        TEST_CASE(a_new_option_out_of_range_is_rejected),
        TEST_CASE(an_option_out_of_range_is_rejected),
        TEST_CASE(the_now_playing_face_is_saved_in_its_option_byte),
        TEST_CASE(a_face_from_a_newer_firmware_is_accepted_up_to_the_eighth_slot),
        TEST_CASE(the_checksum_covers_the_now_playing_face_byte),
        TEST_CASE(a_settings_file_with_the_last_theme_round_trips),
        TEST_CASE(a_theme_past_the_last_one_rejects_the_file),
        TEST_CASE(an_unknown_dre_setting_is_rejected),
        TEST_CASE(defaults_are_comfortable),
        TEST_CASE(timeout_options_end_with_never),
        TEST_CASE(sleep_options_start_with_off),
        TEST_CASE(settings_survive_a_round_trip),
        TEST_CASE(a_corrupted_file_is_rejected),
        TEST_CASE(out_of_range_values_are_rejected),
        TEST_CASE(auto_off_options_are_stocks),
        TEST_CASE(the_usb_mode_and_dac_volume_are_saved_at_bytes_fourteen_and_fifteen),
        TEST_CASE(a_dac_volume_above_the_top_is_rejected),
        TEST_CASE(an_unknown_usb_mode_is_rejected),
        TEST_CASE(the_orientation_is_saved_at_byte_sixteen),
        TEST_CASE(the_language_is_saved_at_byte_seventeen),
        TEST_CASE(an_unknown_language_is_rejected),
        TEST_CASE(an_unknown_orientation_is_rejected),
        TEST_CASE(any_other_version_is_rejected),
        TEST_CASE(the_filter_and_balance_share_byte_eighteen),
        TEST_CASE(an_unknown_filter_or_balance_is_rejected),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
