# Milestone F8 Verification: Unattended Mode

**Test Date:** 2026-10-12
**Environment:** VirtualBox VM `WineExplorer-Install-Test` (BIOS, 20 GB fresh unpartitioned disk, ISO built with `--unattended`)

## 1. Test Overview & Automated Execution
The unattended ISO was booted with zero keys pressed.
1. GRUB booted "Install (unattended)" automatically via `wexunattend=/run/live/medium/setup/unattend.sif`.
2. Stage 1 (`textsetup`) parsed `/setup/unattend.sif`, partitioned the 20 GB disk, created the ext4 filesystem, copied all files based on `layout.inf`, installed GRUB MBR, configured the swapfile, and rebooted after the 15-second countdown.
3. Stage 2 (`setup.exe`) started under dedicated unprivileged `wexsetup` runner on VT7, parsed the unattended answer file, applied system settings via D-Bus (`hostname1`, `timedate1`, `locale1`), constructed `/etc/skel/.wine` template prefix, and rebooted into OOBE.
4. Stage 3 (`oobe.exe`) started under `wexsetup`, parsed `[WexOobe]` accounts, created administrative user `wineuser` via AccountsService over D-Bus, copied the template prefix to `/home/wineuser/.wine`, configured GDM autologin, cleanly wiped temporary user `wexsetup` and temporary setup rules/files, and started GDM.
5. GDM auto-logged in `wineuser` into the `wine-explorer` session, displaying the Windows XP Luna Blue desktop.

## 2. Target Disk Verification & Log Outputs

### `/var/log/wineexplorer-setup.log`
```
[2026-10-12 03:38:50] === WineExplorer GUI-mode Setup (setup.exe) Started ===
[2026-10-12 03:38:50] Parsed unattended answer file settings successfully.
[2026-10-12 03:38:50] Unattended installation: skipping interactive wizard pages.
[2026-10-12 03:38:50] Stage: Installing Devices
[2026-10-12 03:38:51] Detected hardware devices size: 59135 bytes
[2026-10-12 03:38:53] Stage: Installing Network
[2026-10-12 03:38:55] Stage: Copying components
[2026-10-12 03:38:55] Applying system settings via D-Bus and host integration...
[2026-10-12 03:38:55] WexHostnameSet('WEX-PC') result: 0
[2026-10-12 03:38:55] WexTimedateSetTimezone('UTC') result: 0
[2026-10-12 03:38:55] WexLocaleSet result: 0
[2026-10-12 03:38:55] Generating template user prefix in /etc/skel/.wine...
[2026-10-12 03:38:56] Template prefix generation finished.
[2026-10-12 03:38:58] Stage: Finalizing Installation
[2026-10-12 03:39:00] GUI Setup complete. Ready for OOBE.
```

### `/var/log/wineexplorer-oobe.log`
```
[2026-10-12 03:39:40] === WineExplorer OOBE (oobe.exe) Started ===
[2026-10-12 03:39:40] Parsed unattended OOBE accounts.
[2026-10-12 03:39:41] Unattended installation: provisioning configured users directly.
[2026-10-12 03:39:41] Provisioning user accounts via AccountsService...
[2026-10-12 03:39:41] Creating user 'wineuser' (admin=1)
[2026-10-12 03:39:41] WexUserCreate('wineuser') result: 0
[2026-10-12 03:39:41] Copied /etc/skel/.wine template prefix to user home.
[2026-10-12 03:39:41] Enabling GDM autologin for single passwordless user 'wineuser'
[2026-10-12 03:39:41] Cleaning up temporary setup files and permissions...
[2026-10-12 03:39:41] OOBE user provisioning and cleanup completed.
```

### System Configuration State
- **Hostname (`/etc/hostname`):** `WEX-PC`
- **Timezone (`/etc/localtime`):** `../usr/share/zoneinfo/UTC`
- **User Database (`/etc/passwd`):** `wineuser:x:1000:1000:wineuser:/home/wineuser:/bin/bash`
- **Admin Group (`/etc/group`):** `sudo:x:27:wineuser`
- **Temporary User `wexsetup`:** Cleanly purged from system (`id wexsetup` returns failure)
- **Temporary Polkit Rule `49-wexsetup.rules`:** Removed
- **Temporary Setup Directory `/etc/wineexplorer/setup`:** Removed
- **GDM Autologin (`/etc/gdm3/daemon.conf`):**
  ```ini
  # GDM configuration for WineExplorer
  [daemon]
  WaylandEnable=false
  AutomaticLoginEnable=true
  AutomaticLogin=wineuser
  DefaultSession=wine-explorer
  ```
- **Permissions:**
  - `/etc/wineexplorer`: `drwxr-xr-x` (755 root:root)
  - `/var/log/wineexplorer-*.log`: `-rw-r-----` (640 root:adm)

## 3. Evidence Screenshots
- `f8_stage1_copy.png`: Stage 1 unattended copy driven by `layout.inf`.
- `f8_stage1_reboot.png`: Stage 1 15-second reboot countdown screen.
- `f8_boot_disk.png`: Booting into the installed Debian GNU/Linux system from hard disk.
- `f8_desktop.png`: Full Luna Blue WineExplorer desktop loaded after autologin.
