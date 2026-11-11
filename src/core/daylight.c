#include "core/daylight.h"

#include "core/wall_clock.h"

enum {
    SECONDS_PER_MINUTE = 60,
    SECONDS_PER_DAY = 86400,
    DAYS_PER_WEEK = 7,
    MONTHS_PER_YEAR = 12,
    FEBRUARY = 2,
    LONG_MONTHS = 0x15AA
};

static const DaylightRule REGION_RULES[DAYLIGHT_REGION_COUNT] = {
    [DAYLIGHT_EUROPE] = {.save_minutes = 60,
                         .utc = true,
                         .start = {3, DAYLIGHT_LAST_WEEK, 0, 60},
                         .end = {10, DAYLIGHT_LAST_WEEK, 0, 60}},
    [DAYLIGHT_US_CANADA] = {.save_minutes = 60, .start = {3, 2, 0, 120}, .end = {11, 1, 0, 60}},
    [DAYLIGHT_AUSTRALIA] = {.save_minutes = 60, .start = {10, 1, 0, 120}, .end = {4, 1, 0, 120}},
    [DAYLIGHT_NEW_ZEALAND] = {.save_minutes = 60,
                              .start = {9, DAYLIGHT_LAST_WEEK, 0, 120},
                              .end = {4, 1, 0, 120}},
};

const DaylightRule *daylight_region_rule(DaylightRegion region)
{
    return region < DAYLIGHT_REGION_COUNT ? &REGION_RULES[region] : NULL;
}

static bool edge_valid(const DaylightEdge *edge)
{
    return edge->month >= 1 && edge->month <= MONTHS_PER_YEAR && edge->week >= 1 &&
           edge->week <= DAYLIGHT_LAST_WEEK && edge->weekday < DAYS_PER_WEEK &&
           edge->minutes >= -DAYLIGHT_EDGE_MINUTES_LIMIT &&
           edge->minutes <= DAYLIGHT_EDGE_MINUTES_LIMIT;
}

bool daylight_rule_valid(const DaylightRule *rule)
{
    return rule->save_minutes > 0 && rule->save_minutes <= DAYLIGHT_MAX_SAVE_MINUTES &&
           edge_valid(&rule->start) && edge_valid(&rule->end) &&
           (rule->start.month != rule->end.month || rule->start.week != rule->end.week ||
            rule->start.weekday != rule->end.weekday || rule->start.minutes != rule->end.minutes);
}

static uint32_t month_days(uint32_t year, uint32_t month)
{
    if (month == FEBRUARY) {
        bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        return leap ? 29u : 28u;
    }
    return 30u + ((LONG_MONTHS >> month) & 1u);
}

static int64_t edge_seconds(const DaylightEdge *edge, uint32_t year, int32_t shift_minutes)
{
    uint32_t first = wall_clock_days_from_civil(year, edge->month, 1);
    uint32_t first_weekday = wall_clock_weekday(first * SECONDS_PER_DAY);
    uint32_t day = 1u + (edge->weekday + DAYS_PER_WEEK - first_weekday) % DAYS_PER_WEEK +
                   DAYS_PER_WEEK * (edge->week - 1u);
    while (day > month_days(year, edge->month)) {
        day -= DAYS_PER_WEEK;
    }
    return ((int64_t)first + day - 1) * SECONDS_PER_DAY +
           ((int64_t)edge->minutes + shift_minutes) * SECONDS_PER_MINUTE;
}

int32_t daylight_shift_seconds(const DaylightRule *rule, uint32_t standard_seconds,
                               int32_t zone_minutes)
{
    if (rule == NULL || !daylight_rule_valid(rule)) {
        return 0;
    }
    WallCivil civil;
    wall_clock_civil_of(standard_seconds, &civil);
    int32_t shift = rule->utc ? zone_minutes : 0;
    int64_t start = edge_seconds(&rule->start, civil.year, shift);
    int64_t end = edge_seconds(&rule->end, civil.year, shift);
    int64_t now = standard_seconds;
    bool active = start < end ? now >= start && now < end : now >= start || now < end;
    return active ? rule->save_minutes * SECONDS_PER_MINUTE : 0;
}
