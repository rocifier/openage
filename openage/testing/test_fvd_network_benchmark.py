# Copyright 2026 the openage authors. See copying.md for legal info.
"""Regression tests for the stdlib-only FVD network shaper timing helpers."""

import unittest

from .fvd_network_benchmark import bounded_sleep_seconds


class FVDNetworkShaperTimingTests(unittest.TestCase):
    def test_future_deadline_is_bounded(self):
        self.assertAlmostEqual(bounded_sleep_seconds(100.01, 100.0), 0.01)
        self.assertAlmostEqual(bounded_sleep_seconds(101.0, 100.0), 0.02)

    def test_elapsed_deadline_never_produces_negative_sleep(self):
        self.assertEqual(bounded_sleep_seconds(100.0, 100.0), 0.0)
        self.assertEqual(bounded_sleep_seconds(99.999999, 100.0), 0.0)
        self.assertEqual(bounded_sleep_seconds(99.0, 100.0), 0.0)


if __name__ == "__main__":
    unittest.main()
