#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for Safe Mode and the Advanced Options menu (docs/SAFE_MODE.md). Run on Linux: python3 scripts/test_safemode.py"""

import configparser
import contextlib
import curses
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import time
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = REPO_ROOT / "scripts"
SYSTEMD = REPO_ROOT / "installer" / "systemd"
LIVE_ETC = REPO_ROOT / "installer" / "live-build" / "config" / "includes.chroot" / "etc"
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

    def fake_helper(self, mode, status=0, option=""):
        """A wex-bootmode stand-in that prints MODE, and OPTION for --option."""
        helper = self.tmp / "fake-bootmode"
        helper.write_text(f'#!/bin/sh\nif [ "$1" = --option ]; then echo {option}; exit 0; fi\necho {mode}\nexit {status}\n',
                          encoding="ascii")
        helper.chmod(0o755)
        return str(helper)


class TestSyntax(unittest.TestCase):
    def test_shell_scripts_parse(self):
        for script in (BOOTMODE, GENERATOR, PAM_CHECK, STARTUP_CHECK, SCRIPTS / "wex-bootlog.sh", LASTGOOD,
                       SCRIPTS / "wex-session-choice.sh"):
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
        # the Safe Mode before docs/SAFE_MODE.md, and the GRUB path (removed 2026-11-11: Vintner Boot is the loader)
        for old in (SCRIPTS / "wex-safemode-setup.sh", SYSTEMD / "wex-safemode.service",
                    SCRIPTS / "wex-safe-terminal.desktop", LIVE_ETC / "grub.d" / "11_wineexplorer_safemode",
                    LIVE_ETC / "grub.d" / "11_wineexplorer_advanced", LIVE_ETC / "default" / "grub.d"):
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

    def option(self, value=None, root_uid=None, link=False):
        """wex-bootmode --option with /run/wineexplorer/bootoption holding VALUE (None: no file)."""
        option_file = self.tmp / "bootoption"
        option_file.unlink(missing_ok=True)
        if value is not None:
            target = self.tmp / "elsewhere" if link else option_file
            target.write_text(value + "\n", encoding="ascii")
            if link:
                option_file.symlink_to(target)
        env = dict(os.environ, WEX_BOOTMODE_OPTION_STATE=str(option_file), WEX_BOOTMODE_STATE=str(self.tmp / "none"),
                   WEX_BOOTMODE_ROOT_UID=str(os.getuid() if root_uid is None else root_uid))
        result = subprocess.run(["sh", str(BOOTMODE), "--option"], env=env, capture_output=True, text=True, check=True)
        return result.stdout

    def test_option(self):
        """The menu's start option (Enable VGA Mode, work order D2), from a root-owned regular file only."""
        self.assertEqual(self.option("vga"), "vga\n")
        self.assertEqual(self.option("bootlog"), "bootlog\n")
        self.assertEqual(self.option(), "")
        self.assertEqual(self.option("bogus"), "")
        self.assertEqual(self.option("vga", root_uid=os.getuid() + 1), "")
        self.assertEqual(self.option("vga", link=True), "")

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
    """wex-startup-check.sh: which starts count. A failed check keeps boot-complete.target, where Vintner Boot counts
    the start good, from being reached."""

    WELCOME = "lightdm|C:\\windows\\system32\\logonui.exe"
    GTK = "lightdm|/usr/sbin/lightdm-gtk-greeter"
    VARIANT = "root=UUID=x ro quiet vintner.bootstorage=PARTUUID=y wexmode=menu vintner.variant=advanced"

    def check(self, cmdline="root=UUID=x ro quiet", mode="normal", processes=(), sessions="",
              session_class="", session_type="", active="", stage=False, menu_start=False):
        """processes: "user|command line" entries that the fake pgrep matches its -u and -f pattern against.
        menu_start: the menu's record of a start that counts is there. Returns (exit status, counted, output)."""
        self.menu_start = self.tmp / "menu-start"
        if menu_start:
            self.menu_start.write_text(f"{mode} -\n", encoding="ascii")
        else:
            self.menu_start.unlink(missing_ok=True)
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir(exist_ok=True)
        self.lastgood_log = self.tmp / "lastgood.log"
        self.lastgood_log.unlink(missing_ok=True)
        fakes = {
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
        stage_file = self.tmp / "stage"
        if stage:
            stage_file.touch()
        else:
            stage_file.unlink(missing_ok=True)
        env = {"PATH": f"{bin_dir}:/usr/bin:/bin", "WEX_BOOTMODE_CMDLINE": str(cmdline_file),
               "WEX_BOOTMODE_HELPER": self.fake_helper(mode), "WEX_BOOTMODE_SETUP_STAGE": str(stage_file),
               "WEX_STARTUP_TIMEOUT": "0", "WEX_STARTUP_INTERVAL": "0",
               "FAKE_PROCS": "\n".join(processes), "FAKE_SESSIONS": sessions, "FAKE_CLASS": session_class, "FAKE_TYPE": session_type,
               "FAKE_ACTIVE": active, "WEX_LASTGOOD_HELPER": str(bin_dir / "fake-lastgood"),
               "LASTGOOD_LOG": str(self.lastgood_log), "WEX_MENU_START": str(self.menu_start)}
        result = subprocess.run(["sh", str(STARTUP_CHECK)], env=env, capture_output=True, text=True, timeout=30)
        counted = result.returncode == 0 and "counts as successful" in result.stdout
        return result.returncode, counted, result.stdout

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

    def test_safe_mode_through_the_menu_does_not_count(self):
        """The Advanced Options variant in a Safe Mode, also with Safe Mode chosen on the failure screen before the
        restart into it: as on XP, neither success nor failure counts."""
        for mode in ("safe", "safenet", "safecmd"):
            status, counted, out = self.check(cmdline=self.VARIANT, mode=mode, processes=[self.WELCOME])
            self.assertEqual((status, counted), (0, False), mode)
            self.assertIn(f"not counted: Safe Mode through the Advanced Options menu ({mode})", out)
            self.assertEqual(self.check(cmdline=self.VARIANT, mode=mode)[:2], (0, False), f"{mode}, no logon screen")

    def test_other_starts_through_the_menu_count(self):
        """Any other choice of the menu (Enable VGA Mode, Start Normally, the other options) counts as on XP: the
        logon screen makes it a successful start, and without one it fails."""
        status, counted, out = self.check(cmdline=self.VARIANT, mode="normal", processes=[self.WELCOME])
        self.assertEqual((status, counted), (0, True))
        self.assertIn("started through the Advanced Options menu (normal): it counts, as on XP", out)
        self.assertEqual(self.check(cmdline="ro quiet wexmode=menu", processes=[self.WELCOME])[:2], (0, True))
        self.assertEqual(self.check(cmdline=self.VARIANT, mode="normal")[:2], (1, False))

    def test_the_menus_record_goes_once_a_start_succeeds(self):
        """The record of a start through the menu stays when that start fails (the next ordinary start shows the
        failure screen) and goes with any successful start, through the menu or not."""
        self.check(cmdline=self.VARIANT, mode="normal", menu_start=True)
        self.assertTrue(self.menu_start.exists(), "a failed start keeps it")
        self.check(cmdline=self.VARIANT, mode="normal", processes=[self.WELCOME], menu_start=True)
        self.assertFalse(self.menu_start.exists(), "the start through the menu succeeded")
        self.check(processes=[self.WELCOME], menu_start=True)
        self.assertFalse(self.menu_start.exists(), "an ordinary start succeeded")
        self.check(cmdline=self.VARIANT, mode="safe", processes=[self.WELCOME], menu_start=True)
        self.assertTrue(self.menu_start.exists(), "a Safe Mode start counts nothing")

    def test_an_ordinary_vintner_start_counts(self):
        """A generation's own start, also when the failure screen showed and Start Normally went on with it."""
        cmdline = "root=UUID=x ro quiet vintner.bootstorage=PARTUUID=y"
        self.assertEqual(self.check(cmdline=cmdline, processes=[self.WELCOME])[:2], (0, True))

    def test_the_failure_flag_is_gone(self):
        """GRUB's wexfailed=1 and wex_boot_pending (removed 2026-11-11) mean nothing to the check."""
        script = STARTUP_CHECK.read_text(encoding="utf-8")
        for word in ("wexfailed", "wex_boot_pending", "grub"):
            self.assertNotIn(word, script.lower())

    def saved_lastgood(self):
        return self.lastgood_log.exists() and self.lastgood_log.read_text() == "save\n"

    def test_a_counted_start_becomes_the_last_known_good(self):
        self.assertEqual(self.check(processes=[self.WELCOME])[0], 0)
        self.assertTrue(self.saved_lastgood())

    def test_setup_safe_mode_and_failed_starts_are_not_saved(self):
        self.check(stage=True, active="wex-setup.service")
        self.assertFalse(self.saved_lastgood(), "Setup's starts")
        self.check(cmdline=self.VARIANT, mode="safe", processes=[self.WELCOME])
        self.assertFalse(self.saved_lastgood(), "Safe Mode (from the menu or the failure screen)")
        self.check()
        self.assertFalse(self.saved_lastgood(), "a failed start")
        # Enable VGA Mode and the menu's other normal starts update the Last Known Good, as on XP
        self.check(cmdline=self.VARIANT, mode="normal", processes=[self.WELCOME])
        self.assertTrue(self.saved_lastgood(), "a start through the menu")


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

    def test_waits_for_a_server_saving_the_registry(self):
        """Wine's user servers save system.reg in place under an exclusive flock on the machine root's .reg.lock: the
        copy and the restore wait for it, so a half-written file is never read or overwritten."""
        self.write_reg()
        lock = self.machine / ".reg.lock"
        lock.touch()
        for command in ("save", "restore"):
            holder = subprocess.Popen(["flock", "-x", str(lock), "-c", "echo held; sleep 2"], stdout=subprocess.PIPE,
                                      text=True)
            self.assertEqual(holder.stdout.readline(), "held\n")
            start = time.monotonic()
            result = self.lastgood(command)
            waited = time.monotonic() - start
            holder.wait(timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertGreater(waited, 1.0, f"{command} did not wait for the lock")
        # no lock file (no server has used the machine root): no wait
        lock.unlink()
        self.assertEqual(self.lastgood("save").returncode, 0)

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
        self.menu.CRASH_RUNTIME = str(self.tmp / "run-system.conf.d" / "99-wineexplorer-norestart.conf")
        self.menu.LASTGOOD = str(self.tmp / "no-lastgood")      # no copy saved: Last Known Good is grayed
        self.menu.CMDLINE = str(self.tmp / "cmdline")            # none: Vintner Boot did not start this system
        self.menu.VINTNER = str(self.tmp / "no-vintner")
        self.menu.PENDING = str(self.tmp / "next-bootmode")
        self.menu.MENU_START = str(self.tmp / "lib" / "menu-start")
        self.menu.BOOT_CONF = str(self.tmp / "boot.conf")        # none: the failure screen counts down from 30
        self.menu.Screen = FakeScreen
        self.calls = []

    def with_vintner(self, status, report=None):
        """A Vintner Boot start, whose `vintner` logs its arguments and exits with STATUS; `vintner status` prints
        REPORT (a dict, as JSON)."""
        (self.tmp / "cmdline").write_text("BOOT_IMAGE=x root=UUID=1 vintner.bootstorage=PARTUUID=2 quiet\n",
                                          encoding="ascii")
        (self.tmp / "status.json").write_text(json.dumps(report) if report is not None else "", encoding="ascii")
        helper = self.tmp / "fake-vintner"
        helper.write_text(f'#!/bin/sh\necho "$@" >> "{self.tmp}/vintner.log"\n'
                          f'[ "$1" = status ] && cat "{self.tmp}/status.json"\nexit {status}\n', encoding="ascii")
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
        self.assertEqual(grayed, ["Last Known Good Configuration (your most recent settings that worked)",
                                  "Directory Services Restore Mode (Windows domain controllers only)"])
        vga = self.menu.menu_items(False)[5]
        self.assertEqual((vga.label, vga.action, vga.mode, vga.option), ("Enable VGA Mode", "boot", "normal", "vga"))
        grayed = [item.label for item in self.menu.menu_items(True) if item and not item.selectable]
        self.assertIn("Safe Mode with Command Prompt", grayed)

    def test_navigation_skips_gaps_and_grayed_items_and_wraps(self):
        """As XP's lists (C5): Up on the first item goes to the last, Down on the last to the first."""
        items = self.menu.menu_items(False)
        self.assertEqual(self.menu.first_selectable(items), 0)
        self.assertEqual(self.menu.start_normally(items), 11)
        self.assertEqual(self.menu.move(items, 0, -1), 13)           # Safe Mode, Up: Return to OS Choices Menu
        self.assertEqual(self.menu.move(items, 13, 1), 0)            # and back
        self.assertEqual(self.menu.move(items, 2, 1), 4)             # over the blank line
        self.assertEqual(self.menu.move(items, 4, 1), 5)             # Enable VGA Mode
        self.assertEqual(self.menu.move(items, 5, 1), 8)             # over two grayed items
        self.assertEqual(self.menu.move(items, 11, -1), 9)           # Start Normally, Up: Disable automatic restart
        pending = self.menu.menu_items(True)
        self.assertEqual(self.menu.first_selectable(pending), 4)
        self.assertEqual(self.menu.move(pending, 13, 1), 4)          # the grayed Safe Modes are skipped too
        lone = [self.menu.Item("Start", "start"), None, self.menu.Item("Grayed", None, unavailable="no")]
        self.assertEqual(self.menu.move(lone, 0, 1), 0)              # nothing else to go to

    def test_choices(self):
        """The menu opens on Start Normally, as XP's does."""
        enter, up, down, f8 = 10, curses.KEY_UP, curses.KEY_DOWN, curses.KEY_F8
        self.assertEqual(self.run_keys(enter), ("boot", "normal", None))                   # Start Normally
        self.assertEqual(self.run_keys(down, enter), ("reboot", None, None))
        self.assertEqual(self.run_keys(down, down, enter, enter), ("boot", "normal", None))  # OS Choices
        self.assertEqual(self.run_keys(*[down] * 3, enter, enter), ("boot", "safe", None))   # wrapped round
        self.assertEqual(self.run_keys(*[down] * 5, enter, enter), ("boot", "safecmd", None))
        self.assertEqual(self.run_keys(*[down] * 6, enter, enter), ("boot", "normal", "bootlog"))
        self.assertEqual(self.run_keys(up, enter, enter), ("boot", "normal", "norestart"))
        self.assertEqual(self.run_keys(up, down, enter), ("boot", "normal", None))
        # F8 in the OS list returns to the menu, on Start Normally again
        self.assertEqual(self.run_keys(*[down] * 3, enter, f8, *[down] * 4, enter, enter), ("boot", "safenet", None))
        self.assertEqual(self.vintner_log(), [])

    PGREP = ["pgrep", "-x", "plymouthd"]

    def plymouth(self, answers, alive=()):
        """quit_plymouth against a fake run: `plymouth --ping` answers from ANSWERS (0 = still running); `pgrep -x
        plymouthd` from ALIVE (0 = the process is still there), then 1."""
        calls, naps = [], []
        answers, alive = list(answers), list(alive)

        def run(cmd, check=False, **kwargs):
            calls.append(cmd[1:] if cmd[0] == self.menu.PLYMOUTH else cmd)
            if cmd[0] == self.menu.PLYMOUTH:
                self.assertEqual(kwargs.get("timeout"), self.menu.PLYMOUTH_TIMEOUT, "a request that can block")
            if cmd[1:] == ["--ping"]:
                code = answers.pop(0)
                if code == "hang":
                    raise subprocess.TimeoutExpired(cmd, kwargs["timeout"])
            elif cmd == self.PGREP:
                code = alive.pop(0) if alive else 1
            else:
                code = 0
            return subprocess.CompletedProcess(cmd, code)

        # not a virtual console: the mode switch is skipped; the menu's notes go to the journal, not the test output
        with open(self.tmp / "console", "w") as console, contextlib.redirect_stderr(io.StringIO()):
            self.menu.quit_plymouth(run=run, sleep=naps.append, console=console.fileno(), tries=5, grace=3)
        return calls, naps

    def test_plymouth_is_asked_until_it_is_gone(self):
        calls, naps = self.plymouth([0, 0, 1])
        self.assertEqual(calls, [["--ping"], ["quit"], ["--ping"], ["quit"], ["--ping"], self.PGREP])
        self.assertEqual(naps, [0.25, 0.25])
        calls, naps = self.plymouth([1])          # not running: nothing to quit
        self.assertEqual(calls, [["--ping"], self.PGREP])

    def test_plymouth_that_does_not_quit_is_ended(self):
        calls, naps = self.plymouth([0] * 5, alive=[0, 0, 0])
        self.assertEqual(calls.count(["quit"]), 5)
        self.assertEqual(calls[-2:], [["pkill", "-TERM", "-x", "plymouthd"], self.PGREP])   # gone after TERM

    def test_plymouthd_that_lingers_after_quitting_is_ended(self):
        """2026-11-11: plymouthd stopped answering at once, but held the screen for three minutes more."""
        calls, naps = self.plymouth([0, 1], alive=[0, 0, 1])         # leaves within its grace
        self.assertNotIn(["pkill", "-TERM", "-x", "plymouthd"], calls)
        self.assertEqual(calls.count(self.PGREP), 3)
        calls, naps = self.plymouth([0, 1], alive=[0] * 6)            # ignores TERM too
        self.assertEqual([c for c in calls if c[0] == "pkill"],
                         [["pkill", "-TERM", "-x", "plymouthd"], ["pkill", "-KILL", "-x", "plymouthd"]])
        self.assertEqual(calls.count(self.PGREP), 6)

    def test_plymouthd_that_does_not_answer_is_ended(self):
        """2026-11-11: plymouthd answered nothing for three minutes, and the menu's first `--ping` waited all along."""
        calls, naps = self.plymouth(["hang"], alive=[0, 0, 0, 1])
        self.assertEqual(calls, [["--ping"], self.PGREP, self.PGREP, self.PGREP,
                                 ["pkill", "-TERM", "-x", "plymouthd"], self.PGREP])

    def test_no_plymouth_installed(self):
        def run(cmd, check=False, **kwargs):
            raise FileNotFoundError(cmd[0])
        self.menu.quit_plymouth(run=run, sleep=lambda s: None, console=-1)   # neither raises

    def test_os_choices_with_vintner_restarts_into_its_menu(self):
        down, enter = curses.KEY_DOWN, 10
        self.with_vintner(0)
        self.assertEqual(self.run_keys(down, down, enter), ("reboot", None, None))
        self.assertEqual(self.vintner_log(), ["next-boot menu"])
        # the other items do not ask Vintner
        self.assertEqual(self.run_keys(*[down] * 3, enter, enter), ("boot", "safe", None))
        self.assertEqual(self.vintner_log(), ["next-boot menu"])

    @staticmethod
    def report(kind="generation", generation=3, newest_good=2, previous_good=2):
        """A `vintner status --json` report (Vintner Boot's docs/status.md, format 1), cut to what the menu reads."""
        return {"format": 1, "newest_good": newest_good, "previous_good": previous_good,
                "booted": {"kind": kind, "generation": generation, "variant": "advanced" if kind == "variant" else "",
                           "failed_attempts": 1, "is_newest_good": generation == newest_good}}

    def test_previous_good_only_when_this_generation_never_started_well(self):
        previous = self.menu.previous_good
        self.assertEqual(previous(self.report()), 2)                          # generation 3 is not GOOD yet
        self.assertEqual(previous(self.report("variant")), 2)                 # ... started through the variant
        self.assertIsNone(previous(self.report(generation=2)))                # generation 2 is the newest GOOD one
        self.assertIsNone(previous(self.report(generation=1)))                # an older one, after a fallback
        self.assertIsNone(previous(self.report(newest_good=None, previous_good=None)))   # nothing GOOD
        self.assertIsNone(previous(self.report("recovery")))
        self.assertIsNone(previous(self.report("unknown")))
        self.assertIsNone(previous(None))                                     # no report
        self.assertIsNone(previous({"format": 1, "booted": None}))

    def test_vintner_status(self):
        self.assertIsNone(self.menu.vintner_status())                         # not started by Vintner: nothing asked
        self.assertEqual(self.vintner_log(), [])
        self.with_vintner(0, self.report())
        self.assertEqual(self.menu.vintner_status()["previous_good"], 2)
        self.assertEqual(self.vintner_log(), ["status --json"])
        self.with_vintner(0, {"format": 2})                                   # a format this menu does not know
        self.assertIsNone(self.menu.vintner_status())
        self.with_vintner(3, None)                                            # not a vintner installation
        self.assertIsNone(self.menu.vintner_status())

    def test_last_known_good_goes_back_a_generation(self):
        self.with_vintner(0)
        self.with_lastgood()
        restore = self.tmp / "restored"
        self.menu.LASTGOOD = str(self.tmp / "fake-lastgood")
        (self.tmp / "fake-lastgood").write_text(f'#!/bin/sh\n[ "$1" = restore ] && touch "{restore}"\nexit 0\n',
                                                encoding="ascii")
        self.assertTrue(self.menu.go_back_to_previous_good())
        self.assertEqual(self.vintner_log(), ["next-boot previous-good"])
        self.assertTrue(restore.exists())                                     # the registry goes back too

    def test_last_known_good_when_vintner_refuses(self):
        self.with_vintner(3)                                                  # no-previous-good
        self.assertFalse(self.menu.go_back_to_previous_good())

    def test_last_known_good_offered_for_the_previous_generation(self):
        up, enter = curses.KEY_UP, 10
        # no control set saved, but a previous good generation: offered, and chosen like any option
        items = self.menu.menu_items(False, True)
        self.assertTrue(items[6].selectable)
        self.assertEqual(self.menu.run_menu(FakeStdscr([up, up, up, enter, enter]), 2), ("boot", "normal", "lastgood"))
        # neither: grayed, so two Ups from Start Normally reach Debugging Mode, a third Enable VGA Mode and a fourth
        # Enable Boot Logging
        self.assertEqual(self.run_keys(up, up, enter, enter), ("boot", "normal", "debug"))
        self.assertEqual(self.run_keys(up, up, up, enter, enter), ("boot", "normal", "vga"))
        self.assertEqual(self.run_keys(up, up, up, up, enter, enter), ("boot", "normal", "bootlog"))

    # ---- "did not start successfully" with Vintner Boot (wex-failure-check.service, wex-failure-screen.service) ----

    def failing(self, failed=1, kind="generation", generation=1, variant=""):
        report = self.report(kind, generation, newest_good=1, previous_good=None)
        report["booted"].update(failed_attempts=failed, variant=variant)
        return report

    def test_failed_starts(self):
        failed = self.menu.failed_starts
        self.assertEqual(failed(self.failing(2)), 2)
        self.assertEqual(failed(self.failing(0)), 0)
        self.assertEqual(failed(self.failing(None)), 0)                     # no counter (recovery B)
        self.assertEqual(failed(self.failing(1, "variant", variant="advanced")), 0)   # through the menu
        self.assertEqual(failed(self.failing(1, "recovery")), 0)
        self.assertEqual(failed(None), 0)

    def test_failure_check_respects_recovery_options_turned_off(self):
        # XP (measured 2026-11-12): "Time to display recovery options when needed" unchecked, a failed start shows no
        # failure screen at all and starts normally
        calls = []
        self.with_vintner(0, self.failing(1))
        (self.tmp / "boot.conf").write_text("recovery_menu=0\nrecovery_timeout=7\n", encoding="utf-8")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(self.menu.failure_check(run=self.recording_run(calls)), 0)
        self.assertEqual(calls, [[self.menu.VINTNER, "status", "--json"]])
        self.assertIn("recovery options are turned off", err.getvalue())
        # turned on again, the kept seconds apply
        (self.tmp / "boot.conf").write_text("recovery_menu=1\nrecovery_timeout=7\n", encoding="utf-8")
        calls.clear()
        with contextlib.redirect_stderr(io.StringIO()):
            self.menu.failure_check(run=self.recording_run(calls))
        self.assertEqual(calls[-1], ["systemctl", "start", "wex-failure-screen.service"])
        self.assertTrue(self.menu.recovery_enabled())
        self.assertEqual(self.menu.failure_countdown(), 7)

    def test_failure_check_starts_the_screen(self):
        self.menu.PENDING = str(self.tmp / "pending")
        calls = []
        self.with_vintner(0, self.failing(1))
        with contextlib.redirect_stderr(io.StringIO()):
            self.menu.failure_check(run=self.recording_run(calls))
        self.assertEqual(calls, [[self.menu.VINTNER, "status", "--json"],
                                 ["systemctl", "start", "wex-failure-screen.service"]])
        # nothing failed: no screen; while Setup runs: not even a question
        calls.clear()
        self.with_vintner(0, self.failing(0))
        self.menu.failure_check(run=self.recording_run(calls))
        self.assertEqual(calls, [[self.menu.VINTNER, "status", "--json"]])
        # a start through the menu that counts did not finish (its record is still there): the screen too
        calls.clear()
        os.makedirs(os.path.dirname(self.menu.MENU_START))
        with open(self.menu.MENU_START, "w", encoding="ascii") as f:
            f.write("normal vga\n")
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.menu.failure_check(run=self.recording_run(calls))
        self.assertEqual(calls, [[self.menu.VINTNER, "status", "--json"],
                                 ["systemctl", "start", "wex-failure-screen.service"]])
        self.assertIn("a start through the Advanced Options menu did not finish", err.getvalue())
        calls.clear()
        (self.tmp / "stage").write_text("oobe\n")
        self.menu.failure_check(run=self.recording_run(calls))
        self.assertEqual(calls, [])

    def recording_run(self, calls, codes=None, boots_next=1):
        """A subprocess.run that records each command and runs `vintner status` for real (the fake prints the
        report); CODES maps a command's first two words to its exit code (default 0), and ("--generation",) to the
        code of a command with that flag (2: an older vintner without it). `vintner next-boot --json variant` answers
        as vintner does: the generation --generation names, else BOOTS_NEXT."""
        def run(cmd, check=False, **kwargs):
            calls.append(list(cmd))
            if cmd[:2] == [self.menu.VINTNER, "status"]:
                return subprocess.run(cmd, check=check, **kwargs)
            code = (codes or {}).get(tuple(cmd[:2]), 0)
            if "--generation" in cmd:
                code = (codes or {}).get(("--generation",), code)
            if check and code:
                raise subprocess.CalledProcessError(code, cmd)
            stdout = ""
            if cmd[:4] == [self.menu.VINTNER, "next-boot", "--json", "variant"] and code in (0, 3):
                generation = int(cmd[cmd.index("--generation") + 1]) if "--generation" in cmd else boots_next
                stdout = json.dumps({"ok": True, "target": "variant", "firmware": "bios", "generation": generation,
                                     "variant": cmd[4]} if code == 0 else
                                    {"ok": False, "code": "nothing-bootable",
                                     "error": "no ordinary generation has tries left"})
            return subprocess.CompletedProcess(cmd, code, stdout, "")
        return run

    def test_failure_screen_safe_mode_restarts_into_the_variant(self):
        self.menu.PENDING = str(self.tmp / "lib" / "next-bootmode")
        calls = []
        restarted = self.menu.finish_failure(self.failing(1, generation=3), "safecmd", None,
                                             run=self.recording_run(calls))
        self.assertTrue(restarted)
        # the generation that showed the screen, named: on its last try it is not the one that would boot next
        self.assertEqual(calls, [[self.menu.VINTNER, "next-boot", "--json", "variant", "advanced", "--generation", "3"],
                                 ["systemctl", "reboot"]])
        self.assertEqual(Path(self.menu.PENDING).read_text(), "safecmd 3\n")
        self.assertEqual(Path(self.menu.PENDING).stat().st_mode & 0o777, 0o600)

    def test_failure_screen_safe_mode_when_vintner_refuses(self):
        self.menu.PENDING = str(self.tmp / "next-bootmode")
        calls = []
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            restarted = self.menu.finish_failure(self.failing(2), "safe", None,
                                                 run=self.recording_run(calls, {(self.menu.VINTNER, "next-boot"): 3}))
        self.assertFalse(restarted)                                           # this start goes on normally
        self.assertFalse(Path(self.menu.PENDING).exists())
        self.assertNotIn(["systemctl", "reboot"], calls)
        self.assertIn("cannot restart into safe (no ordinary generation has tries left)", err.getvalue())

    def test_failure_screen_safe_mode_with_an_older_vintner(self):
        """A vintner without --generation (exit 2) takes the generation that would boot next: the same one restarts;
        another one, or none, on this generation's last try, does not, and the request is taken back."""
        older = {("--generation",): 2}
        plain = [self.menu.VINTNER, "next-boot", "--json", "variant", "advanced"]
        self.menu.PENDING = str(self.tmp / "next-bootmode")
        calls = []
        self.assertTrue(self.menu.finish_failure(self.failing(1, generation=3), "safenet", None,
                                                 run=self.recording_run(calls, older, boots_next=3)))
        self.assertEqual(calls, [plain + ["--generation", "3"], plain, ["systemctl", "reboot"]])
        self.assertEqual(Path(self.menu.PENDING).read_text(), "safenet 3\n")
        calls.clear()
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertFalse(self.menu.finish_failure(self.failing(2, generation=3), "safecmd", None,
                                                      run=self.recording_run(calls, older, boots_next=2)))
        self.assertEqual(calls, [plain + ["--generation", "3"], plain, [self.menu.VINTNER, "next-boot", "clear"]])
        self.assertFalse(Path(self.menu.PENDING).exists())
        self.assertIn("the variant of generation 2, not of 3", err.getvalue())
        # no generation in the report: whatever vintner takes
        calls.clear()
        self.assertTrue(self.menu.finish_failure(None, "safe", None, run=self.recording_run(calls)))
        self.assertEqual(calls, [plain, ["systemctl", "reboot"]])

    def test_failure_screen_last_known_good_and_start_normally(self):
        calls = []
        # this generation was GOOD before: the registry goes back, and this start goes on (and counts)
        self.assertFalse(self.menu.finish_failure(self.failing(1), "normal", "lastgood", run=self.recording_run(calls)))
        self.assertEqual(calls, [[self.menu.LASTGOOD, "restore"]])
        # it never started successfully: the previous good generation, after a restart
        calls.clear()
        report = self.report(generation=3, newest_good=2, previous_good=2)
        self.assertTrue(self.menu.finish_failure(report, "normal", "lastgood", run=self.recording_run(calls)))
        self.assertEqual(calls[0], [self.menu.VINTNER, "next-boot", "previous-good"])
        self.assertEqual(calls[-1], ["systemctl", "reboot"])
        calls.clear()
        self.assertFalse(self.menu.finish_failure(self.failing(1), "normal", None, run=self.recording_run(calls)))
        self.assertEqual(calls, [])

    def test_pending_safe_mode_applies_once_to_its_variant(self):
        self.menu.PENDING = str(self.tmp / "next-bootmode")
        variant = self.failing(1, "variant", generation=3, variant="advanced")
        self.menu.write_pending("safenet", 3)
        self.assertEqual(self.menu.take_pending(variant), "safenet")
        self.assertIsNone(self.menu.take_pending(variant))                     # removed once read
        for status in (self.failing(1, generation=3),                          # an ordinary start
                       self.failing(1, "variant", generation=2, variant="advanced"),   # another generation
                       None):                                                  # no report
            self.menu.write_pending("safe", 3)
            self.assertIsNone(self.menu.take_pending(status))
            self.assertFalse(Path(self.menu.PENDING).exists())
        Path(self.menu.PENDING).write_text("normal 3\n")                        # only a Safe Mode is recorded
        self.assertIsNone(self.menu.take_pending(variant))

    def test_failure_units(self):
        check = (SYSTEMD / "wex-failure-check.service").read_text(encoding="utf-8").splitlines()
        for line in ("ConditionKernelCommandLine=vintner.bootstorage", "ConditionKernelCommandLine=!wexmode",
                     "ExecStart=/usr/local/sbin/wex-advanced-options --failure-check", "Before=basic.target shutdown.target",
                     "WantedBy=basic.target"):
            self.assertIn(line, check)
        self.assertFalse(any(line.startswith(("StandardInput=", "TTYPath=")) for line in check),
                         "the check must leave tty1 and the boot splash alone")
        screen = (SYSTEMD / "wex-failure-screen.service").read_text(encoding="utf-8").splitlines()
        for line in ("ExecStart=/usr/local/sbin/wex-advanced-options --failure-screen", "StandardInput=tty-force",
                     "TTYPath=/dev/tty1", "Before=basic.target shutdown.target"):
            self.assertIn(line, screen)
        for unit in (check, screen):
            self.assertLess(len(re.search(r"^Description=(.*)$", "\n".join(unit), re.M).group(1)), 30)

    def test_plymouth_ends_before_tty1_is_set_up(self):
        """systemd's set-up of tty1 for the menu and the failure screen waited for a key while plymouthd read tty1
        (2026-11-11): a unit without a terminal ends plymouthd before either."""
        quit_unit = (SYSTEMD / "wex-plymouth-quit.service").read_text(encoding="utf-8").splitlines()
        for line in ("ExecStart=/usr/local/sbin/wex-advanced-options --quit-plymouth", "DefaultDependencies=no",
                     "Before=wex-advanced-options.service wex-failure-screen.service shutdown.target"):
            self.assertIn(line, quit_unit)
        self.assertFalse(any(line.startswith(("StandardInput=", "TTYPath=", "TTYReset=")) for line in quit_unit))
        self.assertLess(len(re.search(r"^Description=(.*)$", "\n".join(quit_unit), re.M).group(1)), 30)
        for name in ("wex-advanced-options.service", "wex-failure-screen.service"):
            unit = (SYSTEMD / name).read_text(encoding="utf-8").splitlines()
            self.assertIn("Wants=wex-plymouth-quit.service", unit, name)
            after = next(line for line in unit if line.startswith("After="))
            self.assertIn("wex-plymouth-quit.service", after.split(), name)
            self.assertIn("TTYPath=/dev/tty1", unit, name)
        self.assertIn("wex-plymouth-quit.service", PACKAGE.read_text(encoding="utf-8"))

    def test_main_dispatches_the_failure_modes(self):
        seen = []
        self.menu.failure_check = lambda: seen.append("check") or 0
        self.menu.failure_screen = lambda: seen.append("screen") or 0
        self.menu.end_plymouth = lambda: seen.append("plymouth")
        self.assertEqual(self.menu.main(["--failure-check"]), 0)
        self.assertEqual(self.menu.main(["--failure-screen"]), 0)
        self.assertEqual(self.menu.main(["--quit-plymouth"]), 0)
        self.assertEqual(seen, ["check", "screen", "plymouth"])

    def test_os_choices_when_vintner_cannot_set_the_menu(self):
        down, enter = curses.KEY_DOWN, 10
        self.with_vintner(1)
        self.assertEqual(self.run_keys(down, down, enter, enter), ("boot", "normal", None))    # the local list
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

    def test_failure_screen_countdown_follows_startup_and_recovery(self):
        # "Time to display recovery options when needed" (sysdm.cpl, wex-boot1): recovery_timeout= in boot.conf
        conf = self.tmp / "boot.conf"
        conf.write_text("# written by wex-boot1\nrecovery_timeout=7\n", encoding="utf-8")
        self.assertEqual(self.menu.failure_countdown(), 7)
        self.assertEqual(self.run_failure([], 0.25), ("boot", "normal", None))
        self.assertEqual(FakeScreen.shown[0][1], 7)
        # 0 seconds: Start Normally at once (failure_check does not even start the screen)
        conf.write_text("recovery_timeout=0\n", encoding="utf-8")
        self.assertEqual(self.run_failure([curses.KEY_DOWN, 10], 0.25), ("boot", "normal", None))
        self.assertEqual(FakeScreen.shown, [])
        # a value that is not a number, or none: 30; a large one is cut to the dialog's 999
        for text, seconds in (("recovery_timeout=soon\n", 30), ("other=1\n", 30), ("recovery_timeout=5000\n", 999)):
            conf.write_text(text, encoding="utf-8")
            self.assertEqual(self.menu.failure_countdown(), seconds, text)
        conf.unlink()
        self.assertEqual(self.menu.failure_countdown(), 30)

    def test_failure_screen_arrow_stops_the_countdown(self):
        enter, up = 10, curses.KEY_UP
        # Up skips the grayed Last Known Good; the countdown line goes; waiting then chooses nothing
        self.assertEqual(self.run_failure([-1, up, *[-1] * 200, enter], 0.25), ("boot", "safecmd", None))
        after = FakeScreen.shown[2:]
        self.assertTrue(after and all(seconds is None for _, seconds in after))
        self.assertEqual(after[-1][0], 2)
        # Down on Start Normally wraps to Safe Mode, as on XP
        self.assertEqual(self.run_failure([curses.KEY_DOWN, enter], 0.25), ("boot", "safe", None))
        self.assertEqual(self.run_failure([enter], 0.25), ("boot", "normal", None))
        self.assertEqual(self.run_failure([up, up, up, enter], 0.25), ("boot", "safe", None))
        self.assertEqual(self.run_failure([up, up, up, up, enter], 0.25), ("boot", "normal", None))   # wrapped round

    def test_last_known_good_once_a_copy_is_saved(self):
        enter, up, down = 10, curses.KEY_UP, curses.KEY_DOWN
        label = "Last Known Good Configuration (your most recent settings that worked)"
        self.assertFalse(self.menu.lastgood_saved())
        self.assertNotIn(label, [i.label for i in self.menu.menu_items(False) if i and i.selectable])
        self.with_lastgood()
        self.assertTrue(self.menu.lastgood_saved())
        self.assertIn(label, [i.label for i in self.menu.menu_items(False, True) if i and i.selectable])
        self.assertIn(label, [i.label for i in self.menu.failure_items(False, True) if i and i.selectable])
        # the menu: Up from Start Normally, over Debugging Mode and Directory Services, then the OS list
        self.assertEqual(self.run_keys(up, up, up, enter, enter), ("boot", "normal", "lastgood"))
        self.assertEqual(self.run_keys(*[down] * 8, enter, enter), ("boot", "normal", "lastgood"))   # round the end
        # the failure screen: Up from Start Normally
        self.assertEqual(self.run_failure([up, enter], 0.25), ("boot", "normal", "lastgood"))
        self.assertEqual(self.run_failure([up, up, enter], 0.25), ("boot", "safecmd", None))

    def test_apply_last_known_good_restores_before_the_start(self):
        self.menu.apply_choice("normal", "lastgood", run=self.fake_run)
        self.assertEqual(self.calls, [[self.menu.LASTGOOD, "restore"], ["systemctl", "daemon-reload"],
                                      ["systemctl", "--no-block", "start", "default.target"]])
        self.assertEqual((self.tmp / "run" / "bootmode").read_text(), "normal\n")

    def test_the_menu_ignores_the_grub_failure_flag(self):
        """GRUB's wexfailed=1 (removed 2026-11-11) no longer turns the menu into the failure screen."""
        self.assertFalse(hasattr(self.menu, "start_failed"))
        source = (SCRIPTS / "wex-advanced-options.py").read_text(encoding="utf-8")
        self.assertNotIn("wexfailed", source)
        self.assertNotIn("grub", source.lower())

    def drawn(self, method, *args, rows=25, cols=80):
        """What Screen.METHOD(*ARGS) writes, as {row: (column, text, attribute)}, on a ROWSxCOLS console."""
        class Recorder:
            def __init__(self):
                self.text = {}

            def getmaxyx(self):
                return rows, cols

            def erase(self):
                self.text = {}

            def refresh(self):
                pass

            def addstr(self, row, col, text, attr=curses.A_NORMAL):
                assert row not in self.text, f"row {row} written twice"
                self.text[row] = (col, text, attr)

        screen = object.__new__(load_menu().Screen)       # setUp replaced self.menu.Screen; no curses needed
        screen.scr, screen.blue, screen.gray = Recorder(), "blue", "gray"
        getattr(screen, method)(*args)
        return screen.scr.text

    FOOTER = "Use the up and down arrow keys to move the highlight to your choice."

    def test_menu_layout_follows_xp(self):
        """Rows and columns as measured on XP (work order C5): headings from column 0, items from column 4, the
        bar from column 0 to the label's end, the footer one empty row below the last item."""
        items = self.menu.menu_items(False)
        text = self.drawn("advanced_menu", items, self.menu.start_normally(items))
        self.assertEqual(sorted(text), [1, 2, 4, 5, 6, 8, 9, 10, 11, 12, 13, 15, 16, 17, 19])
        self.assertEqual(text[1], (0, "WineExplorer Advanced Options Menu", curses.A_NORMAL))
        self.assertEqual(text[2], (0, "Please select an option:", curses.A_NORMAL))
        self.assertEqual(text[4], (4, "Safe Mode", curses.A_NORMAL))
        self.assertEqual(text[11], (4, "Directory Services Restore Mode (Windows domain controllers only)", "gray"))
        self.assertEqual(text[15], (0, "    Start WineExplorer Normally", curses.A_REVERSE))
        self.assertEqual(text[17], (4, "Return to OS Choices Menu", curses.A_NORMAL))
        self.assertEqual(text[19], (0, self.FOOTER, curses.A_NORMAL))
        self.assertEqual(self.drawn("advanced_menu", items, 0)[4], (0, "    Safe Mode", curses.A_REVERSE))

    def test_vm_test_rows_match_the_menu(self):
        """installer/tests/safe-mode.sh finds items by the highlight bar's row."""
        rows = dict(re.findall(r"\bROW_([A-Z]+)=(\d+)", re.search(r"^ROW_SAFE=.*$", VM_TEST.read_text(encoding="utf-8"),
                                                                  re.M).group(0)))
        items = self.menu.menu_items(False, True)
        text = self.drawn("advanced_menu", items, None)
        labels = {"SAFE": "Safe Mode", "SAFENET": "Safe Mode with Networking", "SAFECMD": "Safe Mode with Command Prompt",
                  "BOOTLOG": "Enable Boot Logging", "VGA": "Enable VGA Mode", "LASTGOOD": items[6].label,
                  "NORMALLY": "Start WineExplorer Normally", "OSCHOICES": "Return to OS Choices Menu"}
        self.assertEqual(set(rows), set(labels))
        for name, label in labels.items():
            self.assertEqual(text[int(rows[name])][1], label, name)

    def test_highlight_row_finds_the_bar(self):
        """installer/tests/highlight-row.py on a 1280x800 screendump with 8x16 cells."""
        width, height = 1280, 800
        pixels = bytearray(width * height * 3)

        def fill(row, first_col, last_col, rgb):
            for y in range(row * 16, row * 16 + 16):
                for x in range(first_col * 8, last_col * 8 + 8):
                    pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] = bytes(rgb)

        fill(11, 4, 40, (85, 85, 85))          # a grayed item: dark gray text, not at column 0
        fill(1, 0, 3, (255, 255, 255))         # white is not the bar either
        ppm = self.tmp / "screen.ppm"
        helper = [sys.executable, str(VM_TEST.with_name("highlight-row.py")), str(ppm)]
        ppm.write_bytes(b"P6\n# QEMU\n1280 800\n255\n" + bytes(pixels))
        self.assertEqual(subprocess.run(helper, capture_output=True, text=True, check=True).stdout, "")
        fill(15, 0, 30, (170, 170, 170))       # the bar on Start Normally
        ppm.write_bytes(b"P6\n1280 800\n255\n" + bytes(pixels))
        self.assertEqual(subprocess.run(helper, capture_output=True, text=True, check=True).stdout, "15\n")

    def test_os_list_layout_follows_xp(self):
        text = self.drawn("os_choices", "Safe Mode")
        self.assertEqual(text, {
            2: (0, "Please select the operating system to start:", curses.A_NORMAL),
            5: (0, "    WineExplorer", curses.A_REVERSE),
            7: (0, self.FOOTER, curses.A_NORMAL),
            8: (0, "Press ENTER to choose.", curses.A_NORMAL),
            21: (0, "For troubleshooting and advanced startup options for WineExplorer, press F8.", curses.A_NORMAL),
            23: (0, "Safe Mode", "blue")})
        self.assertNotIn(23, self.drawn("os_choices", None))     # Return to OS Choices Menu: no mode line

    def test_failure_screen_layout_follows_xp(self):
        """Text from row 1, column 0, broken as on XP's 80 columns even on a wider console; the list after an empty
        row; the footer one empty row below it, and the countdown directly under the footer."""
        items = self.menu.failure_items(False, True)
        for rows, cols in ((25, 80), (50, 160)):
            text = self.drawn("failure_screen", items, 6, 29, rows=rows, cols=cols)
            normally = next(row for row, (_, line, _) in text.items() if line.endswith("Start WineExplorer Normally"))
            self.assertEqual(text[normally], (0, "    Start WineExplorer Normally", curses.A_REVERSE))
            self.assertEqual(text[normally - 6], (4, "Safe Mode", curses.A_NORMAL))
            self.assertNotIn(normally - 7, text)
            self.assertNotIn(normally + 1, text)
            self.assertEqual(text[normally + 2], (0, self.FOOTER, curses.A_NORMAL))
            self.assertEqual(text[normally + 3], (0, "Seconds until WineExplorer starts: 29", curses.A_NORMAL))
            explanation = [line for row, (col, line, _) in text.items() if row < normally - 6]
            self.assertEqual(min(text), 1)
            self.assertTrue(all(len(line) <= 79 for line in explanation), explanation)
            self.assertLessEqual(normally + 3, 23, "the countdown fits a 25-row console")
        # an arrow key removes the countdown line
        self.assertNotIn(normally + 3, self.drawn("failure_screen", items, 6, None))

    def test_apply_safe_mode(self):
        self.menu.apply_choice("safe", run=self.fake_run)
        self.assertEqual((self.tmp / "run" / "bootmode").read_text(), "safe\n")
        self.assertEqual(oct((self.tmp / "run" / "bootmode").stat().st_mode & 0o777), "0o644")
        self.assertEqual(self.calls, [["systemctl", "daemon-reload"],
                                      ["systemctl", "--no-block", "start", "default.target"]])
        self.assertFalse(os.path.exists(self.menu.MENU_START), "a Safe Mode start counts nothing")

    def test_apply_records_the_starts_that_count(self):
        """Every start through the menu but a Safe Mode counts, as on XP: it is recorded on disk until the start-up
        check sees it succeed. Without the record (a disk that cannot be written) the start still goes on."""
        record = self.tmp / "lib" / "menu-start"
        self.menu.apply_choice("normal", "vga", run=self.fake_run)
        self.assertEqual(record.read_text(), "normal vga\n")
        self.menu.apply_choice("normal", run=self.fake_run)
        self.assertEqual(record.read_text(), "normal -\n")
        record.unlink()
        for mode in ("safe", "safenet", "safecmd"):
            self.menu.apply_choice(mode, run=self.fake_run)
            self.assertFalse(record.exists(), mode)
        self.menu.MENU_START = str(self.tmp / "stage" / "x" / "menu-start")   # "stage" is not a directory
        (self.tmp / "stage").write_text("")
        self.calls.clear()
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.menu.apply_choice("normal", "vga", run=self.fake_run)
        self.assertIn("this start is not recorded", err.getvalue())
        self.assertEqual(self.calls[-1], ["systemctl", "--no-block", "start", "default.target"])

    def test_apply_options(self):
        self.menu.apply_choice("normal", "bootlog", run=self.fake_run)
        self.assertEqual(self.calls[-1], ["systemctl", "--no-block", "start", "default.target", "wex-bootlog.service"])
        self.assertEqual((self.tmp / "run" / "bootoption").read_text(), "bootlog\n")
        self.menu.apply_choice("normal", "norestart", run=self.fake_run)
        self.assertEqual((self.tmp / "panic").read_text(), "0\n")
        # systemd's own crash as well, for this start: a /run drop-in after Startup and Recovery's 90- file, read by
        # the reload that follows
        self.assertTrue(Path(self.menu.CRASH_RUNTIME).name > "90-wineexplorer-recovery.conf")
        self.assertIn("[Manager]\nCrashAction=freeze\n", Path(self.menu.CRASH_RUNTIME).read_text(encoding="ascii"))
        self.assertIn(["systemctl", "daemon-reload"], self.calls)
        self.calls.clear()
        self.menu.apply_choice("normal", "debug", run=self.fake_run)
        self.assertEqual(self.calls[0], ["systemd-analyze", "log-level", "debug"])
        # Enable VGA Mode (D2): an ordinary start; the greeter and the sessions read the option
        self.calls.clear()
        self.menu.apply_choice("normal", "vga", run=self.fake_run)
        self.assertEqual((self.tmp / "run" / "bootmode").read_text(), "normal\n")
        self.assertEqual((self.tmp / "run" / "bootoption").read_text(), "vga\n")
        self.assertEqual(self.calls, [["systemctl", "daemon-reload"],
                                      ["systemctl", "--no-block", "start", "default.target"]])
        with self.assertRaises(ValueError):
            self.menu.apply_choice("bogus", run=self.fake_run)


class TestRecoveryConsole(TempDirTest):
    """wex-safe-terminal.sh's console shape (work order C6): as XP's cmd.exe, at the top left, 80 columns, as tall as
    the screen, with a bitmap font that fits 80 columns and the scroll bar across the screen."""

    def xterm_args(self, dimensions):
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir(exist_ok=True)
        fakes = {
            "xdpyinfo": f'echo "  dimensions:    {dimensions} pixels (169x127 millimeters)"\n' if dimensions else "exit 1\n",
            "xterm": 'printf "%s\\n" "$@" > "$LOG"\n',
            "zenity": 'echo "Log Out"\n',
            "xset": "exit 0\n", "xsetroot": "exit 0\n",
        }
        for name, body in fakes.items():
            (bin_dir / name).write_text("#!/bin/sh\n" + body, encoding="ascii")
            (bin_dir / name).chmod(0o755)
        log = self.tmp / "xterm.log"
        env = {"PATH": f"{bin_dir}:/usr/bin:/bin", "LOG": str(log), "TMPDIR": str(self.tmp), "HOME": str(self.tmp)}
        subprocess.run(["bash", str(SCRIPTS / "wex-safe-terminal.sh")], env=env, check=True, timeout=30,
                       capture_output=True)
        args = log.read_text(encoding="ascii").splitlines()
        return {args[i]: args[i + 1] for i in range(len(args) - 1) if args[i].startswith("-")}, args

    def test_shape_follows_the_screen(self):
        cases = (("640x480", "70", "80x36+0+0"),         # Safe Mode (C3): 80 columns of 8 pixels do not fit
                 ("1280x800", "80", "80x61+0+0"),
                 ("480x360", "60", "80x27+0+0"),
                 (None, "80", "80x25+0+0"))              # no screen size: XP's 25 rows
        for dimensions, width, geometry in cases:
            opts, args = self.xterm_args(dimensions)
            self.assertEqual(opts["-geometry"], geometry, dimensions)
            self.assertTrue(opts["-fn"].endswith(f"-13-120-75-75-c-{width}-iso10646-1"), (dimensions, opts["-fn"]))
            self.assertEqual(opts["-title"], "bash")
            resources = [args[i + 1] for i in range(len(args) - 1) if args[i] == "-xrm"]
            self.assertIn("XTerm*allowTitleOps: false", resources)    # the shell's prompt does not retitle it
            # no scroll bar border: in UTF-8 mode xterm grew by that pixel after mapping, and wexcomp snapped it back
            self.assertIn("XTerm*vt100.scrollBarBorder: 0", resources)
            self.assertEqual(opts["-fg"], "#c0c0c0")
            self.assertIn("-rightbar", args)
            self.assertEqual(args[-4:-2], ["bash", "--rcfile"])

    def test_banner_fits_80_columns(self):
        script = (SCRIPTS / "wex-safe-terminal.sh").read_text(encoding="utf-8")
        banner = script[script.index("cat << 'EOF' > \"$BANNER_RC\""):]
        banner = banner[:banner.index("\nEOF\n")]
        lines = re.findall(r'^echo "(.*)"$', banner, re.M)
        self.assertIn("  Administrative Recovery Commands:", lines)     # the VM test waits for it
        self.assertTrue(all(len(line) <= 80 for line in lines), [line for line in lines if len(line) > 80])


class TestSessionAndGreeter(unittest.TestCase):
    def test_session_keeps_its_name_and_branches_on_the_mode(self):
        session = (SCRIPTS / "wine-explorer-session.sh").read_text(encoding="utf-8")
        branch = session.index("exec /usr/local/bin/wex-safe-terminal.sh")
        self.assertLess(branch, session.index(". \"$(dirname \"$0\")/wine-explorer-session-common.sh\""),
                        "the recovery console must start before anything touches the Wine prefix")
        # the saved display mode: only in a normal start without Enable VGA Mode (D2)
        self.assertIn('[ "$WEXMODE" = normal ] && [ "$WEXOPTION" != vga ] && [ -z "$WIDTH" ] && [ -r "$DISPLAY_PROFILE" ]',
                      session)
        self.assertLess(session.index("WEXOPTION=$(/usr/local/bin/wex-bootmode --option"),
                        session.index('[ "$WEXOPTION" != vga ] && [ -z "$WIDTH" ]'))
        # C3, D2: Safe Mode and Enable VGA Mode set 640x480 before the desktop's size is read
        low = session.index('if { [ "$WEXMODE" != normal ] || [ "$WEXOPTION" = vga ]; } && [ -z "$WIDTH" ]')
        self.assertLess(session.index("low_resolution() {"), low)
        self.assertLess(low, session.index("RES=$(xrandr"))
        self.assertIn("--mode 640x480", session)
        common = (SCRIPTS / "wine-explorer-session-common.sh").read_text(encoding="utf-8")
        skip = common.index("safe|safenet) return 0")
        self.assertLess(common.index("wineboot.exe -u"), skip)
        self.assertLess(skip, common.index("--apply-user-defaults"))
        self.assertLess(skip, common.index("vulkan prepare"))
        # the default theme, Open Luna (2026-11-12): not in Safe Mode, after the theme files are in the prefix, once
        theme = common.index("'/theme=C:\\windows\\Resources\\Themes\\Luna.theme'")
        self.assertLess(skip, theme)
        self.assertLess(common.index('cp -rf "$theme_dir"'), theme)
        self.assertIn("""if ! grep -qs '^"CurrentTheme"=' "$WINEPREFIX/user.reg" \\""", common)

    def test_vm_test_checks_safe_mode_has_no_visual_style(self):
        # 2026-11-12: XP loads no visual style in Safe Mode (its Themes service is in no SafeBoot list); uxtheme does
        # the same, and the probe reports IsThemeActive
        probe = (REPO_ROOT / "installer" / "tests" / "safeboot-probe.c").read_text(encoding="utf-8")
        self.assertIn('GetProcAddress(uxtheme, "IsThemeActive")', probe)
        self.assertLess(probe.index("print_themed();"), probe.rindex('printf("\\n")'))
        vm = VM_TEST.read_text(encoding="utf-8")
        for mode in ("1)", "2)", "cmd)"):
            line = next(l for l in vm.splitlines() if l.strip().startswith(mode + " want=(cleanboot="))
            self.assertIn(" themed=0)", line, mode)
        line = next(l for l in vm.splitlines() if l.strip().startswith("0) want=(cleanboot=0"))
        self.assertNotIn("themed=", line)   # a normal start keeps the account's choice, checked by expect_theme
        waylandsafe = vm[vm.index("phase_waylandsafe() {"):]
        waylandsafe = waylandsafe[:waylandsafe.index("\n}\n")]
        self.assertIn("expect_safeboot 1", waylandsafe)
        # the message box: OCR reads its first line or its last
        self.assertIn('*"running in Safe Mode"*|*"click Yes. To log"*)', vm)

    def test_vm_test_holds_xwayland_while_the_compositor_ends(self):
        # 2026-11-12: wexcomp 0d524d63 ends XWayland before the X display's lock goes; waylandxlock makes the race
        # certain (XWayland stopped, the compositor alone ended) and LightDM must not restart
        vm = VM_TEST.read_text(encoding="utf-8")
        self.assertRegex(vm, r"\|waylandfallback\|waylandxlock[|)][^\n]*\) ;;")
        self.assertIn("waylandlogon|waylandfallback|waylandxlock) saved_session restore", vm)
        phase = vm[vm.index("phase_waylandxlock() {"):]
        phase = phase[:phase.index("\n}\n")]
        self.assertLess(phase.index("kill -STOP $x && kill -TERM $c"), phase.index("logon_screen"))
        self.assertIn("NRestarts", phase)
        self.assertIn('fail "LightDM restarted after the compositor ended', phase)
        for line in phase.splitlines():   # commands sent over the serial console must be one line
            if "gsh '" in line:
                self.assertEqual(line.count("'") % 2, 0, line)

    def test_vm_test_checks_startup_and_recovery(self):
        # work order D5: the dialog opens, the service refuses a change without a password, root's change reaches
        # the next start, and everything goes back (/etc byte for byte, on the stopped disk)
        vm = VM_TEST.read_text(encoding="utf-8")
        self.assertIn("|waylandxlock|startrec|classic) ;;", vm)
        self.assertIn("#   startrec ", vm)
        phase = vm[vm.index("phase_startrec() {"):]
        phase = phase[:phase.index("\n}\n")]
        for needle in ("control.exe sysdm.cpl,,3", "alt-i", "Apply ububb 5 true 7 false false 2>&1", "NotAuthorized",
                       "as_root 'id -u'", "wextest.d5=1", 'wait_text "Advanced Options" 60',
                       "Apply ububb 0 true 30 true false", "rm -f \"$MNT/etc/wineexplorer/boot.conf\"",
                       'cp -p "$OUT/startrec-cmdline" "$MNT/etc/kernel/cmdline"',
                       # the GUI path: wexhost's polkit agent, cancelled and then answered with Administrator's password
                       'wait_text "type an administrator password" 60', '"$(cat "$ADMIN_PWFILE")"',
                       "expect_settings MenuTimeout=30",
                       # Automatically restart: on by default, off waits, on restarts after a panic and a systemd crash
                       "AutoRestart=True \\\n        OperatingSystem=WineExplorer", "panic=0 CrashAction=freeze",
                       "echo c > /proc/sysrq-trigger", "kill -SEGV 1", "90-wineexplorer-recovery.conf"):
            self.assertIn(needle, phase)
        # the refusal comes before root's change, and the restore before the stopped disk is cleaned
        self.assertLess(phase.index("NotAuthorized"), phase.index("as_root 'id -u'"))
        self.assertLess(phase.index("Apply ububb 0 true 30 true false"), phase.index("attach rw"))
        # the panic that must wait comes with Automatically restart off; the restarts after the defaults are back
        self.assertLess(phase.index("restarted \"$id\" 75 &&"), phase.index("Apply ububb 0 true 30 true false"))
        self.assertLess(phase.index("attach rw"), phase.index("kill -SEGV 1"))
        for line in phase.splitlines():   # commands sent over the serial console must be one line
            if "gsh '" in line:
                self.assertEqual(line.count("'") % 2, 0, line)
        # the dialog's accelerator for Settings under Startup and Recovery is Alt+I
        rc = (REPO_ROOT / "src" / "dlls" / "sysdm.cpl" / "sysdm.rc").read_text(encoding="utf-8")
        self.assertIn('"Sett&ings...", IDC_ADV_STARTUP_BTN', rc)
        self.assertIn('"Environment Variables"', rc)

    def test_classic_logon_dialog(self):
        # work order C4: Ctrl+Alt+Delete twice on the Welcome screen shows msgina's "Log On to Windows" (XP, measured)
        msgina = REPO_ROOT / "src" / "dlls" / "msgina"
        self.assertIn("@ cdecl logon_show_dialog(ptr ptr)", (msgina / "msgina.spec").read_text(encoding="utf-8"))
        self.assertIn("logon.c logon.rc", (msgina / "Makefile.in").read_text(encoding="utf-8"))
        rc = (msgina / "logon.rc").read_text(encoding="utf-8")
        # XP's 411 x 210 client area and its button row, in dialog units of 8-point Tahoma
        self.assertIn("IDD_LOGON DIALOGEX 0, 0, 274, 129", rc)
        self.assertIn('CAPTION "Log On to Windows"', rc)
        self.assertIn('"&Shut Down...", IDC_LOGON_SHUTDOWN, 163, 108, 50, 14, NOT WS_VISIBLE', rc)
        self.assertIn("The system could not log you on.", rc)
        logonui = (REPO_ROOT / "src" / "programs" / "logonui" / "logonui.c").read_text(encoding="utf-8")
        # the first press changes nothing; the second shows the dialog
        self.assertIn("if (!classic && ++sas_presses >= 2) show_classic();", logonui)
        self.assertIn('GetProcAddress( module, "logon_show_dialog" )', logonui)
        # names in any case, only accounts AccountsService lists (not root), and no domains
        self.assertIn("lstrcmpiW( wname, want )", logonui)
        self.assertIn("wcschr( want, '@' )", logonui)
        # a password typed for an account without one fails, as on XP
        self.assertIn("result == 0 && (classic_answered || !classic_typed)", logonui)
        # the last user, kept where the lightdm account may write it
        self.assertIn("RegCreateKeyExW( HKEY_CURRENT_USER, winlogon_key", logonui)
        vm = VM_TEST.read_text(encoding="utf-8")
        self.assertIn("#   classic ", vm)
        phase = vm[vm.index("phase_classic() {"):]
        phase = phase[:phase.index("\n}\n")]
        for needle in ('wait_text "Log On to Windows" 3 &&', 'wait_text "Safe Mode" 10',
                       'classic_logon Administrator "$(cat "$ADMIN_PWFILE")"', "*Administrator*",
                       'wait_text "Shut Down" 10', 'logon_refused "root"', 'classic_logon WexHidden ""',
                       "grep -qw wexhidden"):
            self.assertIn(needle, phase)

    def test_wayland_session_reads_the_mode_before_the_prefix(self):
        session = (SCRIPTS / "wine-explorer-wayland-session.sh").read_text(encoding="utf-8")
        prepare = session.index("\nwex_prepare_prefix")
        self.assertLess(session.index("export WEXMODE"), prepare)
        branch = session.index('if [ "$WEXMODE" = safecmd ]')
        self.assertLess(branch, prepare, "the recovery console must start before anything touches the Wine prefix")
        self.assertLess(branch, session.index('\n"$WINE" '))   # the first line that runs Wine


class TestWaylandSession(TempDirTest):
    """wine-explorer-wayland-session.sh with a stand-in compositor and Wine."""

    def run_session(self, mode, compositor_status=0, option=""):
        log = self.tmp / "calls.log"
        log.unlink(missing_ok=True)
        compositor = self.tmp / "wexcomp"
        compositor.write_text('#!/bin/sh\necho "wexcomp $* WEXMODE=$WEXMODE DISPLAY=${DISPLAY-unset}'
                              ' WEX_OUTPUT_MODE=${WEX_OUTPUT_MODE-unset}" >> "$LOG"\n'
                              '[ "$FAKE_COMPOSITOR" = sleep ] && exec sleep 30\n'
                              'exit "$FAKE_COMPOSITOR"\n', encoding="ascii")
        compositor.chmod(0o755)
        wine = self.tmp / "wine"
        wine.write_text('#!/bin/sh\necho "wine $*" >> "$LOG"\n', encoding="ascii")
        wine.chmod(0o755)
        runtime = self.tmp / "run"
        runtime.mkdir(exist_ok=True)
        choice = self.tmp / "session-choice"
        choice.write_text('#!/bin/sh\necho "choice $*" >> "$LOG"\n', encoding="ascii")
        choice.chmod(0o755)
        env = {"PATH": os.environ["PATH"], "LOG": str(log), "HOME": str(self.tmp), "USER": "wineuser",
               "XDG_RUNTIME_DIR": str(runtime), "DISPLAY": ":9", "WINE": str(wine), "WEX_SESSION_CHOICE": str(choice),
               "WEX_WEXCOMP_BIN": str(compositor), "WEX_WAYLAND_CONFIG": "/cfg",
               "WEX_BOOTMODE_HELPER": self.fake_helper(mode, option=option), "WEX_SAFE_TERMINAL": "/safe-terminal",
               "FAKE_COMPOSITOR": str(compositor_status)}
        result = subprocess.run(["bash", str(SCRIPTS / "wine-explorer-wayland-session.sh")], env=env,
                                capture_output=True, text=True, timeout=30)
        calls = log.read_text(encoding="ascii").splitlines() if log.exists() else []
        return result.returncode, calls

    def test_command_prompt_runs_the_recovery_console_in_the_compositor(self):
        status, calls = self.run_session("safecmd")
        self.assertEqual(status, 0)
        self.assertEqual(calls, ["choice --restore",   # the session choice before anything else
                                 "wexcomp -C /cfg -S /safe-terminal WEXMODE=safecmd DISPLAY=unset"
                                 " WEX_OUTPUT_MODE=640x480"])

    FALLBACK = "choice --fallback wine-explorer"

    def test_other_modes_start_the_desktop_compositor(self):
        # The stand-in compositor ends at once, so the session stops before Wine, as it does when wexcomp fails, and
        # asks for X11 at the next logon (wex-session-choice --fallback, which itself does nothing in Safe Mode).
        # Every Safe Mode starts the compositor at 640x480 (work order C3); a normal start at the output's own mode.
        # A Safe Mode session puts the account's session choice back first; a normal one leaves it.
        for mode, expected, size in (("safe", "safe", "640x480"), ("safenet", "safenet", "640x480"),
                                     ("normal", "normal", "unset"), ("bogus", "normal", "unset")):
            status, calls = self.run_session(mode, compositor_status=1)
            self.assertEqual(status, 1, mode)
            self.assertEqual(calls, (["choice --restore"] if expected != "normal" else []) +
                             [f"wexcomp -C /cfg WEXMODE={expected} DISPLAY=unset WEX_OUTPUT_MODE={size}",
                              self.FALLBACK], mode)
        # Enable VGA Mode (D2): a normal start at 640x480
        for option, size in (("vga", "640x480"), ("bootlog", "unset")):
            status, calls = self.run_session("normal", compositor_status=1, option=option)
            self.assertEqual(calls, [f"wexcomp -C /cfg WEXMODE=normal DISPLAY=unset WEX_OUTPUT_MODE={size}",
                                     self.FALLBACK], option)

    def test_a_compositor_that_never_opens_its_socket_falls_back_too(self):
        """A compositor that keeps running without the shell's socket: after the wait, the same fallback."""
        status, calls = self.run_session("normal", compositor_status="sleep")
        self.assertEqual(status, 1)
        self.assertEqual(calls[-1], self.FALLBACK)

    def test_a_normal_start_restores_the_saved_display_mode(self):
        """Display Properties' profile (wexhost): a normal Wayland session starts the output at that size; Safe Mode
        and Enable VGA Mode keep 640x480, and a malformed profile is ignored."""
        profile = self.tmp / ".config" / "wineexplorer" / "display"
        profile.parent.mkdir(parents=True)
        profile.write_text("1024 768 60004 1024x768\n")
        def compositor(calls):
            return next(call for call in calls if call.startswith("wexcomp "))
        for mode, option, size in (("normal", "", "1024x768"), ("safe", "", "640x480"), ("normal", "vga", "640x480")):
            calls = self.run_session(mode, compositor_status=1, option=option)[1]
            self.assertIn(f"WEX_OUTPUT_MODE={size}", compositor(calls), (mode, option))
        profile.write_text("1024 x768 60004\n")
        self.assertIn("WEX_OUTPUT_MODE=unset", compositor(self.run_session("normal", compositor_status=1)[1]))


class TestLocaleGen(TempDirTest):
    """wex-locale-gen.sh: the system locale that Setup set through localed is generated when glibc lacks it."""

    SCRIPT = SCRIPTS / "wex-locale-gen.sh"
    SUPPORTED = ("en_US.UTF-8 UTF-8\nen_US ISO-8859-1\nde_DE.UTF-8 UTF-8\n"
                 "sr_RS.UTF-8@latin UTF-8\nC.UTF-8 UTF-8\n")
    LOCALE_GEN = "# This file lists locales that you wish to have built.\n# de_DE.UTF-8 UTF-8\n#  en_US.UTF-8 UTF-8\n"

    def setUp(self):
        super().setUp()
        (self.tmp / "SUPPORTED").write_text(self.SUPPORTED, encoding="ascii")
        self.gen = self.tmp / "locale.gen"
        self.gen.write_text(self.LOCALE_GEN, encoding="ascii")
        self.default = self.tmp / "default-locale"

    def run_script(self, *args, have=("C", "C.utf8", "POSIX")):
        """Runs the script with stand-ins: `locale -a` lists HAVE, locale-gen logs its calls."""
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir(exist_ok=True)
        (self.tmp / "have").write_text("".join(f"{name}\n" for name in have), encoding="ascii")
        (bin_dir / "locale").write_text('#!/bin/sh\n[ "$1" = -a ] && cat "$HAVE"\n', encoding="ascii")
        (bin_dir / "locale-gen").write_text('#!/bin/sh\necho locale-gen >> "$LOG"\n', encoding="ascii")
        for name in ("locale", "locale-gen"):
            (bin_dir / name).chmod(0o755)
        log = self.tmp / "calls.log"
        log.unlink(missing_ok=True)
        env = {"PATH": f"{bin_dir}:{os.environ['PATH']}", "LOG": str(log), "HAVE": str(self.tmp / "have"),
               "WEX_LOCALE_DEFAULT": str(self.default), "WEX_LOCALE_CONF": str(self.tmp / "locale.conf"),
               "WEX_LOCALE_GEN": str(self.gen), "WEX_LOCALE_SUPPORTED": str(self.tmp / "SUPPORTED")}
        result = subprocess.run(["sh", str(self.SCRIPT), *args], env=env, capture_output=True, text=True,
                                timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result, log.read_text().splitlines() if log.exists() else []

    def test_generates_a_missing_locale_from_its_commented_line(self):
        _, calls = self.run_script("en_US.UTF-8")
        self.assertEqual(calls, ["locale-gen"])
        self.assertEqual(self.gen.read_text(), "# This file lists locales that you wish to have built.\n"
                                               "# de_DE.UTF-8 UTF-8\nen_US.UTF-8 UTF-8\n")

    def test_adds_a_locale_that_locale_gen_does_not_list(self):
        self.gen.write_text("# nothing here\n", encoding="ascii")
        _, calls = self.run_script("de_DE.UTF-8")
        self.assertEqual(calls, ["locale-gen"])
        self.assertEqual(self.gen.read_text(), "# nothing here\nde_DE.UTF-8 UTF-8\n")

    def test_reads_lang_from_the_system_setting(self):
        """localed on Debian writes /etc/default/locale, possibly quoted."""
        self.default.write_text('LANG="de_DE.UTF-8"\n', encoding="ascii")
        _, calls = self.run_script()
        self.assertEqual(calls, ["locale-gen"])
        self.assertIn("\nde_DE.UTF-8 UTF-8\n", self.gen.read_text())

    def test_nothing_to_do(self):
        for args, have in ((("en_US.UTF-8",), ("C.utf8", "en_US.utf8")),                 # generated already
                           (("sr_RS.UTF-8@latin",), ("sr_RS.utf8@latin",)),               # with a modifier
                           (("C.UTF-8",), ("C.utf8",)),                                   # built into glibc
                           ((), ("C.utf8",))):                                            # no system locale
            _, calls = self.run_script(*args, have=have)
            self.assertEqual(calls, [], args)
        self.assertEqual(self.gen.read_text(), self.LOCALE_GEN)

    def test_a_locale_glibc_does_not_know_is_left_alone(self):
        result, calls = self.run_script("xx_YY.UTF-8")
        self.assertEqual(calls, [])
        self.assertIn("cannot generate xx_YY.UTF-8", result.stderr)
        self.assertEqual(self.gen.read_text(), self.LOCALE_GEN)

    def test_the_package_installs_and_runs_it(self):
        package = PACKAGE.read_text(encoding="utf-8")
        self.assertIn('"$ROOT_DIR/scripts/wex-locale-gen.sh" "$STAGE_SESSION/usr/local/sbin/wex-locale-gen"', package)
        self.assertIn("wex-locale-gen.service", package)
        self.assertIn("/usr/local/sbin/wex-locale-gen en_US.UTF-8 || true", package)
        self.assertIn("systemctl enable wex-locale-gen.service", package)
        self.assertRegex(package, r"Depends: [^\n]*\blocales\b")


class TestSessionChoice(TempDirTest):
    """wex-session-choice.sh: a Safe Mode logon leaves the account's X11 or Wayland choice as it was."""

    SCRIPT = SCRIPTS / "wex-session-choice.sh"

    def setUp(self):
        super().setUp()
        self.users = self.tmp / "users"
        self.users.mkdir()
        (self.users / "wineuser").write_text("[User]\nXSession=wine-explorer-wayland\nSystemAccount=false\n")
        (self.users / "administrator").write_text("[User]\nSystemAccount=false\n")   # never chose one
        (self.users / "bad name").write_text("[User]\nXSession=x\n")                  # not a valid account name
        conf = self.tmp / "lightdm" / "lightdm.conf.d"
        conf.mkdir(parents=True)
        (conf / "50-wineexplorer.conf").write_text("[Seat:*]\nuser-session=wine-explorer\n")
        self.run_dir = self.tmp / "run" / "session-choice"
        self.home = self.tmp / "homes" / "wineuser"
        self.home.mkdir(parents=True)

    def choice(self, arg, mode="safe", current="wine-explorer", record_uid=None, extra=()):
        """Runs the script with a stand-in busctl: AccountsService reports CURRENT as the saved session and logs
        SetXSession calls."""
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir(exist_ok=True)
        (bin_dir / "busctl").write_text(
            '#!/bin/sh\n'
            'case "$*" in\n'
            '    *FindUserByName*) echo \'o "/org/freedesktop/Accounts/User1001"\' ;;\n'
            '    *get-property*XSession*) echo "s \\"$FAKE_CURRENT\\"" ;;\n'
            '    *SetXSession*) echo "SetXSession $8" >> "$LOG" ;;\n'
            'esac\n', encoding="ascii")
        (bin_dir / "busctl").chmod(0o755)
        log = self.tmp / "busctl.log"
        log.unlink(missing_ok=True)
        env = {"PATH": f"{bin_dir}:{os.environ['PATH']}", "LOG": str(log), "FAKE_CURRENT": current,
               "USER": "wineuser", "HOME": str(self.home),
               "WEX_SESSION_CHOICE_USERS": str(self.users), "WEX_SESSION_CHOICE_RUN": str(self.run_dir),
               "WEX_SESSION_CHOICE_HOMES": str(self.tmp / "homes"),
               "WEX_SESSION_CHOICE_LIGHTDM": str(self.tmp / "lightdm"), "WEX_BOOTMODE_HELPER": self.fake_helper(mode),
               "WEX_SESSION_CHOICE_ROOT_UID": str(os.getuid() if record_uid is None else record_uid)}
        result = subprocess.run(["sh", str(self.SCRIPT), arg, *extra], env=env, capture_output=True, text=True,
                                timeout=30)
        return result, log.read_text().splitlines() if log.exists() else []

    def test_fallback_saves_the_x11_session(self):
        """The Wayland session's compositor did not start: the account's next logon is X11, in AccountsService and
        in ~/.dmrc. Safe Mode logons change nothing; the session name must be one."""
        dmrc = self.home / ".dmrc"
        dmrc.write_text("[Desktop]\nSession=wine-explorer-wayland\n")
        result, calls = self.choice("--fallback", mode="normal", extra=("wine-explorer",))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(calls, ["SetXSession wine-explorer"])
        self.assertIn("the next logon of wineuser starts wine-explorer", result.stdout)
        self.assertEqual(dmrc.read_text(), "[Desktop]\nSession=wine-explorer\n")
        dmrc.write_text("[Desktop]\nSession=wine-explorer-wayland\n")
        self.assertEqual(self.choice("--fallback", mode="safe", extra=("wine-explorer",))[1], [])
        self.assertEqual(dmrc.read_text(), "[Desktop]\nSession=wine-explorer-wayland\n")
        self.assertEqual(self.choice("--fallback", mode="normal")[0].returncode, 2)
        self.assertEqual(self.choice("--fallback", mode="normal", extra=("x/../y",))[0].returncode, 2)

    def test_snapshot_in_safe_mode_only(self):
        self.choice("--snapshot", mode="normal")
        self.assertFalse(self.run_dir.exists())
        (self.home / ".dmrc").write_text("[Desktop]\nSession=wine-explorer-wayland\n")
        self.assertEqual(self.choice("--snapshot")[0].returncode, 0)
        self.assertEqual((self.run_dir / "wineuser").read_text(), "wine-explorer-wayland\ndmrc=present\n")
        self.assertEqual((self.run_dir / "administrator").read_text(),
                         "wine-explorer\ndmrc=absent\n")                               # LightDM's default, no .dmrc
        self.assertFalse((self.run_dir / "bad name").exists())
        self.assertEqual(oct((self.run_dir / "wineuser").stat().st_mode & 0o777), "0o644")

    def test_restore_puts_the_choice_back(self):
        dmrc = self.home / ".dmrc"
        dmrc.write_text("[Desktop]\nSession=wine-explorer-wayland\n")
        self.choice("--snapshot")
        # this Safe Mode logon went through the X11 session (LightDM saved it); the account had chosen Wayland
        dmrc.write_text("[Desktop]\nSession=wine-explorer\n")
        result, calls = self.choice("--restore", current="wine-explorer")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(calls, ["SetXSession wine-explorer-wayland"])
        self.assertEqual(dmrc.read_text(), "[Desktop]\nSession=wine-explorer-wayland\n")
        # the same session: nothing to put back
        self.assertEqual(self.choice("--restore", current="wine-explorer-wayland")[1], [])

    def test_restore_removes_a_dmrc_made_at_the_logon(self):
        """No ~/.dmrc before Safe Mode: LightDM writes one at the logon, and it goes again."""
        self.choice("--snapshot")
        dmrc = self.home / ".dmrc"
        dmrc.write_text("[Desktop]\nSession=wine-explorer\n")
        self.choice("--restore", current="wine-explorer")
        self.assertFalse(dmrc.exists())

    def test_restore_needs_safe_mode_and_root_s_record(self):
        self.choice("--snapshot")
        self.assertEqual(self.choice("--restore", mode="normal")[1], [])
        self.assertEqual(self.choice("--restore", record_uid=os.getuid() + 1)[1], [])   # not root's
        (self.run_dir / "wineuser").write_text("wine-explorer/../x\n")
        self.assertEqual(self.choice("--restore")[1], [])                                # not a session name
        self.assertEqual(self.choice("--bogus")[0].returncode, 2)


class TestGreeter(TempDirTest):
    """wex-greeter.sh: the Welcome screen in every mode; in Safe Mode the GTK greeter when Wine cannot start."""

    def run_greeter(self, mode, wine_exists=True, cmd_status=0, explorer_status=0, home=None, xrandr=False, option="",
                    extra_env=None):
        """home: run with HOME=home and no WINEPREFIX, as LightDM starts the greeter (the prefix is then ~/.wine).
        xrandr: a stand-in X server at 1280x800 whose output also offers 640x480. option: the menu's start option."""
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
        wine.unlink(missing_ok=True)   # an earlier call in the same test may have made it
        if wine_exists:
            wine.write_text('#!/bin/sh\necho "wine $*" >> "$LOG"\necho "$WEXMODE $WINEPREFIX" > "$LOG.env"\n'
                            'case "$1" in\n'
                            '    cmd.exe) exit "$FAKE_CMD" ;;\n    explorer.exe) exit "$FAKE_EXPLORER" ;;\nesac\n',
                            encoding="ascii")
            wine.chmod(0o755)
        gtk = self.tmp / "gtk-greeter"
        gtk.write_text('#!/bin/sh\necho gtk >> "$LOG"\necho "$XDG_DATA_DIRS" > "$LOG.xdg"\n', encoding="ascii")
        gtk.chmod(0o755)
        env = {"PATH": path, "LOG": str(log), "WINE": str(wine),
               "WEX_BOOTMODE_HELPER": self.fake_helper(mode, option=option), "WEX_GTK_GREETER": str(gtk),
               "FAKE_CMD": str(cmd_status), "FAKE_EXPLORER": str(explorer_status), **(extra_env or {})}
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
        # the GTK greeter, when Wine cannot start, at 640x480 too
        self.assertEqual(self.run_greeter("safecmd", wine_exists=False, xrandr=True)[1],
                         ["xrandr --output Virtual-1 --mode 640x480", "gtk"])

    def test_vga_mode_runs_at_640x480(self):
        """work order D2: Enable VGA Mode, a normal start, begins at 640x480 too; other start options do not"""
        calls = self.run_greeter("normal", xrandr=True, option="vga")[1]
        self.assertEqual(calls, ["xrandr --output Virtual-1 --mode 640x480",
                                 "wine explorer.exe /desktop=Logon,640x480 logonui.exe"])
        self.assertEqual((self.tmp / "calls.log.env").read_text().split()[0], "normal")
        calls = self.run_greeter("normal", xrandr=True, option="bootlog")[1]
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

    def test_the_fallback_greeter_is_black(self):
        """work order A4: the GTK greeter reads WineExplorer's drop-in after Debian's, so it is black. Its order
        (lightdm-gtk-greeter 2.0.9, config_init): each XDG data directory in turn, its .conf.d files sorted, then the
        configuration directories and /etc/lightdm; a later file overrides an earlier one."""
        self.run_greeter("safecmd", wine_exists=False)
        dirs = (self.tmp / "calls.log.xdg").read_text().strip().split(":")
        self.assertEqual(dirs, ["/usr/local/share", "/usr/share", "/usr/share/wineexplorer/safe-greeter"])
        root = self.tmp / "fs"
        debian = root / "usr/share/lightdm/lightdm-gtk-greeter.conf.d"
        debian.mkdir(parents=True)
        (debian / "01_debian.conf").write_text("[greeter]\nbackground=/usr/share/images/desktop-base/login-background.svg\n"
                                               "theme-name=Adwaita\n", encoding="ascii")
        ours = root / "usr/share/wineexplorer/safe-greeter/lightdm/lightdm-gtk-greeter.conf.d"
        ours.mkdir(parents=True)
        shutil.copy(REPO_ROOT / "installer/lightdm/90-wineexplorer-safe-mode.conf", ours)
        settings = {}
        for directory in dirs:
            conf_d = root / directory.lstrip("/") / "lightdm/lightdm-gtk-greeter.conf.d"
            for path in sorted(conf_d.glob("*.conf")) if conf_d.is_dir() else []:
                parser = configparser.ConfigParser(interpolation=None, comment_prefixes=("#",))
                parser.read(path, encoding="utf-8")
                settings.update(parser["greeter"])
        self.assertEqual(settings["background"], "#000000")
        self.assertEqual(settings["user-background"], "false")
        self.assertEqual(settings["theme-name"], "Adwaita")                   # the rest stays Debian's
        # an XDG_DATA_DIRS set by LightDM is kept, with the drop-in still last
        self.run_greeter("safe", cmd_status=1, extra_env={"XDG_DATA_DIRS": "/opt/share:/usr/share"})
        self.assertEqual((self.tmp / "calls.log.xdg").read_text().strip(),
                         "/opt/share:/usr/share:/usr/share/wineexplorer/safe-greeter")

    def test_screen_pixel(self):
        """installer/tests/screen-pixel.py, which the VM test uses for the greeter's background (A4)."""
        width, height = 640, 480
        pixels = bytearray(width * height * 3)
        pixels[((height - 5) * width + 4) * 3:((height - 5) * width + 4) * 3 + 3] = bytes((1, 2, 3))
        ppm = self.tmp / "screen.ppm"
        ppm.write_bytes(b"P6\n640 480\n255\n" + bytes(pixels))
        helper = [sys.executable, str(VM_TEST.with_name("screen-pixel.py")), str(ppm)]
        self.assertEqual(subprocess.run(helper + ["4", "-5"], capture_output=True, text=True, check=True).stdout,
                         "1 2 3\n")
        self.assertEqual(subprocess.run(helper + ["4", "475"], capture_output=True, text=True, check=True).stdout,
                         "1 2 3\n")
        self.assertEqual(subprocess.run(helper + ["0", "0"], capture_output=True, text=True, check=True).stdout,
                         "0 0 0\n")
        self.assertNotEqual(subprocess.run(helper + ["640", "0"], capture_output=True, text=True).returncode, 0)

    def test_safe_mode_falls_back_when_wine_does_not_start(self):
        self.assertEqual(self.run_greeter("safe", cmd_status=1)[1], ["wine cmd.exe /c exit 0", "gtk"])

    def test_safe_mode_falls_back_when_the_logon_screen_fails(self):
        self.assertEqual(self.run_greeter("safe", explorer_status=3)[1],
                         ["wine cmd.exe /c exit 0", "wine explorer.exe /desktop=Logon,1024x768 logonui.exe", "gtk"])


class TestUnitsAndLoader(unittest.TestCase):
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

    def test_setup_installs_no_grub(self):
        """Since 2026-11-11 Setup installs Vintner Boot only: no GRUB step, settings or F8 entry, no stock loader in
        the live system, and the CD's "Continue Setup" entry starts the disk through Vintner Boot."""
        installer = REPO_ROOT / "installer"
        screens = (installer / "textsetup" / "screens.c").read_text(encoding="utf-8")
        for word in ("install_grub", "grub-install", "update-grub", "GRUB_DISTRIBUTOR", "grub.d", "device.map"):
            self.assertNotIn(word, screens)
        self.assertIn("if (!install_vintner(state)) {", screens)
        packages = (installer / "live-build" / "config" / "package-lists" / "wineexplorer.list.chroot").read_text(
            encoding="utf-8").split()
        self.assertIn("grub-common", packages)   # Vintner Boot's grub-mkimage, for the BIOS core image
        for package in ("grub-pc", "grub-pc-bin", "grub-efi-amd64-bin", "grub2-common", "grub-efi-amd64"):
            self.assertNotIn(package, packages)
        iso_menu = (installer / "live-build" / "config" / "hooks" / "live" / "9000-grub-menu.binary").read_text(
            encoding="utf-8")
        self.assertNotIn("configfile /boot/grub/grub.cfg", iso_menu)
        self.assertIn("chainloader ($wexesp)/EFI/vintner/systemd-bootx64.efi", iso_menu)
        self.assertNotIn("desktop-base", (installer / "live-build" / "config" / "hooks" / "normal" /
                                          "0500-enable-services.hook.chroot").read_text(encoding="utf-8"))
        self.assertNotIn("wineexplorer.cfg", (installer / "textsetup" / "install-vintner.sh").read_text(encoding="utf-8"))

    def test_startup_check_unit(self):
        unit = (SYSTEMD / "wex-startup-check.service").read_text(encoding="utf-8")
        for line in ("Type=oneshot", "ExecStart=/usr/local/sbin/wex-startup-check", "Before=boot-complete.target",
                     "WantedBy=multi-user.target", "RequiredBy=boot-complete.target"):
            self.assertIn(line, unit.splitlines())
        # Vintner's CI found systemd cuts status lines at 80 columns on serial (docs/VINTNER_BOOT.md section 4.4)
        self.assertLess(len(re.search(r"^Description=(.*)$", unit, re.M).group(1)), 30)


class TestPackage(TempDirTest):
    def safe_mode_block(self):
        """The postinst's Safe Mode part, with its paths moved under the test directory."""
        script = PACKAGE.read_text(encoding="utf-8")
        start = script.index("# Safe Mode (docs/SAFE_MODE.md). The earlier")
        end = script.index("systemctl daemon-reload || true", start)
        block = script[start:end]
        for path in ("/etc/", "/run/", "/var/lib/", "/boot/"):
            block = block.replace(path, f"{self.tmp}{path}")
        return block

    def run_block(self, fakes):
        """Run the postinst's Safe Mode part with stand-ins (name: shell body) first on PATH; LOG collects calls."""
        bin_dir = self.tmp / "bin"
        shutil.rmtree(bin_dir, ignore_errors=True)
        bin_dir.mkdir()
        for name, body in {"systemctl": "exit 0\n", "getent": "exit 1\n", "useradd": "exit 0\n", **fakes}.items():
            (bin_dir / name).write_text("#!/bin/sh\n" + body, encoding="ascii")
            (bin_dir / name).chmod(0o755)
        log = self.tmp / "calls.log"
        log.unlink(missing_ok=True)
        env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}", LOG=str(log))
        subprocess.run(["sh", "-e", "-c", self.safe_mode_block()], check=True, env=env)
        return log.read_text().splitlines() if log.exists() else []

    def test_a_grub_install_loses_the_f8_entry(self):
        """Upgrading a machine installed with GRUB before 2026-11-11: the F8 entry's script goes, grub.cfg is made
        again without it, and its record of unfinished starts is dropped. A Vintner install is left alone."""
        grub_tools = {"update-grub": 'echo update-grub >> "$LOG"\n', "grub-editenv": 'echo "grub-editenv $*" >> "$LOG"\n'}
        self.assertEqual(self.run_block(grub_tools), [], "no grub.cfg or grubenv: nothing to do")
        entry = self.tmp / "etc" / "grub.d" / "11_wineexplorer_advanced"
        entry.parent.mkdir(parents=True)
        entry.write_text("#!/bin/sh\n", encoding="ascii")
        (self.tmp / "boot" / "grub").mkdir(parents=True)
        (self.tmp / "boot" / "grub" / "grub.cfg").touch()
        (self.tmp / "boot" / "grub" / "grubenv").touch()
        self.assertEqual(self.run_block(grub_tools),
                         ["update-grub", f"grub-editenv {self.tmp}/boot/grub/grubenv unset wex_boot_pending"])
        self.assertFalse(entry.exists())
        # a failing GRUB tool does not fail the postinst
        self.assertEqual(self.run_block({"update-grub": 'echo update-grub >> "$LOG"; exit 1\n',
                                         "grub-editenv": 'echo grub-editenv >> "$LOG"; exit 1\n'}),
                         ["update-grub", "grub-editenv"])

    def test_installs_the_new_pieces(self):
        script = PACKAGE.read_text(encoding="utf-8")
        for piece in ("wex-bootmode.sh", "wex-advanced-options.py", "wex-bootlog.sh", "wex-bootmode-generator",
                      "system-generators", "wex-advanced-options.target", "wex-safemode.target",
                      "wex-safenet.target", "wex-bootlog.service",
                      "wex-startup-check.sh", "wex-startup-check.service", "wex-lastgood.sh"):
            self.assertIn(piece, script)
        # GRUB's F8 entry is no longer packaged (removed 2026-11-11)
        self.assertNotIn("$STAGE_SESSION/etc/grub.d", script)
        self.assertNotIn("includes.chroot/etc/grub.d", script)
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

    def test_iso_installs_vintner(self):
        """Setup installs Vintner Boot (since 2026-11-10; the GRUB path was removed on 2026-11-11). Its packages come
        from the vintner submodule unless --vintner=DIR gives them (docs/VINTNER_BOOT.md section 4.2)."""
        path = REPO_ROOT / "installer" / "build-iso.sh"
        script = path.read_text(encoding="utf-8")
        self.assertNotIn("LOADER", script)
        self.assertIn('bash "$VINTNER_SRC/ci/package/build-debs.sh" "$VINTNER_DEBS"', script)
        self.assertIn('VINTNER_SRC="$ROOT_DIR/vintner"', script)
        self.assertIn('install -m755 "$SCRIPT_DIR/textsetup/install-vintner.sh"', script)
        modules = (REPO_ROOT / ".gitmodules").read_text(encoding="utf-8")
        self.assertIn('[submodule "vintner"]', modules)
        self.assertIn("url = https://github.com/zcomputerwiz/vintner-boot.git", modules)
        gone = subprocess.run(["bash", str(path), "--grub"], capture_output=True, text=True)
        self.assertEqual(gone.returncode, 1)
        self.assertIn("Unknown option: --grub", gone.stdout)
        for test in ("safe-mode.sh", "install-unattended.sh"):
            text = (REPO_ROOT / "installer" / "tests" / test).read_text(encoding="utf-8")
            self.assertNotIn("WEX_TEST_LOADER", text, test)
            self.assertNotIn("grub-editenv", text, test)

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
            globbed = f'{Path(src).parent.as_posix()}/"*{Path(src).suffix}'   # e.g. "$ROOT_DIR/scripts/wayland/"*.xml
            self.assertTrue(src_name in package or globbed in package, f"the package does not install {src}")
            installed = (f'$STAGE_SESSION/{dst}"' in package
                         or (src_name == dst_name and f'$STAGE_SESSION/{dst_dir}/' in package))
            self.assertTrue(installed, f"the package does not install {src} as /{dst}")
        sources = {Path(src).name for _, src, _ in entries}
        for piece in ("wex-bootmode.sh", "wex-advanced-options.py", "wex-bootlog.sh", "wex-bootmode-generator",
                      "wex-safemode-pam-check.sh", "wex-greeter.sh", "wine-explorer-session.sh",
                      "wine-explorer-session-common.sh", "wex-safe-terminal.sh", "wex-advanced-options.target",
                      "wex-advanced-options.service", "wex-safemode.target", "wex-safenet.target",
                      "wex-bootlog.service", "wex-startup-check.sh",
                      "wex-startup-check.service", "wex-lastgood.sh", "wex-failure-check.service",
                      "wex-failure-screen.service", "wex-plymouth-quit.service", "wex-session-choice.sh",
                      "wex-session-choice.service"):
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
            "systemctl enable wex-failure-check.service",
            "systemctl enable wex-session-choice.service",
            "useradd --create-home --comment Administrator --shell /bin/bash --groups sudo administrator",
            "systemctl enable wex-startup-check.service",
            "systemctl enable wex-bootmode-record.service",
            "systemctl enable wex-failure-check.service",
            "systemctl enable wex-session-choice.service"])
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


def load_boot1():
    spec = importlib.util.spec_from_file_location("wex_boot1", SCRIPTS / "wex-boot1.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestBoot1(TempDirTest):
    """org.wineexplorer.Boot1 (scripts/wex-boot1.py): Startup and Recovery's settings, Safe Mode work order D5."""

    def setUp(self):
        super().setUp()
        self.boot = load_boot1()
        self.boot.BOOT_CONF = str(self.tmp / "etc" / "boot.conf")
        self.boot.PANIC_CONF = str(self.tmp / "sysctl.d" / "90-wineexplorer-recovery.conf")
        self.boot.PANIC_DEFAULT = str(self.tmp / "usr-sysctl.d" / "90-wineexplorer-recovery.conf")
        self.boot.CRASH_CONF = str(self.tmp / "system.conf.d" / "90-wineexplorer-recovery.conf")
        self.boot.PANIC_SYSCTL = str(self.tmp / "panic")
        self.boot.PRODUCT_CONF = str(self.tmp / "product.conf")
        self.boot.VINTNER = str(self.tmp / "no-vintner")
        (self.tmp / "panic").write_text("0\n", encoding="ascii")
        self.with_systemctl()

    def with_systemctl(self, code=0):
        """A `systemctl` that logs its arguments and exits with CODE."""
        helper = self.tmp / "fake-systemctl"
        helper.write_text(f'#!/bin/sh\necho "$@" >> "{self.tmp}/systemctl.log"\necho failed >&2\nexit {code}\n',
                          encoding="ascii")
        helper.chmod(0o755)
        self.boot.SYSTEMCTL = str(helper)

    def systemctl_log(self):
        path = self.tmp / "systemctl.log"
        return path.read_text(encoding="ascii").splitlines() if path.exists() else []

    def with_vintner(self, menu=0, options=("quiet",), code=0, reply=None):
        """A `vintner` that logs its arguments; menu-timeout and cmdline show answer from MENU and OPTIONS, a set
        answers REPLY (JSON) and exits with CODE."""
        helper = self.tmp / "fake-vintner"
        show = json.dumps({"ok": True, "file": "/etc/kernel/cmdline", "exists": True, "options": list(options),
                           "running": "x"})
        timeout = json.dumps({"ok": True, "firmware": "both", "timeout": menu, "uefi": menu, "bios": menu})
        answer = json.dumps(reply if reply is not None else {"ok": True, "published": True, "generation": 4})
        helper.write_text("#!/bin/sh\n"
                          f'echo "$@" >> "{self.tmp}/vintner.log"\n'
                          f'if [ "$1 $3" = "menu-timeout " ]; then echo \'{timeout}\'; exit 0; fi\n'
                          f'if [ "$1 $3" = "cmdline show" ]; then echo \'{show}\'; exit 0; fi\n'
                          f"echo '{answer}'\nexit {code}\n", encoding="ascii")
        helper.chmod(0o755)
        self.boot.VINTNER = str(helper)

    def vintner_log(self):
        path = self.tmp / "vintner.log"
        return path.read_text(encoding="ascii").splitlines() if path.exists() else []

    def test_defaults_without_vintner_or_files(self):
        values = self.boot.settings()
        self.assertEqual(values, {"OperatingSystem": "WineExplorer", "HasLoader": False, "MenuTimeout": 0,
                                  "RecoveryEnabled": True, "RecoveryTimeout": 30, "AutoRestart": False,
                                  "HasOptions": False, "Options": ""})

    def test_settings_from_vintner_and_the_files(self):
        self.with_vintner(menu=5, options=("quiet", "splash"))
        (self.tmp / "product.conf").write_text("# product\nname=WineExplorer Test\n", encoding="utf-8")
        (self.tmp / "panic").write_text("10\n", encoding="ascii")
        values = self.boot.settings()
        self.assertEqual(values["OperatingSystem"], "WineExplorer Test")
        self.assertEqual((values["HasLoader"], values["MenuTimeout"]), (True, 5))
        self.assertEqual((values["HasOptions"], values["Options"]), (True, "quiet splash"))
        self.assertTrue(values["AutoRestart"])
        self.assertEqual(self.vintner_log(), ["menu-timeout --json", "cmdline --json show"])

    def test_apply_changes_only_what_differs(self):
        self.with_vintner(menu=0)
        self.boot.apply(0, True, 30, False)
        self.assertEqual(self.vintner_log(), ["menu-timeout --json"])
        self.assertFalse(Path(self.boot.BOOT_CONF).exists())
        self.assertFalse(Path(self.boot.PANIC_CONF).exists())
        self.boot.apply(30, True, 7, True)
        self.assertEqual(self.vintner_log()[-2:], ["menu-timeout --json", "menu-timeout --json 30"])
        self.assertEqual(self.boot.recovery_timeout(), 7)
        self.assertIn("recovery_timeout=7\n", Path(self.boot.BOOT_CONF).read_text(encoding="utf-8"))
        self.assertIn("kernel.panic = 10\n", Path(self.boot.PANIC_CONF).read_text(encoding="utf-8"))
        self.assertEqual((self.tmp / "panic").read_text(encoding="ascii"), "10\n")    # the running kernel too
        # and a crash of systemd itself, which rereads system.conf.d at once
        self.assertIn("[Manager]\nCrashAction=reboot\n", Path(self.boot.CRASH_CONF).read_text(encoding="utf-8"))
        self.assertEqual(self.systemctl_log(), ["daemon-reload"])
        self.assertTrue(self.boot.auto_restart())
        self.boot.apply(30, True, 7, False)
        self.assertIn("kernel.panic = 0\n", Path(self.boot.PANIC_CONF).read_text(encoding="utf-8"))
        self.assertIn("CrashAction=freeze\n", Path(self.boot.CRASH_CONF).read_text(encoding="utf-8"))
        self.assertEqual(self.systemctl_log(), ["daemon-reload"] * 2)
        self.assertFalse(self.boot.auto_restart())
        # recovery options turned off keep their seconds, as XP keeps bootstat.dat's byte 9
        self.boot.apply(30, False, 7, False)
        self.assertFalse(self.boot.recovery_enabled())
        self.assertEqual(self.boot.recovery_timeout(), 7)
        self.assertIn("recovery_menu=0\n", Path(self.boot.BOOT_CONF).read_text(encoding="utf-8"))

    def test_automatic_restart_is_on_by_default_as_on_xp(self):
        # the package's /usr/lib/sysctl.d file (XP: CrashControl AutoReboot 1); /etc's file of the same name replaces it
        default = Path(self.boot.PANIC_DEFAULT)
        default.parent.mkdir(parents=True)
        default.write_text((REPO_ROOT / "installer" / "systemd" / "90-wineexplorer-recovery.conf")
                           .read_text(encoding="utf-8"), encoding="utf-8")
        self.assertEqual(self.boot.panic_value(), 10)
        self.assertTrue(self.boot.settings()["AutoRestart"])
        Path(self.boot.PANIC_CONF).parent.mkdir(parents=True)
        Path(self.boot.PANIC_CONF).write_text("kernel.panic = 0\n", encoding="utf-8")
        self.assertFalse(self.boot.auto_restart())
        crash = (REPO_ROOT / "installer" / "systemd" / "system-recovery.conf").read_text(encoding="utf-8")
        self.assertIn("[Manager]\nCrashAction=reboot\n", crash)
        package = PACKAGE.read_text(encoding="utf-8")
        self.assertIn('"$SCRIPT_DIR/systemd/90-wineexplorer-recovery.conf" '
                      '"$STAGE_SESSION/usr/lib/sysctl.d/90-wineexplorer-recovery.conf"', package)
        self.assertIn('"$SCRIPT_DIR/systemd/system-recovery.conf" '
                      '"$STAGE_SESSION/usr/lib/systemd/system.conf.d/90-wineexplorer-recovery.conf"', package)

    def test_a_failed_reload_is_reported(self):
        self.with_systemctl(code=1)
        with self.assertRaises(self.boot.Failed) as caught:
            self.boot.set_auto_restart(True)
        self.assertIn("crash action", str(caught.exception))
        self.assertIn("CrashAction=reboot", Path(self.boot.CRASH_CONF).read_text(encoding="utf-8"))

    def test_a_dismissed_password_dialog_is_cancelled_not_refused(self):
        # polkit's CheckAuthorization result: (authorized, challenge, details); the agent's Cancel sets polkit.dismissed
        self.assertEqual(self.boot.refusal((False, False, {"polkit.dismissed": "true"})), "Cancelled")
        self.assertEqual(self.boot.refusal((False, False, {})), "NotAuthorized")
        self.assertEqual(self.boot.refusal((False, True, {"polkit.retains_authorization_after_challenge": "1"})),
                         "NotAuthorized")
        # wexhost maps Boot1's Cancelled to ERROR_CANCELLED, which keeps Startup and Recovery open without a message
        unixlib = (REPO_ROOT / "src" / "dlls" / "wexhost" / "unixlib.c").read_text(encoding="utf-8")
        self.assertIn('"org.wineexplorer.Boot1.Error.Cancelled" )) return ERROR_CANCELLED', unixlib)
        startrec = (REPO_ROOT / "src" / "dlls" / "sysdm.cpl" / "startrec.c").read_text(encoding="utf-8")
        self.assertIn("if (err == ERROR_CANCELLED) return FALSE;", startrec)

    def test_the_saved_panic_setting_wins_over_this_start(self):
        # "Disable automatic restart on system failure" (F8) sets kernel.panic=0 for one start only
        Path(self.boot.PANIC_CONF).parent.mkdir(parents=True)
        Path(self.boot.PANIC_CONF).write_text("kernel.panic = 10\n", encoding="utf-8")
        self.assertTrue(self.boot.auto_restart())

    def test_boot_conf_keeps_its_other_lines(self):
        conf = Path(self.boot.BOOT_CONF)
        conf.parent.mkdir(parents=True)
        conf.write_text("# mine\nother=1\nrecovery_timeout=30\n", encoding="utf-8")
        self.boot.set_recovery(True, 0)
        self.assertEqual(conf.read_text(encoding="utf-8"), "# mine\nother=1\nrecovery_timeout=0\nrecovery_menu=1\n")
        self.assertEqual(self.boot.recovery_timeout(), 0)
        self.assertEqual(oct(conf.stat().st_mode & 0o777), "0o644")

    def test_timeouts_out_of_range_are_refused(self):
        self.with_vintner()
        for bad in (-1, 1000, True):
            with self.assertRaises(self.boot.Refused):
                self.boot.apply(bad, True, 30, False)
            with self.assertRaises(self.boot.Refused):
                self.boot.apply(0, True, bad, False)
        self.assertEqual(self.vintner_log(), [])

    def test_menu_timeout_failure_is_reported(self):
        self.with_vintner(menu=0, code=1, reply={"ok": False, "code": "failed", "error": "no ESP"})
        with self.assertRaises(self.boot.Failed) as caught:
            self.boot.apply(5, True, 30, False)
        self.assertIn("no ESP", str(caught.exception))

    def test_options_as_etc_kernel_cmdline_reads_them(self):
        text = "# WineExplorer startup options\n\nquiet  splash\n# a comment\nnomodeset\n"
        self.assertEqual(self.boot.parse_options(text), ["quiet", "splash", "nomodeset"])
        for bad in ("--clear", "-x", "quiet --file=/etc/shadow", "café"):
            with self.assertRaises(self.boot.Refused, msg=bad):
                self.boot.parse_options(bad)

    def test_set_options_publishes_through_vintner(self):
        self.with_vintner()
        self.assertEqual(self.boot.set_options("# options\nquiet nomodeset\n"), (True, 4))
        self.assertEqual(self.vintner_log(), ["cmdline --json set quiet nomodeset"])
        self.assertEqual(self.boot.set_options("# nothing\n"), (True, 4))
        self.assertEqual(self.vintner_log()[-1], "cmdline --json set --clear")
        self.with_vintner(code=3, reply={"ok": False, "code": "refused", "error": "root= belongs to vintner"})
        with self.assertRaises(self.boot.Refused):
            self.boot.set_options("root=/dev/sda1")
        self.with_vintner(code=1, reply={"ok": False, "code": "failed", "error": "publish failed"})
        with self.assertRaises(self.boot.Failed):
            self.boot.set_options("quiet")

    def test_get_prints_json(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(self.boot.main(["--get"]), 0)
        self.assertEqual(json.loads(out.getvalue())["RecoveryTimeout"], 30)
        self.assertEqual(self.boot.main(["--bad"]), 2)

    def test_service_files_name_one_bus_name_and_the_admin_rule(self):
        installer = REPO_ROOT / "installer"
        unit = (installer / "systemd" / "wex-boot1.service").read_text(encoding="utf-8")
        activation = (installer / "dbus" / "org.wineexplorer.Boot1.service").read_text(encoding="utf-8")
        policy = (installer / "dbus" / "org.wineexplorer.Boot1.conf").read_text(encoding="utf-8")
        action = (installer / "polkit" / "org.wineexplorer.boot1.policy").read_text(encoding="utf-8")
        self.assertIn("BusName=org.wineexplorer.Boot1", unit)
        self.assertIn("ExecStart=/usr/local/sbin/wex-boot1", unit)
        self.assertIn("SystemdService=wex-boot1.service", activation)
        self.assertIn('<allow own="org.wineexplorer.Boot1"/>', policy)
        self.assertIn(f'<action id="{self.boot.ACTION}">', action)
        # the same rule as hostname1 and timedate1: no second notion of administrator
        self.assertEqual(re.findall(r"<allow_\w+>(\w+)</allow_\w+>", action), ["auth_admin_keep"] * 3)
        package = PACKAGE.read_text(encoding="utf-8")
        for needle in ("scripts/wex-boot1.py", "org.wineexplorer.Boot1.conf", "org.wineexplorer.Boot1.service",
                       "org.wineexplorer.boot1.policy", "wex-boot1.service"):
            self.assertIn(needle, package)


if __name__ == "__main__":
    unittest.main()
