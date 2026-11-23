#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""org.wineexplorer.TimeSync1: the time servers of Date and Time's Internet Time tab (timedate.cpl), on the system
bus, as root.

timedated can turn network time on and off (SetNTP) but cannot name a server, and systemd-timesyncd reads its servers
from files. This service writes the drop-in /etc/systemd/timesyncd.conf.d/90-wineexplorer.conf (NTP=) and restarts
systemd-timesyncd, which synchronises at once when it starts: that is also "Update Now".

- Servers (property): the servers in effect, as a space separated string: ours when set, else the NTP= of the system's
  timesyncd.conf and its drop-ins; empty means the distribution's defaults (FallbackNTP).
- SetServers(servers, interactive): servers is a space separated list of host names or addresses, at most 8; an empty
  string removes our drop-in (the system's defaults apply again).
- SyncNow(interactive): restart systemd-timesyncd.

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

BUS_NAME = "org.wineexplorer.TimeSync1"
OBJECT_PATH = "/org/wineexplorer/TimeSync1"
INTERFACE = BUS_NAME
ACTION = "org.wineexplorer.timesync1.set-servers"
CONF_DIR = os.environ.get("WEX_TIMESYNC_DIR", "/etc/systemd")
SYSTEMCTL = os.environ.get("WEX_SYSTEMCTL", "systemctl")
DROPIN = "timesyncd.conf.d/90-wineexplorer.conf"
UNIT = "systemd-timesyncd.service"
MAX_SERVERS = 8
IDLE_SECONDS = 60
# a host name (labels of letters, digits and hyphens) or an address
_LABEL = r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
_HOST = re.compile(r"^" + _LABEL + r"(?:\." + _LABEL + r")*\.?$")
_IPV6 = re.compile(r"^[0-9A-Fa-f:.]{2,45}$")


class Refused(Exception):
    """A value the service does not take (D-Bus error InvalidArgs)."""


class Failed(Exception):
    """A change that did not happen (D-Bus error Failed)."""


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


def sync_now(run=None):
    run_systemctl("restart", UNIT, run=run)


def refusal(result):
    """Cancelled when the password dialog was dismissed (polkit.dismissed), else NotAuthorized."""
    details = result[2] if len(result) > 2 else {}
    return "Cancelled" if str(details.get("polkit.dismissed", "")) == "true" else "NotAuthorized"


def serve():
    import dbus
    import dbus.mainloop.glib
    import dbus.service
    from gi.repository import GLib
    import time

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    name = dbus.service.BusName(BUS_NAME, bus, do_not_queue=True)
    loop = GLib.MainLoop()
    state = {"busy": 0, "last": time.monotonic()}

    def error(kind, message):
        return dbus.exceptions.DBusException(str(message), name=f"{INTERFACE}.Error.{kind}")

    def as_dbus(exc):
        if isinstance(exc, dbus.exceptions.DBusException):
            return exc
        if isinstance(exc, Refused):
            return error("InvalidArgs", exc)
        return error("Failed", exc)

    class TimeSync1(dbus.service.Object):
        def authorized(self, sender, interactive, then, fail):
            """polkit's CheckAuthorization for the caller, without blocking the loop while the password dialog waits."""
            state["busy"] += 1

            def done(result):
                state["busy"] -= 1
                state["last"] = time.monotonic()
                if not result[0]:
                    fail(error(refusal(result), "not authorized to change the time servers"))
                    return
                try:
                    then()
                except Exception as exc:  # report it to the caller; the service goes on
                    fail(as_dbus(exc))

            def failed(exc):
                state["busy"] -= 1
                state["last"] = time.monotonic()
                fail(as_dbus(exc))

            authority = bus.get_object("org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority")
            subject = dbus.Struct((dbus.String("system-bus-name"),
                                   dbus.Dictionary({"name": dbus.String(sender, variant_level=1)}, signature="sv")),
                                  signature="sa{sv}")
            authority.CheckAuthorization(subject, ACTION, dbus.Dictionary({}, signature="ss"),
                                         dbus.UInt32(1 if interactive else 0), "",
                                         dbus_interface="org.freedesktop.PolicyKit1.Authority", timeout=24 * 3600,
                                         reply_handler=done, error_handler=failed)

        @dbus.service.method(INTERFACE, in_signature="sb", out_signature="", sender_keyword="sender",
                             async_callbacks=("ok", "fail"))
        def SetServers(self, text, interactive, sender=None, ok=None, fail=None):
            try:
                check_servers(str(text))
            except Refused as exc:
                fail(as_dbus(exc))
                return

            def change():
                set_servers(str(text))
                sync_now()   # timesyncd reads its files when it starts
                ok()
            self.authorized(sender, interactive, change, fail)

        @dbus.service.method(INTERFACE, in_signature="b", out_signature="", sender_keyword="sender",
                             async_callbacks=("ok", "fail"))
        def SyncNow(self, interactive, sender=None, ok=None, fail=None):
            self.authorized(sender, interactive, lambda: (sync_now(), ok()), fail)

        @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="s", out_signature="a{sv}")
        def GetAll(self, interface):
            state["last"] = time.monotonic()
            if interface not in (INTERFACE, ""):
                raise error("InvalidArgs", f"no interface {interface}")
            return dbus.Dictionary({"Servers": dbus.String(servers())}, signature="sv")

        @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ss", out_signature="v")
        def Get(self, interface, prop):
            values = self.GetAll(interface)
            if prop not in values:
                raise error("InvalidArgs", f"no property {prop}")
            return values[prop]

    TimeSync1(bus, OBJECT_PATH)

    def idle():
        if not state["busy"] and time.monotonic() - state["last"] >= IDLE_SECONDS:
            loop.quit()
            return False
        return True

    GLib.timeout_add_seconds(10, idle)
    loop.run()
    del name
    return 0


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
