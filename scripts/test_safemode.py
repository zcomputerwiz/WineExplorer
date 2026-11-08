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
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = REPO_ROOT / "scripts"
SYSTEMD = REPO_ROOT / "installer" / "systemd"
GRUB_ENTRY = REPO_ROOT / "installer" / "live-build" / "config" / "includes.chroot" / "etc" / "grub.d" / "11_wineexplorer_advanced"
BOOTMODE = SCRIPTS / "wex-bootmode.sh"
GENERATOR = SYSTEMD / "wex-bootmode-generator"
PAM_CHECK = SCRIPTS / "wex-safemode-pam-check.sh"
PACKAGE = REPO_ROOT / "installer" / "package-wine.sh"


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
        for script in (BOOTMODE, GENERATOR, PAM_CHECK, SCRIPTS / "wex-bootlog.sh", GRUB_ENTRY):
            result = subprocess.run(["sh", "-n", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, f"{script}: {result.stderr}")
        for script in (SCRIPTS / "wex-greeter.sh", SCRIPTS / "wine-explorer-session.sh",
                       SCRIPTS / "wine-explorer-session-common.sh", SCRIPTS / "wex-safe-terminal.sh", PACKAGE):
            result = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, f"{script}: {result.stderr}")

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
        return {p.name: os.readlink(p) for p in early.iterdir()}

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
            shutil.rmtree(self.tmp / "early")

    def test_safe_mode_with_networking(self):
        links = self.generate("safenet")
        self.assertEqual(links.pop("default.target"), "/units/wex-safenet.target")
        self.assertEqual(links.get("timers.target"), "/dev/null")
        self.assertNotIn("NetworkManager.service", links)

    def test_offline_update_comes_first(self):
        self.assertEqual(self.generate("safe", system_update=True), {})


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


class FakeScreen:
    def __init__(self, stdscr):
        pass

    def advanced_menu(self, items, index):
        pass

    def os_choices(self, chosen_label):
        pass

    def flush_input(self):
        pass


class FakeStdscr:
    def __init__(self, keys):
        self.keys = list(keys)

    def getch(self):
        return self.keys.pop(0)


class TestMenu(TempDirTest):
    def setUp(self):
        super().setUp()
        self.menu = load_menu()
        self.menu.STATE_DIR = str(self.tmp / "run")
        self.menu.SETUP_STAGE = str(self.tmp / "stage")
        self.menu.PANIC_SYSCTL = str(self.tmp / "panic")
        self.menu.Screen = FakeScreen
        self.calls = []

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
        common = (SCRIPTS / "wine-explorer-session-common.sh").read_text(encoding="utf-8")
        skip = common.index("safe|safenet) return 0")
        self.assertLess(common.index("wineboot.exe -u"), skip)
        self.assertLess(skip, common.index("--apply-user-defaults"))
        self.assertLess(skip, common.index("vulkan prepare"))

    def test_greeter_needs_no_wine_in_safe_mode(self):
        greeter = (SCRIPTS / "wex-greeter.sh").read_text(encoding="utf-8")
        self.assertLess(greeter.index("exec /usr/sbin/lightdm-gtk-greeter"), greeter.index("export WINEPREFIX="))


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
        screens = (REPO_ROOT / "installer" / "textsetup" / "screens.c").read_text(encoding="utf-8")
        self.assertIn("11_wineexplorer_advanced", screens)
        self.assertNotIn("11_wineexplorer_safemode", screens)


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
                      "wex-safenet.target", "wex-bootlog.service", "11_wineexplorer_advanced"):
            self.assertIn(piece, script)
        self.assertFalse("scripts/wex-safemode-setup.sh" in script, "the old setup script is still packaged")
        self.assertFalse("xsessions/wex-safe-terminal.desktop" in script, "the old session is still packaged")
        self.assertIsNone(re.search(r"systemctl enable [^\n]*wex-safemode", script))

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
        block = self.safe_mode_block()
        for _ in range(2):
            subprocess.run(["sh", "-e", "-c", block], check=True)
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


if __name__ == "__main__":
    unittest.main()
