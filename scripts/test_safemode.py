#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for WineExplorer Safe Mode with Command Prompt recovery mode."""

import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


class TestSafeModeSyntax(unittest.TestCase):
    """Verifies all Safe Mode scripts and configs are syntactically valid."""

    def test_scripts_syntax(self):
        scripts = [
            REPO_ROOT / "scripts" / "wex-safe-terminal.sh",
            REPO_ROOT / "scripts" / "wex-safemode-setup.sh",
            REPO_ROOT / "scripts" / "wex-safemode-pam-check.sh",
            REPO_ROOT / "installer" / "live-build" / "config" / "includes.chroot" / "etc" / "grub.d" / "11_wineexplorer_safemode",
        ]
        for script in scripts:
            self.assertTrue(script.is_file(), f"Missing script: {script}")
            result = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, f"Syntax error in {script}:\n{result.stderr}")

    def test_desktop_file(self):
        desktop = REPO_ROOT / "scripts" / "wex-safe-terminal.desktop"
        self.assertTrue(desktop.is_file(), f"Missing desktop file: {desktop}")
        content = desktop.read_text(encoding="utf-8")
        self.assertIn("[Desktop Entry]", content)
        self.assertIn("Exec=/usr/local/bin/wex-safe-terminal.sh", content)
        self.assertIn("Name=Safe Mode with Command Prompt", content)
        self.assertIn("Type=Application", content)

    def test_systemd_service(self):
        service = REPO_ROOT / "installer" / "systemd" / "wex-safemode.service"
        self.assertTrue(service.is_file(), f"Missing systemd service: {service}")
        content = service.read_text(encoding="utf-8")
        self.assertIn("ExecStart=/usr/local/sbin/wex-safemode-setup.sh start", content)
        self.assertIn("ExecStop=/usr/local/sbin/wex-safemode-setup.sh stop", content)
        self.assertIn("Before=display-manager.service lightdm.service", content)

    def test_package_wine_includes_safemode(self):
        pkg_script = REPO_ROOT / "installer" / "package-wine.sh"
        content = pkg_script.read_text(encoding="utf-8")
        self.assertIn("wex-safe-terminal.sh", content)
        self.assertIn("wex-safemode-setup.sh", content)
        self.assertIn("wex-safemode-pam-check.sh", content)
        self.assertIn("wex-safe-terminal.desktop", content)
        self.assertIn("wex-safemode.service", content)
        self.assertIn("11_wineexplorer_safemode", content)
        self.assertIn("xterm", content)

    def test_grub_timeout_setting(self):
        grub_cfg = REPO_ROOT / "installer" / "live-build" / "config" / "includes.chroot" / "etc" / "default" / "grub.d" / "wineexplorer.cfg"
        content = grub_cfg.read_text(encoding="utf-8")
        self.assertIn("GRUB_TIMEOUT=1", content)
        self.assertIn("GRUB_TIMEOUT_STYLE=hidden", content)

        screens_c = (REPO_ROOT / "installer" / "textsetup" / "screens.c").read_text(encoding="utf-8")
        self.assertIn('fprintf(f_grub,', screens_c)
        self.assertIn('GRUB_TIMEOUT=1', screens_c)


class TestSafeModeSetup(unittest.TestCase):
    """Tests runtime behavior of wex-safemode-setup.sh."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp(prefix="wex_test_safemode_")
        self.state_dir = os.path.join(self.temp_dir, "run_wex_safemode")
        self.lightdm_dir = os.path.join(self.temp_dir, "lightdm_conf_d")
        self.pam_dir = os.path.join(self.temp_dir, "pam_d")
        self.acc_dir = os.path.join(self.temp_dir, "accounts_users")

        os.makedirs(self.lightdm_dir, exist_ok=True)
        os.makedirs(self.pam_dir, exist_ok=True)
        os.makedirs(self.acc_dir, exist_ok=True)

        self.override_conf = os.path.join(self.lightdm_dir, "99-safemode.conf")
        self.pam_file = os.path.join(self.pam_dir, "lightdm")
        Path(self.pam_file).write_text("#%PAM-1.0\n@include common-auth\n", encoding="utf-8")

        # Create simulated script with substituted paths
        setup_src = (REPO_ROOT / "scripts" / "wex-safemode-setup.sh").read_text(encoding="utf-8")
        setup_mod = setup_src.replace('STATE_DIR="/run/wex-safemode"', f'STATE_DIR="{self.state_dir}"')
        setup_mod = setup_mod.replace('OVERRIDE_CONF="/etc/lightdm/lightdm.conf.d/99-safemode.conf"', f'OVERRIDE_CONF="{self.override_conf}"')
        setup_mod = setup_mod.replace('PAM_LIGHTDM="/etc/pam.d/lightdm"', f'PAM_LIGHTDM="{self.pam_file}"')
        setup_mod = setup_mod.replace('ACC_DIR="/var/lib/AccountsService/users"', f'ACC_DIR="{self.acc_dir}"')
        setup_mod = setup_mod.replace('/etc/lightdm/lightdm.conf.d', self.lightdm_dir)

        self.test_setup_script = os.path.join(self.temp_dir, "test-setup.sh")
        Path(self.test_setup_script).write_text(setup_mod, encoding="utf-8")
        os.chmod(self.test_setup_script, stat.S_IRWXU)

    def tearDown(self):
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_start_and_clean(self):
        # Initial state: autologin config exists
        autologin_conf = os.path.join(self.lightdm_dir, "60-autologin.conf")
        Path(autologin_conf).write_text("[Seat:*]\nautologin-user=wineuser\n", encoding="utf-8")

        # Existing regular user in AccountsService
        wineuser_acc = os.path.join(self.acc_dir, "wineuser")
        Path(wineuser_acc).write_text("[User]\nSession=wine-explorer\nSystemAccount=false\n", encoding="utf-8")

        # 1. Start safe mode
        env = dict(os.environ, WEXMODE_FORCE_SAFECMD="1")
        res = subprocess.run(["bash", self.test_setup_script, "start"], env=env, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"Setup start failed: {res.stderr}")

        # Check override conf exists
        self.assertTrue(os.path.isfile(self.override_conf), "99-safemode.conf should exist")
        override_content = Path(self.override_conf).read_text(encoding="utf-8")
        self.assertIn("greeter-session=lightdm-gtk-greeter", override_content)
        self.assertIn("user-session=wex-safe-terminal", override_content)
        self.assertIn("autologin-user=", override_content)

        # Check autologin was backed up
        self.assertFalse(os.path.isfile(autologin_conf), "Autologin conf should have been moved")
        self.assertTrue(os.path.isfile(os.path.join(self.state_dir, "60-autologin.conf.bak")))

        # Check PAM check injected
        pam_content = Path(self.pam_file).read_text(encoding="utf-8")
        self.assertIn("wex-safemode-pam-check.sh", pam_content)

        # 2. Stop/clean safe mode
        res_clean = subprocess.run(["bash", self.test_setup_script, "stop"], env=dict(os.environ), capture_output=True, text=True)
        self.assertEqual(res_clean.returncode, 0, f"Setup stop failed: {res_clean.stderr}")

        # Check override removed and autologin restored
        self.assertFalse(os.path.isfile(self.override_conf), "99-safemode.conf should be removed")
        self.assertTrue(os.path.isfile(autologin_conf), "Autologin conf should be restored")

        # Check PAM rule removed
        pam_clean = Path(self.pam_file).read_text(encoding="utf-8")
        self.assertNotIn("wex-safemode-pam-check.sh", pam_clean)


class TestSafeModePamCheck(unittest.TestCase):
    """Tests runtime authorization of wex-safemode-pam-check.sh."""

    def setUp(self):
        self.pam_script = str(REPO_ROOT / "scripts" / "wex-safemode-pam-check.sh")

    def test_inactive_allows_any_user(self):
        # With Safe Mode inactive, any user authentication should succeed immediately (exit 0)
        env = dict(os.environ)
        env.pop("WEXMODE_FORCE_SAFECMD", None)
        res = subprocess.run(["bash", self.pam_script, "randomuser"], env=env)
        self.assertEqual(res.returncode, 0)

    def test_active_allows_root_and_sudoer(self):
        env = dict(os.environ, WEXMODE_FORCE_SAFECMD="1")
        # Root is always authorized
        res_root = subprocess.run(["bash", self.pam_script, "root"], env=env)
        self.assertEqual(res_root.returncode, 0)

        # Current user if in sudo
        cur_user = os.getenv("USER", "wineuser")
        is_sudo = subprocess.run(["id", "-nG", cur_user], capture_output=True, text=True)
        if "sudo" in is_sudo.stdout.split():
            res_user = subprocess.run(["bash", self.pam_script, cur_user], env=env)
            self.assertEqual(res_user.returncode, 0)

    def test_active_denies_non_sudoer(self):
        env = dict(os.environ, WEXMODE_FORCE_SAFECMD="1")
        # 'nobody' or 'daemon' are non-sudo system users
        res_nobody = subprocess.run(["bash", self.pam_script, "nobody"], env=env)
        self.assertEqual(res_nobody.returncode, 1, "Non-sudoer should be denied during Safe Mode")


if __name__ == "__main__":
    unittest.main()
