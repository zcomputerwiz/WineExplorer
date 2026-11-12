#!/usr/bin/env python3
# WineExplorer Desktop Environment - registry request statistics from a +server trace
#
# Copyright 2026 WineExplorer Desktop Environment Project
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""Reads a WINEDEBUG=+server trace on stdin and prints, for the system tier design (ST-01, ST-03):
all requests, registry requests by tree (Machine, User, other), and the key-handle cases that would cross
servers (duplicated, inheritable, watched). Key handles are followed per process from open/create to close.

Usage: wine program 2>&1 >/dev/null | registry-trace-stats.py LABEL
"""
import re
import sys

REQ = re.compile(r"^([0-9a-f]{4}): (\w+)\((.*)$")
REPLY = re.compile(r"^([0-9a-f]{4}): (\w+)\(\) = (\S+)(.*)$")
KEY_REQUESTS = {"create_key", "open_key", "delete_key", "flush_key", "enum_key", "set_key_value", "get_key_value",
                "enum_key_value", "delete_key_value", "load_registry", "unload_registry", "save_registry",
                "set_registry_notification", "rename_key", "create_key_transacted"}


def field(text, name):
    m = re.search(r"\b%s=([0-9a-fx]+)" % name, text)
    return int(m.group(1), 16) if m else None


def tree(path):
    low = path.lower()
    if low.startswith("\\registry\\machine"):
        return "machine"
    if low.startswith("\\registry\\user"):
        return "user"
    return "other"


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "trace"
    pid_of = {}                 # tid -> pid
    keys = {}                   # (pid, handle) -> path
    pending = {}                # tid -> (request, parent path, name, inheritable)
    total = registry = 0
    by_tree = {"machine": 0, "user": 0, "other": 0}
    by_request = {}
    writes = {"machine": 0, "user": 0, "other": 0}
    dup = inherit = watch = watch_machine = 0
    processes = set()

    for raw in sys.stdin.buffer:
        line = raw.decode("utf-8", "replace").rstrip()
        m = REPLY.match(line)
        if m:
            tid, name, status, rest = m.groups()
            if name == "init_first_thread" and status == "0":
                pid = field(rest, "pid")
                if pid is not None:
                    pid_of[tid] = pid
                    processes.add(pid)
            elif name == "new_thread" and status == "0":
                new = re.search(r"\btid=([0-9a-f]+)", rest)
                if new and tid in pid_of:
                    pid_of.setdefault(new.group(1).rjust(4, "0"), pid_of[tid])
            elif name in ("create_key", "open_key") and tid in pending:
                req, parent, sub, inh = pending.pop(tid)
                hkey = field(rest, "hkey")
                if status in ("0", "OBJECT_NAME_EXISTS") or hkey:
                    if hkey:
                        keys[(pid_of.get(tid, tid), hkey)] = parent + sub
            continue
        m = REQ.match(line)
        if not m:
            continue
        tid, name, rest = m.groups()
        total += 1
        pid = pid_of.get(tid, tid)
        if name in ("create_key", "open_key"):
            parent = field(rest, "parent") or field(rest, "rootdir") or 0
            nm = re.search(r'name=L"((?:[^"\\]|\\.)*)"', rest)
            sub = nm.group(1).replace("\\\\", "\\") if nm else ""
            base = keys.get((pid, parent), "") if parent else ""
            if base and sub and not sub.startswith("\\"):
                sub = "\\" + sub
            attributes = field(rest, "attributes") or 0
            if attributes & 2:
                inherit += 1
            pending[tid] = (name, base, sub, bool(attributes & 2))
            t = tree(base + sub)
            registry += 1
            by_tree[t] += 1
            by_request[name] = by_request.get(name, 0) + 1
            if name == "create_key":
                writes[t] += 1
        elif name in KEY_REQUESTS:
            hkey = field(rest, "hkey")
            t = tree(keys.get((pid, hkey), ""))
            registry += 1
            by_tree[t] += 1
            by_request[name] = by_request.get(name, 0) + 1
            if name in ("set_key_value", "delete_key", "delete_key_value", "rename_key"):
                writes[t] += 1
            if name == "set_registry_notification":
                watch += 1
                if t == "machine":
                    watch_machine += 1
        elif name == "close_handle":
            keys.pop((pid, field(rest, "handle")), None)
        elif name == "dup_handle":
            if (pid, field(rest, "src_handle")) in keys:
                dup += 1

    share = (100.0 * registry / total) if total else 0.0
    print("%s: processes=%d requests=%d registry=%d (%.1f%%) machine=%d user=%d other=%d"
          % (label, len(processes), total, registry, share, by_tree["machine"], by_tree["user"], by_tree["other"]))
    print("%s: writes machine=%d user=%d other=%d" % (label, writes["machine"], writes["user"], writes["other"]))
    print("%s: key handles duplicated=%d opened inheritable=%d watched=%d (machine %d)"
          % (label, dup, inherit, watch, watch_machine))
    print("%s: by request %s" % (label, " ".join("%s=%d" % kv for kv in sorted(by_request.items(), key=lambda kv: -kv[1]))))


if __name__ == "__main__":
    main()
