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

With --writes (ST-05) it also lists the machine-tree writes by key path, each classified as a per-session
key ("session") or a session-0 key ("session0"), using the per-session key lists from wine/wex_session0.h
(--header selects the header).

Usage: wine program 2>&1 >/dev/null | registry-trace-stats.py LABEL [--writes] [--header PATH]
"""
import argparse
import os
import re
import sys

REQ = re.compile(r"^([0-9a-f]{4}): (\w+)\((.*)$")
REPLY = re.compile(r"^([0-9a-f]{4}): (\w+)\(\) = (\S+)(.*)$")
KEY_REQUESTS = {"create_key", "open_key", "delete_key", "flush_key", "enum_key", "set_key_value", "get_key_value",
                "enum_key_value", "delete_key_value", "load_registry", "unload_registry", "save_registry",
                "set_registry_notification", "rename_key", "create_key_transacted"}

MACHINE_ROOT = "\\Registry\\Machine"
DEFAULT_USER_ROOT = "\\Registry\\User\\.Default"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
DEFAULT_HEADER = os.path.join(REPO_ROOT, "wine", "include", "wine", "wex_session0.h")


def field(text, name):
    m = re.search(r"\b%s=([0-9a-fx]+)" % name, text)
    return int(m.group(1), 16) if m else None


def name_field(text):
    nm = re.search(r'name=L"((?:[^"\\]|\\.)*)"', text)
    return nm.group(1).replace("\\\\", "\\") if nm else None


def tree(path):
    low = path.lower()
    if low.startswith("\\registry\\machine"):
        return "machine"
    if low.startswith("\\registry\\user"):
        return "user"
    return "other"


def parse_macro(text, macro):
    m = re.search(r"#define\s+%s\b" % re.escape(macro), text)
    if not m:
        return []
    lines = text[m.end():].split("\n")
    block = []
    for line in lines:
        block.append(line)
        if not line.rstrip().endswith("\\"):
            break
    strings = re.findall(r'"((?:[^"\\]|\\.)*)"', "\n".join(block))
    return [s.replace("\\\\", "\\") for s in strings]


def load_session_keys(header_path):
    try:
        with open(header_path, "r", encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return [], []
    return (parse_macro(text, "WEX_SESSION_MACHINE_KEYS"),
            parse_macro(text, "WEX_SESSION_DEFAULT_USER_KEYS"))


def classify(path, machine_keys, default_user_keys):
    low = path.lower()
    if low.startswith(MACHINE_ROOT.lower()):
        rel = path[len(MACHINE_ROOT):].lstrip("\\").lower()
        entries = machine_keys
    elif low.startswith(DEFAULT_USER_ROOT.lower()):
        rel = path[len(DEFAULT_USER_ROOT):].lstrip("\\").lower()
        entries = default_user_keys
    else:
        return "session0"
    for entry in entries:
        e = entry.lower()
        if rel == e or rel.startswith(e + "\\"):
            return "session"
    return "session0"


def parse_trace(lines):
    pid_of = {}                 # tid -> pid
    keys = {}                   # (pid, handle) -> path
    pending = {}                # tid -> (request, parent path, name, inheritable)
    total = registry = 0
    by_tree = {"machine": 0, "user": 0, "other": 0}
    by_request = {}
    writes = {"machine": 0, "user": 0, "other": 0}
    dup = inherit = watch = watch_machine = 0
    processes = set()
    write_events = []           # (kind, path, value name or None)

    for line in lines:
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
                    if req == "create_key" and status in ("0", "OBJECT_NAME_EXISTS"):
                        kind = "create_key created" if status == "0" else "create_key existed"
                        write_events.append((kind, parent + sub, None))
            continue
        m = REQ.match(line)
        if not m:
            continue
        tid, name, rest = m.groups()
        total += 1
        pid = pid_of.get(tid, tid)
        if name in ("create_key", "open_key"):
            parent = field(rest, "parent") or field(rest, "rootdir") or 0
            sub = name_field(rest) or ""
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
            path = keys.get((pid, hkey), "")
            t = tree(path)
            registry += 1
            by_tree[t] += 1
            by_request[name] = by_request.get(name, 0) + 1
            if name in ("set_key_value", "delete_key", "delete_key_value", "rename_key"):
                writes[t] += 1
            if name in ("set_key_value", "delete_key_value", "delete_key"):
                vname = name_field(rest) if name in ("set_key_value", "delete_key_value") else None
                write_events.append((name, path, vname))
            if name == "set_registry_notification":
                watch += 1
                if t == "machine":
                    watch_machine += 1
        elif name == "close_handle":
            keys.pop((pid, field(rest, "handle")), None)
        elif name == "dup_handle":
            if (pid, field(rest, "src_handle")) in keys:
                dup += 1

    return {
        "processes": processes,
        "total": total,
        "registry": registry,
        "by_tree": by_tree,
        "by_request": by_request,
        "writes": writes,
        "dup": dup,
        "inherit": inherit,
        "watch": watch,
        "watch_machine": watch_machine,
        "write_events": write_events,
    }


def format_writes(stats, machine_keys, default_user_keys, label):
    agg = {}
    for kind, path, vname in stats["write_events"]:
        # the machine trees session 0 holds: \Registry\Machine and \Registry\User\.Default
        if tree(path) != "machine" and not path.lower().startswith(DEFAULT_USER_ROOT.lower()):
            continue
        key = (kind, path, vname)
        agg[key] = agg.get(key, 0) + 1
    items = sorted(agg.items(),
                   key=lambda kv: (-kv[1], kv[0][1], kv[0][0], "" if kv[0][2] is None else kv[0][2]))
    lines = ["%s: machine writes by key (session = per-session key, session0 = session 0):" % label]
    for (kind, path, vname), count in items:
        cls = classify(path, machine_keys, default_user_keys)
        line = "  %d %s %s %s" % (count, cls, kind, path)
        if vname is not None:
            line += " value=" + vname
        lines.append(line)
    return lines


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("label", nargs="?", default="trace")
    parser.add_argument("--writes", action="store_true",
                        help="list the machine-tree writes by key path, classified session/session0")
    parser.add_argument("--header", default=DEFAULT_HEADER,
                        help="path to wex_session0.h with the per-session key lists")
    args = parser.parse_args(argv)

    lines = []
    for raw in sys.stdin.buffer:
        lines.append(raw.decode("utf-8", "replace").rstrip())
    stats = parse_trace(lines)

    share = (100.0 * stats["registry"] / stats["total"]) if stats["total"] else 0.0
    print("%s: processes=%d requests=%d registry=%d (%.1f%%) machine=%d user=%d other=%d"
          % (args.label, len(stats["processes"]), stats["total"], stats["registry"], share,
             stats["by_tree"]["machine"], stats["by_tree"]["user"], stats["by_tree"]["other"]))
    print("%s: writes machine=%d user=%d other=%d"
          % (args.label, stats["writes"]["machine"], stats["writes"]["user"], stats["writes"]["other"]))
    print("%s: key handles duplicated=%d opened inheritable=%d watched=%d (machine %d)"
          % (args.label, stats["dup"], stats["inherit"], stats["watch"], stats["watch_machine"]))
    print("%s: by request %s"
          % (args.label, " ".join("%s=%d" % kv for kv in sorted(stats["by_request"].items(), key=lambda kv: -kv[1]))))
    if args.writes:
        machine_keys, default_user_keys = load_session_keys(args.header)
        for line in format_writes(stats, machine_keys, default_user_keys, args.label):
            print(line)


if __name__ == "__main__":
    main()
