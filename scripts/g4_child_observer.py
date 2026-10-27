# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only owned Chromium child-exit observer.
Samples /proc during browser execution to record actual PID, PPID, command line, role,
and observed lifecycle/timing of child processes (content renderers, network service,
extension renderer, GPU, utility). Correlates child lifetimes with Chromium log events
without debugger attachment, privilege escalation, or Wine source modifications."""
import json
import os
from pathlib import Path
import re
import time

def parse_cmdline(raw_bytes):
    """Parse null-delimited cmdline bytes into argument list and classification."""
    if not raw_bytes:
        return [], 'unknown'
    args = [a.decode('utf-8', errors='replace') for a in raw_bytes.split(b'\0') if a]
    if not args:
        return [], 'unknown'

    # Check for chrome.exe
    exe = Path(args[0]).name.lower()
    if 'chrome' not in exe and 'wine' not in exe:
        return args, 'foreign'

    # Classify role based on arguments
    type_arg = None
    subtype_arg = None
    is_extension = False
    for a in args:
        if a.startswith('--type='):
            type_arg = a.split('=', 1)[1]
        elif a.startswith('--utility-sub-type='):
            subtype_arg = a.split('=', 1)[1]
        elif a == '--extension-process':
            is_extension = True

    if type_arg is None:
        role = 'browser_main'
    elif type_arg == 'renderer':
        role = 'extension_renderer' if is_extension else 'content_renderer'
    elif type_arg == 'gpu-process':
        role = 'gpu_process'
    elif type_arg == 'crashpad-handler':
        role = 'crashpad_handler'
    elif type_arg == 'utility':
        if subtype_arg:
            if 'network' in subtype_arg.lower():
                role = 'network_service'
            elif 'storage' in subtype_arg.lower():
                role = 'storage_service'
            else:
                role = f'utility_{subtype_arg}'
        else:
            role = 'utility'
    else:
        role = f'type_{type_arg}'

    return args, role


def parse_stat(raw_text):
    """Parse Linux /proc/[pid]/stat into pid, comm, state, ppid."""
    if not raw_text:
        return None
    # Format: pid (comm) state ppid ...
    match = re.match(r'^(\d+)\s+\((.*)\)\s+([RSDZTW])\s+(\d+)', raw_text)
    if not match:
        return None
    return {
        'pid': int(match.group(1)),
        'comm': match.group(2),
        'state': match.group(3),
        'ppid': int(match.group(4))
    }


class ChildObserver:
    def __init__(self, output_dir, *, now=time.monotonic, proc_root=Path('/proc')):
        self.output_dir = Path(output_dir)
        self.now = now
        self.proc_root = proc_root
        self.start_time = now()
        self.observed = {}  # pid -> record
        self.events = []

    def sample(self):
        t = self.now()
        current_pids = set()

        if not self.proc_root.exists():
            return

        try:
            entries = os.listdir(str(self.proc_root))
        except Exception:
            return

        for name in entries:
            if not name.isdigit():
                continue
            pid = int(name)
            pid_dir = self.proc_root / name
            try:
                # Read cmdline
                cmdline_path = pid_dir / 'cmdline'
                with cmdline_path.open('rb') as f:
                    cmd_bytes = f.read(4096)
                if not cmd_bytes:
                    continue

                args, role = parse_cmdline(cmd_bytes)
                if role == 'foreign':
                    continue

                # Read stat
                stat_path = pid_dir / 'stat'
                with stat_path.open('r', encoding='utf-8', errors='replace') as f:
                    stat_text = f.read(1024)
                stat_info = parse_stat(stat_text)
                if not stat_info:
                    continue

                current_pids.add(pid)

                if pid not in self.observed:
                    rec = {
                        'pid': pid,
                        'ppid': stat_info['ppid'],
                        'comm': stat_info['comm'],
                        'role': role,
                        'cmdline_snippet': ' '.join(args[:8]),
                        'first_seen_monotonic': t,
                        'last_seen_monotonic': t,
                        'first_state': stat_info['state'],
                        'last_state': stat_info['state'],
                        'alive': True
                    }
                    self.observed[pid] = rec
                    self.events.append({
                        't': round(t - self.start_time, 3),
                        'event': 'spawn',
                        'pid': pid,
                        'ppid': stat_info['ppid'],
                        'role': role
                    })
                else:
                    rec = self.observed[pid]
                    rec['last_seen_monotonic'] = t
                    rec['last_state'] = stat_info['state']
                    if stat_info['state'] == 'Z' and not rec.get('zombie_seen'):
                        rec['zombie_seen'] = t
                        self.events.append({
                            't': round(t - self.start_time, 3),
                            'event': 'zombie',
                            'pid': pid,
                            'role': rec['role']
                        })
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                continue
            except Exception:
                continue

        # Check for processes that vanished
        for pid, rec in self.observed.items():
            if rec['alive'] and pid not in current_pids:
                rec['alive'] = False
                rec['exit_observed_monotonic'] = t
                rec['survival_seconds'] = round(t - rec['first_seen_monotonic'], 3)
                self.events.append({
                    't': round(t - self.start_time, 3),
                    'event': 'vanished',
                    'pid': pid,
                    'role': rec['role'],
                    'survival_seconds': rec['survival_seconds']
                })

    def parse_chromium_log(self):
        log_file = self.output_dir / 'chromium.log'
        if not log_file.exists():
            return []
        log_events = []
        try:
            with log_file.open('r', encoding='utf-8', errors='replace') as f:
                for line in f:
                    line_s = line.strip()
                    if 'SadTab: Tab Killed:' in line_s or 'SadTab' in line_s:
                        log_events.append({'type': 'sad_tab', 'line': line_s})
                    elif 'network_service_instance_impl' in line_s:
                        log_events.append({'type': 'network_service_event', 'line': line_s})
                    elif 'Created script context' in line_s:
                        log_events.append({'type': 'script_context', 'line': line_s})
                    elif 'terminat' in line_s.lower() or 'crash' in line_s.lower() or 'killed' in line_s.lower():
                        log_events.append({'type': 'termination_mention', 'line': line_s[:200]})
        except Exception as error:
            log_events.append({'type': 'parse_error', 'error': repr(error)})
        return log_events

    def finish(self):
        self.sample()
        end_t = self.now()
        for pid, rec in self.observed.items():
            if rec['alive']:
                rec['alive_at_finish'] = True
                rec['survival_seconds'] = round(end_t - rec['first_seen_monotonic'], 3)

        chromium_events = self.parse_chromium_log()

        # Build clean summary
        summary = {
            'total_processes_observed': len(self.observed),
            'roles_observed': sorted(list({r['role'] for r in self.observed.values()})),
            'processes': sorted(list(self.observed.values()), key=lambda p: p['pid']),
            'lifecycle_events': self.events,
            'chromium_log_events': chromium_events
        }

        # Save structured artifact
        try:
            out_file = self.output_dir / 'child-exit-diagnostic.json'
            with out_file.open('w', encoding='utf-8') as f:
                json.dump(summary, f, indent=2)
        except Exception:
            pass

        return summary
