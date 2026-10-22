#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
import unittest
from graphics_resources import parse_stat, summarize


class Resources(unittest.TestCase):
    def test_stat_name_spaces_and_parentheses(self):
        fields = ['0'] * 22
        fields[0], fields[11], fields[12], fields[17], fields[19], fields[21] = 'S', '120', '30', '8', '99', '10'
        self.assertEqual(parse_stat('17 (name (worker)) ' + ' '.join(fields), 4096),
                         dict(start_ticks=99, cpu_ticks=150, threads=8, rss_bytes=40960))

    def test_stable_phase_and_pid_reuse(self):
        def point(t, ticks, start=1, phase='frozen'):
            return dict(seconds=t, phase=phase, processes={'1': dict(start_ticks=start, cpu_ticks=ticks, rss_bytes=12)})
        values = [point(0, 0, phase='active'), point(1, 5), point(3, 15), point(4, 20, start=2), point(5, 25, start=2)]
        group = summarize(values, 100)['phases']['frozen']
        self.assertEqual(group['stable_seconds'], 3)
        self.assertEqual(group['churn_intervals_excluded'], 1)
        self.assertAlmostEqual(group['mean_cpu_percent_one_core'], 5)
        self.assertEqual(group['peak_sum_rss_bytes'], 12)


if __name__ == '__main__':
    unittest.main()
