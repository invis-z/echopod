import calendar
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone

HOUR = 3600
STEP = 6 * HOUR
MINUTE = 60
DAY_MINUTES = 24 * 60
DAYS_PER_WEEK = 7
LAST_WEEK = 5
SPANS = (8, 2)
EDGE_MINUTES_LIMIT = 7 * DAY_MINUTES
MAX_SAVE_MINUTES = 180


@dataclass(frozen=True)
class Edge:
    month: int
    week: int
    weekday: int
    minutes: int


@dataclass(frozen=True)
class Rule:
    save_minutes: int
    start: Edge
    end: Edge


def local_offset_minutes(utc_seconds):
    return time.localtime(utc_seconds).tm_gmtoff // MINUTE


def change_second(offset_at, low, high):
    before = offset_at(low)
    while high - low > 1:
        middle = (low + high) // 2
        if offset_at(middle) == before:
            low = middle
        else:
            high = middle
    return high


def changes(offset_at, first, last):
    found = []
    previous = offset_at(first)
    for moment in range(first + STEP, last + 1, STEP):
        offset = offset_at(moment)
        if offset != previous:
            found.append((change_second(offset_at, moment - STEP, moment), previous, offset))
            previous = offset
    return found


def wall_time(utc_seconds, offset_minutes):
    return datetime(1970, 1, 1) + timedelta(seconds=utc_seconds + offset_minutes * MINUTE)


def edge_day(year, edge):
    first_weekday = (calendar.weekday(year, edge.month, 1) + 1) % 7
    day = 1 + (edge.weekday - first_weekday) % 7 + 7 * (edge.week - 1)
    days = calendar.monthrange(year, edge.month)[1]
    while day > days:
        day -= 7
    return day


def edge_seconds(year, edge):
    day = datetime(year, edge.month, edge_day(year, edge))
    return int((day - datetime(1970, 1, 1)).total_seconds()) + edge.minutes * MINUTE


def shift_minutes(rule, standard_seconds):
    year = (datetime(1970, 1, 1) + timedelta(seconds=standard_seconds)).year
    start = edge_seconds(year, rule.start)
    end = edge_seconds(year, rule.end)
    if start < end:
        active = start <= standard_seconds < end
    else:
        active = standard_seconds >= start or standard_seconds < end
    return rule.save_minutes if active else 0


def edge_on(moments):
    months = {moment.month for moment, _ in moments}
    weekdays = {(moment.weekday() + 1) % 7 for moment, _ in moments}
    minutes = {value for _, value in moments}
    if len(months) != 1 or len(weekdays) != 1 or len(minutes) != 1:
        return None
    month = months.pop()
    if all(moment.day + 7 > calendar.monthrange(moment.year, month)[1] for moment, _ in moments):
        week = LAST_WEEK
    else:
        weeks = {(moment.day - 1) // 7 + 1 for moment, _ in moments}
        if len(weeks) != 1:
            return None
        week = weeks.pop()
    value = minutes.pop()
    if not -EDGE_MINUTES_LIMIT <= value <= EDGE_MINUTES_LIMIT:
        return None
    return Edge(month, week, weekdays.pop(), value)


def edge_of(moments):
    for days_back in range(DAYS_PER_WEEK):
        edge = edge_on([(moment - timedelta(days=days_back), value + days_back * DAY_MINUTES)
                        for moment, value in moments])
        if edge is not None:
            return edge
    return None


def rule_from(found, standard, save, years):
    starts, ends = [], []
    for moment, before, after in found:
        wall = wall_time(moment, before)
        if wall.second != 0:
            return None
        minutes = wall.hour * MINUTE + wall.minute
        if after > before:
            starts.append((wall, minutes))
        else:
            ends.append((wall, minutes - save))
    if len(starts) != years or len(ends) != years:
        return None
    start = edge_of(starts)
    end = edge_of(ends)
    if start is None or end is None or start == end:
        return None
    return Rule(save, start, end)


def predicted(rule, standard, moment):
    return standard + shift_minutes(rule, moment + standard * MINUTE)


def rule_matches(rule, offset_at, standard, first, last, found):
    for moment in range(first, last + 1, STEP):
        if offset_at(moment) != predicted(rule, standard, moment):
            return False
    return all(predicted(rule, standard, moment - 1) == before and
               predicted(rule, standard, moment) == after for moment, before, after in found)


def zone_over(offset_at, year, years):
    first = int(datetime(year, 1, 1, tzinfo=timezone.utc).timestamp())
    last = int(datetime(year + years, 1, 1, tzinfo=timezone.utc).timestamp()) - 1
    found = changes(offset_at, first, last)
    offsets = {before for _, before, _ in found} | {after for _, _, after in found}
    if not found or len(offsets) != 2:
        return None
    standard = min(offsets)
    save = max(offsets) - standard
    if not 0 < save <= MAX_SAVE_MINUTES:
        return None
    rule = rule_from(found, standard, save, years)
    if rule is None or not rule_matches(rule, offset_at, standard, first, last, found):
        return None
    return standard, rule


def computer_zone(now=None, offset_at=local_offset_minutes):
    now = int(time.time()) if now is None else int(now)
    year = datetime.fromtimestamp(now, timezone.utc).year
    for years in SPANS:
        zone = zone_over(offset_at, year, years)
        if zone is not None:
            return zone
    return offset_at(now), None
