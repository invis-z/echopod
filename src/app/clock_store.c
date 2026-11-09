#include "clock_store.h"

#include "core/hash.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "hal/time.h"

const char CLOCK_RECORD_PATH[] = "/.rippl/clock.bin";

_Static_assert((int)TIME_ZONE_OPTION_COUNT == (int)WALL_CLOCK_ZONE_COUNT,
               "the time zone setting counts every zone step and unset");

enum {
    RECORD_MAGIC_BYTES = 4,
    RECORD_VERSION_AT = 4,
    RECORD_VERSION = 1,
    RECORD_FLAGS_AT = 5,
    RECORD_HAS_RULE = 1,
    RECORD_TIME_AT = 8,
    RECORD_OFFSET_AT = 12,
    RECORD_SAVE_AT = 14,
    RECORD_START_AT = 16,
    RECORD_END_AT = 22,
    RECORD_EDGE_MINUTES_AT = 4,
    RECORD_CHECK_AT = 28,
    RECORD_EARLIEST_YEAR = 2024,
    BITS_PER_BYTE = 8,
    SECONDS_PER_MINUTE = 60,
    SECONDS_PER_DAY = 86400,
    BUILD_SLACK_DAYS = 1
};

static const uint8_t RECORD_MAGIC[RECORD_MAGIC_BYTES] = {'E', 'C', 'L', 'K'};

static uint32_t get_u32(const uint8_t *bytes)
{
    uint32_t value = 0;
    for (int index = 3; index >= 0; index--) {
        value = value << BITS_PER_BYTE | bytes[index];
    }
    return value;
}

static uint32_t earliest_unix_time(void)
{
    return wall_clock_days_from_civil(RECORD_EARLIEST_YEAR, 1, 1) * SECONDS_PER_DAY;
}

static uint32_t latest_unix_time(void)
{
    return wall_clock_days_from_civil(WALL_CLOCK_LAST_YEAR, 12, 31) * SECONDS_PER_DAY;
}

static int16_t get_i16(const uint8_t *bytes)
{
    return (int16_t)(bytes[0] | bytes[1] << BITS_PER_BYTE);
}

static DaylightEdge get_edge(const uint8_t *bytes)
{
    return (DaylightEdge){.month = bytes[0],
                          .week = bytes[1],
                          .weekday = bytes[2],
                          .minutes = get_i16(bytes + RECORD_EDGE_MINUTES_AT)};
}

static bool decode_rule(const uint8_t *bytes, ClockRecord *record)
{
    uint8_t flags = bytes[RECORD_FLAGS_AT];
    if ((flags & ~RECORD_HAS_RULE) != 0) {
        return false;
    }
    record->has_rule = (flags & RECORD_HAS_RULE) != 0;
    if (!record->has_rule) {
        return true;
    }
    record->rule = (DaylightRule){.save_minutes = get_i16(bytes + RECORD_SAVE_AT),
                                  .start = get_edge(bytes + RECORD_START_AT),
                                  .end = get_edge(bytes + RECORD_END_AT)};
    return daylight_rule_valid(&record->rule);
}

bool clock_store_record_decode(const uint8_t *bytes, uint32_t size, ClockRecord *record)
{
    *record = (ClockRecord){0};
    if (size < CLOCK_RECORD_BYTES) {
        return false;
    }
    for (int index = 0; index < RECORD_MAGIC_BYTES; index++) {
        if (bytes[index] != RECORD_MAGIC[index]) {
            return false;
        }
    }
    if (bytes[RECORD_VERSION_AT] != RECORD_VERSION ||
        get_u32(bytes + RECORD_CHECK_AT) != hash_update(HASH_START, bytes, RECORD_CHECK_AT)) {
        return false;
    }
    record->unix_time = get_u32(bytes + RECORD_TIME_AT);
    record->offset_minutes = get_i16(bytes + RECORD_OFFSET_AT);
    return decode_rule(bytes, record) && record->unix_time >= earliest_unix_time() &&
           record->unix_time < latest_unix_time() &&
           record->offset_minutes >= WALL_CLOCK_ZONE_FIRST_MINUTES &&
           record->offset_minutes <= WALL_CLOCK_ZONE_LAST_MINUTES;
}

static bool read_record(ClockRecord *record)
{
    HalFile file;
    uint8_t bytes[CLOCK_RECORD_BYTES];
    if (!hal_file_open(&file, CLOCK_RECORD_PATH)) {
        return false;
    }
    bool read = file.size >= CLOCK_RECORD_BYTES && hal_file_read(file, 0, bytes, sizeof bytes);
    hal_file_close(&file);
    return read && clock_store_record_decode(bytes, sizeof bytes, record);
}

static const DaylightRule *active_rule(const ClockStore *store)
{
    if (store->daylight == DAYLIGHT_CHOICE_COMPUTER) {
        return store->has_computer_rule ? &store->computer_rule : NULL;
    }
    if (store->daylight == DAYLIGHT_CHOICE_OFF || store->daylight >= DAYLIGHT_CHOICE_COUNT) {
        return NULL;
    }
    return daylight_region_rule((DaylightRegion)(store->daylight - DAYLIGHT_CHOICE_EUROPE));
}

static int32_t shift_of(const ClockStore *store, uint32_t standard_seconds)
{
    return daylight_shift_seconds(active_rule(store), standard_seconds,
                                  wall_clock_zone_minutes(store->clock.zone));
}

static uint32_t shown_of(const ClockStore *store, uint32_t standard_seconds)
{
    return standard_seconds + (uint32_t)shift_of(store, standard_seconds);
}

static uint32_t standard_of(const ClockStore *store, uint32_t shown_seconds)
{
    const DaylightRule *rule = active_rule(store);
    if (rule == NULL) {
        return shown_seconds;
    }
    uint32_t summer = shown_seconds - (uint32_t)(rule->save_minutes * SECONDS_PER_MINUTE);
    return shift_of(store, summer) != 0 ? summer : shown_seconds;
}

static void read_chip(ClockStore *store, uint32_t now_ms)
{
    HalTime time;
    store->chip_answered = hal_time_read(&time);
    store->chip_valid =
        store->chip_answered && time.valid && time.year >= WALL_CLOCK_FIRST_YEAR &&
        time.year <= WALL_CLOCK_LAST_YEAR &&
        wall_clock_days_from_civil(time.year, time.month, time.day) + BUILD_SLACK_DAYS >=
            hal_system_build_day();
    if (store->chip_valid) {
        WallCivil civil = {time.year, time.month, time.day, time.hour, time.minute, time.second};
        wall_clock_set(&store->clock, wall_clock_seconds_of(&civil), now_ms);
    } else if (store->clock.known) {
        wall_clock_set(&store->clock, wall_clock_local(&store->clock, now_ms), now_ms);
    }
    store->next_read_ms = now_ms + CLOCK_READ_INTERVAL_MS;
}

void clock_store_start(ClockStore *store, const Settings *settings, uint32_t now_ms)
{
    if (!store->started) {
        ClockRecord record;
        store->started = true;
        store->clock.known = false;
        bool read = read_record(&record);
        store->seen_record = read ? record.unix_time : 0;
        store->has_computer_rule = read && record.has_rule;
        store->computer_rule = record.rule;
    }
    store->clock.zone = settings->options[OPTION_TIME_ZONE];
    store->daylight = settings->options[OPTION_DAYLIGHT];
    read_chip(store, now_ms);
}

void clock_store_step(ClockStore *store, uint32_t now_ms)
{
    if ((int32_t)(now_ms - store->next_read_ms) >= 0) {
        read_chip(store, now_ms);
    }
}

void clock_store_wake(ClockStore *store, uint32_t now_ms)
{
    store->next_read_ms = now_ms;
}

static bool write_chip(ClockStore *store, uint32_t local_seconds)
{
    WallCivil civil;
    wall_clock_civil_of(local_seconds, &civil);
    HalTime time = {.present = true,
                    .valid = true,
                    .year = civil.year,
                    .month = civil.month,
                    .day = civil.day,
                    .hour = civil.hour,
                    .minute = civil.minute,
                    .second = civil.second,
                    .weekday = wall_clock_weekday(local_seconds)};
    bool written = hal_time_write(&time);
    store->chip_answered = written;
    store->chip_valid = written;
    return written;
}

static bool adopt(ClockStore *store, Settings *settings, uint32_t local_seconds, uint8_t zone,
                  uint32_t now_ms)
{
    settings->options[OPTION_TIME_ZONE] = zone;
    store->clock.zone = zone;
    wall_clock_set(&store->clock, local_seconds, now_ms);
    store->next_read_ms = now_ms + CLOCK_READ_INTERVAL_MS;
    return write_chip(store, local_seconds);
}

static void use_daylight(ClockStore *store, Settings *settings, uint8_t choice)
{
    settings->options[OPTION_DAYLIGHT] = choice;
    store->daylight = choice;
}

bool clock_store_after_usb(ClockStore *store, Settings *settings, uint32_t now_ms)
{
    ClockRecord record;
    if (!read_record(&record) || record.unix_time == store->seen_record) {
        return false;
    }
    store->seen_record = record.unix_time;
    store->has_computer_rule = record.has_rule;
    store->computer_rule = record.rule;
    use_daylight(store, settings, record.has_rule ? DAYLIGHT_CHOICE_COMPUTER : DAYLIGHT_CHOICE_OFF);
    uint8_t zone = wall_clock_zone_of(record.offset_minutes);
    adopt(store, settings, wall_clock_local_of_unix(record.unix_time, zone), zone, now_ms);
    return true;
}

bool clock_store_set(ClockStore *store, Settings *settings, const WallCivil *civil, uint8_t zone,
                     uint32_t now_ms)
{
    store->clock.zone = zone;
    return adopt(store, settings, standard_of(store, wall_clock_seconds_of(civil)), zone, now_ms);
}

bool clock_store_set_daylight(ClockStore *store, Settings *settings, uint8_t choice,
                              uint32_t now_ms)
{
    if (choice >= DAYLIGHT_CHOICE_COUNT) {
        return false;
    }
    if (!store->clock.known) {
        use_daylight(store, settings, choice);
        return true;
    }
    uint32_t standard = wall_clock_local(&store->clock, now_ms);
    uint32_t shown = shown_of(store, standard);
    use_daylight(store, settings, choice);
    uint32_t moved = standard_of(store, shown);
    if (moved == standard) {
        return true;
    }
    uint8_t zone = store->clock.zone;
    if (zone != WALL_CLOCK_ZONE_UNSET) {
        int32_t minutes = (int32_t)(moved - standard) / SECONDS_PER_MINUTE;
        zone = wall_clock_zone_of(wall_clock_zone_minutes(zone) + minutes);
    }
    return adopt(store, settings, moved, zone, now_ms);
}

bool clock_store_now(const ClockStore *store, uint32_t now_ms, WallCivil *civil)
{
    if (!store->clock.known) {
        return false;
    }
    wall_clock_civil_of(shown_of(store, wall_clock_local(&store->clock, now_ms)), civil);
    return true;
}
