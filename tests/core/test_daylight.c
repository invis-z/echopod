#include <stdio.h>

#include "check.h"
#include "core/daylight.h"

typedef struct Transition {
    const char *zone_name;
    DaylightRegion region;
    int32_t zone_minutes;
    uint32_t unix_time;
    int32_t save_before;
    int32_t save_after;
} Transition;

static const Transition TRANSITIONS[] = {
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1774746000u, 0, 60},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1792890000u, 60, 0},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1806195600u, 0, 60},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1824944400u, 60, 0},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1837645200u, 0, 60},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1856394000u, 60, 0},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1869094800u, 0, 60},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1887843600u, 60, 0},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1901149200u, 0, 60},
    {"Europe/Berlin", DAYLIGHT_EUROPE, 60, 1919293200u, 60, 0},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1774746000u, 0, 60},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1792890000u, 60, 0},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1806195600u, 0, 60},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1824944400u, 60, 0},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1837645200u, 0, 60},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1856394000u, 60, 0},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1869094800u, 0, 60},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1887843600u, 60, 0},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1901149200u, 0, 60},
    {"Europe/London", DAYLIGHT_EUROPE, 0, 1919293200u, 60, 0},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1774746000u, 0, 60},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1792890000u, 60, 0},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1806195600u, 0, 60},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1824944400u, 60, 0},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1837645200u, 0, 60},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1856394000u, 60, 0},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1869094800u, 0, 60},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1887843600u, 60, 0},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1901149200u, 0, 60},
    {"Europe/Athens", DAYLIGHT_EUROPE, 120, 1919293200u, 60, 0},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1772953200u, 0, 60},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1793512800u, 60, 0},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1805007600u, 0, 60},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1825567200u, 60, 0},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1836457200u, 0, 60},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1857016800u, 60, 0},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1867906800u, 0, 60},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1888466400u, 60, 0},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1899356400u, 0, 60},
    {"America/New_York", DAYLIGHT_US_CANADA, -300, 1919916000u, 60, 0},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1772964000u, 0, 60},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1793523600u, 60, 0},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1805018400u, 0, 60},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1825578000u, 60, 0},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1836468000u, 0, 60},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1857027600u, 60, 0},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1867917600u, 0, 60},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1888477200u, 60, 0},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1899367200u, 0, 60},
    {"America/Los_Angeles", DAYLIGHT_US_CANADA, -480, 1919926800u, 60, 0},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1775318400u, 60, 0},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1791043200u, 0, 60},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1806768000u, 60, 0},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1822492800u, 0, 60},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1838217600u, 60, 0},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1853942400u, 0, 60},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1869667200u, 60, 0},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1885996800u, 0, 60},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1901721600u, 60, 0},
    {"Australia/Sydney", DAYLIGHT_AUSTRALIA, 600, 1917446400u, 0, 60},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1775320200u, 60, 0},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1791045000u, 0, 60},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1806769800u, 60, 0},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1822494600u, 0, 60},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1838219400u, 60, 0},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1853944200u, 0, 60},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1869669000u, 60, 0},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1885998600u, 0, 60},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1901723400u, 60, 0},
    {"Australia/Adelaide", DAYLIGHT_AUSTRALIA, 570, 1917448200u, 0, 60},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1775311200u, 60, 0},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1790431200u, 0, 60},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1806760800u, 60, 0},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1821880800u, 0, 60},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1838210400u, 60, 0},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1853330400u, 0, 60},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1869660000u, 60, 0},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1885384800u, 0, 60},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1901714400u, 60, 0},
    {"Pacific/Auckland", DAYLIGHT_NEW_ZEALAND, 720, 1916834400u, 0, 60},
};

typedef struct ComputerTransition {
    const char *zone_name;
    DaylightRule rule;
    int32_t zone_minutes;
    uint32_t unix_time;
    int32_t save_before;
    int32_t save_after;
} ComputerTransition;

static const ComputerTransition COMPUTER_TRANSITIONS[] = {
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1775314800u, 30, 0},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1791041400u, 0, 30},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1806764400u, 30, 0},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1822491000u, 0, 30},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1838214000u, 30, 0},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1853940600u, 0, 30},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1869663600u, 30, 0},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1885995000u, 0, 30},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1901718000u, 30, 0},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1917444600u, 0, 30},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1933167600u, 30, 0},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1948894200u, 0, 30},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1964617200u, 30, 0},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1980343800u, 0, 30},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 1996066800u, 30, 0},
    {"Australia/Lord_Howe", {30, false, {10, 1, 0, 120}, {4, 1, 0, 90}}, 630, 2011793400u, 0, 30},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1775358000u, 60, 0},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1788667200u, 0, 60},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1806807600u, 60, 0},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1820116800u, 0, 60},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1838257200u, 60, 0},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1851566400u, 0, 60},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1870311600u, 60, 0},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1883016000u, 0, 60},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1901761200u, 60, 0},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1915070400u, 0, 60},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1933210800u, 60, 0},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1946520000u, 0, 60},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1964660400u, 60, 0},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1977969600u, 0, 60},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 1996110000u, 60, 0},
    {"America/Santiago", {60, false, {9, 1, 6, 1440}, {4, 1, 6, 1380}}, -240, 2009419200u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1774569600u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1792882800u, 60, 0},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1806019200u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1824937200u, 60, 0},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1837468800u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1856386800u, 60, 0},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1868918400u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1887836400u, 60, 0},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1900972800u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1919286000u, 60, 0},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1932422400u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1950735600u, 60, 0},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1963872000u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1982790000u, 60, 0},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 1995321600u, 0, 60},
    {"Asia/Jerusalem", {60, false, {3, 4, 4, 1560}, {10, 5, 0, 60}}, 120, 2014239600u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1775311200u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1790431200u, 0, 60},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1806760800u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1821880800u, 0, 60},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1838210400u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1853330400u, 0, 60},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1869660000u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1885384800u, 0, 60},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1901714400u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1916834400u, 0, 60},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1933164000u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1948284000u, 0, 60},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1964613600u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1979733600u, 0, 60},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 1996063200u, 60, 0},
    {"Pacific/Chatham", {60, false, {9, 5, 0, 165}, {4, 1, 0, 165}}, 765, 2011183200u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1772946000u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1793509200u, 60, 0},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1805000400u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1825563600u, 60, 0},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1836450000u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1857013200u, 60, 0},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1867899600u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1888462800u, 60, 0},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1899349200u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1919912400u, 60, 0},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1930798800u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1951362000u, 60, 0},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1962853200u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1983416400u, 60, 0},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 1994302800u, 0, 60},
    {"America/Havana", {60, false, {3, 2, 0, 0}, {11, 1, 0, 0}}, -300, 2014866000u, 60, 0},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1774746000u, 0, 120},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1792890000u, 120, 0},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1806195600u, 0, 120},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1824944400u, 120, 0},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1837645200u, 0, 120},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1856394000u, 120, 0},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1869094800u, 0, 120},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1887843600u, 120, 0},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1901149200u, 0, 120},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1919293200u, 120, 0},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1932598800u, 0, 120},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1950742800u, 120, 0},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1964048400u, 0, 120},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1982797200u, 120, 0},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 1995498000u, 0, 120},
    {"Antarctica/Troll", {120, false, {3, 5, 0, 60}, {10, 5, 0, 60}}, 0, 2014246800u, 120, 0},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1774656000u, 0, 60},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1792796400u, 60, 0},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1806105600u, 0, 60},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1824850800u, 60, 0},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1837555200u, 0, 60},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1856300400u, 60, 0},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1869004800u, 0, 60},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1887750000u, 60, 0},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1901059200u, 0, 60},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1919199600u, 60, 0},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1932508800u, 0, 60},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1950649200u, 60, 0},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1963958400u, 0, 60},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1982703600u, 60, 0},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 1995408000u, 0, 60},
    {"Asia/Gaza", {60, false, {3, 4, 4, 3000}, {10, 4, 4, 2940}}, 120, 2014153200u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1775358000u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1788667200u, 0, 60},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1806807600u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1820116800u, 0, 60},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1838257200u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1851566400u, 0, 60},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1870311600u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1883016000u, 0, 60},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1901761200u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1915070400u, 0, 60},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1933210800u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1946520000u, 0, 60},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1964660400u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1977969600u, 0, 60},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 1996110000u, 60, 0},
    {"Pacific/Easter", {60, false, {9, 1, 6, 1320}, {4, 1, 6, 1260}}, -360, 2009419200u, 0, 60},
};

enum {
    SECONDS_PER_MINUTE = 60,
    COMPUTER_TRANSITION_COUNT = (int)(sizeof COMPUTER_TRANSITIONS / sizeof COMPUTER_TRANSITIONS[0]),
    TRANSITION_COUNT = (int)(sizeof TRANSITIONS / sizeof TRANSITIONS[0]),
    UNIX_2026_07_01 = 1782864000,
    UNIX_2026_01_15 = 1768435200
};

static int32_t shift_at(const Transition *transition, int32_t offset_seconds)
{
    uint32_t standard = transition->unix_time +
                        (uint32_t)(transition->zone_minutes * SECONDS_PER_MINUTE + offset_seconds);
    return daylight_shift_seconds(daylight_region_rule(transition->region), standard,
                                  transition->zone_minutes);
}

static void every_region_changes_at_the_second_the_tz_database_says(void)
{
    for (int index = 0; index < TRANSITION_COUNT; index++) {
        const Transition *transition = &TRANSITIONS[index];
        bool before = shift_at(transition, -1) == transition->save_before * SECONDS_PER_MINUTE;
        bool after = shift_at(transition, 0) == transition->save_after * SECONDS_PER_MINUTE;
        if (!before || !after) {
            printf("%s at %u\n", transition->zone_name, transition->unix_time);
        }
        CHECK(before);
        CHECK(after);
    }
}

static int32_t computer_shift_at(const ComputerTransition *transition, int32_t offset_seconds)
{
    uint32_t standard = transition->unix_time +
                        (uint32_t)(transition->zone_minutes * SECONDS_PER_MINUTE + offset_seconds);
    return daylight_shift_seconds(&transition->rule, standard, transition->zone_minutes);
}

static void rules_tide_finds_change_where_the_tz_database_says(void)
{
    for (int index = 0; index < COMPUTER_TRANSITION_COUNT; index++) {
        const ComputerTransition *transition = &COMPUTER_TRANSITIONS[index];
        bool before =
            computer_shift_at(transition, -1) == transition->save_before * SECONDS_PER_MINUTE;
        bool after =
            computer_shift_at(transition, 0) == transition->save_after * SECONDS_PER_MINUTE;
        if (!before || !after) {
            printf("%s at %u\n", transition->zone_name, transition->unix_time);
        }
        CHECK(daylight_rule_valid(&transition->rule));
        CHECK(before);
        CHECK(after);
    }
}

static void summer_is_on_and_winter_is_off_in_both_hemispheres(void)
{
    const DaylightRule *europe = daylight_region_rule(DAYLIGHT_EUROPE);
    const DaylightRule *australia = daylight_region_rule(DAYLIGHT_AUSTRALIA);
    CHECK_EQUAL(3600, daylight_shift_seconds(europe, UNIX_2026_07_01 + 3600, 60));
    CHECK_EQUAL(0, daylight_shift_seconds(europe, UNIX_2026_01_15 + 3600, 60));
    CHECK_EQUAL(0, daylight_shift_seconds(australia, UNIX_2026_07_01 + 36000, 600));
    CHECK_EQUAL(3600, daylight_shift_seconds(australia, UNIX_2026_01_15 + 36000, 600));
}

static void no_rule_or_a_broken_one_never_shifts(void)
{
    CHECK_EQUAL(0, daylight_shift_seconds(NULL, UNIX_2026_07_01, 60));
    DaylightRule rule = *daylight_region_rule(DAYLIGHT_EUROPE);
    CHECK(daylight_rule_valid(&rule));
    DaylightRule broken = rule;
    broken.start.month = 13;
    CHECK(!daylight_rule_valid(&broken));
    CHECK_EQUAL(0, daylight_shift_seconds(&broken, UNIX_2026_07_01, 60));
    broken = rule;
    broken.end.week = 0;
    CHECK(!daylight_rule_valid(&broken));
    broken = rule;
    broken.start.weekday = 7;
    CHECK(!daylight_rule_valid(&broken));
    broken = rule;
    broken.save_minutes = 0;
    CHECK(!daylight_rule_valid(&broken));
    broken.save_minutes = DAYLIGHT_MAX_SAVE_MINUTES + 1;
    CHECK(!daylight_rule_valid(&broken));
    broken = rule;
    broken.end = broken.start;
    CHECK(!daylight_rule_valid(&broken));
    broken = rule;
    broken.start.minutes = DAYLIGHT_EDGE_MINUTES_LIMIT + 1;
    CHECK(!daylight_rule_valid(&broken));
    CHECK(daylight_region_rule(DAYLIGHT_REGION_COUNT) == NULL);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_region_changes_at_the_second_the_tz_database_says),
        TEST_CASE(rules_tide_finds_change_where_the_tz_database_says),
        TEST_CASE(summer_is_on_and_winter_is_off_in_both_hemispheres),
        TEST_CASE(no_rule_or_a_broken_one_never_shifts),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
