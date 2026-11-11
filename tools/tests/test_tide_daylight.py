import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path
import zoneinfo
from zoneinfo import ZoneInfo

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

from tide import daylight

SUMMER_2026 = 1_782_907_200
HOUR = 3600


zoneinfo.reset_tzpath(to=[])


def zone_offsets(name):
    zone = ZoneInfo(name)

    def offset_at(utc_seconds):
        moment = datetime.fromtimestamp(utc_seconds, timezone.utc).astimezone(zone)
        return int(moment.utcoffset().total_seconds()) // 60
    return offset_at


def utc(year, month, day, hour=0):
    return int(datetime(year, month, day, hour, tzinfo=timezone.utc).timestamp())


class ComputerZoneTest(unittest.TestCase):
    def rule_of(self, name):
        return daylight.computer_zone(SUMMER_2026, zone_offsets(name))

    def test_the_big_regions_come_out_as_the_players_own_rules(self):
        self.assertEqual((60, daylight.Rule(60, daylight.Edge(3, 5, 0, 120),
                                            daylight.Edge(10, 5, 0, 120))),
                         self.rule_of("Europe/Berlin"))
        self.assertEqual((-300, daylight.Rule(60, daylight.Edge(3, 2, 0, 120),
                                              daylight.Edge(11, 1, 0, 60))),
                         self.rule_of("America/New_York"))
        self.assertEqual((600, daylight.Rule(60, daylight.Edge(10, 1, 0, 120),
                                             daylight.Edge(4, 1, 0, 120))),
                         self.rule_of("Australia/Sydney"))
        self.assertEqual((720, daylight.Rule(60, daylight.Edge(9, 5, 0, 120),
                                             daylight.Edge(4, 1, 0, 120))),
                         self.rule_of("Pacific/Auckland"))

    def test_odd_rules_still_fit(self):
        standard, rule = self.rule_of("Australia/Lord_Howe")
        self.assertEqual((630, 30), (standard, rule.save_minutes))
        standard, rule = self.rule_of("Asia/Jerusalem")
        self.assertEqual((120, daylight.Edge(3, 4, 4, 26 * 60)), (standard, rule.start))
        standard, rule = self.rule_of("America/Santiago")
        self.assertEqual((-240, daylight.Edge(9, 1, 6, 24 * 60)), (standard, rule.start))
        self.assertEqual(0, self.rule_of("Europe/Dublin")[0])

    def test_a_zone_without_daylight_saving_has_its_offset_and_no_rule(self):
        self.assertEqual((480, None), self.rule_of("Asia/Shanghai"))
        self.assertEqual((330, None), self.rule_of("Asia/Kolkata"))
        self.assertEqual((0, None), daylight.computer_zone(SUMMER_2026, lambda _: 0))

    def test_changes_that_follow_no_rule_give_the_current_offset_and_no_rule(self):
        for name in ("Africa/Casablanca", "America/Havana"):
            self.assertEqual((zone_offsets(name)(SUMMER_2026), None), self.rule_of(name), name)

    def test_the_rule_predicts_every_hour_of_the_next_years(self):
        for name in ("Europe/London", "America/Los_Angeles", "Australia/Adelaide",
                     "Pacific/Chatham", "Asia/Jerusalem", "America/Santiago"):
            offsets = zone_offsets(name)
            standard, rule = self.rule_of(name)
            for moment in range(utc(2028, 1, 1), utc(2031, 1, 1), HOUR):
                self.assertEqual(offsets(moment), daylight.predicted(rule, standard, moment),
                                 f"{name} at {moment}")

    def test_the_last_week_and_numbered_weeks_are_told_apart(self):
        moments = [(datetime(2026, 3, 29), 120), (datetime(2027, 3, 28), 120)]
        self.assertEqual(daylight.Edge(3, 5, 0, 120), daylight.edge_of(moments))
        moments = [(datetime(2026, 3, 8), 120), (datetime(2027, 3, 14), 120)]
        self.assertEqual(daylight.Edge(3, 2, 0, 120), daylight.edge_of(moments))
        moments = [(datetime(2026, 3, 27), 120), (datetime(2029, 3, 23), 120)]
        self.assertEqual(daylight.Edge(3, 4, 5, 120), daylight.edge_of(moments))
        moments.append((datetime(2024, 3, 29), 120))
        self.assertEqual(daylight.Edge(3, 4, 4, 26 * 60), daylight.edge_of(moments))
        moments = [(datetime(2026, 3, 8), 120), (datetime(2027, 3, 14), 60)]
        self.assertIsNone(daylight.edge_of(moments))


if __name__ == "__main__":
    unittest.main()
