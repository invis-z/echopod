#include <string.h>

#include "check.h"
#include "clock_store.h"
#include "core/hash.h"
#include "fake_hal.h"
#include "hal/storage.h"

enum {
    START_MS = 1000,
    TEN_MINUTES_MS = 10 * 60 * 1000,
    UNIX_2026_11_05_NOON = 1793880000,
    UNIX_2026_07_01_NOON = 1782907200,
    UNIX_2026_03_29_ONE_UTC = 1774746000,
    PLUS_ONE_HOUR = 60,
    MINUS_FIVE_HOURS = -300,
    SECONDS_PER_HOUR = 3600
};

static ClockStore store;
static Settings settings;
static uint8_t record[CLOCK_RECORD_BYTES];

static HalTime chip_at(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
                       uint8_t second)
{
    return (HalTime){.present = true,
                     .valid = true,
                     .year = year,
                     .month = month,
                     .day = day,
                     .hour = hour,
                     .minute = minute,
                     .second = second};
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static const uint8_t *make_record(uint32_t unix_time, int16_t offset_minutes)
{
    memset(record, 0, sizeof record);
    memcpy(record, "ECLK", 4);
    record[4] = 1;
    put_u32(record + 8, unix_time);
    record[12] = (uint8_t)offset_minutes;
    record[13] = (uint8_t)((uint16_t)offset_minutes >> 8);
    put_u32(record + 28, hash_update(HASH_START, record, 28));
    return record;
}

static void start(void)
{
    fake_hal_reset();
    settings = settings_defaults();
    store = (ClockStore){0};
}

static WallCivil now_at(uint32_t now_ms)
{
    WallCivil civil = {0};
    CHECK(clock_store_now(&store, now_ms, &civil));
    return civil;
}

static void a_valid_chip_sets_the_clock_at_start(void)
{
    start();
    fake_hal_set_time(chip_at(2026, 11, 5, 12, 0, 30));
    clock_store_start(&store, &settings, START_MS);
    CHECK(store.chip_answered);
    CHECK(store.chip_valid);
    CHECK(store.clock.known);
    WallCivil civil = now_at(START_MS + 90 * 1000);
    CHECK_EQUAL(2026, civil.year);
    CHECK_EQUAL(11, civil.month);
    CHECK_EQUAL(5, civil.day);
    CHECK_EQUAL(12, civil.hour);
    CHECK_EQUAL(2, civil.minute);
    CHECK_EQUAL(0, civil.second);
}

static void a_chip_that_lost_power_leaves_the_clock_unknown(void)
{
    start();
    HalTime chip = chip_at(2025, 5, 7, 4, 37, 22);
    chip.valid = false;
    chip.low_voltage = true;
    fake_hal_set_time(chip);
    clock_store_start(&store, &settings, START_MS);
    WallCivil civil;
    CHECK(store.chip_answered);
    CHECK(!store.chip_valid);
    CHECK(!clock_store_now(&store, START_MS, &civil));
}

static uint32_t day_of(uint32_t year, uint32_t month, uint32_t day)
{
    return wall_clock_days_from_civil(year, month, day);
}

static void a_chip_dated_before_the_firmware_build_is_not_trusted(void)
{
    start();
    fake_hal_set_build_day(day_of(2026, 11, 1));
    fake_hal_set_time(chip_at(2026, 1, 14, 9, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    WallCivil civil;
    CHECK(store.chip_answered);
    CHECK(!store.chip_valid);
    CHECK(!clock_store_now(&store, START_MS, &civil));
}

static void a_chip_from_the_day_before_the_build_on_is_used(void)
{
    start();
    fake_hal_set_build_day(day_of(2026, 11, 1));
    fake_hal_set_time(chip_at(2026, 10, 31, 23, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    CHECK(store.chip_valid);
    CHECK_EQUAL(31, now_at(START_MS).day);
    fake_hal_set_time(chip_at(2026, 10, 30, 23, 59, 0));
    store = (ClockStore){0};
    clock_store_start(&store, &settings, START_MS);
    CHECK(!store.chip_valid);
}

static void setting_the_time_by_hand_wins_over_an_early_chip(void)
{
    start();
    fake_hal_set_build_day(day_of(2026, 11, 1));
    fake_hal_set_time(chip_at(2026, 1, 14, 9, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    WallCivil wanted = {2026, 11, 6, 20, 15, 0};
    CHECK(clock_store_set(&store, &settings, &wanted, WALL_CLOCK_ZONE_UTC, START_MS));
    clock_store_step(&store, START_MS + TEN_MINUTES_MS);
    CHECK(store.chip_valid);
    WallCivil civil = now_at(START_MS + TEN_MINUTES_MS);
    CHECK_EQUAL(11, civil.month);
    CHECK_EQUAL(6, civil.day);
    CHECK_EQUAL(20, civil.hour);
}

static void a_silent_chip_leaves_the_clock_unknown(void)
{
    start();
    clock_store_start(&store, &settings, START_MS);
    WallCivil civil;
    CHECK(!store.chip_answered);
    CHECK(!clock_store_now(&store, START_MS, &civil));
    CHECK_EQUAL(1, fake_hal_time_read_count());
}

static void the_chip_is_read_again_every_ten_minutes(void)
{
    start();
    fake_hal_set_time(chip_at(2026, 11, 5, 12, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    CHECK_EQUAL(1, fake_hal_time_read_count());
    clock_store_step(&store, START_MS + TEN_MINUTES_MS - 1);
    CHECK_EQUAL(1, fake_hal_time_read_count());
    fake_hal_set_time(chip_at(2026, 11, 5, 12, 11, 0));
    clock_store_step(&store, START_MS + TEN_MINUTES_MS);
    CHECK_EQUAL(2, fake_hal_time_read_count());
    CHECK_EQUAL(11, now_at(START_MS + TEN_MINUTES_MS).minute);
    clock_store_wake(&store, START_MS + TEN_MINUTES_MS);
    clock_store_step(&store, START_MS + TEN_MINUTES_MS + 1);
    CHECK_EQUAL(3, fake_hal_time_read_count());
}

static void a_software_clock_keeps_running_when_the_chip_stops_answering(void)
{
    start();
    fake_hal_set_time(chip_at(2026, 11, 5, 12, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    fake_hal_set_time((HalTime){.present = false});
    clock_store_step(&store, START_MS + TEN_MINUTES_MS);
    CHECK(!store.chip_answered);
    CHECK_EQUAL(10, now_at(START_MS + TEN_MINUTES_MS).minute);
    clock_store_step(&store, START_MS + 2 * TEN_MINUTES_MS);
    CHECK_EQUAL(20, now_at(START_MS + 2 * TEN_MINUTES_MS).minute);
}

static void a_record_seen_at_start_is_not_applied(void)
{
    start();
    fake_hal_add_file(CLOCK_RECORD_PATH, make_record(UNIX_2026_11_05_NOON, PLUS_ONE_HOUR),
                      CLOCK_RECORD_BYTES);
    clock_store_start(&store, &settings, START_MS);
    WallCivil civil;
    CHECK(!clock_store_now(&store, START_MS, &civil));
    CHECK(!clock_store_after_usb(&store, &settings, START_MS));
    CHECK(!clock_store_now(&store, START_MS, &civil));
    CHECK_EQUAL(0, fake_hal_time_write_count());
    CHECK_EQUAL(TIME_ZONE_UNSET, settings.options[OPTION_TIME_ZONE]);
}

static void a_new_record_after_usb_sets_the_clock_the_chip_and_the_zone(void)
{
    start();
    HalTime chip = chip_at(2025, 5, 7, 4, 37, 22);
    chip.valid = false;
    chip.low_voltage = true;
    fake_hal_set_time(chip);
    clock_store_start(&store, &settings, START_MS);
    CHECK(hal_file_write(CLOCK_RECORD_PATH, make_record(UNIX_2026_11_05_NOON, PLUS_ONE_HOUR),
                         CLOCK_RECORD_BYTES));
    CHECK(clock_store_after_usb(&store, &settings, START_MS));
    WallCivil civil = now_at(START_MS);
    CHECK_EQUAL(2026, civil.year);
    CHECK_EQUAL(13, civil.hour);
    CHECK_EQUAL(0, civil.minute);
    CHECK_EQUAL(1, fake_hal_time_write_count());
    HalTime written = fake_hal_time();
    CHECK(written.valid);
    CHECK_EQUAL(13, written.hour);
    CHECK_EQUAL(4, written.weekday);
    CHECK_EQUAL(wall_clock_zone_of(PLUS_ONE_HOUR), settings.options[OPTION_TIME_ZONE]);
    uint32_t unix_time = 0;
    CHECK(wall_clock_unix(&store.clock, START_MS, &unix_time));
    CHECK_EQUAL(UNIX_2026_11_05_NOON, unix_time);
    CHECK(!clock_store_after_usb(&store, &settings, START_MS));
    CHECK_EQUAL(1, fake_hal_time_write_count());
}

static void a_record_with_a_negative_zone_applies_west_of_utc(void)
{
    start();
    clock_store_start(&store, &settings, START_MS);
    CHECK(hal_file_write(CLOCK_RECORD_PATH, make_record(UNIX_2026_11_05_NOON, MINUS_FIVE_HOURS),
                         CLOCK_RECORD_BYTES));
    CHECK(clock_store_after_usb(&store, &settings, START_MS));
    CHECK_EQUAL(7, now_at(START_MS).hour);
    CHECK_EQUAL(wall_clock_zone_of(MINUS_FIVE_HOURS), settings.options[OPTION_TIME_ZONE]);
    CHECK_EQUAL(1, fake_hal_time_write_count());
    CHECK(!store.chip_answered);
}

static void a_damaged_or_old_record_is_ignored(void)
{
    start();
    clock_store_start(&store, &settings, START_MS);
    uint8_t broken[CLOCK_RECORD_BYTES];
    memcpy(broken, make_record(UNIX_2026_11_05_NOON, 0), sizeof broken);
    broken[9] ^= 1;
    CHECK(hal_file_write(CLOCK_RECORD_PATH, broken, sizeof broken));
    CHECK(!clock_store_after_usb(&store, &settings, START_MS));
    memcpy(broken, make_record(UNIX_2026_11_05_NOON, 0), sizeof broken);
    broken[4] = 2;
    put_u32(broken + 28, hash_update(HASH_START, broken, 28));
    CHECK(hal_file_write(CLOCK_RECORD_PATH, broken, sizeof broken));
    CHECK(!clock_store_after_usb(&store, &settings, START_MS));
    CHECK(hal_file_write(CLOCK_RECORD_PATH, make_record(1600000000, 0), CLOCK_RECORD_BYTES));
    CHECK(!clock_store_after_usb(&store, &settings, START_MS));
    WallCivil civil;
    CHECK(!clock_store_now(&store, START_MS, &civil));
}

static void a_record_decodes_its_fields(void)
{
    ClockRecord decoded;
    CHECK(clock_store_record_decode(make_record(UNIX_2026_11_05_NOON, MINUS_FIVE_HOURS),
                                    CLOCK_RECORD_BYTES, &decoded));
    CHECK_EQUAL(UNIX_2026_11_05_NOON, decoded.unix_time);
    CHECK_EQUAL(MINUS_FIVE_HOURS, decoded.offset_minutes);
    CHECK(!decoded.has_rule);
    CHECK(!clock_store_record_decode(record, CLOCK_RECORD_BYTES - 1, &decoded));
}

static void put_i16(uint8_t *bytes, int32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)((uint32_t)value >> 8);
}

static void put_edge(uint8_t *bytes, DaylightEdge edge)
{
    bytes[0] = edge.month;
    bytes[1] = edge.week;
    bytes[2] = edge.weekday;
    put_i16(bytes + 4, edge.minutes);
}

static void seal_v2(void)
{
    put_u32(record + 28, hash_update(HASH_START, record, 28));
}

static const DaylightRule BERLIN = {
    .save_minutes = 60, .start = {3, 5, 0, 120}, .end = {10, 5, 0, 120}};

static const uint8_t *make_rule_record(uint32_t unix_time, int16_t standard_minutes,
                                       const DaylightRule *rule)
{
    memset(record, 0, sizeof record);
    memcpy(record, "ECLK", 4);
    record[4] = 1;
    put_u32(record + 8, unix_time);
    put_i16(record + 12, standard_minutes);
    if (rule != NULL) {
        record[5] = 1;
        put_i16(record + 14, rule->save_minutes);
        put_edge(record + 16, rule->start);
        put_edge(record + 22, rule->end);
    }
    seal_v2();
    return record;
}

static void sync_with(const uint8_t *bytes)
{
    CHECK(hal_file_write(CLOCK_RECORD_PATH, bytes, CLOCK_RECORD_BYTES));
    CHECK(clock_store_after_usb(&store, &settings, START_MS));
}

static void a_summer_sync_keeps_standard_time_on_the_chip_and_shows_summer_time(void)
{
    start();
    fake_hal_set_time((HalTime){.present = true, .low_voltage = true});
    clock_store_start(&store, &settings, START_MS);
    sync_with(make_rule_record(UNIX_2026_07_01_NOON, PLUS_ONE_HOUR, &BERLIN));
    CHECK_EQUAL(DAYLIGHT_CHOICE_COMPUTER, settings.options[OPTION_DAYLIGHT]);
    CHECK_EQUAL(wall_clock_zone_of(PLUS_ONE_HOUR), settings.options[OPTION_TIME_ZONE]);
    CHECK_EQUAL(13, fake_hal_time().hour);
    CHECK_EQUAL(14, now_at(START_MS).hour);
    uint32_t unix_time = 0;
    CHECK(wall_clock_unix(&store.clock, START_MS, &unix_time));
    CHECK_EQUAL(UNIX_2026_07_01_NOON, unix_time);
}

static void a_sync_without_a_rule_turns_daylight_saving_off(void)
{
    start();
    settings.options[OPTION_DAYLIGHT] = DAYLIGHT_CHOICE_EUROPE;
    clock_store_start(&store, &settings, START_MS);
    sync_with(make_rule_record(UNIX_2026_07_01_NOON, 480, NULL));
    CHECK_EQUAL(DAYLIGHT_CHOICE_OFF, settings.options[OPTION_DAYLIGHT]);
    CHECK_EQUAL(20, now_at(START_MS).hour);
}

static void the_computers_rule_is_read_again_at_start(void)
{
    start();
    fake_hal_add_file(CLOCK_RECORD_PATH,
                      make_rule_record(UNIX_2026_07_01_NOON, PLUS_ONE_HOUR, &BERLIN),
                      CLOCK_RECORD_BYTES);
    settings.options[OPTION_TIME_ZONE] = wall_clock_zone_of(PLUS_ONE_HOUR);
    settings.options[OPTION_DAYLIGHT] = DAYLIGHT_CHOICE_COMPUTER;
    fake_hal_set_time(chip_at(2026, 7, 1, 13, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    CHECK(store.has_computer_rule);
    CHECK_EQUAL(14, now_at(START_MS).hour);
    fake_hal_reset();
    fake_hal_set_time(chip_at(2026, 7, 1, 13, 0, 0));
    store = (ClockStore){0};
    clock_store_start(&store, &settings, START_MS);
    CHECK(!store.has_computer_rule);
    CHECK_EQUAL(13, now_at(START_MS).hour);
}

static void a_broken_rule_or_unknown_flags_spoil_the_record(void)
{
    ClockRecord decoded;
    DaylightRule broken = BERLIN;
    broken.end.month = 13;
    make_rule_record(UNIX_2026_07_01_NOON, PLUS_ONE_HOUR, &broken);
    CHECK(!clock_store_record_decode(record, CLOCK_RECORD_BYTES, &decoded));
    make_rule_record(UNIX_2026_07_01_NOON, PLUS_ONE_HOUR, &BERLIN);
    record[5] = 3;
    seal_v2();
    CHECK(!clock_store_record_decode(record, CLOCK_RECORD_BYTES, &decoded));
    make_rule_record(UNIX_2026_07_01_NOON, PLUS_ONE_HOUR, &BERLIN);
    CHECK(!clock_store_record_decode(record, CLOCK_RECORD_BYTES - 1, &decoded));
    record[20] ^= 1;
    CHECK(!clock_store_record_decode(record, CLOCK_RECORD_BYTES, &decoded));
    make_rule_record(UNIX_2026_07_01_NOON, 15 * 60, NULL);
    CHECK(!clock_store_record_decode(record, CLOCK_RECORD_BYTES, &decoded));
    make_rule_record(UNIX_2026_07_01_NOON, PLUS_ONE_HOUR, &BERLIN);
    CHECK(clock_store_record_decode(record, CLOCK_RECORD_BYTES, &decoded));
    CHECK(decoded.has_rule);
    CHECK_EQUAL(120, decoded.rule.end.minutes);
}

static void choosing_daylight_saving_in_summer_keeps_the_time_shown(void)
{
    start();
    settings.options[OPTION_TIME_ZONE] = wall_clock_zone_of(120);
    fake_hal_set_time(chip_at(2026, 7, 1, 14, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    CHECK(clock_store_set_daylight(&store, &settings, DAYLIGHT_CHOICE_EUROPE, START_MS));
    CHECK_EQUAL(DAYLIGHT_CHOICE_EUROPE, settings.options[OPTION_DAYLIGHT]);
    CHECK_EQUAL(14, now_at(START_MS).hour);
    CHECK_EQUAL(13, fake_hal_time().hour);
    CHECK_EQUAL(wall_clock_zone_of(PLUS_ONE_HOUR), settings.options[OPTION_TIME_ZONE]);
    CHECK(clock_store_set_daylight(&store, &settings, DAYLIGHT_CHOICE_OFF, START_MS));
    CHECK_EQUAL(14, now_at(START_MS).hour);
    CHECK_EQUAL(14, fake_hal_time().hour);
    CHECK_EQUAL(wall_clock_zone_of(120), settings.options[OPTION_TIME_ZONE]);
    CHECK(!clock_store_set_daylight(&store, &settings, DAYLIGHT_CHOICE_COUNT, START_MS));
}

static void choosing_daylight_saving_in_winter_changes_nothing_else(void)
{
    start();
    settings.options[OPTION_TIME_ZONE] = wall_clock_zone_of(PLUS_ONE_HOUR);
    fake_hal_set_time(chip_at(2026, 1, 15, 9, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    CHECK(clock_store_set_daylight(&store, &settings, DAYLIGHT_CHOICE_EUROPE, START_MS));
    CHECK_EQUAL(0, fake_hal_time_write_count());
    CHECK_EQUAL(9, now_at(START_MS).hour);
    CHECK_EQUAL(wall_clock_zone_of(PLUS_ONE_HOUR), settings.options[OPTION_TIME_ZONE]);
}

static void the_time_set_by_hand_is_the_time_shown(void)
{
    start();
    settings.options[OPTION_DAYLIGHT] = DAYLIGHT_CHOICE_US_CANADA;
    fake_hal_set_time((HalTime){.present = true, .low_voltage = true});
    clock_store_start(&store, &settings, START_MS);
    WallCivil summer = {2026, 7, 1, 8, 0, 0};
    CHECK(clock_store_set(&store, &settings, &summer, wall_clock_zone_of(MINUS_FIVE_HOURS),
                          START_MS));
    CHECK_EQUAL(7, fake_hal_time().hour);
    CHECK_EQUAL(8, now_at(START_MS).hour);
    uint32_t unix_time = 0;
    CHECK(wall_clock_unix(&store.clock, START_MS, &unix_time));
    CHECK_EQUAL(UNIX_2026_07_01_NOON, unix_time);
    WallCivil winter = {2026, 12, 1, 8, 0, 0};
    CHECK(clock_store_set(&store, &settings, &winter, wall_clock_zone_of(MINUS_FIVE_HOURS),
                          START_MS));
    CHECK_EQUAL(8, fake_hal_time().hour);
    CHECK_EQUAL(8, now_at(START_MS).hour);
}

static void the_clock_springs_forward_by_itself(void)
{
    start();
    settings.options[OPTION_TIME_ZONE] = wall_clock_zone_of(PLUS_ONE_HOUR);
    settings.options[OPTION_DAYLIGHT] = DAYLIGHT_CHOICE_EUROPE;
    fake_hal_set_time(chip_at(2026, 3, 29, 1, 59, 59));
    clock_store_start(&store, &settings, START_MS);
    WallCivil before = now_at(START_MS);
    CHECK_EQUAL(1, before.hour);
    CHECK_EQUAL(59, before.minute);
    WallCivil after = now_at(START_MS + 1000);
    CHECK_EQUAL(3, after.hour);
    CHECK_EQUAL(0, after.minute);
    uint32_t unix_time = 0;
    CHECK(wall_clock_unix(&store.clock, START_MS + 1000, &unix_time));
    CHECK_EQUAL(UNIX_2026_03_29_ONE_UTC, unix_time);
}

static void setting_the_time_by_hand_writes_the_chip_and_keeps_the_zone(void)
{
    start();
    fake_hal_set_time((HalTime){.present = true, .low_voltage = true});
    clock_store_start(&store, &settings, START_MS);
    WallCivil wanted = {2026, 2, 28, 23, 59, 0};
    CHECK(clock_store_set(&store, &settings, &wanted, wall_clock_zone_of(330), START_MS));
    CHECK_EQUAL(1, fake_hal_time_write_count());
    CHECK_EQUAL(28, fake_hal_time().day);
    CHECK_EQUAL(6, fake_hal_time().weekday);
    WallCivil civil = now_at(START_MS + 61 * 1000);
    CHECK_EQUAL(3, civil.month);
    CHECK_EQUAL(1, civil.day);
    CHECK_EQUAL(0, civil.hour);
    CHECK_EQUAL(wall_clock_zone_of(330), settings.options[OPTION_TIME_ZONE]);
    uint32_t unix_time = 0;
    CHECK(wall_clock_unix(&store.clock, START_MS, &unix_time));
    CHECK_EQUAL(wall_clock_seconds_of(&wanted) - 330 * 60, unix_time);
}

static void setting_the_time_without_a_chip_still_runs_the_clock(void)
{
    start();
    clock_store_start(&store, &settings, START_MS);
    WallCivil wanted = {2026, 11, 5, 9, 30, 0};
    CHECK(!clock_store_set(&store, &settings, &wanted, WALL_CLOCK_ZONE_UTC, START_MS));
    CHECK_EQUAL(1, fake_hal_time_write_count());
    CHECK(!store.chip_answered);
    CHECK_EQUAL(31, now_at(START_MS + 60 * 1000).minute);
}

static void the_zone_setting_is_taken_from_the_settings_at_start(void)
{
    start();
    settings.options[OPTION_TIME_ZONE] = wall_clock_zone_of(PLUS_ONE_HOUR);
    fake_hal_set_time(chip_at(2026, 11, 5, 13, 0, 0));
    clock_store_start(&store, &settings, START_MS);
    uint32_t unix_time = 0;
    CHECK(wall_clock_unix(&store.clock, START_MS, &unix_time));
    CHECK_EQUAL(UNIX_2026_11_05_NOON, unix_time);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_valid_chip_sets_the_clock_at_start),
        TEST_CASE(a_chip_that_lost_power_leaves_the_clock_unknown),
        TEST_CASE(a_silent_chip_leaves_the_clock_unknown),
        TEST_CASE(a_chip_dated_before_the_firmware_build_is_not_trusted),
        TEST_CASE(a_chip_from_the_day_before_the_build_on_is_used),
        TEST_CASE(setting_the_time_by_hand_wins_over_an_early_chip),
        TEST_CASE(the_chip_is_read_again_every_ten_minutes),
        TEST_CASE(a_software_clock_keeps_running_when_the_chip_stops_answering),
        TEST_CASE(a_record_seen_at_start_is_not_applied),
        TEST_CASE(a_new_record_after_usb_sets_the_clock_the_chip_and_the_zone),
        TEST_CASE(a_record_with_a_negative_zone_applies_west_of_utc),
        TEST_CASE(a_damaged_or_old_record_is_ignored),
        TEST_CASE(a_record_decodes_its_fields),
        TEST_CASE(a_summer_sync_keeps_standard_time_on_the_chip_and_shows_summer_time),
        TEST_CASE(a_sync_without_a_rule_turns_daylight_saving_off),
        TEST_CASE(the_computers_rule_is_read_again_at_start),
        TEST_CASE(a_broken_rule_or_unknown_flags_spoil_the_record),
        TEST_CASE(choosing_daylight_saving_in_summer_keeps_the_time_shown),
        TEST_CASE(choosing_daylight_saving_in_winter_changes_nothing_else),
        TEST_CASE(the_time_set_by_hand_is_the_time_shown),
        TEST_CASE(the_clock_springs_forward_by_itself),
        TEST_CASE(setting_the_time_by_hand_writes_the_chip_and_keeps_the_zone),
        TEST_CASE(setting_the_time_without_a_chip_still_runs_the_clock),
        TEST_CASE(the_zone_setting_is_taken_from_the_settings_at_start),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
