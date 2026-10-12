# Work order: fix the installer ISO (follow-up to commit `131d336`)

**For:** Gemini (implementation agent) · **Written:** 2026-10-11 · **Read first:** `docs/INSTALLER_WORK_ORDER.md` (the
original order; everything in it still applies), `CLAUDE.md`

## 0. Why this order exists

Commit `131d336` was reviewed by reading the code and by installing the ISO (`installer/wineexplorer.iso`) in a fresh
VirtualBox VM: BIOS firmware, a 20 GB blank disk, no keys pressed.

What happened:
1. GRUB booted "Install" after its timeout.
2. Setup wiped the disk, copied the system and rebooted, all within about 2 minutes, with no screen asking anything.
3. After the reboot, GUI-mode Setup ran for 6 seconds and OOBE for 4.
4. The machine auto-logged into the live system's `wineuser` account. The WineExplorer session created its prefix,
   then died about a minute later ("X connection to :0 broken"), leaving the stock Debian GDM greeter.

The installed disk showed the following:

| Area | Finding |
|---|---|
| Repository | A 147 MB `.deb` (`installer/live-build/config/packages.chroot/wineexplorer_11.19-wineexplorer1_amd64.deb`) and the `textsetup` binary (twice) are committed. GitHub refuses files over 100 MB, so the history cannot be pushed. `setup.exe` and `oobe.exe` are prebuilt and untracked, and they are not part of the Wine build. |
| Stage 1 | The default ISO contains `/setup/unattend.sif`. `textsetup` treats the mere presence of that file as unattended mode, so the **normal Install entry erases the first disk without a prompt**. |
| Stage 1 | The partition screen is a mock-up. It shows the whole first disk (`sda`, `vda`, `nvme0n1`) as "Unpartitioned space" and ignores existing partitions. D does nothing, C is the same as ENTER, and both erase the disk with `sfdisk --wipe always`. "Leave the current file system intact" still formats. |
| Stage 1 | The copy is `cp -a` of ten folders with a progress bar that jumps to fixed values. It sat at 85% during `/usr`. Kernel and systemd messages print over the Setup screen. `/setup/layout.inf` is generated but never used. |
| Stage 1 | The live system is copied as it is: `live-boot`, `live-config`, the live user `wineuser` ("Wine User", live password), `/etc/sudoers.d/live` and `/etc/sudoers.d/wineuser` (both `NOPASSWD: ALL`), and the live GDM autologin. No swap is set up. |
| Stage 2 | Nothing parses `unattend.sif`: the code only checks that the file exists. The wizard window procedure exists but is never shown. There are no dialog resources. Interactive mode waits 2 seconds, then applies built-in defaults. **Result:** the hostname stayed `debian`, though the answer file says `WEX-PC`, and root stayed locked. |
| Stage 2 | Privileged work is `system("sudo …")`. User input is interpolated into shell strings (`echo 'root:%s' \| chpasswd`, `hostnamectl set-hostname '%s'`), so a `'` in a password or name runs arbitrary commands as root. `wexsetup` gets `NOPASSWD: ALL` sudo. |
| Stage 3 | `oobe.exe` only paints text. No user is created from the answer file or from input. The Users page says "wineuser". `wex-oobe.sh` creates `wineuser:wineuser` (a known password, with sudo) if the user is missing. |
| Stage 3 | Cleanup is incomplete: the `wexsetup` user remains. `/etc/wineexplorer` is left 777 and `/var/log/wineexplorer-*.log` 666. |
| Branding | Every screen says "Windows XP Professional Setup", "Windows XP Licensing Agreement", "Welcome to Microsoft Windows XP" or "Windows XP Setup". The product is **"Debian GNU/Linux with WineExplorer"** (work order §4.3). Microsoft's product names must not appear. XP's *layout, colours and sentence structure* are what we clone. |
| Docs | `docs/INSTALLER.md` describes wizard pages, user creation and a copy driven by the file list, none of which exist. |

**Process requirement for this order.** Every milestone in §6 must be shown working in the test VM before it is
reported as done, with:
- screenshots (`VBoxManage controlvm … screenshotpng`) committed to `installer/test-evidence/<milestone>/`
  (small PNGs are fine in git);
- the relevant log excerpts from the installed disk.

Describe only what was observed. If something was not tested, say so.

## 1. Repository cleanup (do this first, before any other commit)

**Already done (2026-10-11):** the whole history was rewritten. The `.deb` files and the `textsetup` binaries are
gone from every commit, and so is all Microsoft material. **Start from a fresh clone of the new repository**; any
old clone, and any branch made from one, must not be pushed. Read `LICENSE.md` (the project is now
GPL-2.0-or-later) and `docs/ASSETS.md` (no Microsoft files, names or reference-source paths in this repository;
`scripts/open_assets.py` makes the open art).

Still to do:
- **`.gitignore` covers them:** `installer/**/*.deb`, `installer/textsetup/textsetup`,
  `installer/live-build/config/includes.chroot/setup/textsetup`, `wine/programs/*/*.exe`, `installer/*.iso`,
  `installer/test_disk.vdi`.
- **`build-iso.sh` builds what it needs:** it runs `package-wine.sh` and `make -C installer/textsetup` itself, and
  copies the results into the live-build tree in its work directory, never into the source tree.
- **`setup` and `oobe` are proper Wine programs:**
  - `WINE_CONFIG_MAKEFILE(programs/setup)` and `WINE_CONFIG_MAKEFILE(programs/oobe)` in `wine/configure.ac`, then
    `autoconf` and `config.status` on the build VM. Copy the regenerated `wine/configure` back.
  - Each program gets a `.rc` (dialogs, strings, icon) and the GPL-2.0-or-later header (`scripts/add-license.py`).
  - Wine installs them to `C:\windows\system32\setup.exe` / `oobe.exe`. The scripts call them by those names.
    Remove the copies to `/usr/local/bin/*.exe`.
- **`scripts/vm.sh`:** keep the key-path fallback, but document it in the script's header.

## 2. Stage 1: text-mode Setup

### 2.1 Disk safety (highest priority)
- **Unattended only on purpose.** Unattended mode is used only when the kernel command line has
  `wexunattend=<path>`. The *Install (unattended)* GRUB entry is the only one that passes it. The default ISO
  ships `unattend.sif.example` only, never `/setup/unattend.sif`. `build-iso.sh --unattended` writes the answer file
  and sets that GRUB entry as the default.
- **The answer file is read.** Parse it as the INI file it is. `TargetDisk` and `Repartition` are honoured; without
  `Repartition = Yes`, unattended Setup must not touch existing partitions and stops with an error screen.
- **A real partition list** (libfdisk + libblkid, linked; no `sfdisk` through `system()`):
  - Every disk, except the boot medium itself, which you find by its `ID_FS_LABEL`/`/run/live/medium`, and loop or
    zram devices.
  - Each disk's partitions (size, file system, label) and the free regions, in XP's layout: "Disk 0 at Id 0 on bus
    0 on atapi" becomes "Disk 0 (`<model>`, `<size>` MB) on `<bus>`", then the rows.
- **The keys do what XP's do:**
  - **C** asks for a size (XP's "Create partition" screen, with min/max shown), then creates it in the selected free
    region.
  - **D** shows XP's delete-confirmation screen, with L to confirm, then deletes.
  - **ENTER** on a partition selects it. On free space, it first creates a partition filling it.
- **Confirm before writing.** Nothing is written to a disk until the format screen is confirmed. The format screen
  offers "Format the partition using the ext4 file system (Quick)", "… ext4 file system", and, only when the
  partition already has ext4, "Leave the current file system intact". That last option does not format.
- **UEFI:** create an EFI System Partition only when the disk has none. Reuse an existing one, as Windows does.
- **Swap:** a swap file of 1.5 × RAM, capped at 8 GB, in `/swapfile`, with its entry in `fstab`.

### 2.2 The copy
- **Driven by the file list.** It reads `/run/live/medium/setup/layout.inf`, the list generated at build time.
  - The bar is `bytes copied / total bytes` of that list. The status bar shows "Copying: `<file name>`", updated at
    least every 250 ms.
  - Copy in-process: `copy_file_range`, falling back to `read`/`write`. Preserve owner, mode, timestamps, xattrs and
    ACLs. Recreate symlinks, hardlinks and device nodes from the list's sections.
- **Errors** show XP's "Setup cannot copy the file…" screen (retry / skip / quit), not a silent `|| true`.
- **The console belongs to Setup.** In `wex-textsetup.service`, use `StandardOutput=tty` and set kernel messages to
  `quiet loglevel=3` in that GRUB entry, so no messages print over the screen. Also `systemd.show_status=false`.
- **Exclude what belongs to the live system**, in `mklayout.py`, and also purge it in a cleanup step on the target:
  - the packages `live-boot`, `live-config`, `live-config-systemd`, `live-tools`;
  - `/etc/sudoers.d/live`, `/etc/sudoers.d/wineuser`, and the live user `wineuser` with its home;
  - the live GDM autologin;
  - `/setup`, `/etc/live`;
  - and the `wex-textsetup` unit.

  The target starts with no regular users. OOBE creates them.

### 2.3 Branding and fidelity
- Header line: "Debian GNU/Linux with WineExplorer Setup". Licence screen: "Licensing Agreement". Keep XP's
  sentence structure with the product name substituted: "This portion of the Setup program prepares Debian
  GNU/Linux with WineExplorer to run on your computer."
- Compare each screen against the reference images listed in `docs/INSTALLER_WORK_ORDER.md` §6, and commit
  side-by-side screenshots.
- Remove "To repair … press R" until Repair exists, or show it grayed. Do not offer an option that does nothing.

## 3. Stage 2: GUI-mode Setup (`setup.exe`)

- **Real wizard pages**, built from dialog resources in `setup.rc` and shown over the billboard in XP's order:
  - Regional and Language Options
  - Personalize Your Software
  - Computer Name and Administrator Password
  - Date and Time Settings
  - Networking Settings
  - Workgroup or Computer Domain

  No Product Key page. Each page validates input as XP did, for example the computer name rules and matching
  passwords.
- **Unattended mode** reads the answer file (`GetPrivateProfileStringW` works on it) and skips exactly the pages it
  answers, as XP did (`[GuiUnattended]`, `[UserData]`, `[Networking]`, `[Identification]`).
- **No shell commands** for privileged work. Use D-Bus, extending `wine/dlls/wexhost` where needed, and document new
  calls in `docs/DBUS_PLAN.md`:

  | Setting | Service |
  |---|---|
  | Hostname | `org.freedesktop.hostname1` `SetStaticHostname` (`WexHostnameSet` exists) |
  | Time zone, NTP | `org.freedesktop.timedate1` (`WexTimedateSetTimezone`, `WexTimedateSetNtp` exist) |
  | Locale, keyboard | `org.freedesktop.locale1` `SetLocale`, `SetX11Keyboard`, `SetVConsoleKeyboard` |
  | Administrator password | AccountsService `SetPassword` on root's object, with a crypt(3) SHA-512 hash made in-process; or a tiny root D-Bus helper with a single method if AccountsService refuses root. Never `chpasswd` through a shell. |
  | Optional components | PackageKit `InstallPackages` from the local `/pool` repository |

- **Polkit, not sudo.** `49-wexsetup.rules` grants exactly these action ids to `wexsetup`, and only while
  `/etc/wineexplorer/setup/stage` exists. Remove `/etc/sudoers.d/wexsetup` and every `sudo` call.
- **The billboard's automatic stages do real work and report it** in the log: devices (`udevadm settle`, then the
  devices without a driver from `WexHardwareList`), network, components, and building `/etc/skel/.wine` with
  `wineboot` (this prefix creation was missing).
- **The time estimate** is computed from the remaining steps, not a counter.

## 4. Stage 3: OOBE (`oobe.exe`)

- **Real pages** (dialog resources, native Win32), in XP's sequence: Welcome, Help protect your PC (Automatic
  Updates: switches `unattended-upgrades` on or off through a D-Bus call or the helper), Checking your Internet
  connectivity (NetworkManager), "Who will use this computer?" (five name fields), Thank you!
- **Users are created through AccountsService `CreateUser`**:
  - The first user gets `AccountType=1` (administrator, which puts them in the `sudo` group).
  - Each user gets a password page, as XP's later "set a password" prompt did. The password is set with
    `SetPassword`. If the user leaves it empty, the account has no password, as on XP.
  - From `unattend.sif`: read the `[WexOobe]` users.
  - **No hard-coded user and no default password, anywhere.**
- **Each user's prefix** is copied from `/etc/skel/.wine`, owned by that user.
- **GDM autologin** only when exactly one user exists and that user has no password (XP's behavior). Otherwise the
  greeter shows. Write the session through AccountsService (`SetXSession` / `SetSession`, value `wine-explorer`).
- **Cleanup** in `wex-oobe.sh`:
  - Remove the polkit rule, the `wexsetup` user and home, and the stage flags.
  - Set `/etc/wineexplorer` to 755 root, and the logs to 640 root:adm.
  - Then `systemctl disable wex-setup wex-oobe`.
- **Branding:** "Welcome to Debian GNU/Linux with WineExplorer". No "Microsoft Windows" anywhere.

## 5. First logon

The installed system must reach a working WineExplorer desktop on first logon: taskbar, Start menu, desktop icons.
The test install's session died a minute after creating its prefix.
- Find the cause. Start from `~/.wine-session.log` and the journal (`gdm-x-session`, `session-N.scope`).
- If the cause is in WineExplorer itself, describe it in the report rather than working around it in the installer.

## 6. Milestones and acceptance (each with evidence in `installer/test-evidence/`)

Test VM: create your own with `installer/test-vm.py`. Do not use `WineExplorer-VM` or `ProtonExplorer-VM`. Run
everything once with BIOS firmware and once with EFI.

| # | Done when |
|---|---|
| F1 | History rewritten (with the user's OK), `.gitignore` updated, a clean `build-iso.sh` from a fresh clone produces the ISO, `setup.exe`/`oobe.exe` built by Wine's build |
| F2 | Booting the ISO and waiting 5 minutes without keys leaves the disk **unchanged**: compare `sha256sum` of the VDI before and after |
| F3 | On a disk pre-partitioned with two partitions (one ext4 with a file on it): the partition list shows both; D asks for confirmation; "Leave intact" keeps the file; installing into the other partition keeps the first partition's file |
| F4 | The copy bar moves smoothly by bytes, and no kernel or systemd text appears on the Setup screen. Screenshots at 25/50/75% |
| F5 | The installed disk has no `live-*` packages, no `wineuser`, no `/etc/sudoers.d/live` or `/etc/sudoers.d/wineuser`, a swap file, and no `/setup` |
| F6 | Interactive GUI Setup: every page shown, as screenshots. The entered computer name, time zone, locale and Administrator password are applied (`hostnamectl`, `timedatectl`, `localectl`, root login on tty2). A name containing `'` is rejected or stored literally, never executed. |
| F7 | Interactive OOBE: two users entered, the first with a password. Both exist, the first is in `sudo`, GDM shows the greeter (no autologin). Each user logs into a working WineExplorer desktop. |
| F8 | Unattended (`Install (unattended)` entry, `--unattended` build): boot to desktop with no keys. Every value in the answer file is applied (check each). No setup leftovers (`wexsetup` user, polkit rule, flags, 777/666 modes). |
| F9 | `docs/INSTALLER.md` rewritten to describe what exists and how it was tested. A list of what is still missing goes in the same file. |

Report per milestone: done / not done, the evidence path, and anything that was not tested.
