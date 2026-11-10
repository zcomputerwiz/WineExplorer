#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for Safe Mode and the Advanced Options menu (docs/SAFE_MODE.md). Run on Linux: python3 scripts/test_safemode.py"""

import curses
import importlib.util
import os
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = REPO_ROOT / "scripts"
SYSTEMD = REPO_ROOT / "installer" / "systemd"
GRUB_ENTRY = REPO_ROOT / "installer" / "live-build" / "config" / "includes.chroot" / "etc" / "grub.d" / "11_wineexplorer_advanced"
BOOTMODE = SCRIPTS / "wex-bootmode.sh"
GENERATOR = SYSTEMD / "wex-bootmode-generator"
PAM_CHECK = SCRIPTS / "wex-safemode-pam-check.sh"
STARTUP_CHECK = SCRIPTS / "wex-startup-check.sh"
LASTGOOD = SCRIPTS / "wex-lastgood.sh"
PACKAGE = REPO_ROOT / "installer" / "package-wine.sh"
VM_TEST = REPO_ROOT / "installer" / "tests" / "safe-mode.sh"


def load_menu():
    spec = importlib.util.spec_from_file_location("wex_advanced_options", SCRIPTS / "wex-advanced-options.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TempDirTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="wex_safemode_"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def fake_helper(self, mode, status=0):
        """A wex-bootmode stand-in that prints MODE."""
        helper = self.tmp / "fake-bootmode"
        helper.write_text(f"#!/bin/sh\necho {mode}\nexit {status}\n", encoding="ascii")
        helper.chmod(0o755)
        return str(helper)


class TestSyntax(unittest.TestCase):
    def test_shell_scripts_parse(self):
        for script in (BOOTMODE, GENERATOR, PAM_CHECK, STARTUP_CHECK, SCRIPTS / "wex-bootlog.sh", LASTGOOD,
                       GRUB_ENTRY):
            result = subprocess.run(["sh", "-n", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, f"{script}: {result.stderr}")
        for script in (SCRIPTS / "wex-greeter.sh", SCRIPTS / "setup-greeter.sh", SCRIPTS / "wine-explorer-session.sh",
                       SCRIPTS / "wine-explorer-wayland-session.sh", SCRIPTS / "wine-explorer-session-common.sh",
                       SCRIPTS / "wex-safe-terminal.sh", PACKAGE, VM_TEST):
            result = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, f"{script}: {result.stderr}")
        session_run = VM_TEST.with_name("session-run.py")
        compile(session_run.read_text(encoding="utf-8"), str(session_run), "exec")

    def test_old_implementation_is_gone(self):
        for old in (SCRIPTS / "wex-safemode-setup.sh", SYSTEMD / "wex-safemode.service",
                    SCRIPTS / "wex-safe-terminal.desktop", GRUB_ENTRY.with_name("11_wineexplorer_safemode")):
            self.assertFalse(old.exists(), f"{old} should be removed")


class TestBootMode(TempDirTest):
    def bootmode(self, cmdline="", state=None, stage=False, root_uid=None):
        cmdline_file = self.tmp / "cmdline"
        cmdline_file.write_text(cmdline + "\n", encoding="ascii")
        state_file = self.tmp / "bootmode"
        if state is not None:
            state_file.write_text(state + "\n", encoding="ascii")
        stage_file = self.tmp / "stage"
        if stage:
            stage_file.touch()
        env = dict(os.environ, WEX_BOOTMODE_CMDLINE=str(cmdline_file), WEX_BOOTMODE_STATE=str(state_file),
                   WEX_BOOTMODE_SETUP_STAGE=str(stage_file),
                   WEX_BOOTMODE_ROOT_UID=str(os.getuid() if root_uid is None else root_uid))
        result = subprocess.run(["sh", str(BOOTMODE)], env=env, capture_output=True, text=True, check=True)
        return result.stdout.strip()

    def test_command_line(self):
        self.assertEqual(self.bootmode(""), "normal")
        self.assertEqual(self.bootmode("quiet wexmode=safe"), "safe")
        self.assertEqual(self.bootmode("wexmode=safenet ro"), "safenet")
        self.assertEqual(self.bootmode("wexmode=safecmd"), "safecmd")
        self.assertEqual(self.bootmode("wexmode=menu"), "menu")
        self.assertEqual(self.bootmode("wexmode=bogus"), "normal")
        self.assertEqual(self.bootmode("wexmode=safe *"), "safe")

    def test_menu_choice_wins_over_command_line(self):
        self.assertEqual(self.bootmode("wexmode=menu", state="safecmd"), "safecmd")
        self.assertEqual(self.bootmode("wexmode=menu", state="normal"), "normal")

    def test_state_file_must_belong_to_root(self):
        self.assertEqual(self.bootmode("wexmode=menu", state="safe", root_uid=os.getuid() + 1), "menu")
        target = self.tmp / "elsewhere"
        target.write_text("safe\n", encoding="ascii")
        (self.tmp / "bootmode").unlink()
        (self.tmp / "bootmode").symlink_to(target)
        self.assertEqual(self.bootmode("wexmode=menu"), "menu")

    def test_setup_stage_blocks_safe_mode_only(self):
        self.assertEqual(self.bootmode("wexmode=safe", stage=True), "normal")
        self.assertEqual(self.bootmode("wexmode=menu", stage=True), "menu")

    def record(self, cmdline, existing=None):
        """wex-bootmode --record (wex-bootmode-record.service): the state file afterwards, or None."""
        state_file = self.tmp / "run" / "bootmode"
        if existing is not None:
            state_file.parent.mkdir(exist_ok=True)
            state_file.write_text(existing + "\n", encoding="ascii")
        (self.tmp / "cmdline").write_text(cmdline + "\n", encoding="ascii")
        env = dict(os.environ, WEX_BOOTMODE_CMDLINE=str(self.tmp / "cmdline"), WEX_BOOTMODE_STATE=str(state_file),
                   WEX_BOOTMODE_SETUP_STAGE=str(self.tmp / "no-stage"), WEX_BOOTMODE_ROOT_UID=str(os.getuid()))
        result = subprocess.run(["sh", str(BOOTMODE), "--record"], env=env, capture_output=True, text=True, check=True)
        self.assertEqual(result.stdout, "")
        return state_file.read_text(encoding="ascii") if state_file.exists() else None

    def test_record_writes_the_mode_once(self):
        """B0: every boot leaves its mode where Wine reads it; the menu's own choice is never overwritten."""
        self.assertEqual(self.record(""), "normal\n")
        self.assertEqual(oct((self.tmp / "run" / "bootmode").stat().st_mode & 0o777), "0o644")
        self.assertFalse((self.tmp / "run" / "bootmode.tmp").exists())
        self.assertEqual(self.record("wexmode=safe"), "normal\n")   # already recorded
        (self.tmp / "run" / "bootmode").unlink()
        self.assertEqual(self.record("vintner.variant=safe wexmode=safe"), "safe\n")
        (self.tmp / "run" / "bootmode").unlink()
        self.assertIsNone(self.record("wexmode=menu"))
        self.assertEqual(self.record("wexmode=menu", existing="safecmd"), "safecmd\n")

    def test_record_unit(self):
        unit = (SYSTEMD / "wex-bootmode-record.service").read_text(encoding="utf-8")
        for line in ("DefaultDependencies=no", "After=systemd-tmpfiles-setup.service",
                     "Before=sysinit.target shutdown.target", "ConditionPathExists=!/run/wineexplorer/bootmode",
                     "ExecStart=/usr/local/bin/wex-bootmode --record", "WantedBy=sysinit.target"):
            self.assertIn(line + "\n", unit)
        # the menu starts after sysinit.target, so a menu boot's choice comes after the record
        self.assertIn("After=sysinit.target", (SYSTEMD / "wex-advanced-options.service").read_text(encoding="utf-8"))

    def test_state_directory_belongs_to_root(self):
        """Wine cannot check who owns the mode file, so only root may write in /run/wineexplorer."""
        tmpfiles = (SYSTEMD / "wexprefetchd.tmpfiles").read_text(encoding="utf-8")
        self.assertIn("d /run/wineexplorer 0755 root root -\n", tmpfiles)
        service = (SYSTEMD / "wexprefetchd.service").read_text(encoding="utf-8")
        self.assertNotIn("RuntimeDirectory=", service)
        self.assertNotIn("/run/wineexplorer\n", service.split("ReadWritePaths=", 1)[1].split("\n", 1)[0] + "\n")


class TestGenerator(TempDirTest):
    def generate(self, mode, status=0, system_update=False):
        early = self.tmp / "early"
        for d in ("normal", "early", "late"):
            (self.tmp / d).mkdir(exist_ok=True)
        update = self.tmp / "system-update"
        if system_update:
            update.touch()
        env = dict(os.environ, WEX_BOOTMODE_HELPER=self.fake_helper(mode, status),
                   WEX_BOOTMODE_UNIT_DIR="/units", WEX_BOOTMODE_SYSTEM_UPDATE=str(update))
        subprocess.run(["sh", str(GENERATOR), str(self.tmp / "normal"), str(early), str(self.tmp / "late")],
                       env=env, check=True)
        return {p.relative_to(early).as_posix(): os.readlink(p) for p in early.rglob("*") if p.is_symlink()}

    def test_normal_writes_nothing(self):
        self.assertEqual(self.generate("normal"), {})
        self.assertEqual(self.generate("safe", status=1), {})

    def test_menu(self):
        self.assertEqual(self.generate("menu"), {"default.target": "/units/wex-advanced-options.target"})

    def test_safe_modes(self):
        for mode in ("safe", "safecmd"):
            links = self.generate(mode)
            self.assertEqual(links.pop("default.target"), "/units/wex-safemode.target")
            for unit in ("timers.target", "wexprefetchd.socket", "NetworkManager.service",
                         "dbus-org.freedesktop.NetworkManager.service"):
                self.assertEqual(links.get(unit), "/dev/null", f"{mode}: {unit}")
            # XP writes the boot log in every Safe Mode start (work order C6)
            self.assertEqual(links.get("wex-safemode.target.wants/wex-bootlog.service"), "/units/wex-bootlog.service")
            shutil.rmtree(self.tmp / "early")

    def test_safe_mode_with_networking(self):
        links = self.generate("safenet")
        self.assertEqual(links.pop("default.target"), "/units/wex-safenet.target")
        self.assertEqual(links.get("timers.target"), "/dev/null")
        self.assertNotIn("NetworkManager.service", links)
        self.assertEqual(links.get("wex-safenet.target.wants/wex-bootlog.service"), "/units/wex-bootlog.service")

    def test_menu_and_normal_start_log_nothing(self):
        for mode in ("menu", "normal"):
            self.assertFalse([k for k in self.generate(mode) if "wex-bootlog" in k], mode)

    def test_offline_update_comes_first(self):
        self.assertEqual(self.generate("safe", system_update=True), {})


class TestBootLog(TempDirTest):
    """wex-bootlog.sh writes ntbtlog.txt in XP's format (work order C6)."""

    DATE = r"[ 1][0-9] [ 123][0-9] \d{4} \d\d:\d\d:\d\d\.\d{3}$"
    HEADER = re.compile(r"^ Service Pack 3" + DATE)
    # winecfg's XP setting
    XP_REG = ('[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion] 1731234567\n'
              '"CSDVersion"="Service Pack 3"\n"CurrentMajorVersionNumber"=dword:00000005\n'
              '"CurrentMinorVersionNumber"=dword:00000001\n"CurrentVersion"="5.1"\n'
              '"ProductName"="Microsoft Windows XP"\n\n'
              '[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\Winlogon] 1731234567\n'
              '"CSDVersion"="not this one"\n')
    # Wine's default (loader/wine.inf.in, [VersionInfo]), as regedit saves it
    WIN10_REG = ('[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion] 1731234567\n'
                 '#time=1db33b1c5ac3a2e\n"CurrentBuild"="19045"\n"CurrentBuildNumber"="19045"\n'
                 '"CurrentMajorVersionNumber"=dword:0000000a\n"CurrentMinorVersionNumber"=dword:00000000\n'
                 '"CurrentType"="Multiprocessor Free"\n"CurrentVersion"="6.3"\n'
                 '"DigitalProductId"=hex:00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,\\\n'
                 '  00,00,00,00\n"EditionId"="Professional"\n"ProductName"="Windows 10 Pro"\n'
                 '"UBR"=dword:000016a4\n\n'
                 '[System\\\\CurrentControlSet\\\\Control\\\\Windows] 1731234567\n"CSDVersion"=dword:00000000\n')

    def run_bootlog(self, registry=XP_REG):
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir(exist_ok=True)
        (bin_dir / "modinfo").write_text('#!/bin/sh\n[ "$2" = virtio_net ] || exit 1\n'
                                         'echo /lib/modules/test/virtio_net.ko.xz\n', encoding="ascii")
        (bin_dir / "systemctl").write_text('#!/bin/sh\necho "broken.service loaded failed failed Broken"\n',
                                           encoding="ascii")
        for f in bin_dir.iterdir():
            f.chmod(0o755)
        modules = self.tmp / "modules"
        modules.write_text("virtio_net 69632 0 - Live 0x0\nbuiltin_less 4096 1 - Live 0x0\n", encoding="ascii")
        system_reg = self.tmp / "system.reg"
        if registry is not None:
            system_reg.write_text('WINE REGISTRY Version 2\n\n' + registry, encoding="ascii")
        windir = self.tmp / "windows"
        windir.mkdir(exist_ok=True)
        env = {"PATH": f"{bin_dir}:{os.environ['PATH']}", "WEX_BOOTLOG_WINDIR": str(windir),
               "WEX_BOOTLOG_SYSTEM_REG": str(system_reg), "WEX_BOOTLOG_MODULES": str(modules)}
        subprocess.run(["sh", str(SCRIPTS / "wex-bootlog.sh")], env=env, check=True)
        return windir / "ntbtlog.txt"

    def lines(self, data):
        self.assertTrue(data.startswith(b"\xff\xfe"), data[:8])
        text = data[2:].decode("utf-16-le")
        self.assertNotIn("\ufeff", text, "a byte order mark only at the start of the file")
        self.assertTrue(text.endswith("\r\n"))
        self.assertNotIn("\n", text.replace("\r\n", ""), "CRLF line ends only")
        return text.split("\r\n")[:-1]

    def test_two_starts_in_xp_format(self):
        self.run_bootlog()
        log = self.run_bootlog()
        lines = self.lines(log.read_bytes())
        start = ["Loaded driver /lib/modules/test/virtio_net.ko.xz", "Loaded driver builtin_less",
                 "Did not start service broken.service"]
        self.assertEqual(len(lines), 2 * (1 + len(start)), "no empty line between starts")
        for first in (0, 1 + len(start)):
            self.assertRegex(lines[first], self.HEADER)
            self.assertEqual(lines[first + 1:first + 1 + len(start)], start)

    def test_a_log_from_before_the_format_is_converted(self):
        windir = self.tmp / "windows"
        windir.mkdir()
        (windir / "ntbtlog.txt").write_bytes(b"WineExplorer 6.12 boot log, 11 08 2026 10:00:00\r\nLoaded driver a\r\n\r\n")
        lines = self.lines(self.run_bootlog().read_bytes())
        self.assertEqual(lines[:3], ["WineExplorer 6.12 boot log, 11 08 2026 10:00:00", "Loaded driver a", ""])
        self.assertRegex(lines[3], self.HEADER)

    def header(self, registry):
        return self.lines(self.run_bootlog(registry).read_bytes())[0]

    def test_without_a_service_pack_the_header_names_wines_version(self):
        self.assertRegex(self.header(self.WIN10_REG), r"^ Windows 10 Pro Version 10\.0 \(Build 19045\) " + self.DATE)

    def test_without_a_registry_the_header_has_the_date_only(self):
        self.assertRegex(self.header(None), r"^ " + self.DATE)


class TestPamCheck(TempDirTest):
    def check(self, mode, user, arg=None, sudo=False):
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir(exist_ok=True)
        groups = f"{user} sudo" if sudo else user
        (bin_dir / "id").write_text(f"#!/bin/sh\necho {groups}\n", encoding="ascii")
        (bin_dir / "logger").write_text("#!/bin/sh\nexit 0\n", encoding="ascii")
        for f in bin_dir.iterdir():
            f.chmod(0o755)
        env = {"PATH": f"{bin_dir}:/usr/bin:/bin", "WEX_BOOTMODE_HELPER": self.fake_helper(mode)}
        if user is not None:
            env["PAM_USER"] = user
        cmd = ["sh", str(PAM_CHECK)] + ([arg] if arg else [])
        return subprocess.run(cmd, env=env, capture_output=True, text=True)

    def test_normal_start_allows_everything(self):
        self.assertEqual(self.check("normal", "alice").returncode, 0)
        self.assertEqual(self.check("normal", "alice", "autologin").returncode, 0)

    def test_no_automatic_logon_in_safe_mode(self):
        for mode in ("safe", "safenet", "safecmd"):
            self.assertEqual(self.check(mode, "alice", "autologin").returncode, 1, mode)

    def test_safe_mode_lets_everyone_log_on(self):
        self.assertEqual(self.check("safe", "alice").returncode, 0)
        self.assertEqual(self.check("safenet", "alice").returncode, 0)

    def test_command_prompt_admins_only(self):
        self.assertEqual(self.check("safecmd", "root").returncode, 0)
        self.assertEqual(self.check("safecmd", "alice", sudo=True).returncode, 0)
        refused = self.check("safecmd", "alice")
        self.assertEqual(refused.returncode, 1)
        self.assertIn("Only administrators", refused.stdout)
        self.assertEqual(self.check("safecmd", None).returncode, 1)


class TestStartupCheck(TempDirTest):
    """wex-startup-check.sh: which starts count, and when a counted start clears GRUB's wex_boot_pending."""

    WELCOME = "lightdm|C:\\windows\\system32\\logonui.exe"
    GTK = "lightdm|/usr/sbin/lightdm-gtk-greeter"

    def check(self, cmdline="root=UUID=x ro quiet", mode="normal", processes=(), sessions="",
              session_class="", session_type="", active="", stage=False):
        """processes: "user|command line" entries that the fake pgrep matches its -u and -f pattern against."""
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir(exist_ok=True)
        log = self.tmp / "grub-editenv.log"
        log.unlink(missing_ok=True)
        self.lastgood_log = self.tmp / "lastgood.log"
        self.lastgood_log.unlink(missing_ok=True)
        fakes = {
            "grub-editenv": 'echo "$*" >> "$LOG"\n',
            "fake-lastgood": 'echo "$*" >> "$LASTGOOD_LOG"\n',
            "pgrep": 'user=\nwhile [ $# -gt 1 ]; do\n    case "$1" in -u) user=$2; shift 2 ;; *) shift ;; esac\ndone\n'
                     'printf "%s\\n" "$FAKE_PROCS" | while IFS="|" read -r u cmd; do\n'
                     '    [ "$u" = "$user" ] && printf "%s\\n" "$cmd"\ndone | grep -Eq -- "$1"\n',
            "loginctl": 'case "$1" in\n    list-sessions) [ -z "$FAKE_SESSIONS" ] || echo "$FAKE_SESSIONS" ;;\n'
                        '    show-session) case "$4" in Class) echo "$FAKE_CLASS" ;; Type) echo "$FAKE_TYPE" ;; esac ;;\n'
                        'esac\n',
            "systemctl": '[ "$2" = is-active ] && [ "$3" = "$FAKE_ACTIVE" ]\n',
        }
        for name, body in fakes.items():
            (bin_dir / name).write_text("#!/bin/sh\n" + body, encoding="ascii")
            (bin_dir / name).chmod(0o755)
        cmdline_file = self.tmp / "cmdline"
        cmdline_file.write_text(cmdline + "\n", encoding="ascii")
        grubenv = self.tmp / "grubenv"
        grubenv.touch()
        stage_file = self.tmp / "stage"
        if stage:
            stage_file.touch()
        else:
            stage_file.unlink(missing_ok=True)
        env = {"PATH": f"{bin_dir}:/usr/bin:/bin", "LOG": str(log), "WEX_BOOTMODE_CMDLINE": str(cmdline_file),
               "WEX_BOOTMODE_HELPER": self.fake_helper(mode), "WEX_BOOTMODE_SETUP_STAGE": str(stage_file),
               "WEX_GRUBENV": str(grubenv), "WEX_STARTUP_TIMEOUT": "0", "WEX_STARTUP_INTERVAL": "0",
               "FAKE_PROCS": "\n".join(processes), "FAKE_SESSIONS": sessions, "FAKE_CLASS": session_class, "FAKE_TYPE": session_type,
               "FAKE_ACTIVE": active, "WEX_LASTGOOD_HELPER": str(bin_dir / "fake-lastgood"),
               "LASTGOOD_LOG": str(self.lastgood_log)}
        result = subprocess.run(["sh", str(STARTUP_CHECK)], env=env, capture_output=True, text=True, timeout=30)
        cleared = log.exists() and f"{grubenv} unset wex_boot_pending" in log.read_text()
        return result.returncode, cleared, result.stdout

    def test_the_logon_screen_counts(self):
        status, cleared, out = self.check(processes=[self.WELCOME])
        self.assertEqual((status, cleared), (0, True))
        self.assertIn("the Welcome screen is up", out)
        self.assertIn("counts as successful", out)
        self.assertEqual(self.check(processes=["lightdm|logonui.exe"])[:2], (0, True))
        status, cleared, out = self.check(processes=[self.GTK])
        self.assertEqual((status, cleared), (0, True))
        self.assertIn("the GTK greeter is up", out)

    def test_explorer_starting_the_logon_screen_does_not_count(self):
        """Explorer's command line names logonui.exe before the Welcome screen exists; Wine may still fail there."""
        starting = ["lightdm|/usr/local/bin/wine explorer.exe /desktop=Logon,1024x768 logonui.exe",
                    "lightdm|C:\\windows\\system32\\explorer.exe /desktop=Logon,1024x768 logonui.exe",
                    "lightdm|/bin/bash /usr/local/bin/wex-greeter.sh"]
        self.assertEqual(self.check(processes=starting)[:2], (1, False))
        self.assertEqual(self.check(processes=["wineuser|C:\\windows\\system32\\logonui.exe"])[:2], (1, False))
        self.assertEqual(self.check(processes=starting + [self.WELCOME])[:2], (0, True))

    def test_the_vm_test_looks_for_the_same_process(self):
        script = STARTUP_CHECK.read_text(encoding="utf-8")
        pattern = re.search(r"^LOGONUI_PATTERN='([^']*)'$", VM_TEST.read_text(encoding="utf-8"), re.M).group(1)
        self.assertIn(f"pgrep -u lightdm -f '{pattern}'", script)

    def test_an_automatic_logon_counts_a_console_logon_does_not(self):
        session = "3 1000 wineuser seat0 tty7"
        for kind in ("x11", "wayland"):
            self.assertEqual(self.check(sessions=session, session_class="user", session_type=kind)[:2], (0, True))
        self.assertEqual(self.check(sessions=session, session_class="user", session_type="tty")[:2], (1, False))
        self.assertEqual(self.check(sessions=session, session_class="greeter", session_type="x11")[:2], (1, False))

    def test_a_setup_stage_counts(self):
        self.assertEqual(self.check(stage=True, active="wex-setup.service")[:2], (0, True))
        self.assertEqual(self.check(stage=True, active="wex-oobe.service")[:2], (0, True))
        self.assertEqual(self.check(stage=False, active="wex-setup.service")[:2], (1, False))

    def test_no_logon_screen_is_a_failed_start(self):
        status, cleared, out = self.check()
        self.assertEqual((status, cleared), (1, False))
        self.assertIn("counts as failed", out)

    def test_menu_starts_do_not_count(self):
        for mode in ("normal", "safe", "safecmd"):
            status, cleared, out = self.check(cmdline="ro quiet wexmode=menu", mode=mode, processes=[self.WELCOME])
            self.assertEqual((status, cleared), (0, False), mode)
            self.assertIn("not counted", out)

    def test_start_normally_on_the_failure_screen_counts(self):
        failed = "ro quiet wexmode=menu wexfailed=1"
        self.assertEqual(self.check(cmdline=failed, mode="normal", processes=[self.WELCOME])[:2], (0, True))
        for mode in ("safe", "safenet", "safecmd"):
            status, cleared, out = self.check(cmdline=failed, mode=mode, processes=[self.WELCOME])
            self.assertEqual((status, cleared), (0, False), mode)
            self.assertIn("failure screen", out)

    def saved_lastgood(self):
        return self.lastgood_log.exists() and self.lastgood_log.read_text() == "save\n"

    def test_a_counted_start_becomes_the_last_known_good(self):
        self.assertEqual(self.check(processes=[self.WELCOME])[0], 0)
        self.assertTrue(self.saved_lastgood())
        failed = "ro quiet wexmode=menu wexfailed=1"
        self.check(cmdline=failed, mode="normal", processes=[self.WELCOME])
        self.assertTrue(self.saved_lastgood(), "Start Normally on the failure screen counts")

    def test_setup_menu_starts_and_failed_starts_are_not_saved(self):
        self.check(stage=True, active="wex-setup.service")
        self.assertFalse(self.saved_lastgood(), "Setup's starts")
        self.check(cmdline="ro quiet wexmode=menu", mode="normal", processes=[self.WELCOME])
        self.assertFalse(self.saved_lastgood(), "a start through the menu")
        self.check(cmdline="ro quiet wexmode=menu wexfailed=1", mode="safe", processes=[self.WELCOME])
        self.assertFalse(self.saved_lastgood(), "Safe Mode from the failure screen")
        self.check()
        self.assertFalse(self.saved_lastgood(), "a failed start")


class TestLastGood(TempDirTest):
    """wex-lastgood.sh: the machine registry's control set saved at a successful start and put back from the menu."""

    REG = ('WINE REGISTRY Version 2\n;; All keys relative to \\\\Machine\n\n#arch=win64\n\n'
           '[Software\\\\Wine] 1731234567\n#time=1db33b1c5ac3a2e\n"Version"="{software}"\n\n'
           '[System\\\\ControlSet001] 1731234567\n#time=1db33b1c5ac3a2e\n\n'
           '[System\\\\ControlSet001\\\\Control] 1731234567\n"Marker"="{control}"\n'
           '"Blob"=hex:00,01,02,03,04,05,06,07,08,09,0a,0b,0c,0d,0e,0f,10,11,12,13,14,15,16,17,\\\n'
           '  18,19\n\n'
           '[System\\\\ControlSet001\\\\Services\\\\Spooler] 1731234567\n"Start"=dword:{start}\n\n'
           '[System\\\\ControlSet0010] 1731234567\n"NotTheSet"="{control}"\n\n'
           '[System\\\\CurrentControlSet] 1731234567\n#link\n"SymbolicLinkValue"=hex(6):5c,00\n\n'
           '[System\\\\Select] 1731234567\n"Current"=dword:00000001\n')

    def setUp(self):
        super().setUp()
        self.machine = self.tmp / "machine"
        self.machine.mkdir()
        self.state = self.tmp / "lastgood"
        self.system_reg = self.machine / "system.reg"

    def write_reg(self, software="1", control="good", start="00000002"):
        self.system_reg.write_text(self.REG.format(software=software, control=control, start=start),
                                   encoding="ascii")

    def lastgood(self, command):
        env = {"PATH": os.environ["PATH"], "WEX_LASTGOOD_MACHINE": str(self.machine),
               "WEX_LASTGOOD_DIR": str(self.state)}
        return subprocess.run(["sh", str(LASTGOOD), command], env=env, capture_output=True, text=True, timeout=30)

    def test_save_copies_the_control_set_only(self):
        self.write_reg()
        self.assertNotEqual(self.lastgood("status").returncode, 0)
        result = self.lastgood("save")
        self.assertEqual(result.returncode, 0, result.stdout)
        saved = (self.state / "controlset.reg").read_text(encoding="ascii")
        keys = re.findall(r"^\[([^]]*)\]", saved, re.M)
        self.assertEqual(keys, ["System\\\\ControlSet001", "System\\\\ControlSet001\\\\Control",
                                "System\\\\ControlSet001\\\\Services\\\\Spooler"])
        self.assertIn("  18,19\n", saved, "continuation lines stay with their key")
        self.assertEqual(self.lastgood("status").returncode, 0)
        self.assertEqual(oct((self.state / "controlset.reg").stat().st_mode & 0o777), "0o600")

    def test_restore_puts_the_control_set_back_and_keeps_the_rest(self):
        self.write_reg()
        self.lastgood("save")
        good = self.system_reg.read_text(encoding="ascii")
        # a later change to a driver's settings, and a program installed after it
        self.write_reg(software="2", control="bad", start="00000004")
        self.system_reg.chmod(0o664)
        result = self.lastgood("restore")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        restored = self.system_reg.read_text(encoding="ascii")
        self.assertEqual(restored, good.replace('"Version"="1"', '"Version"="2"').replace(
            '"NotTheSet"="good"', '"NotTheSet"="bad"'), "the control set goes back, its place in the file too")
        failed = (self.state / "failed-controlset.reg").read_text(encoding="ascii")
        self.assertIn('"Marker"="bad"', failed)
        self.assertIn('"Start"=dword:00000004', failed)
        self.assertEqual(oct(self.system_reg.stat().st_mode & 0o777), "0o664")
        self.assertEqual(list(self.machine.iterdir()), [self.system_reg], "no temporary file left")

    def test_restore_without_a_copy_changes_nothing(self):
        self.write_reg(control="bad")
        before = self.system_reg.read_bytes()
        result = self.lastgood("restore")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no last known good configuration", result.stdout)
        self.assertEqual(self.system_reg.read_bytes(), before)

    def test_save_needs_a_control_set(self):
        self.assertNotEqual(self.lastgood("save").returncode, 0)
        self.system_reg.write_text('WINE REGISTRY Version 2\n\n[Software\\\\Wine] 1\n"a"="b"\n', encoding="ascii")
        self.assertIn("no control set", self.lastgood("save").stdout)
        self.assertNotEqual(self.lastgood("status").returncode, 0)


class FakeScreen:
    shown = []      # (index, seconds) of every failure screen drawn

    def __init__(self, stdscr):
        pass

    def advanced_menu(self, items, index):
        pass

    def os_choices(self, chosen_label):
        pass

    def failure_screen(self, items, index, seconds):
        FakeScreen.shown.append((index, seconds))

    def flush_input(self):
        pass


class FakeStdscr:
    """Keys in order; -1 is getch's timeout (no key). Each getch call advances the clock by `step` seconds."""

    def __init__(self, keys, clock=None, step=0.0):
        self.keys = list(keys)
        self.clock = clock
        self.step = step

    def timeout(self, ms):
        pass

    def getch(self):
        if self.clock is None:
            if not self.keys:
                raise AssertionError("the menu asked for more keys than the test gave")
            return self.keys.pop(0)
        self.clock.now += self.step
        if self.clock.now > 1000:
            raise AssertionError("the failure screen never ended")
        return self.keys.pop(0) if self.keys else -1


class FakeClock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now


class TestMenu(TempDirTest):
    def setUp(self):
        super().setUp()
        self.menu = load_menu()
        self.menu.STATE_DIR = str(self.tmp / "run")
        self.menu.SETUP_STAGE = str(self.tmp / "stage")
        self.menu.PANIC_SYSCTL = str(self.tmp / "panic")
        self.menu.LASTGOOD = str(self.tmp / "no-lastgood")      # no copy saved: Last Known Good is grayed
        self.menu.CMDLINE = str(self.tmp / "cmdline")            # none: a GRUB start
        self.menu.VINTNER = str(self.tmp / "no-vintner")
        self.menu.Screen = FakeScreen
        self.calls = []

    def with_vintner(self, status):
        """A Vintner Boot start, whose `vintner next-boot` exits with STATUS and logs its arguments."""
        (self.tmp / "cmdline").write_text("BOOT_IMAGE=x root=UUID=1 vintner.bootstorage=PARTUUID=2 quiet\n",
                                          encoding="ascii")
        helper = self.tmp / "fake-vintner"
        helper.write_text(f'#!/bin/sh\necho "$@" >> "{self.tmp}/vintner.log"\nexit {status}\n', encoding="ascii")
        helper.chmod(0o755)
        self.menu.VINTNER = str(helper)

    def vintner_log(self):
        path = self.tmp / "vintner.log"
        return path.read_text(encoding="ascii").splitlines() if path.exists() else []

    def with_lastgood(self):
        helper = self.tmp / "fake-lastgood"
        helper.write_text('#!/bin/sh\n[ "$1" = status ]\n', encoding="ascii")
        helper.chmod(0o755)
        self.menu.LASTGOOD = str(helper)

    def run_keys(self, *keys):
        return self.menu.run_menu(FakeStdscr(keys))

    def fake_run(self, cmd, check=False):
        self.calls.append(cmd)

    def test_items_follow_xp(self):
        labels = [item.label if item else None for item in self.menu.menu_items(False)]
        self.assertEqual(labels, [
            "Safe Mode", "Safe Mode with Networking", "Safe Mode with Command Prompt", None,
            "Enable Boot Logging", "Enable VGA Mode",
            "Last Known Good Configuration (your most recent settings that worked)",
            "Directory Services Restore Mode (Windows domain controllers only)", "Debugging Mode",
            "Disable automatic restart on system failure", None,
            "Start WineExplorer Normally", "Reboot", "Return to OS Choices Menu"])
        grayed = [item.label for item in self.menu.menu_items(False) if item and not item.selectable]
        self.assertEqual(len(grayed), 3)
        grayed = [item.label for item in self.menu.menu_items(True) if item and not item.selectable]
        self.assertIn("Safe Mode with Command Prompt", grayed)

    def test_navigation_skips_gaps_and_grayed_items(self):
        items = self.menu.menu_items(False)
        self.assertEqual(self.menu.first_selectable(items), 0)
        self.assertEqual(self.menu.move(items, 0, -1), 0)
        self.assertEqual(self.menu.move(items, 2, 1), 4)             # over the blank line
        self.assertEqual(self.menu.move(items, 4, 1), 8)             # over three grayed items
        self.assertEqual(self.menu.move(items, 13, 1), 13)
        pending = self.menu.menu_items(True)
        self.assertEqual(self.menu.first_selectable(pending), 4)

    def test_choices(self):
        enter, up, down, f8 = 10, curses.KEY_UP, curses.KEY_DOWN, curses.KEY_F8
        self.assertEqual(self.run_keys(enter, enter), ("boot", "safe", None))
        self.assertEqual(self.run_keys(down, down, enter, enter), ("boot", "safecmd", None))
        self.assertEqual(self.run_keys(enter, f8, down, enter, enter), ("boot", "safenet", None))
        self.assertEqual(self.run_keys(down, down, down, enter, enter), ("boot", "normal", "bootlog"))
        self.assertEqual(self.run_keys(*[down] * 6, enter), ("boot", "normal", None))      # Start Normally
        self.assertEqual(self.run_keys(*[down] * 7, enter), ("reboot", None, None))
        self.assertEqual(self.run_keys(*[down] * 8, enter, enter), ("boot", "normal", None))  # OS Choices
        self.assertEqual(self.run_keys(down, up, enter, enter), ("boot", "safe", None))
        self.assertEqual(self.vintner_log(), [])

    def test_os_choices_with_vintner_restarts_into_its_menu(self):
        down, enter = curses.KEY_DOWN, 10
        self.with_vintner(0)
        self.assertEqual(self.run_keys(*[down] * 8, enter), ("reboot", None, None))
        self.assertEqual(self.vintner_log(), ["next-boot menu"])
        # the other items do not ask Vintner
        self.assertEqual(self.run_keys(enter, enter), ("boot", "safe", None))
        self.assertEqual(self.vintner_log(), ["next-boot menu"])

    def test_os_choices_when_vintner_cannot_set_the_menu(self):
        down, enter = curses.KEY_DOWN, 10
        self.with_vintner(1)
        self.assertEqual(self.run_keys(*[down] * 8, enter, enter), ("boot", "normal", None))    # the local list
        self.assertEqual(self.vintner_log(), ["next-boot menu"])

    def test_failure_screen_items_follow_xp(self):
        items = self.menu.failure_items(False)
        self.assertEqual([item.label if item else None for item in items], [
            "Safe Mode", "Safe Mode with Networking", "Safe Mode with Command Prompt", None,
            "Last Known Good Configuration (your most recent settings that worked)", None,
            "Start WineExplorer Normally"])
        self.assertEqual([item.label for item in items if item and not item.selectable],
                         ["Last Known Good Configuration (your most recent settings that worked)"])
        pending = [item.label for item in self.menu.failure_items(True) if item and item.selectable]
        self.assertEqual(pending, ["Start WineExplorer Normally"])

    def run_failure(self, keys, step):
        FakeScreen.shown = []
        clock = FakeClock()
        return self.menu.run_failure(FakeStdscr(keys, clock, step), clock=clock)

    def test_failure_screen_countdown_starts_normally(self):
        self.assertEqual(self.run_failure([], 0.25), ("boot", "normal", None))
        seconds = [s for _, s in FakeScreen.shown]
        self.assertEqual(seconds[0], 30)
        self.assertEqual(min(seconds), 1)
        self.assertEqual(seconds, sorted(seconds, reverse=True))
        self.assertTrue(all(index == 6 for index, _ in FakeScreen.shown), "Start Normally is highlighted")

    def test_failure_screen_arrow_stops_the_countdown(self):
        enter, up = 10, curses.KEY_UP
        # Up skips the grayed Last Known Good; the countdown line goes; waiting then chooses nothing
        self.assertEqual(self.run_failure([-1, up, *[-1] * 200, enter], 0.25), ("boot", "safecmd", None))
        after = FakeScreen.shown[2:]
        self.assertTrue(after and all(seconds is None for _, seconds in after))
        self.assertEqual(after[-1][0], 2)
        self.assertEqual(self.run_failure([curses.KEY_DOWN, enter], 0.25), ("boot", "normal", None))
        self.assertEqual(self.run_failure([enter], 0.25), ("boot", "normal", None))
        self.assertEqual(self.run_failure([up, up, up, enter], 0.25), ("boot", "safe", None))

    def test_last_known_good_once_a_copy_is_saved(self):
        enter, up, down = 10, curses.KEY_UP, curses.KEY_DOWN
        label = "Last Known Good Configuration (your most recent settings that worked)"
        self.assertFalse(self.menu.lastgood_saved())
        self.assertNotIn(label, [i.label for i in self.menu.menu_items(False) if i and i.selectable])
        self.with_lastgood()
        self.assertTrue(self.menu.lastgood_saved())
        self.assertIn(label, [i.label for i in self.menu.menu_items(False, True) if i and i.selectable])
        self.assertIn(label, [i.label for i in self.menu.failure_items(False, True) if i and i.selectable])
        # the menu: Safe Mode, ..., Enable Boot Logging, over VGA Mode to Last Known Good, then the OS list
        self.assertEqual(self.run_keys(down, down, down, down, enter, enter), ("boot", "normal", "lastgood"))
        # the failure screen: Up from Start Normally
        self.assertEqual(self.run_failure([up, enter], 0.25), ("boot", "normal", "lastgood"))
        self.assertEqual(self.run_failure([up, up, enter], 0.25), ("boot", "safecmd", None))

    def test_apply_last_known_good_restores_before_the_start(self):
        self.menu.apply_choice("normal", "lastgood", run=self.fake_run)
        self.assertEqual(self.calls, [[self.menu.LASTGOOD, "restore"], ["systemctl", "daemon-reload"],
                                      ["systemctl", "--no-block", "start", "default.target"]])
        self.assertEqual((self.tmp / "run" / "bootmode").read_text(), "normal\n")

    def test_failure_screen_needs_wexfailed(self):
        cmdline = self.tmp / "cmdline"
        cmdline.write_text("root=UUID=x ro quiet wexmode=menu wexfailed=1\n", encoding="ascii")
        self.assertTrue(self.menu.start_failed(str(cmdline)))
        cmdline.write_text("root=UUID=x ro quiet wexmode=menu \n", encoding="ascii")
        self.assertFalse(self.menu.start_failed(str(cmdline)))
        self.assertFalse(self.menu.start_failed(str(self.tmp / "missing")))

    def test_failure_text_fits_the_console(self):
        width = 80 - load_menu().Screen.LEFT - 1      # setUp replaced self.menu.Screen
        lines = sum(len(textwrap.wrap(p, width)) + 1 for p in self.menu.FAILURE_TEXT)
        # explanation, the seven list rows, a blank row, the footer and the countdown on a 25-row console
        self.assertLessEqual(1 + lines + 7 + 1 + 2, 24)

    def test_apply_safe_mode(self):
        self.menu.apply_choice("safe", run=self.fake_run)
        self.assertEqual((self.tmp / "run" / "bootmode").read_text(), "safe\n")
        self.assertEqual(oct((self.tmp / "run" / "bootmode").stat().st_mode & 0o777), "0o644")
        self.assertEqual(self.calls, [["systemctl", "daemon-reload"],
                                      ["systemctl", "--no-block", "start", "default.target"]])

    def test_apply_options(self):
        self.menu.apply_choice("normal", "bootlog", run=self.fake_run)
        self.assertEqual(self.calls[-1], ["systemctl", "--no-block", "start", "default.target", "wex-bootlog.service"])
        self.assertEqual((self.tmp / "run" / "bootoption").read_text(), "bootlog\n")
        self.menu.apply_choice("normal", "norestart", run=self.fake_run)
        self.assertEqual((self.tmp / "panic").read_text(), "0\n")
        self.calls.clear()
        self.menu.apply_choice("normal", "debug", run=self.fake_run)
        self.assertEqual(self.calls[0], ["systemd-analyze", "log-level", "debug"])
        with self.assertRaises(ValueError):
            self.menu.apply_choice("bogus", run=self.fake_run)


class TestSessionAndGreeter(unittest.TestCase):
    def test_session_keeps_its_name_and_branches_on_the_mode(self):
        session = (SCRIPTS / "wine-explorer-session.sh").read_text(encoding="utf-8")
        branch = session.index("exec /usr/local/bin/wex-safe-terminal.sh")
        self.assertLess(branch, session.index(". \"$(dirname \"$0\")/wine-explorer-session-common.sh\""),
                        "the recovery console must start before anything touches the Wine prefix")
        self.assertIn('[ "$WEXMODE" = normal ] && [ -z "$WIDTH" ] && [ -r "$DISPLAY_PROFILE" ]', session)
        # C3: Safe Mode sets 640x480 before the desktop's size is read, and only in Safe Mode
        safe_display = session.index('if [ "$WEXMODE" != normal ] && [ -z "$WIDTH" ]')
        self.assertLess(session.index("safe_display() {"), safe_display)
        self.assertLess(safe_display, session.index("RES=$(xrandr"))
        self.assertIn("--mode 640x480", session)
        common = (SCRIPTS / "wine-explorer-session-common.sh").read_text(encoding="utf-8")
        skip = common.index("safe|safenet) return 0")
        self.assertLess(common.index("wineboot.exe -u"), skip)
        self.assertLess(skip, common.index("--apply-user-defaults"))
        self.assertLess(skip, common.index("vulkan prepare"))

    def test_wayland_session_reads_the_mode_before_the_prefix(self):
        session = (SCRIPTS / "wine-explorer-wayland-session.sh").read_text(encoding="utf-8")
        prepare = session.index("\nwex_prepare_prefix")
        self.assertLess(session.index("export WEXMODE"), prepare)
        branch = session.index('if [ "$WEXMODE" = safecmd ]')
        self.assertLess(branch, prepare, "the recovery console must start before anything touches the Wine prefix")
        self.assertLess(branch, session.index('\n"$WINE" '))   # the first line that runs Wine


class TestWaylandSession(TempDirTest):
    """wine-explorer-wayland-session.sh with a stand-in compositor and Wine."""

    def run_session(self, mode, compositor_status=0):
        log = self.tmp / "calls.log"
        log.unlink(missing_ok=True)
        compositor = self.tmp / "wexcomp"
        compositor.write_text('#!/bin/sh\necho "wexcomp $* WEXMODE=$WEXMODE DISPLAY=${DISPLAY-unset}" >> "$LOG"\n'
                              'exit "$FAKE_COMPOSITOR"\n', encoding="ascii")
        compositor.chmod(0o755)
        wine = self.tmp / "wine"
        wine.write_text('#!/bin/sh\necho "wine $*" >> "$LOG"\n', encoding="ascii")
        wine.chmod(0o755)
        runtime = self.tmp / "run"
        runtime.mkdir(exist_ok=True)
        env = {"PATH": os.environ["PATH"], "LOG": str(log), "HOME": str(self.tmp), "USER": "wineuser",
               "XDG_RUNTIME_DIR": str(runtime), "DISPLAY": ":9", "WINE": str(wine),
               "WEX_WEXCOMP_BIN": str(compositor), "WEX_WAYLAND_CONFIG": "/cfg",
               "WEX_BOOTMODE_HELPER": self.fake_helper(mode), "WEX_SAFE_TERMINAL": "/safe-terminal",
               "FAKE_COMPOSITOR": str(compositor_status)}
        result = subprocess.run(["bash", str(SCRIPTS / "wine-explorer-wayland-session.sh")], env=env,
                                capture_output=True, text=True, timeout=30)
        calls = log.read_text(encoding="ascii").splitlines() if log.exists() else []
        return result.returncode, calls

    def test_command_prompt_runs_the_recovery_console_in_the_compositor(self):
        status, calls = self.run_session("safecmd")
        self.assertEqual(status, 0)
        self.assertEqual(calls, ["wexcomp -C /cfg -S /safe-terminal WEXMODE=safecmd DISPLAY=unset"])

    def test_other_modes_start_the_desktop_compositor(self):
        # The stand-in compositor ends at once, so the session stops before Wine, as it does when wexcomp fails.
        for mode, expected in (("safe", "safe"), ("safenet", "safenet"), ("normal", "normal"), ("bogus", "normal")):
            status, calls = self.run_session(mode, compositor_status=1)
            self.assertEqual(status, 1, mode)
            self.assertEqual(calls, [f"wexcomp -C /cfg WEXMODE={expected} DISPLAY=unset"], mode)


class TestGreeter(TempDirTest):
    """wex-greeter.sh: the Welcome screen in every mode; in Safe Mode the GTK greeter when Wine cannot start."""

    def run_greeter(self, mode, wine_exists=True, cmd_status=0, explorer_status=0, home=None, xrandr=False):
        """home: run with HOME=home and no WINEPREFIX, as LightDM starts the greeter (the prefix is then ~/.wine).
        xrandr: a stand-in X server at 1280x800 whose output also offers 640x480."""
        log = self.tmp / "calls.log"
        log.unlink(missing_ok=True)
        path = os.environ["PATH"]
        if xrandr:
            bin_dir = self.tmp / "bin"
            bin_dir.mkdir(exist_ok=True)
            (self.tmp / "calls.log.mode").unlink(missing_ok=True)
            (bin_dir / "xrandr").write_text(
                '#!/bin/sh\ncase "$*" in\n'
                '    *--output*) echo "xrandr $*" >> "$LOG"; echo "640 x 480" > "$LOG.mode"; exit 0 ;;\nesac\n'
                'echo "Screen 0: minimum 320 x 200, current $(cat "$LOG.mode" 2>/dev/null || echo "1280 x 800"),'
                ' maximum 8192 x 8192"\n'
                'echo "Virtual-2 disconnected (normal left inverted right x axis y axis)"\n'
                'echo "Virtual-1 connected primary 1280x800+0+0 (normal left inverted right x axis y axis) 0mm x 0mm"\n'
                'echo "   1280x800      59.81*+"\necho "   640x480       59.94"\n', encoding="ascii")
            (bin_dir / "xrandr").chmod(0o755)
            path = f"{bin_dir}:{path}"
        wine = self.tmp / "wine"
        if wine_exists:
            wine.write_text('#!/bin/sh\necho "wine $*" >> "$LOG"\necho "$WEXMODE $WINEPREFIX" > "$LOG.env"\n'
                            'case "$1" in\n'
                            '    cmd.exe) exit "$FAKE_CMD" ;;\n    explorer.exe) exit "$FAKE_EXPLORER" ;;\nesac\n',
                            encoding="ascii")
            wine.chmod(0o755)
        gtk = self.tmp / "gtk-greeter"
        gtk.write_text('#!/bin/sh\necho gtk >> "$LOG"\n', encoding="ascii")
        gtk.chmod(0o755)
        env = {"PATH": path, "LOG": str(log), "WINE": str(wine),
               "WEX_BOOTMODE_HELPER": self.fake_helper(mode), "WEX_GTK_GREETER": str(gtk),
               "FAKE_CMD": str(cmd_status), "FAKE_EXPLORER": str(explorer_status)}
        if home is None:
            prefix = self.tmp / "prefix"
            prefix.mkdir(exist_ok=True)
            (prefix / "user.reg").write_text("", encoding="ascii")   # a made prefix: no update
            env["WINEPREFIX"] = str(prefix)
        else:
            env["HOME"] = str(home)
        result = subprocess.run(["bash", str(SCRIPTS / "wex-greeter.sh")], env=env, capture_output=True, text=True,
                                timeout=60)
        calls = log.read_text().splitlines() if log.exists() else []
        return result.returncode, calls

    def test_the_prefix_is_the_accounts_wine_and_moves_once(self):
        """The system role serves only <home>/.wine (docs/SAFE_MODE_WORKORDER.md C7)."""
        home = self.tmp / "lightdm"
        old = home / ".wine-logon"
        old.mkdir(parents=True)
        (old / "user.reg").write_text("old hive", encoding="ascii")
        self.assertEqual(self.run_greeter("normal", home=home)[1], ["wine explorer.exe /desktop=Logon,1024x768 logonui.exe"])
        self.assertFalse(old.exists())
        self.assertEqual((home / ".wine" / "user.reg").read_text(), "old hive")
        self.assertEqual((self.tmp / "calls.log.env").read_text().split(), ["normal", str(home / ".wine")])
        # both exist: nothing moves
        old.mkdir()
        self.run_greeter("normal", home=home)
        self.assertTrue(old.is_dir())
        self.assertEqual((home / ".wine" / "user.reg").read_text(), "old hive")

    def test_a_made_prefix_is_not_updated_at_every_start(self):
        """Under the system role the logon prefix has user.reg but no system.reg; only a new prefix runs wineboot."""
        home = self.tmp / "lightdm"
        (home / ".wine").mkdir(parents=True)
        calls = self.run_greeter("normal", home=home)[1]
        self.assertTrue(calls[0].startswith("wine wineboot.exe -u"), calls)
        (home / ".wine" / "user.reg").write_text("", encoding="ascii")
        self.assertEqual(self.run_greeter("normal", home=home)[1], ["wine explorer.exe /desktop=Logon,1024x768 logonui.exe"])

    def test_the_logon_screen_learns_the_mode(self):
        for mode in ("normal", "safe", "safecmd"):
            self.run_greeter(mode)
            self.assertEqual((self.tmp / "calls.log.env").read_text().split()[0], mode)

    def test_safe_mode_runs_at_640x480(self):
        """work order C3: the connected output switches to 640x480 before the Welcome screen starts"""
        for mode in ("safe", "safenet", "safecmd"):
            calls = self.run_greeter(mode, xrandr=True)[1]
            self.assertEqual([c for c in calls if c.startswith("xrandr")], ["xrandr --output Virtual-1 --mode 640x480"],
                             mode)
            self.assertIn("wine explorer.exe /desktop=Logon,640x480 logonui.exe", calls, mode)
        calls = self.run_greeter("normal", xrandr=True)[1]
        self.assertEqual(calls, ["wine explorer.exe /desktop=Logon,1280x800 logonui.exe"])

    def test_normal_start_shows_the_welcome_screen(self):
        status, calls = self.run_greeter("normal", cmd_status=1)
        self.assertEqual(calls, ["wine explorer.exe /desktop=Logon,1024x768 logonui.exe"])

    def test_safe_mode_shows_the_welcome_screen(self):
        for mode in ("safe", "safenet", "safecmd"):
            status, calls = self.run_greeter(mode)
            self.assertEqual(status, 0, mode)
            self.assertEqual(calls, ["wine cmd.exe /c exit 0",
                                     "wine explorer.exe /desktop=Logon,1024x768 logonui.exe"], mode)

    def test_safe_mode_falls_back_when_wine_is_missing(self):
        self.assertEqual(self.run_greeter("safecmd", wine_exists=False)[1], ["gtk"])

    def test_safe_mode_falls_back_when_wine_does_not_start(self):
        self.assertEqual(self.run_greeter("safe", cmd_status=1)[1], ["wine cmd.exe /c exit 0", "gtk"])

    def test_safe_mode_falls_back_when_the_logon_screen_fails(self):
        self.assertEqual(self.run_greeter("safe", explorer_status=3)[1],
                         ["wine cmd.exe /c exit 0", "wine explorer.exe /desktop=Logon,1024x768 logonui.exe", "gtk"])


class TestUnitsAndGrub(unittest.TestCase):
    def test_targets(self):
        menu = (SYSTEMD / "wex-advanced-options.target").read_text(encoding="utf-8")
        self.assertIn("Requires=sysinit.target", menu)
        self.assertNotIn("basic.target", menu)
        service = (SYSTEMD / "wex-advanced-options.service").read_text(encoding="utf-8")
        self.assertIn("DefaultDependencies=no", service)
        self.assertIn("OnFailure=graphical.target", service)
        self.assertIn("TTYPath=/dev/tty1", service)
        safe = "\n".join(line for line in (SYSTEMD / "wex-safemode.target").read_text(encoding="utf-8").splitlines()
                         if not line.startswith("#"))
        self.assertIn("display-manager.service", safe)
        self.assertNotIn("multi-user.target", safe)
        self.assertNotIn("NetworkManager", safe)
        self.assertIn("NetworkManager.service", (SYSTEMD / "wex-safenet.target").read_text(encoding="utf-8"))

    def test_grub_entry(self):
        entry = GRUB_ENTRY.read_text(encoding="utf-8")
        self.assertIn("--hotkey=f8", entry)
        self.assertIn("wexmode=menu", entry)
        cfg = (GRUB_ENTRY.parent.parent / "default" / "grub.d" / "wineexplorer.cfg").read_text(encoding="utf-8")
        self.assertIn("GRUB_TIMEOUT_STYLE=hidden", cfg)
        self.assertIn("GRUB_TIMEOUT=1", cfg)
        # the menu names WineExplorer (Debian's 10_linux makes it "WineExplorer GNU/Linux"), in both copies
        self.assertIn('GRUB_DISTRIBUTOR="WineExplorer"', cfg.splitlines())
        screens = (REPO_ROOT / "installer" / "textsetup" / "screens.c").read_text(encoding="utf-8")
        self.assertIn(r'"GRUB_DISTRIBUTOR=\"WineExplorer\"\n"', screens)
        self.assertIn("11_wineexplorer_advanced", screens)
        self.assertNotIn("11_wineexplorer_safemode", screens)

    def test_startup_check_unit(self):
        unit = (SYSTEMD / "wex-startup-check.service").read_text(encoding="utf-8")
        for line in ("Type=oneshot", "ExecStart=/usr/local/sbin/wex-startup-check", "Before=boot-complete.target",
                     "WantedBy=multi-user.target", "RequiredBy=boot-complete.target"):
            self.assertIn(line, unit.splitlines())
        # Vintner's CI found systemd cuts status lines at 80 columns on serial (docs/VINTNER_BOOT.md section 4.4)
        self.assertLess(len(re.search(r"^Description=(.*)$", unit, re.M).group(1)), 30)


# grub-mkconfig_lib's functions, as far as 11_wineexplorer_advanced uses them
FAKE_GRUB_LIB = r"""
grub_file_is_not_garbage() { test -f "$1"; }
version_sort() { sort -V "$@"; }
make_system_path_relative_to_its_root() { echo /boot; }
grub_get_device_id() { echo testid; }
prepare_grub_to_access_device() { echo "insmod ext2"; }
grub_add_tab() { sed -e 's/^/\t/'; }
grub_quote() { cat; }
uses_abstraction() { return 1; }
"""


class TestGrubOutput(TempDirTest):
    """What 11_wineexplorer_advanced adds to grub.cfg. GRUB's own syntax check runs in update-grub (the VM test)."""

    def generate(self):
        boot = self.tmp / "boot"
        boot.mkdir()
        (boot / "vmlinuz-6.12.1-amd64").write_text("kernel", encoding="ascii")
        (boot / "initrd.img-6.12.1-amd64").write_text("initrd", encoding="ascii")
        (self.tmp / "grub-mkconfig_lib").write_text(FAKE_GRUB_LIB, encoding="ascii")
        source = GRUB_ENTRY.read_text(encoding="utf-8")
        kernels = "/boot/vmlinuz-* /vmlinuz-* /boot/kernel-*"
        self.assertIn(kernels, source)
        script = self.tmp / "11_test"
        script.write_text(source.replace(kernels, f"{boot}/vmlinuz-*"), encoding="utf-8")
        env = {"PATH": os.environ["PATH"], "pkgdatadir": str(self.tmp), "GRUB_DEVICE": "/dev/vda4",
               "GRUB_DEVICE_BOOT": "/dev/vda4", "GRUB_CMDLINE_LINUX": "", "GRUB_CMDLINE_LINUX_DEFAULT": "quiet"}
        return subprocess.run(["sh", str(script)], env=env, capture_output=True, text=True, check=True).stdout

    def test_records_every_start_and_boots_the_failure_screen(self):
        cfg = self.generate()
        entry = cfg.index("menuentry 'WineExplorer Advanced Options (F8)'")
        head, body = cfg[:entry], cfg[entry:]
        # before any entry boots: remember the last start, mark this one, and choose the failure screen if needed
        self.assertIn('set wex_last_start="${wex_boot_pending}"', head)
        self.assertLess(head.index("set wex_boot_pending=1"), head.index("save_env wex_boot_pending"))
        self.assertIn('if [ -n "${have_grubenv}" ]; then', head)
        self.assertIn("set wex_failed=wexfailed=1", head)
        self.assertIn("set default='wex-advanced-testid'", head)
        self.assertIn('if [ -z "${boot_once}" ]; then', head)
        for name in ("wex_last_start", "wex_failed", "wex_env"):
            self.assertIn(f"export {name}", head)
        self.assertNotIn("recordfail", cfg)
        # the entry: --id and hotkey as before, the previous value written back, wexfailed passed on
        self.assertIn("--hotkey=f8 $menuentry_id_option 'wex-advanced-testid' {", body)
        self.assertIn('if [ "${wex_last_start}" = 1 ]; then set wex_boot_pending=1; else set wex_boot_pending=0; fi',
                      body)
        self.assertLess(body.index("save_env wex_boot_pending"), body.index("\tlinux\t"))
        linux = next(line for line in body.splitlines() if line.startswith("\tlinux\t"))
        self.assertTrue(linux.endswith("quiet wexmode=menu ${wex_failed}"), linux)
        self.assertEqual(cfg.count("{"), cfg.count("}"))


class TestPackage(TempDirTest):
    def safe_mode_block(self):
        """The postinst's Safe Mode part, with its paths moved under the test directory."""
        script = PACKAGE.read_text(encoding="utf-8")
        start = script.index("# Safe Mode (docs/SAFE_MODE.md). The earlier")
        end = script.index("systemctl daemon-reload || true", start)
        block = script[start:end]
        for path in ("/etc/", "/run/", "/var/lib/"):
            block = block.replace(path, f"{self.tmp}{path}")
        return block

    def test_installs_the_new_pieces(self):
        script = PACKAGE.read_text(encoding="utf-8")
        for piece in ("wex-bootmode.sh", "wex-advanced-options.py", "wex-bootlog.sh", "wex-bootmode-generator",
                      "system-generators", "wex-advanced-options.target", "wex-safemode.target",
                      "wex-safenet.target", "wex-bootlog.service", "11_wineexplorer_advanced",
                      "wex-startup-check.sh", "wex-startup-check.service", "wex-lastgood.sh"):
            self.assertIn(piece, script)
        self.assertIn("systemctl enable wex-startup-check.service", self.safe_mode_block(),
                      "the VM test runs only the postinst's Safe Mode part, so the unit is enabled there")
        self.assertIn('"$STAGE_SESSION/etc/ssh/sshd_config.d/wineexplorer.conf"', script)
        self.assertIn("echo /etc/ssh/sshd_config.d/wineexplorer.conf >> \"$STAGE_SESSION/DEBIAN/conffiles\"", script)

        self.assertFalse("scripts/wex-safemode-setup.sh" in script, "the old setup script is still packaged")
        self.assertFalse("xsessions/wex-safe-terminal.desktop" in script, "the old session is still packaged")
        self.assertIsNone(re.search(r"systemctl enable [^\n]*wex-safemode", script))

    def test_administrator_joins_e1s_administrators_group(self):
        """admin_group= in sysrun.conf (E1) names the group; a missing group gives an account outside any."""
        etc = self.tmp / "etc"
        (etc / "wineexplorer").mkdir(parents=True)
        (etc / "pam.d").mkdir()
        (etc / "wineexplorer" / "sysrun.conf").write_text("group=wexsys\nadmin_group=wexadmins\n", encoding="ascii")
        block = self.safe_mode_block()
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir()
        for name, body in (("systemctl", "exit 0\n"),
                           ("getent", '[ "$1" = group ] && [ "$2" = "$FAKE_GROUP" ]\n'),
                           ("useradd", 'echo "useradd $*" >> "$LOG"\n')):
            (bin_dir / name).write_text("#!/bin/sh\n" + body, encoding="ascii")
            (bin_dir / name).chmod(0o755)
        log = self.tmp / "calls.log"
        for group, expected in (("wexadmins", "--groups wexadmins administrator"), ("other", "--shell /bin/bash administrator")):
            log.unlink(missing_ok=True)
            env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}", LOG=str(log), FAKE_GROUP=group)
            subprocess.run(["sh", "-e", "-c", block], check=True, env=env)
            self.assertTrue(log.read_text().strip().endswith(expected), log.read_text())

    def test_setup_copies_roots_password_to_administrator(self):
        setup = (REPO_ROOT / "installer" / "scripts" / "wex-gui-setup.sh").read_text(encoding="utf-8")
        copy = setup.index("chpasswd -e")
        self.assertLess(setup.index("wex-setup-machine.sh || echo"), copy)
        self.assertIn("''|'*'|'!'*) echo \"No Administrator password", setup)

    def test_vintner_variant(self):
        """The Advanced Options entry as a Vintner Boot variant, in the format of Vintner's docs/variants.md (V1,
        branch wo-0010-p43a): key=value lines, # comments, no quoting; title printable and at most 64 bytes;
        tokens blank-separated, without root=, vintner.* tokens, quotes, backslashes or $."""
        path = REPO_ROOT / "installer" / "vintner" / "advanced.conf"
        self.assertRegex(path.stem, r"^[a-z][a-z0-9_]{0,23}$")
        values = {}
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, _, value = line.partition("=")
            self.assertIn(key, ("title", "tokens"))
            self.assertNotIn(key, values)
            values[key] = value.strip()
        title, tokens = values["title"], values["tokens"].split()
        self.assertTrue(title.isprintable() and 0 < len(title.encode("utf-8")) <= 64, title)
        self.assertEqual(tokens, ["wexmode=menu"])
        for token in tokens:
            self.assertFalse(token.startswith(("root=", "vintner.")), token)
            self.assertFalse(set(token) & set("'\"\\$"), token)
        script = PACKAGE.read_text(encoding="utf-8")
        self.assertIn('"$SCRIPT_DIR/vintner/advanced.conf" "$STAGE_SESSION/etc/vintner/variants.d/advanced.conf"', script)
        self.assertIn('echo /etc/vintner/variants.d/advanced.conf >> "$STAGE_SESSION/DEBIAN/conffiles"', script)

    def test_vintner_product_name(self):
        """The menu's name for Vintner Boot, in the format of Vintner's docs/product.md (4.9): key=value lines,
        # comments, no quoting, only the key name; printable, 1 to 64 bytes."""
        path = REPO_ROOT / "installer" / "vintner" / "product.conf"
        lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
        values = [line for line in lines if line and not line.startswith("#")]
        self.assertEqual(values, ["name=WineExplorer"])
        script = PACKAGE.read_text(encoding="utf-8")
        self.assertIn('"$SCRIPT_DIR/vintner/product.conf" "$STAGE_SESSION/etc/vintner/product.conf"', script)
        self.assertIn('echo /etc/vintner/product.conf >> "$STAGE_SESSION/DEBIAN/conffiles"', script)

    def test_no_network_logon_for_root_or_administrator(self):
        conf = (REPO_ROOT / "installer" / "ssh" / "sshd-wineexplorer.conf").read_text(encoding="utf-8").splitlines()
        for line in ("PermitRootLogin no", "DenyUsers administrator", "PermitEmptyPasswords no"):
            self.assertIn(line, conf)

    def test_vm_test_installs_what_the_package_installs(self):
        """installer/tests/safe-mode.sh copies the Safe Mode files to the places the package puts them."""
        package = PACKAGE.read_text(encoding="utf-8")
        vm_test = VM_TEST.read_text(encoding="utf-8")
        listing = vm_test[vm_test.index("SAFE_MODE_FILES=(\n"):]
        listing = listing[:listing.index("\n)\n")]
        entries = re.findall(r'^\s+"(\d{3}) (\S+) (\S+)"$', listing, re.M)
        self.assertGreater(len(entries), 10)
        for mode, src, dst in entries:
            self.assertTrue((REPO_ROOT / src).is_file(), f"{src} does not exist")
            src_name, dst_dir, dst_name = Path(src).name, str(Path(dst).parent), Path(dst).name
            self.assertIn(src_name, package, f"the package does not install {src}")
            installed = (f'$STAGE_SESSION/{dst}"' in package
                         or (src_name == dst_name and f'$STAGE_SESSION/{dst_dir}/' in package))
            self.assertTrue(installed, f"the package does not install {src} as /{dst}")
        sources = {Path(src).name for _, src, _ in entries}
        for piece in ("wex-bootmode.sh", "wex-advanced-options.py", "wex-bootlog.sh", "wex-bootmode-generator",
                      "wex-safemode-pam-check.sh", "wex-greeter.sh", "wine-explorer-session.sh",
                      "wine-explorer-session-common.sh", "wex-safe-terminal.sh", "wex-advanced-options.target",
                      "wex-advanced-options.service", "wex-safemode.target", "wex-safenet.target",
                      "wex-bootlog.service", "11_wineexplorer_advanced", "wex-startup-check.sh",
                      "wex-startup-check.service", "wex-lastgood.sh"):
            self.assertIn(piece, sources, f"the VM test does not install {piece}")

    def test_postinst_migrates_and_is_idempotent(self):
        etc, run, acc = self.tmp / "etc", self.tmp / "run", self.tmp / "var/lib/AccountsService/users"
        for d in (etc / "pam.d", etc / "lightdm/lightdm.conf.d", etc / "systemd/system/multi-user.target.wants",
                  run / "wex-safemode", acc):
            d.mkdir(parents=True)
        (etc / "pam.d/lightdm").write_text(
            "auth requisite pam_exec.so quiet /usr/local/sbin/wex-safemode-pam-check.sh\n#%PAM-1.0\n"
            "@include common-auth\n", encoding="ascii")
        (etc / "pam.d/lightdm-autologin").write_text("#%PAM-1.0\nauth required pam_permit.so\n", encoding="ascii")
        (etc / "lightdm/lightdm.conf.d/99-safemode.conf").write_text("[Seat:*]\n", encoding="ascii")
        (run / "wex-safemode/60-wineexplorer-autologin.conf.bak").write_text("[Seat:*]\nautologin-user=bob\n")
        (etc / "systemd/system/multi-user.target.wants/wex-safemode.service").symlink_to("/nonexistent")
        (acc / "alice").write_text("[User]\nSession=wex-safe-terminal\nXSession=wex-safe-terminal\n")
        (acc / "carol").write_text("[User]\nSession=wine-explorer-wayland\n")
        # the old Safe Mode wrote dave a file that hid him; erin's own file was backed up; svc is a system account
        hidden = "[User]\nSession=wine-explorer\nXSession=wine-explorer\nSystemAccount=true\n"
        (acc / "dave").write_text(hidden)
        (acc / "erin").write_text(hidden)
        (run / "wex-safemode/orig-erin").write_text("[User]\nSession=wine-explorer\n")
        (acc / "svc").write_text(hidden)
        block = self.safe_mode_block()
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir()
        fakes = {
            "systemctl": 'echo "systemctl $*" >> "$LOG"\n',
            # getent passwd NAME: known once the fake useradd made it; getent group GROUP: only $FAKE_GROUP exists
            "getent": 'case "$1" in\n    passwd) grep -qx "$2" "$LOG.users" 2>/dev/null ;;\n'
                      '    group) [ "$2" = "$FAKE_GROUP" ] ;;\n    *) exit 2 ;;\nesac\n',
            "useradd": 'echo "useradd $*" >> "$LOG"\nfor last; do :; done\necho "$last" >> "$LOG.users"\n',
            # id -u NAME: the test accounts are standard users, svc a system account
            "id": 'case "$2" in svc) echo 120 ;; alice|carol|dave|erin) echo 1001 ;; *) exit 1 ;; esac\n',
        }
        for name, body in fakes.items():
            (bin_dir / name).write_text("#!/bin/sh\n" + body, encoding="ascii")
            (bin_dir / name).chmod(0o755)
        log = self.tmp / "calls.log"
        env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}", LOG=str(log), FAKE_GROUP="sudo")
        for _ in range(2):
            subprocess.run(["sh", "-e", "-c", block], check=True, env=env)
        self.assertEqual(log.read_text().splitlines(), [
            "systemctl enable wex-startup-check.service",
            "systemctl enable wex-bootmode-record.service",
            "useradd --create-home --comment Administrator --shell /bin/bash --groups sudo administrator",
            "systemctl enable wex-startup-check.service",
            "systemctl enable wex-bootmode-record.service"])
        self.assertEqual((acc / "administrator").read_text(),
                         "[User]\nSession=wine-explorer\nXSession=wine-explorer\nSystemAccount=false\n")
        lightdm = (etc / "pam.d/lightdm").read_text()
        self.assertEqual(lightdm.count("wex-safemode-pam-check.sh"), 1)
        self.assertTrue(lightdm.rstrip().endswith(
            "account requisite pam_exec.so quiet stdout /usr/local/sbin/wex-safemode-pam-check.sh"))
        autologin = (etc / "pam.d/lightdm-autologin").read_text()
        self.assertEqual(autologin.count("wex-safemode-pam-check.sh autologin"), 1)
        self.assertFalse((etc / "lightdm/lightdm.conf.d/99-safemode.conf").exists())
        self.assertIn("bob", (etc / "lightdm/lightdm.conf.d/60-wineexplorer-autologin.conf").read_text())
        self.assertFalse((run / "wex-safemode").exists())
        self.assertFalse(os.path.lexists(etc / "systemd/system/multi-user.target.wants/wex-safemode.service"))
        self.assertEqual((acc / "alice").read_text(), "[User]\nSession=wine-explorer\nXSession=wine-explorer\n")
        self.assertEqual((acc / "carol").read_text(), "[User]\nSession=wine-explorer-wayland\n")
        self.assertEqual((acc / "dave").read_text(), hidden.replace("=true", "=false"))
        self.assertEqual((acc / "erin").read_text(), "[User]\nSession=wine-explorer\n")
        self.assertEqual((acc / "svc").read_text(), hidden)


if __name__ == "__main__":
    unittest.main()
