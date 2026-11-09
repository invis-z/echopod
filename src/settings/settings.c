#include "settings/settings.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    VERSION = 1,
    FILTER_SHIFT = 5,
    BALANCE_MASK = 0x1F,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    BRIGHTNESS_FIELD = 5,
    TIMEOUT_FIELD = 6,
    GAIN_FIELD = 7,
    OUTPUT_FIELD = 8,
    START_FIELD = 9,
    SHUFFLE_FIELD = 10,
    REPEAT_FIELD = 11,
    VOLUME_FIELD = 12,
    AUTO_OFF_FIELD = 13,
    USB_MODE_FIELD = 14,
    DAC_VOLUME_FIELD = 15,
    ORIENTATION_FIELD = 16,
    LANGUAGE_FIELD = 17,
    DAC_FIELD = 18,
    OPTIONS_FIELD = 19,
    CHECKSUM_FIELD = OPTIONS_FIELD + SETTINGS_OPTION_COUNT,
    DEFAULT_BRIGHTNESS = 7,
    DEFAULT_TIMEOUT_OPTION = 2,
    DEFAULT_VOLUME = 40,
    DEFAULT_AUTO_OFF_OPTION = 2,
    MILLISECONDS_PER_MINUTE = 60000,
    MILLISECONDS_PER_SECOND = 1000,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'M', 'S', 'T'};
static const uint16_t TIMEOUT_SECONDS[SCREEN_TIMEOUT_OPTION_COUNT] = {10, 20, 30, 60, 120, 300, 0};
static const uint8_t SLEEP_MINUTES[SLEEP_TIMER_OPTION_COUNT] = {0, 15, 30, 45, 60, 90, 120};
static const uint8_t AUTO_OFF_MINUTES[AUTO_OFF_OPTION_COUNT] = {0, 1, 5, 10};
static const uint8_t ANIMATION_PERCENT[ANIMATION_SPEED_COUNT] = {100, 150, 60};
static const uint8_t OPTION_COUNTS[SETTINGS_OPTION_COUNT] = {
    [OPTION_BATTERY] = 2,
    [OPTION_VOLUME_KEYS] = 2,
    [OPTION_ROCKER] = ROCKER_MODE_COUNT,
    [OPTION_MENU_KEY] = 2,
    [OPTION_ACCENT] = ACCENT_COUNT,
    [OPTION_HIGHLIGHT] = 2,
    [OPTION_COVER_ORDER] = 2,
    [OPTION_ARTIST_VIEW] = 2,
    [OPTION_COVER_FLOW_COVERS] = 2,
    [OPTION_ANIMATION_SPEED] = ANIMATION_SPEED_COUNT,
    [OPTION_CLOCK] = 2,
    [OPTION_USB_PROMPT] = 2,
    [OPTION_TIME_ZONE] = TIME_ZONE_OPTION_COUNT,
    [OPTION_THEME] = 2,
    [OPTION_LYRICS] = 2,
    [OPTION_FOLDER_VIEW] = 2,
    [OPTION_STATUS_BAR] = 2,
    [OPTION_POWER_ON] = 2,
    [OPTION_DRE] = DRE_MODE_COUNT,
    [OPTION_DAYLIGHT] = DAYLIGHT_CHOICE_COUNT};

_Static_assert(CHECKSUM_FIELD + 4 == SETTINGS_FILE_SIZE, "the checksum ends the file");

Settings settings_defaults(void)
{
    return (Settings){
        .brightness = DEFAULT_BRIGHTNESS,
        .screen_timeout = DEFAULT_TIMEOUT_OPTION,
        .gain = GAIN_LOW,
        .output = OUTPUT_AUTO,
        .start_screen = START_MAIN_MENU,
        .shuffle = SHUFFLE_OFF,
        .repeat = REPEAT_OFF,
        .volume = DEFAULT_VOLUME,
        .auto_off = DEFAULT_AUTO_OFF_OPTION,
        .usb_mode = USB_MODE_STORAGE,
        .dac_volume = SETTINGS_DEFAULT_DAC_VOLUME,
        .orientation = ORIENTATION_LANDSCAPE,
        .language = LANGUAGE_ENGLISH,
        .filter = 0,
        .balance = SETTINGS_BALANCE_CENTER,
    };
}

uint32_t settings_timeout_option_seconds(int option)
{
    return option >= 0 && option < SCREEN_TIMEOUT_OPTION_COUNT ? TIMEOUT_SECONDS[option] : 0;
}

uint32_t settings_sleep_option_minutes(int option)
{
    return option >= 0 && option < SLEEP_TIMER_OPTION_COUNT ? SLEEP_MINUTES[option] : 0;
}

uint32_t settings_screen_timeout_ms(const Settings *settings)
{
    return settings_timeout_option_seconds(settings->screen_timeout) * MILLISECONDS_PER_SECOND;
}

uint32_t settings_auto_off_option_minutes(int option)
{
    return option >= 0 && option < AUTO_OFF_OPTION_COUNT ? AUTO_OFF_MINUTES[option] : 0;
}

uint32_t settings_auto_off_ms(const Settings *settings)
{
    return settings_auto_off_option_minutes(settings->auto_off) * MILLISECONDS_PER_MINUTE;
}

uint32_t settings_animation_ms(const uint8_t *options, uint32_t normal_ms)
{
    uint8_t speed = options != NULL ? options[OPTION_ANIMATION_SPEED] : ANIMATION_NORMAL;
    uint32_t percent = speed < ANIMATION_SPEED_COUNT ? ANIMATION_PERCENT[speed] : 100;
    return normal_ms * percent / 100;
}

uint8_t settings_option_count(SettingOption option)
{
    return option < SETTINGS_OPTION_COUNT ? OPTION_COUNTS[option] : 0;
}

static uint32_t checksum_of(const uint8_t *bytes)
{
    return hash_update(HASH_START, bytes, CHECKSUM_FIELD);
}

static void put_u32(uint8_t *bytes, int offset, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[offset + index] = (uint8_t)(value >> (BITS_PER_BYTE * index));
    }
}

static uint32_t get_u32(const uint8_t *bytes, int offset)
{
    uint32_t value = 0;
    for (int index = 3; index >= 0; index--) {
        value = value << BITS_PER_BYTE | bytes[offset + index];
    }
    return value;
}

void settings_encode(const Settings *settings, uint8_t *bytes)
{
    for (int index = 0; index < SETTINGS_FILE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
    bytes[VERSION_FIELD] = VERSION;
    bytes[BRIGHTNESS_FIELD] = settings->brightness;
    bytes[TIMEOUT_FIELD] = settings->screen_timeout;
    bytes[GAIN_FIELD] = (uint8_t)settings->gain;
    bytes[OUTPUT_FIELD] = (uint8_t)settings->output;
    bytes[START_FIELD] = (uint8_t)settings->start_screen;
    bytes[SHUFFLE_FIELD] = (uint8_t)settings->shuffle;
    bytes[REPEAT_FIELD] = (uint8_t)settings->repeat;
    bytes[VOLUME_FIELD] = settings->volume;
    bytes[AUTO_OFF_FIELD] = settings->auto_off;
    bytes[USB_MODE_FIELD] = (uint8_t)settings->usb_mode;
    bytes[DAC_VOLUME_FIELD] = settings->dac_volume;
    bytes[ORIENTATION_FIELD] = (uint8_t)settings->orientation;
    bytes[LANGUAGE_FIELD] = (uint8_t)settings->language;
    bytes[DAC_FIELD] = (uint8_t)(settings->filter << FILTER_SHIFT | settings->balance);
    for (int option = 0; option < SETTINGS_OPTION_COUNT; option++) {
        bytes[OPTIONS_FIELD + option] = settings->options[option];
    }
    put_u32(bytes, CHECKSUM_FIELD, checksum_of(bytes));
}

static bool header_valid(const uint8_t *bytes, uint32_t length)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (bytes[index] != MAGIC[index]) {
            return false;
        }
    }
    return length >= SETTINGS_FILE_SIZE && bytes[VERSION_FIELD] == VERSION &&
           get_u32(bytes, CHECKSUM_FIELD) == checksum_of(bytes);
}

static bool options_valid(const uint8_t *bytes)
{
    for (int option = 0; option < SETTINGS_OPTION_COUNT; option++) {
        if (bytes[OPTIONS_FIELD + option] >= OPTION_COUNTS[option]) {
            return false;
        }
    }
    return true;
}

static bool values_valid(const uint8_t *bytes)
{
    return bytes[BRIGHTNESS_FIELD] >= SETTINGS_BRIGHTNESS_MIN &&
           bytes[BRIGHTNESS_FIELD] <= SETTINGS_BRIGHTNESS_MAX &&
           bytes[TIMEOUT_FIELD] < SCREEN_TIMEOUT_OPTION_COUNT &&
           bytes[GAIN_FIELD] < GAIN_LEVEL_COUNT && bytes[OUTPUT_FIELD] < OUTPUT_JACK_COUNT &&
           bytes[START_FIELD] < START_SCREEN_COUNT && bytes[SHUFFLE_FIELD] < SHUFFLE_MODE_COUNT &&
           bytes[REPEAT_FIELD] < REPEAT_MODE_COUNT && bytes[VOLUME_FIELD] <= SETTINGS_VOLUME_MAX &&
           bytes[AUTO_OFF_FIELD] < AUTO_OFF_OPTION_COUNT &&
           bytes[USB_MODE_FIELD] < USB_MODE_COUNT &&
           bytes[DAC_VOLUME_FIELD] <= SETTINGS_VOLUME_MAX &&
           bytes[ORIENTATION_FIELD] < ORIENTATION_COUNT && bytes[LANGUAGE_FIELD] < LANGUAGE_COUNT &&
           (bytes[DAC_FIELD] >> FILTER_SHIFT) < DAC_FILTER_COUNT &&
           (bytes[DAC_FIELD] & BALANCE_MASK) < SETTINGS_BALANCE_STEPS && options_valid(bytes);
}

bool settings_decode(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    if (!header_valid(bytes, length) || !values_valid(bytes)) {
        return false;
    }
    *settings = (Settings){
        .brightness = bytes[BRIGHTNESS_FIELD],
        .screen_timeout = bytes[TIMEOUT_FIELD],
        .gain = (GainLevel)bytes[GAIN_FIELD],
        .output = (OutputJack)bytes[OUTPUT_FIELD],
        .start_screen = (StartScreen)bytes[START_FIELD],
        .shuffle = (ShuffleMode)bytes[SHUFFLE_FIELD],
        .repeat = (RepeatMode)bytes[REPEAT_FIELD],
        .volume = bytes[VOLUME_FIELD],
        .auto_off = bytes[AUTO_OFF_FIELD],
        .usb_mode = (UsbMode)bytes[USB_MODE_FIELD],
        .dac_volume = bytes[DAC_VOLUME_FIELD],
        .orientation = (Orientation)bytes[ORIENTATION_FIELD],
        .language = (Language)bytes[LANGUAGE_FIELD],
        .filter = bytes[DAC_FIELD] >> FILTER_SHIFT,
        .balance = bytes[DAC_FIELD] & BALANCE_MASK,
    };
    for (int option = 0; option < SETTINGS_OPTION_COUNT; option++) {
        settings->options[option] = bytes[OPTIONS_FIELD + option];
    }
    return true;
}
