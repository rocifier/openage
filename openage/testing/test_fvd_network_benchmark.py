# Copyright 2026 the openage authors. See copying.md for legal info.
"""Regression tests for the stdlib-only FVD network shaper timing helpers."""

import unittest

from .fvd_network_benchmark import bounded_sleep_seconds, parse_report_fields


class FVDNetworkShaperTimingTests(unittest.TestCase):
    def test_future_deadline_is_bounded(self):
        self.assertAlmostEqual(bounded_sleep_seconds(100.01, 100.0), 0.01)
        self.assertAlmostEqual(bounded_sleep_seconds(101.0, 100.0), 0.02)

    def test_elapsed_deadline_never_produces_negative_sleep(self):
        self.assertEqual(bounded_sleep_seconds(100.0, 100.0), 0.0)
        self.assertEqual(bounded_sleep_seconds(99.999999, 100.0), 0.0)
        self.assertEqual(bounded_sleep_seconds(99.0, 100.0), 0.0)


class FVDNetworkReportParsingTests(unittest.TestCase):
    def test_continuous_mode_and_numeric_metrics(self):
        fields = parse_report_fields(
            "players=5 remote=4 mode=independent "
            "command_p95_ms=328.46 command_offered=80 "
            "world_p50_ms=0.00 world_samples=79"
        )
        self.assertEqual(fields["players"], 5)
        self.assertEqual(fields["remote"], 4)
        self.assertEqual(fields["mode"], "independent")
        self.assertEqual(fields["command_p95_ms"], 328.46)
        self.assertEqual(fields["command_offered"], 80)
        self.assertEqual(fields["world_p50_ms"], 0.0)
        self.assertEqual(fields["world_samples"], 79)

    def test_unknown_text_fields_do_not_crash_reporting(self):
        fields = parse_report_fields("mode=independent status=congested extra flag=ok")
        self.assertEqual(fields, {
            "mode": "independent", "status": "congested", "flag": "ok"
        })



if __name__ == "__main__":
    unittest.main()
