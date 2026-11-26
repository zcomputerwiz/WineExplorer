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
- AutoRestart: "Automatically restart" after a system failure, on by default as on XP. Linux has two: a kernel panic
  (kernel.panic, /etc/sysctl.d/90-wineexplorer-recovery.conf over the package's /usr/lib/sysctl.d one, and the running
  kernel's value) and a crash of systemd itself (CrashAction=, /etc/systemd/system.conf.d/90-wineexplorer-recovery.conf
  over the package's /usr/lib one). Both restart after 10 seconds when it is on, and wait when it is off.
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
import time

for _dir in (os.path.dirname(os.path.realpath(__file__)), "/usr/local/lib/wineexplorer/python"):
    if _dir not in sys.path:
        sys.path.insert(0, _dir)

import wexcommon
from wexcommon import (
    COUNTDOWN_MAX,
    atomic_write,
    boot_conf_value,
    read_boot_conf,
    set_boot_conf_value,
)
import wexrootsvc
from wexrootsvc import Failed, Refused, refusal  # noqa: F401 (refusal: the tests read it here)

BUS_NAME = "org.wineexplorer.Boot1"
OBJECT_PATH = "/org/wineexplorer/Boot1"
INTERFACE = BUS_NAME
ACTION = "org.wineexplorer.boot1.set-startup"
VINTNER = os.environ.get("WEX_VINTNER", "/usr/sbin/vintner")
BOOT_CONF = os.environ.get("WEX_BOOT_CONF", "/etc/wineexplorer/boot.conf")
PANIC_CONF = os.environ.get("WEX_PANIC_CONF", "/etc/sysctl.d/90-wineexplorer-recovery.conf")
PANIC_SYSCTL = os.environ.get("WEX_PANIC_SYSCTL", "/proc/sys/kernel/panic")
PANIC_DEFAULT = os.environ.get("WEX_PANIC_DEFAULT", "/usr/lib/sysctl.d/90-wineexplorer-recovery.conf")
CRASH_CONF = os.environ.get("WEX_CRASH_CONF", "/etc/systemd/system.conf.d/90-wineexplorer-recovery.conf")
SYSTEMCTL = os.environ.get("WEX_SYSTEMCTL", "systemctl")
PRODUCT_CONF = os.environ.get("WEX_PRODUCT_CONF", "/etc/vintner/product.conf")
RECOVERY_DEFAULT = 30    # seconds, as XP
TIMEOUT_MAX = COUNTDOWN_MAX        # the dialog's range, as XP's
PANIC_RESTART = 10       # seconds from a kernel panic to the restart, when Automatically restart is on
VINTNER_SECONDS = 600    # a publish (Options) copies a kernel and an initramfs
BLESS_UNIT = os.environ.get("WEX_BLESS_UNIT", "vintner-bless.service")
BLESS_SECONDS = 20       # how long a read waits for the start's bless (B123: 7 s seen); D-Bus callers give up at 25 s


read_conf = read_boot_conf
write_file = atomic_write
set_conf_value = set_boot_conf_value


def check_timeout(seconds):
    if not isinstance(seconds, int) or isinstance(seconds, bool) or not 0 <= seconds <= TIMEOUT_MAX:
        raise Refused(f"a timeout from 0 to {TIMEOUT_MAX} seconds, not {seconds!r}")
    return seconds


def recovery_timeout(path=None):
    """The failure screen's countdown: recovery_timeout= in boot.conf, 30 when missing or not a number."""
    value = boot_conf_value("recovery_timeout", path or BOOT_CONF)
    try:
        return min(max(int(value), 0), TIMEOUT_MAX)
    except (TypeError, ValueError):
        return RECOVERY_DEFAULT


def recovery_enabled(path=None):
    """Whether a failed start shows the failure screen: recovery_menu= in boot.conf, on unless it says 0."""
    return wexcommon.recovery_enabled(path or BOOT_CONF)


BOOT_CONF_HEADER = wexcommon.BOOT_CONF_HEADER


def set_recovery(enabled, seconds, path=None):
    path = path or BOOT_CONF
    check_timeout(seconds)
    set_conf_value(path, "recovery_menu", "1" if enabled else "0", BOOT_CONF_HEADER)
    set_conf_value(path, "recovery_timeout", str(seconds), BOOT_CONF_HEADER)


def panic_value(path=None, live=None, default=None):
    """kernel.panic as the next start sets it: our file in /etc/sysctl.d, which replaces the package's one of the same
    name in /usr/lib/sysctl.d (on, as XP's default); without either, or without the key, the running kernel's value."""
    for name in (path or PANIC_CONF, default or PANIC_DEFAULT):
        if os.path.exists(name):
            value = read_conf(name).get("kernel.panic")
            try:
                return int(value)
            except (TypeError, ValueError):
                break
    try:
        with open(live or PANIC_SYSCTL, encoding="ascii") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return 0


def auto_restart(path=None, live=None, default=None):
    return panic_value(path, live, default) != 0


PANIC_HEADER = """\
# Startup and Recovery's "Automatically restart" (System Properties > Advanced). Written by wex-boot1; it replaces
# /usr/lib/sysctl.d/90-wineexplorer-recovery.conf. kernel.panic: seconds from a kernel panic to the restart; 0 waits.
"""

CRASH_HEADER = """\
# Startup and Recovery's "Automatically restart" (System Properties > Advanced). Written by wex-boot1; it replaces
# /usr/lib/systemd/system.conf.d/90-wineexplorer-recovery.conf. CrashAction: what systemd (PID 1) does when it
# crashes, reboot (after 10 seconds) or freeze.
"""


def set_auto_restart(enable, path=None, live=None, crash=None, run=None):
    """Both of Linux's system failures: a kernel panic (kernel.panic) and a crash of systemd itself (CrashAction=).
    Applied at once, as XP's setting is: the running kernel's value, and systemd rereads system.conf.d when it
    reloads."""
    run = run or subprocess.run
    seconds = PANIC_RESTART if enable else 0
    write_file(path or PANIC_CONF, PANIC_HEADER + f"kernel.panic = {seconds}\n")
    write_file(crash or CRASH_CONF, CRASH_HEADER + "[Manager]\n" + f"CrashAction={'reboot' if enable else 'freeze'}\n")
    try:
        with open(live or PANIC_SYSCTL, "w", encoding="ascii") as f:
            f.write(f"{seconds}\n")
    except OSError as error:
        raise Failed(f"kernel.panic was saved for the next start but not set now: {error}") from error
    try:
        done = run([SYSTEMCTL, "daemon-reload"], capture_output=True, text=True, timeout=120, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        raise Failed(f"systemd's crash action was saved for the next start but not set now: {error}") from error
    if done.returncode != 0:
        raise Failed("systemd's crash action was saved for the next start but not set now: "
                     + ((done.stderr or "").strip() or f"systemctl daemon-reload ended with {done.returncode}"))


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


def bless_running(run=None):
    """The start's `vintner bless` (vintner-bless.service, after the logon) is still running."""
    run = run or subprocess.run
    try:
        done = run([SYSTEMCTL, "is-active", BLESS_UNIT], capture_output=True, text=True, timeout=10, check=False)
    except (OSError, subprocess.SubprocessError):
        return False
    return (done.stdout or "").strip() in ("activating", "deactivating", "reloading")


def wait_bless(run=None, seconds=BLESS_SECONDS, sleep=time.sleep):
    """Wait, up to SECONDS, until the start's bless has finished. Returns the seconds waited."""
    waited = 0
    while waited < seconds and bless_running(run):
        sleep(1)
        waited += 1
    return waited


def menu_timeout(run=None, sleep=time.sleep):
    """(Vintner Boot answers, its menu timeout).
    B123: right after a start, vintner-bless.service holds boot storage and the ESP mounted read-write for several
    seconds, and the kernel refuses vintner's read-only mount meanwhile (it retries for 5 s only). So the read waits for
    a running bless first, and a read that fails while vintner did run waits again and is tried once more."""
    if os.access(VINTNER, os.X_OK):
        wait_bless(run, sleep=sleep)
    code, reply = run_vintner(["menu-timeout", "--json"], run, timeout=60)
    if code is not None and not (code == 0 and reply.get("ok")):
        wait_bless(run, sleep=sleep)
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
        set_auto_restart(bool(restart), run=run)


def define(dbus, base, service):
    """The service's object (wexrootsvc.serve): its methods and properties. A dismissed password dialog is Cancelled,
    which keeps Startup and Recovery open without an error message."""
    def change_options(text):
        published, generation = set_options(text)
        return dbus.Boolean(published), dbus.UInt32(generation)

    class Boot1(base):
        @dbus.service.method(INTERFACE, in_signature="ububb", out_signature="", sender_keyword="sender",
                             async_callbacks=("ok", "fail"))
        def Apply(self, menu, recovery_on, recovery, restart, interactive, sender=None, ok=None, fail=None):
            menu, recovery_on, recovery, restart = int(menu), bool(recovery_on), int(recovery), bool(restart)
            service.change(sender, interactive, ok, fail, lambda: apply(menu, recovery_on, recovery, restart),
                           check=lambda: (check_timeout(menu), check_timeout(recovery)))

        @dbus.service.method(INTERFACE, in_signature="sb", out_signature="bu", sender_keyword="sender",
                             async_callbacks=("ok", "fail"))
        def SetOptions(self, text, interactive, sender=None, ok=None, fail=None):
            text = str(text)
            service.change(sender, interactive, ok, fail, lambda: change_options(text),
                           check=lambda: parse_options(text))

        def properties(self):
            values = settings()
            return {
                "OperatingSystem": dbus.String(values["OperatingSystem"]),
                "HasLoader": dbus.Boolean(values["HasLoader"]),
                "MenuTimeout": dbus.UInt32(values["MenuTimeout"]),
                "RecoveryEnabled": dbus.Boolean(values["RecoveryEnabled"]),
                "RecoveryTimeout": dbus.UInt32(values["RecoveryTimeout"]),
                "AutoRestart": dbus.Boolean(values["AutoRestart"]),
                "HasOptions": dbus.Boolean(values["HasOptions"]),
                "Options": dbus.String(values["Options"]),
            }

    return Boot1


def serve():
    return wexrootsvc.serve(BUS_NAME, OBJECT_PATH, ACTION, "not authorized to change the startup settings", define)


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
