#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""org.wineexplorer.Boot1: the settings of System Properties' Startup and Recovery dialog (sysdm.cpl, Safe Mode work
order D5, docs/VINTNER_BOOT.md section 4.6), on the system bus, as root.

XP keeps them in boot.ini and the registry. Here they belong to root:
- MenuTimeout: "Time to display list of operating systems", Vintner Boot's menu timeout (`vintner menu-timeout`).
  0 hides the menu; a key held at start still opens it.
- RecoveryEnabled, RecoveryTimeout: "Time to display recovery options when needed", the "did not start successfully"
  screen (wex-advanced-options) and its countdown: recovery_menu= and recovery_timeout= in
  /etc/wineexplorer/boot.conf. As on XP (measured 2026-11-12, where they are bytes 8 and 9 of bootstat.dat), turned
  off means no failure screen at all after a failed start, and the seconds are kept for when it is turned on again.
- AutoRestart: "Automatically restart" after a system failure, kernel.panic
  (/etc/sysctl.d/90-wineexplorer-recovery.conf, and the running kernel's value).
- Options: the kernel command line's options, /etc/kernel/cmdline (`vintner cmdline`), which the dialog's Edit opens
  in Notepad, as XP opens boot.ini. A change publishes a new generation, which falls back by itself if it does not
  start.

Reading needs nothing. Changing needs polkit's org.wineexplorer.boot1.set-startup (auth_admin_keep), the rule that
hostname1 and timedate1 use for the computer name and the clock: whoever may change those may change these. There is
no second notion of administrator. sysdm.cpl reaches the service through wexhost (unixlib.c), whose polkit agent asks
for the password. The service knows nothing of Wine or session 0.

D-Bus activated (wex-boot1.service); it ends after a minute without calls. Installed as /usr/local/sbin/wex-boot1.
`wex-boot1 --get` prints the settings as JSON. The WEX_* variables let the tests use other paths.
"""

import json
import os
import subprocess
import sys
import tempfile

BUS_NAME = "org.wineexplorer.Boot1"
OBJECT_PATH = "/org/wineexplorer/Boot1"
INTERFACE = BUS_NAME
ACTION = "org.wineexplorer.boot1.set-startup"
VINTNER = os.environ.get("WEX_VINTNER", "/usr/sbin/vintner")
BOOT_CONF = os.environ.get("WEX_BOOT_CONF", "/etc/wineexplorer/boot.conf")
PANIC_CONF = os.environ.get("WEX_PANIC_CONF", "/etc/sysctl.d/90-wineexplorer-recovery.conf")
PANIC_SYSCTL = os.environ.get("WEX_PANIC_SYSCTL", "/proc/sys/kernel/panic")
PRODUCT_CONF = os.environ.get("WEX_PRODUCT_CONF", "/etc/vintner/product.conf")
RECOVERY_DEFAULT = 30    # seconds, as XP
TIMEOUT_MAX = 999        # the dialog's range, as XP's
PANIC_RESTART = 10       # seconds from a kernel panic to the restart, when Automatically restart is on
IDLE_SECONDS = 60
VINTNER_SECONDS = 600    # a publish (Options) copies a kernel and an initramfs


class Refused(Exception):
    """A value the service does not take (D-Bus error InvalidArgs)."""


class Failed(Exception):
    """A change that did not happen (D-Bus error Failed); the message says why."""


def read_conf(path):
    """key=value lines; blank lines and lines starting with # are ignored."""
    values = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    except OSError:
        pass
    return values


def write_file(path, text, mode=0o644):
    """Replace path atomically: a temporary file in its directory, then a rename."""
    directory = os.path.dirname(path) or "."
    os.makedirs(directory, mode=0o755, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=".wex-boot1-", dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(temp, mode)
        os.replace(temp, path)
    except BaseException:
        try:
            os.unlink(temp)
        except OSError:
            pass
        raise


def set_conf_value(path, key, value, header=""):
    """Set key=value in path, keeping its other lines and comments; a new file starts with header."""
    lines = []
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except FileNotFoundError:
        lines = header.splitlines()
    done = False
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.startswith("#") and "=" in stripped and stripped.split("=", 1)[0].strip() == key:
            lines[i] = f"{key}={value}"
            done = True
            break
    if not done:
        lines.append(f"{key}={value}")
    write_file(path, "\n".join(lines) + "\n")


def check_timeout(seconds):
    if not isinstance(seconds, int) or isinstance(seconds, bool) or not 0 <= seconds <= TIMEOUT_MAX:
        raise Refused(f"a timeout from 0 to {TIMEOUT_MAX} seconds, not {seconds!r}")
    return seconds


def recovery_timeout(path=None):
    """The failure screen's countdown: recovery_timeout= in boot.conf, 30 when missing or not a number."""
    value = read_conf(path or BOOT_CONF).get("recovery_timeout", "")
    try:
        return min(max(int(value), 0), TIMEOUT_MAX)
    except ValueError:
        return RECOVERY_DEFAULT


def recovery_enabled(path=None):
    """Whether a failed start shows the failure screen: recovery_menu= in boot.conf, on unless it says 0."""
    return read_conf(path or BOOT_CONF).get("recovery_menu", "1") != "0"


BOOT_CONF_HEADER = """\
# WineExplorer's startup settings (System Properties > Advanced > Startup and Recovery). Written by wex-boot1.
# recovery_menu: 1 shows the "did not start successfully" screen after a failed start, 0 starts normally instead.
# recovery_timeout: seconds that screen counts down before it starts normally.
"""


def set_recovery(enabled, seconds, path=None):
    path = path or BOOT_CONF
    check_timeout(seconds)
    set_conf_value(path, "recovery_menu", "1" if enabled else "0", BOOT_CONF_HEADER)
    set_conf_value(path, "recovery_timeout", str(seconds), BOOT_CONF_HEADER)


def panic_value(path=None, live=None):
    """kernel.panic as the next start sets it: our sysctl.d file, else the running kernel's value."""
    try:
        with open(path or PANIC_CONF, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                if key.strip() == "kernel.panic":
                    return int(value.strip())
    except (OSError, ValueError):
        pass
    try:
        with open(live or PANIC_SYSCTL, encoding="ascii") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return 0


def auto_restart(path=None, live=None):
    return panic_value(path, live) != 0


PANIC_HEADER = """\
# Startup and Recovery's "Automatically restart" (System Properties > Advanced). Written by wex-boot1.
# kernel.panic: seconds from a kernel panic to the restart; 0 waits.
"""


def set_auto_restart(enable, path=None, live=None):
    seconds = PANIC_RESTART if enable else 0
    write_file(path or PANIC_CONF, PANIC_HEADER + f"kernel.panic = {seconds}\n")
    try:   # the running kernel too, as XP's setting applies at once
        with open(live or PANIC_SYSCTL, "w", encoding="ascii") as f:
            f.write(f"{seconds}\n")
    except OSError as error:
        raise Failed(f"kernel.panic was saved for the next start but not set now: {error}") from error


def run_vintner(args, run=None, timeout=VINTNER_SECONDS):
    """(exit code, the JSON object vintner printed or {}). An exit code of None: vintner did not run."""
    run = run or subprocess.run
    if not os.access(VINTNER, os.X_OK):
        return None, {}
    try:
        done = run([VINTNER, *args], capture_output=True, text=True, timeout=timeout, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        return None, {"error": str(error)}
    reply = {}
    for line in reversed((done.stdout or "").splitlines()):
        line = line.strip()
        if line.startswith("{"):
            try:
                reply = json.loads(line)
            except ValueError:
                pass
            break
    return done.returncode, reply


def menu_timeout(run=None):
    """(Vintner Boot answers, its menu timeout)."""
    code, reply = run_vintner(["menu-timeout", "--json"], run, timeout=60)
    if code == 0 and reply.get("ok") and isinstance(reply.get("timeout"), int):
        return True, min(max(reply["timeout"], 0), TIMEOUT_MAX)
    return False, 0


def set_menu_timeout(seconds, run=None):
    code, reply = run_vintner(["menu-timeout", "--json", str(check_timeout(seconds))], run, timeout=60)
    if code != 0 or not reply.get("ok"):
        raise Failed(reply.get("error") or f"vintner menu-timeout ended with {code}")


def options(run=None):
    """(the options are available, the kernel command line's options as one line)."""
    code, reply = run_vintner(["cmdline", "--json", "show"], run, timeout=60)
    if code == 0 and reply.get("ok") and isinstance(reply.get("options"), list):
        return True, " ".join(str(o) for o in reply["options"])
    return False, ""


def parse_options(text):
    """The options of a text as /etc/kernel/cmdline holds them: separated by white space, # starts a comment line.
    An option that starts with "-" is refused: vintner would take it for one of its own flags."""
    found = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        found.extend(line.split())
    for option in found:
        if option.startswith("-"):
            raise Refused(f"an option cannot start with '-': {option}")
        if any(not 0x21 <= ord(c) <= 0x7e for c in option):
            raise Refused(f"an option of printable ASCII only: {option!r}")
    return found


def set_options(text, run=None):
    """Write the options and publish a generation with them (`vintner cmdline set`). Returns (published, generation)."""
    found = parse_options(text)
    code, reply = run_vintner(["cmdline", "--json", "set", *(found or ["--clear"])], run)
    if code is None:
        raise Failed(reply.get("error") or "Vintner Boot is not installed")
    if code == 3 or reply.get("code") == "refused":
        raise Refused(reply.get("error") or "vintner refused the options")
    if code != 0 or not reply.get("ok"):
        raise Failed(reply.get("error") or f"vintner cmdline ended with {code}")
    generation = reply.get("generation")
    return bool(reply.get("published")), generation if isinstance(generation, int) and generation > 0 else 0


def product_name():
    return read_conf(PRODUCT_CONF).get("name") or "WineExplorer"


def settings(run=None):
    has_loader, menu = menu_timeout(run)
    has_options, line = options(run)
    return {
        "OperatingSystem": product_name(),
        "HasLoader": has_loader,
        "MenuTimeout": menu,
        "RecoveryEnabled": recovery_enabled(),
        "RecoveryTimeout": recovery_timeout(),
        "AutoRestart": auto_restart(),
        "HasOptions": has_options,
        "Options": line,
    }


def apply(menu, recovery_on, recovery, restart, run=None):
    """Startup and Recovery's OK: what differs from the current settings."""
    check_timeout(menu)
    check_timeout(recovery)
    has_loader, current = menu_timeout(run)
    if has_loader and menu != current:
        set_menu_timeout(menu, run)
    if bool(recovery_on) != recovery_enabled() or recovery != recovery_timeout():
        set_recovery(bool(recovery_on), recovery)
    if bool(restart) != auto_restart():
        set_auto_restart(bool(restart))


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

    class Boot1(dbus.service.Object):
        def authorized(self, sender, interactive, then, fail):
            """polkit's CheckAuthorization for the caller, without blocking the loop while the password dialog waits."""
            state["busy"] += 1

            def done(result):
                state["busy"] -= 1
                state["last"] = time.monotonic()
                if not result[0]:
                    fail(error("NotAuthorized", "not authorized to change the startup settings"))
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

        @dbus.service.method(INTERFACE, in_signature="ububb", out_signature="", sender_keyword="sender",
                             async_callbacks=("ok", "fail"))
        def Apply(self, menu, recovery_on, recovery, restart, interactive, sender=None, ok=None, fail=None):
            try:
                check_timeout(int(menu))
                check_timeout(int(recovery))
            except Refused as exc:
                fail(as_dbus(exc))
                return
            self.authorized(sender, interactive,
                            lambda: (apply(int(menu), bool(recovery_on), int(recovery), bool(restart)), ok()), fail)

        @dbus.service.method(INTERFACE, in_signature="sb", out_signature="bu", sender_keyword="sender",
                             async_callbacks=("ok", "fail"))
        def SetOptions(self, text, interactive, sender=None, ok=None, fail=None):
            try:
                parse_options(str(text))
            except Refused as exc:
                fail(as_dbus(exc))
                return

            def change():
                published, generation = set_options(str(text))
                ok(dbus.Boolean(published), dbus.UInt32(generation))
            self.authorized(sender, interactive, change, fail)

        @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="s", out_signature="a{sv}")
        def GetAll(self, interface):
            state["last"] = time.monotonic()
            if interface not in (INTERFACE, ""):
                raise error("InvalidArgs", f"no interface {interface}")
            values = settings()
            return dbus.Dictionary({
                "OperatingSystem": dbus.String(values["OperatingSystem"]),
                "HasLoader": dbus.Boolean(values["HasLoader"]),
                "MenuTimeout": dbus.UInt32(values["MenuTimeout"]),
                "RecoveryEnabled": dbus.Boolean(values["RecoveryEnabled"]),
                "RecoveryTimeout": dbus.UInt32(values["RecoveryTimeout"]),
                "AutoRestart": dbus.Boolean(values["AutoRestart"]),
                "HasOptions": dbus.Boolean(values["HasOptions"]),
                "Options": dbus.String(values["Options"]),
            }, signature="sv")

        @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ss", out_signature="v")
        def Get(self, interface, prop):
            values = self.GetAll(interface)
            if prop not in values:
                raise error("InvalidArgs", f"no property {prop}")
            return values[prop]

    Boot1(bus, OBJECT_PATH)

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
        print(json.dumps(settings(), sort_keys=True))
        return 0
    if args:
        print("usage: wex-boot1 [--get]", file=sys.stderr)
        return 2
    return serve()


if __name__ == "__main__":
    sys.exit(main())
