#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""org.wineexplorer.TimeSync1: the time servers of Date and Time's Internet Time tab (timedate.cpl), on the system
bus, as root.

timedated can turn network time on and off (SetNTP) but cannot name a server, and systemd-timesyncd reads its servers
from files. This service writes the drop-in /etc/systemd/timesyncd.conf.d/90-wineexplorer.conf (NTP=) and restarts
systemd-timesyncd when it runs (try-restart: a stopped one stays stopped), which synchronises at once when it
starts: that is also "Update Now", refused while network time is off.

- Servers (property): the servers in effect, as a space separated string: ours when set, else the NTP= of the system's
  timesyncd.conf and its drop-ins; empty means the distribution's defaults (FallbackNTP).
- SetServers(servers, interactive): servers is a space separated list of host names or addresses, at most 8; an empty
  string removes our drop-in (the system's defaults apply again).
- SyncNow(interactive): restart systemd-timesyncd; an error while network time is off.

Reading needs nothing. Changing needs polkit's org.wineexplorer.timesync1.set-servers (auth_admin_keep), the rule of
timedate1's own changes. D-Bus activated (wex-timesync1.service); it ends after a minute without calls. Installed as
/usr/local/sbin/wex-timesync1. `wex-timesync1 --get` prints the servers. WEX_TIMESYNC_DIR and WEX_SYSTEMCTL let the
tests use other paths.
"""

import os
import re
import subprocess
import sys

for _dir in (os.path.dirname(os.path.realpath(__file__)), "/usr/local/lib/wineexplorer/python"):
    if _dir not in sys.path:
        sys.path.insert(0, _dir)

from wexcommon import atomic_write
import wexrootsvc
from wexrootsvc import Failed, Refused, refusal  # noqa: F401 (refusal: the tests read it here)

BUS_NAME = "org.wineexplorer.TimeSync1"
OBJECT_PATH = "/org/wineexplorer/TimeSync1"
INTERFACE = BUS_NAME
ACTION = "org.wineexplorer.timesync1.set-servers"
CONF_DIR = os.environ.get("WEX_TIMESYNC_DIR", "/etc/systemd")
SYSTEMCTL = os.environ.get("WEX_SYSTEMCTL", "systemctl")
DROPIN = "timesyncd.conf.d/90-wineexplorer.conf"
UNIT = "systemd-timesyncd.service"
MAX_SERVERS = 8
# a host name (labels of letters, digits and hyphens) or an address
_LABEL = r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
_HOST = re.compile(r"^" + _LABEL + r"(?:\." + _LABEL + r")*\.?$")
_IPV6 = re.compile(r"^[0-9A-Fa-f:.]{2,45}$")


def check_servers(text):
    """The servers of a space separated list, or Refused. An empty list is allowed (it removes our setting)."""
    if not isinstance(text, str) or len(text) > 1024:
        raise Refused("a list of at most 1024 characters")
    names = text.split()
    if len(names) > MAX_SERVERS:
        raise Refused(f"at most {MAX_SERVERS} servers")
    for s in names:
        if len(s) > 253 or not (_HOST.match(s) or (":" in s and _IPV6.match(s))):
            raise Refused(f"not a host name or address: {s!r}")
    return names


def _ntp_of(path):
    """The last NTP= of a file's [Time] section, or None."""
    value = None
    section = None
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line.startswith("[") and line.endswith("]"):
                    section = line[1:-1]
                elif section == "Time" and line.startswith("NTP="):
                    value = line[4:].strip()
    except OSError:
        return None
    return value


def servers(conf_dir=None):
    """The servers in effect: later files win (the system's file, its drop-ins in name order, ours last)."""
    conf_dir = conf_dir or CONF_DIR
    paths = [os.path.join(conf_dir, "timesyncd.conf")]
    d = os.path.join(conf_dir, "timesyncd.conf.d")
    try:
        paths += [os.path.join(d, n) for n in sorted(os.listdir(d)) if n.endswith(".conf")]
    except OSError:
        pass
    value = ""
    for p in paths:
        v = _ntp_of(p)
        if v is not None:
            value = v
    return value


def set_servers(text, conf_dir=None):
    conf_dir = conf_dir or CONF_DIR
    check_servers(text)
    path = os.path.join(conf_dir, DROPIN)
    names = " ".join(text.split())
    if not names:
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
        return
    atomic_write(path, f"# written by Date and Time (wex-timesync1)\n[Time]\nNTP={names}\n".encode("utf-8"))


def run_systemctl(*args, run=None):
    run = run or subprocess.run
    try:
        r = run([SYSTEMCTL, *args], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError) as exc:
        raise Failed(f"systemctl: {exc}")
    if r.returncode != 0:
        raise Failed((r.stderr or r.stdout or f"systemctl exited {r.returncode}").strip())


def reload_servers(run=None):
    """Make a running timesyncd read the new servers. try-restart: a stopped unit stays stopped, because timedated's
    SetNTP(false) stopped it on purpose and the servers can be edited while synchronisation is off."""
    run_systemctl("try-restart", UNIT, run=run)


def sync_now(run=None):
    """Synchronise now: a running timesyncd is restarted (it synchronises when it starts). Refused while network time
    is off: starting the unit would run it against the user's setting."""
    run = run or subprocess.run
    try:
        r = run([SYSTEMCTL, "is-active", "--quiet", UNIT], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError) as exc:
        raise Failed(f"systemctl: {exc}")
    if r.returncode != 0:
        raise Failed("network time synchronization is turned off")
    run_systemctl("try-restart", UNIT, run=run)


def define(dbus, base, service):
    """The service's object (wexrootsvc.serve): its methods and properties."""
    def change_servers(text):
        set_servers(text)
        reload_servers()   # a running timesyncd reads its files when it starts

    class TimeSync1(base):
        @dbus.service.method(INTERFACE, in_signature="sb", out_signature="", sender_keyword="sender",
                             async_callbacks=("ok", "fail"))
        def SetServers(self, text, interactive, sender=None, ok=None, fail=None):
            text = str(text)
            service.change(sender, interactive, ok, fail, lambda: change_servers(text),
                           check=lambda: check_servers(text))

        @dbus.service.method(INTERFACE, in_signature="b", out_signature="", sender_keyword="sender",
                             async_callbacks=("ok", "fail"))
        def SyncNow(self, interactive, sender=None, ok=None, fail=None):
            service.change(sender, interactive, ok, fail, lambda: sync_now())

        def properties(self):
            return {"Servers": dbus.String(servers())}

    return TimeSync1


def serve():
    return wexrootsvc.serve(BUS_NAME, OBJECT_PATH, ACTION, "not authorized to change the time servers", define)


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if args == ["--get"]:
        print(servers())
        return 0
    if args:
        print("usage: wex-timesync1 [--get]", file=sys.stderr)
        return 2
    return serve()


if __name__ == "__main__":
    sys.exit(main())
