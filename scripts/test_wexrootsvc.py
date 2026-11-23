#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Tests of scripts/wexrootsvc.py, the frame of the root services (Boot1, TimeSync1).

- ChangeTest: Service.change with a fake dbus module and a fake polkit (no bus needed): the order (arguments, then
  polkit, then the work), the error kinds and the reply.
- BusTest: TimeSync1 itself on a private session bus (dbus-run-session) with a polkit of the test's own there: the
  methods the service declares on the frame's base class, Get and GetAll, the D-Bus error names that wexhost maps,
  and the idle exit. Skipped without dbus-run-session, python3-dbus or GLib.
"""

import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wexrootsvc  # noqa: E402


class FakeDBusException(Exception):
    def __init__(self, message, name=None):
        super().__init__(message)
        self.name = name

    def get_dbus_name(self):
        return self.name


class FakeDBus:
    """What Service uses of the dbus module."""
    class exceptions:
        DBusException = FakeDBusException

    @staticmethod
    def Struct(value, signature=None):
        return value

    @staticmethod
    def Dictionary(value, signature=None):
        return dict(value)

    @staticmethod
    def String(value, variant_level=0):
        return str(value)

    @staticmethod
    def UInt32(value):
        return int(value)


class FakeAuthority:
    def __init__(self, result=(True, False, {}), error=None):
        self.result = result
        self.error = error
        self.calls = []
        self.pending = None

    def CheckAuthorization(self, subject, action, details, flags, cancel_id, **kw):
        self.calls.append((subject, action, flags))
        self.pending = (kw["reply_handler"], kw["error_handler"])

    def answer(self):
        reply, error = self.pending
        self.pending = None
        if self.error is not None:
            error(self.error)
        else:
            reply(self.result)


class FakeBus:
    def __init__(self, authority):
        self.authority = authority

    def get_object(self, name, path):
        assert name == "org.freedesktop.PolicyKit1"
        return self.authority


class ChangeTest(unittest.TestCase):
    def setUp(self):
        self.authority = FakeAuthority()
        self.service = wexrootsvc.Service(FakeDBus, FakeBus(self.authority), "org.wineexplorer.Test1",
                                          "org.wineexplorer.test1.change", "not authorized to test")
        self.replies = []
        self.errors = []

    def change(self, act, check=None, interactive=True):
        self.service.change(":1.42", interactive, lambda *a: self.replies.append(a), self.errors.append, act, check)

    def test_a_refused_value_is_answered_before_polkit(self):
        def check():
            raise wexrootsvc.Refused("not a value")
        self.change(lambda: self.fail("the work ran"), check)
        self.assertEqual(self.authority.calls, [])
        self.assertEqual(self.errors[0].name, "org.wineexplorer.Test1.Error.InvalidArgs")
        self.assertEqual(str(self.errors[0]), "not a value")

    def test_authorized_work_replies_with_its_values(self):
        self.change(lambda: (True, 4))
        self.assertEqual(self.authority.calls[0][1], "org.wineexplorer.test1.change")
        self.assertEqual(self.authority.calls[0][0][1]["name"], ":1.42")
        self.assertEqual(self.authority.calls[0][2], 1)   # interactive: polkit may ask for the password
        self.assertEqual(self.service.idle_for(), 0)      # waiting for polkit: the service does not end
        self.authority.answer()
        self.assertEqual(self.replies, [(True, 4)])
        self.assertEqual(self.errors, [])
        self.assertEqual(self.service.waiting, 0)

    def test_no_reply_values(self):
        self.change(lambda: None, interactive=False)
        self.assertEqual(self.authority.calls[0][2], 0)
        self.authority.answer()
        self.assertEqual(self.replies, [()])

    def test_dismissed_is_cancelled_and_no_is_not_authorized(self):
        for details, kind in (({"polkit.dismissed": "true"}, "Cancelled"), ({}, "NotAuthorized")):
            self.authority.result = (False, False, details)
            self.errors.clear()
            self.change(lambda: self.fail("the work ran"))
            self.authority.answer()
            self.assertEqual(self.errors[0].name, f"org.wineexplorer.Test1.Error.{kind}")
            self.assertEqual(str(self.errors[0]), "not authorized to test")

    def test_failed_work_is_failed_with_its_message(self):
        def act():
            raise wexrootsvc.Failed("the disk is full")
        self.change(act)
        self.authority.answer()
        self.assertEqual(self.errors[0].name, "org.wineexplorer.Test1.Error.Failed")
        self.assertEqual(str(self.errors[0]), "the disk is full")

    def test_a_polkit_error_reaches_the_caller(self):
        self.authority.error = FakeDBusException("no polkit", "org.freedesktop.DBus.Error.ServiceUnknown")
        self.change(lambda: self.fail("the work ran"))
        self.authority.answer()
        self.assertEqual(self.errors[0].name, "org.freedesktop.DBus.Error.ServiceUnknown")
        self.assertEqual(self.service.waiting, 0)

    def test_refusal(self):
        self.assertEqual(wexrootsvc.refusal((False, False, {"polkit.dismissed": "true"})), "Cancelled")
        self.assertEqual(wexrootsvc.refusal((False, True, {})), "NotAuthorized")
        self.assertEqual(wexrootsvc.refusal((False,)), "NotAuthorized")

    def test_the_services_use_the_frame(self):
        for script in ("wex-boot1.py", "wex-timesync1.py"):
            spec = importlib.util.spec_from_file_location(script.replace("-", "_")[:-3], os.path.join(HERE, script))
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            self.assertIs(module.Refused, wexrootsvc.Refused, script)
            self.assertIs(module.Failed, wexrootsvc.Failed, script)
            self.assertTrue(module.BUS_NAME.startswith("org.wineexplorer."), script)   # wexhost's map_error rule
            with open(os.path.join(HERE, script), encoding="utf-8") as f:
                source = f.read()
            self.assertNotIn("CheckAuthorization", source, script)
            self.assertNotIn("MainLoop", source, script)


# ---------------------------------------------------------------------------------------------- on a private bus

FAKE_POLKIT = r'''
import sys
import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

answer_file, log_file = sys.argv[1], sys.argv[2]
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
name = dbus.service.BusName("org.freedesktop.PolicyKit1", bus)


class Authority(dbus.service.Object):
    @dbus.service.method("org.freedesktop.PolicyKit1.Authority", in_signature="(sa{sv})sa{ss}us",
                         out_signature="(bba{ss})")
    def CheckAuthorization(self, subject, action, details, flags, cancel_id):
        with open(log_file, "a") as f:
            f.write(f"{action} {int(flags)}\n")
        with open(answer_file) as f:
            answer = f.read().strip()
        if answer == "yes":
            return (True, False, {})
        if answer == "dismissed":
            return (False, False, {"polkit.dismissed": "true"})
        return (False, False, {})


Authority(bus, "/org/freedesktop/PolicyKit1/Authority")
GLib.MainLoop().run()
'''


def bus_tools():
    if not shutil.which("dbus-run-session"):
        return "no dbus-run-session"
    for module in ("dbus", "gi"):
        if importlib.util.find_spec(module) is None:
            return f"no python3 module {module}"
    return None


class BusTest(unittest.TestCase):
    @unittest.skipIf(bus_tools(), bus_tools())
    def test_on_a_private_bus(self):
        env = dict(os.environ, WEX_ROOTSVC_INSIDE="1")
        done = subprocess.run(["dbus-run-session", "--", sys.executable, os.path.abspath(__file__), "-v", "InsideTest"],
                              env=env, capture_output=True, text=True, timeout=180)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("Ran 4 tests", done.stderr)
        self.assertNotIn("skipped", done.stderr, done.stderr)


@unittest.skipUnless(os.environ.get("WEX_ROOTSVC_INSIDE") == "1", "run by BusTest inside dbus-run-session")
class InsideTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import dbus
        cls.dbus = dbus
        cls.dir = tempfile.mkdtemp()
        cls.answer = os.path.join(cls.dir, "answer")
        cls.log = os.path.join(cls.dir, "polkit.log")
        cls.conf = os.path.join(cls.dir, "systemd")
        os.makedirs(cls.conf)
        with open(os.path.join(cls.conf, "timesyncd.conf"), "w") as f:
            f.write("[Time]\nNTP=system.example\n")
        with open(cls.answer, "w") as f:
            f.write("yes\n")
        systemctl = os.path.join(cls.dir, "systemctl")
        with open(systemctl, "w") as f:   # timesyncd is not running: SyncNow is refused
            f.write("#!/bin/sh\n[ \"$1\" = is-active ] && exit 3\nexit 0\n")
        os.chmod(systemctl, 0o755)
        polkit = os.path.join(cls.dir, "fake-polkit.py")
        with open(polkit, "w") as f:
            f.write(FAKE_POLKIT)
        cls.polkit = subprocess.Popen([sys.executable, polkit, cls.answer, cls.log])
        env = dict(os.environ, WEX_ROOTSVC_BUS="session", WEX_ROOTSVC_IDLE="2", WEX_TIMESYNC_DIR=cls.conf,
                   WEX_SYSTEMCTL=systemctl)
        cls.service = subprocess.Popen([sys.executable, os.path.join(HERE, "wex-timesync1.py")], env=env)
        cls.bus = dbus.SessionBus()
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if cls.bus.name_has_owner("org.wineexplorer.TimeSync1") and \
                    cls.bus.name_has_owner("org.freedesktop.PolicyKit1"):
                break
            time.sleep(0.1)
        cls.proxy = cls.bus.get_object("org.wineexplorer.TimeSync1", "/org/wineexplorer/TimeSync1")

    @classmethod
    def tearDownClass(cls):
        for p in (cls.service, cls.polkit):
            if p.poll() is None:
                p.kill()
            p.wait()

    def call(self, method, *args):
        return self.proxy.get_dbus_method(method, "org.wineexplorer.TimeSync1")(*args)

    def answer_polkit(self, text):
        with open(self.answer, "w") as f:
            f.write(text + "\n")

    def polkit_calls(self):
        try:
            with open(self.log) as f:
                return f.read().splitlines()
        except OSError:
            return []

    def error_of(self, method, *args):
        with self.assertRaises(self.dbus.exceptions.DBusException) as caught:
            self.call(method, *args)
        return caught.exception.get_dbus_name(), caught.exception.get_dbus_message()

    def test_1_properties(self):
        props = self.proxy.GetAll("org.wineexplorer.TimeSync1", dbus_interface="org.freedesktop.DBus.Properties")
        self.assertEqual(str(props["Servers"]), "system.example")
        self.assertEqual(str(self.proxy.Get("org.wineexplorer.TimeSync1", "Servers",
                                            dbus_interface="org.freedesktop.DBus.Properties")), "system.example")
        with self.assertRaises(self.dbus.exceptions.DBusException) as caught:
            self.proxy.Get("org.wineexplorer.TimeSync1", "Nothing", dbus_interface="org.freedesktop.DBus.Properties")
        self.assertEqual(caught.exception.get_dbus_name(), "org.wineexplorer.TimeSync1.Error.InvalidArgs")

    def test_2_an_authorized_change(self):
        self.answer_polkit("yes")
        self.call("SetServers", "time.example.org", True)
        with open(os.path.join(self.conf, "timesyncd.conf.d", "90-wineexplorer.conf")) as f:
            self.assertIn("NTP=time.example.org", f.read())
        self.assertEqual(self.polkit_calls()[-1], "org.wineexplorer.timesync1.set-servers 1")

    def test_3_refusals(self):
        before = len(self.polkit_calls())
        self.assertEqual(self.error_of("SetServers", "a;b", True)[0], "org.wineexplorer.TimeSync1.Error.InvalidArgs")
        self.assertEqual(len(self.polkit_calls()), before)   # refused before polkit was asked
        self.answer_polkit("dismissed")
        self.assertEqual(self.error_of("SetServers", "b.example", True)[0], "org.wineexplorer.TimeSync1.Error.Cancelled")
        self.answer_polkit("no")
        self.assertEqual(self.error_of("SetServers", "b.example", False)[0],
                         "org.wineexplorer.TimeSync1.Error.NotAuthorized")
        self.assertEqual(self.polkit_calls()[-1], "org.wineexplorer.timesync1.set-servers 0")
        self.answer_polkit("yes")
        name, message = self.error_of("SyncNow", True)
        self.assertEqual(name, "org.wineexplorer.TimeSync1.Error.Failed")
        self.assertIn("turned off", message)

    def test_4_it_ends_when_idle(self):
        self.service.wait(timeout=30)   # idle 2 s, checked every 10 s
        self.assertEqual(self.service.returncode, 0)


if __name__ == "__main__":
    unittest.main()
