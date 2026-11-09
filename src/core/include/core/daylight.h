#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    DAYLIGHT_LAST_WEEK = 5,
    DAYLIGHT_MAX_SAVE_MINUTES = 180,
    DAYLIGHT_EDGE_MINUTES_LIMIT = 7 * 24 * 60
};

typedef enum DaylightRegion {
    DAYLIGHT_EUROPE,
    DAYLIGHT_US_CANADA,
    DAYLIGHT_AUSTRALIA,
    DAYLIGHT_NEW_ZEALAND,
    DAYLIGHT_REGION_COUNT
} DaylightRegion;

typedef struct DaylightEdge {
    uint8_t month;
    uint8_t week;
    uint8_t weekday;
    int16_t minutes;
} DaylightEdge;

typedef struct DaylightRule {
    int16_t save_minutes;
    bool utc;
    DaylightEdge start;
    DaylightEdge end;
} DaylightRule;

const DaylightRule *daylight_region_rule(DaylightRegion region);
bool daylight_rule_valid(const DaylightRule *rule);
int32_t daylight_shift_seconds(const DaylightRule *rule, uint32_t standard_seconds,
                               int32_t zone_minutes);
