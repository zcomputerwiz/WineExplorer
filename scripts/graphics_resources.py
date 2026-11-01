#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Read-only Linux metrics for an exact owned Wine prefix; not GPU telemetry."""
import os
from pathlib import Path


def parse_stat(text, page_size):
    fields = text[text.rindex(')') + 2:].split()
    return {'start_ticks': int(fields[19]), 'cpu_ticks': int(fields[11]) + int(fields[12]),
            'rss_bytes': int(fields[21]) * page_size, 'threads': int(fields[17])}


def sample(prefix):
    processes = {}
    expected = b'WINEPREFIX=' + os.fsencode(prefix)
    for process in Path('/proc').glob('[0-9]*'):
        try:
            if expected not in (process / 'environ').read_bytes().split(bytes([0])):
                continue
            values = parse_stat((process / 'stat').read_text(), os.sysconf('SC_PAGE_SIZE'))
            values['name'] = (process / 'comm').read_text().strip()
            processes[process.name] = values
        except (OSError, ValueError, IndexError):
            continue
    return processes


def summarize(samples, ticks_per_second):
    groups = {}
    for previous, current in zip(samples, samples[1:]):
        phase = current['phase']
        if phase != previous['phase']:
            continue
        elapsed = current['seconds'] - previous['seconds']
        if elapsed <= 0:
            continue
        group = groups.setdefault(phase, {'stable_seconds': 0., 'stable_cpu_seconds': 0.,
                                          'churn_intervals_excluded': 0, 'peak_sum_rss_bytes': 0})
        before, after = previous['processes'], current['processes']
        group['peak_sum_rss_bytes'] = max(group['peak_sum_rss_bytes'], sum(p['rss_bytes'] for p in after.values()))
        if ({(pid, p['start_ticks']) for pid, p in before.items()}
                != {(pid, p['start_ticks']) for pid, p in after.items()}):
            group['churn_intervals_excluded'] += 1
            continue
        delta = sum(after[pid]['cpu_ticks'] - p['cpu_ticks'] for pid, p in before.items())
        if delta < 0:
            group['churn_intervals_excluded'] += 1
            continue
        group['stable_seconds'] += elapsed
        group['stable_cpu_seconds'] += delta / ticks_per_second
    for group in groups.values():
        duration = group['stable_seconds']
        group['mean_cpu_percent_one_core'] = (100 * group['stable_cpu_seconds'] / duration) if duration else None
    return {'phases': groups, 'clock_ticks_per_second': ticks_per_second,
            'note': 'Owned prefix processes including compositor; sums RSS with shared pages double-counted. '
                    'CPU means exclude process-churn intervals. Not GPU load/memory or idle-power certification.'}
