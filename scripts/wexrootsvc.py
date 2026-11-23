#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""The frame of WineExplorer's root services on the system bus (org.wineexplorer.Boot1, org.wineexplorer.TimeSync1):
what each of them does the same way, so that a service holds only its own methods and properties.

- Errors: a service raises Refused for a value it does not take and Failed for a change that did not happen. The
  caller sees org.wineexplorer.NAME.Error.InvalidArgs or .Failed with the message, and .NotAuthorized or .Cancelled
  when polkit says no. wexhost (src/dlls/wexhost/unixlib.c, map_error) maps these four kinds the same way for every
  service whose name starts with org.wineexplorer.
- Changes need the service's polkit action (auth_admin_keep, the rule of hostname1 and timedate1: no second notion
  of administrator), checked for the caller's bus name without blocking the loop while the password dialog waits.
  The arguments are checked first, so that a value the service would refuse is answered before a password is asked.
- Reading needs nothing: Get and GetAll come from the object's properties().
- D-Bus activated: the service ends after a minute without calls, never while a change waits for polkit.

dbus and GLib are imported by serve() only, so that the services' own functions are tested without them.
"""

import os
import time

IDLE_SECONDS = int(os.environ.get("WEX_ROOTSVC_IDLE", "60"))
POLKIT_TIMEOUT = 24 * 3600   # seconds: the password dialog may stay open as long as the user likes


class Refused(Exception):
    """A value the service does not take (D-Bus error InvalidArgs)."""


class Failed(Exception):
    """A change that did not happen (D-Bus error Failed); the message says why."""


def refusal(result):
    """The kind of D-Bus error for a CheckAuthorization result (authorized, challenge, details) that is not
    authorized: Cancelled when the password dialog was dismissed (polkit.dismissed), so that the dialog that asked
    stays open without an error message; otherwise NotAuthorized."""
    details = result[2] if len(result) > 2 else {}
    return "Cancelled" if str(details.get("polkit.dismissed", "")) == "true" else "NotAuthorized"


class Service:
    """What a service's methods use: error(), change() and the interface name. Made by serve()."""

    def __init__(self, dbus, bus, interface, action, denied):
        self.dbus = dbus
        self.bus = bus
        self.interface = interface
        self.action = action
        self.denied = denied
        self.waiting = 0
        self.last = time.monotonic()

    def touch(self):
        self.last = time.monotonic()

    def idle_for(self):
        """Seconds since the last call; 0 while a change waits for polkit."""
        return 0 if self.waiting else time.monotonic() - self.last

    def error(self, kind, message):
        return self.dbus.exceptions.DBusException(str(message), name=f"{self.interface}.Error.{kind}")

    def as_dbus(self, exc):
        if isinstance(exc, self.dbus.exceptions.DBusException):
            return exc
        if isinstance(exc, Refused):
            return self.error("InvalidArgs", exc)
        return self.error("Failed", exc)

    def change(self, sender, interactive, ok, fail, act, check=None):
        """A change for the caller sender: check() first (an exception answers at once, before any password), then
        polkit's action, then act(). act returns None or a tuple of D-Bus values, the method's reply. ok and fail are
        the method's async callbacks."""
        self.touch()
        if check is not None:
            try:
                check()
            except Exception as exc:   # Refused, or a value of the wrong type
                fail(self.as_dbus(exc))
                return
        self.waiting += 1

        def done(result):
            self.waiting -= 1
            self.touch()
            if not result[0]:
                fail(self.error(refusal(result), self.denied))
                return
            try:
                reply = act()
            except Exception as exc:   # reported to the caller; the service goes on
                fail(self.as_dbus(exc))
                return
            ok(*(reply or ()))

        def failed(exc):
            self.waiting -= 1
            self.touch()
            fail(self.as_dbus(exc))

        dbus = self.dbus
        authority = self.bus.get_object("org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority")
        subject = dbus.Struct((dbus.String("system-bus-name"),
                               dbus.Dictionary({"name": dbus.String(sender, variant_level=1)}, signature="sv")),
                              signature="sa{sv}")
        authority.CheckAuthorization(subject, self.action, dbus.Dictionary({}, signature="ss"),
                                     dbus.UInt32(1 if interactive else 0), "",
                                     dbus_interface="org.freedesktop.PolicyKit1.Authority", timeout=POLKIT_TIMEOUT,
                                     reply_handler=done, error_handler=failed)


def serve(bus_name, object_path, action, denied, define, idle_seconds=IDLE_SECONDS):
    """Own bus_name (also the interface's name) and serve at object_path the object whose class define(dbus, base,
    service) returns, until no call came for idle_seconds. base is a dbus.service.Object whose Get and GetAll read
    the object's properties() (a dict of D-Bus typed values); service is a Service. action is the polkit action of
    every change, denied the message of polkit's refusal."""
    import dbus
    import dbus.mainloop.glib
    import dbus.service
    from gi.repository import GLib

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    # the tests run a service on a private session bus, with a polkit of their own there
    bus = dbus.SessionBus() if os.environ.get("WEX_ROOTSVC_BUS") == "session" else dbus.SystemBus()
    name = dbus.service.BusName(bus_name, bus, do_not_queue=True)
    loop = GLib.MainLoop()
    service = Service(dbus, bus, bus_name, action, denied)

    class Base(dbus.service.Object):
        def properties(self):
            return {}

        @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="s", out_signature="a{sv}")
        def GetAll(self, interface):
            service.touch()
            if interface not in (service.interface, ""):
                raise service.error("InvalidArgs", f"no interface {interface}")
            return dbus.Dictionary(self.properties(), signature="sv")

        @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ss", out_signature="v")
        def Get(self, interface, prop):
            values = self.GetAll(interface)
            if prop not in values:
                raise service.error("InvalidArgs", f"no property {prop}")
            return values[prop]

    define(dbus, Base, service)(bus, object_path)

    def idle():
        if service.idle_for() >= idle_seconds:
            loop.quit()
            return False
        return True

    GLib.timeout_add_seconds(10, idle)
    loop.run()
    del name
    return 0
