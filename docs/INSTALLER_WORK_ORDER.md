# Work order: Windows XP Setup–style installer ISO for Debian + WineExplorer

**For:** Gemini (implementation agent) · **Written:** 2026-10-10 · **Status:** ready to start

Build a bootable ISO, like other Debian-based distributions, that installs Debian 13 (trixie, amd64) together with
the WineExplorer shell. To the user it should look and behave like Windows XP Setup:

1. The blue **text-mode Setup**.
2. The billboard-and-wizard **GUI-mode Setup** after the first reboot.
3. The **Welcome (OOBE)** screens on the next boot.
4. The WineExplorer desktop.

A Live CD is not required. A "Start from CD" boot entry is wanted anyway, because it makes testing much easier.

---

## 1. Ground rules (from `CLAUDE.md`; read it and `docs/HANDOFF.md` first)

- **XP parity.** Match the XP VM pixel for pixel and step for step, using XP's names and wording.
- **Gray out what does not apply on Linux**, rather than hiding it. Where XP had no equivalent step, leave it out.
  how Setup works, but never copy its code, data files or INF text.
- **ReactOS code and art may be ported.** Keep their licence headers (GPL-2.0+ or LGPL).
- **Genuine Microsoft art** (bitmaps, icons, OOBE images, sounds) never goes into the repository or the default ISO.
  - The installer needs an open fallback for every genuine asset: ReactOS art, or our own.
- **Prefer the D-Bus services** over spawning command-line tools: systemd `locale1`, `timedate1` and `hostname1`,
  AccountsService, NetworkManager and polkit. Reuse `wine/dlls/wexhost`, which already wraps several of them.
- **Keep changes to stock Wine small**, and list every one in `docs/STOCK_CHANGES.md`.
- **Commit after each working step.** Never pass C escapes through Bash heredocs, `sed` or `python -c`: write the file
  with an editor or a script file instead (see `CLAUDE.md`, "Editing").

## 2. How Windows XP Setup works (research summary)

### 2.1 Media and the file lists

XP Setup is driven entirely by INF-style file lists in `\I386` on the CD. Files are stored compressed, with `_` as
the last letter of the extension (for example `NTOSKRNL.EX_`).

| File | Role |
|---|---|
| `LAYOUT.INF` | The master list of every file on the CD. `[SourceDisksNames]` names the media. `[WinntDirectories]` numbers the target directories (1 = `\WINDOWS`, 2 = `system32`, 3 = `system32\config` …). Each `[SourceDisksFiles]` line is `name = disk,subdir,size,checksum,,,bootmedia,targetdir-id,upgrade-disposition,textmode-disposition,target-name`. |
| `TXTSETUP.SIF` | The same file table, plus what text-mode Setup needs: `[SetupData]` (product, default path), `[DiskSpaceRequirements]`, the drivers to load (`[BusExtenders.Load]`, `[SCSI.Load]`, `[Keyboard.Load]` …), the hardware-ID database, and the registry hives to build. |
| `DOSNET.INF` | The copy list `winnt32.exe` uses when Setup is started from inside an existing Windows. |
| `SYSSETUP.INF` | GUI-mode Setup: which INFs to run, registry to write, components to register. |
| `SYSOC.INF` | Optional Components (the Windows Components Wizard), handled by the OC Manager. |
| `HIVE*.INF` | The default registry hives, which text-mode Setup builds. |

- Build tools merge the fragments and filter lines by product. Prefixes such as `@w:` and `@s:` mark lines for
  workstation-only or server-only builds.

**Take the idea, not the text.** Our build generates one file list from the target filesystem, and that list drives
the copy phase and its progress bar.

### 2.2 The three stages the user sees

1. **Text-mode Setup.** The CD boots setupldr → `setupdd.sys`, on a blue 80×25 VGA text screen.
   - Header "Windows XP Professional Setup"; key help in a gray status bar at the bottom.
   - Sequence:
     - "Setup is loading files…" (driver names flash in the status bar).
     - Welcome to Setup (ENTER = continue, R = repair, F3 = quit).
     - Licensing Agreement (F8 = I agree, PAGE DOWN).
     - Partition list: C = create, D = delete, ENTER = install. Unpartitioned space is shown, sizes in MB.
     - Format options (NTFS/FAT, quick or full) with a yellow progress bar.
     - "Setup is examining your disks".
     - "Please wait while Setup copies files…": yellow progress bar, current file name in the status bar.
     - "Setup is initializing your Windows XP configuration".
     - A reboot with a 15-second red countdown bar.
2. **GUI-mode Setup.** The installed system boots, and Winlogon runs `setup.exe -newsetup` (`syssetup.dll`) on top of
   the billboard (`winntbb.dll`).
   - **Billboard, left side:**
     - Five steps, the current one highlighted with an arrow bullet: Collecting information, Dynamic Update,
       Preparing installation, Installing Windows, Finalizing installation.
     - "Setup will complete in approximately: N minutes", with a progress bar and a line of status text.
   - **Billboard, right side:** rotating marketing panels.
   - **Wizard pages, in order:**
     - Regional and Language Options
     - Personalize Your Software (name, organization)
     - Your Product Key
     - Computer Name and Administrator Password
     - Date and Time Settings
     - Networking Settings (Typical / Custom)
     - Workgroup or Computer Domain
   - **Then automatic stages** shown on the billboard: Installing Devices, Installing Network, Copying files,
     Completing installation (Installing Start menu items, Registering components, Saving settings, Removing any
     temporary files used). Then a reboot.
3. **First logon.**
   - The "Display Settings" auto-adjust prompt.
   - Then **OOBE** (`msoobe.exe`): HTML pages in `%windir%\system32\oobe`, with title music and an animated
     background. The pages are:
     - Welcome to Microsoft Windows
     - Help protect your PC (Automatic Updates)
     - Checking your Internet connectivity
     - Ready to activate Windows?
     - Ready to register?
     - Who will use this computer? (up to five accounts)
     - Thank you!
   - Then "Welcome", and the desktop.

### 2.3 Open-source clones you may port: ReactOS (`references/reactos`)

| ReactOS | What it is |
|---|---|
| `base/setup/usetup` | Text-mode Setup: console UI (`consup.c`, `genlist.c`, `progress.c`), partition list UI (`partlist.c`), format checks, keyboard maps. |
| `base/setup/lib` (setuplib) | `txtsetup.sif` parsing, file queue and copy, registry setup, boot code. |
| `base/setup/reactos` | A GUI first stage (wizard) with art in `res/`. |
| `dll/win32/syssetup` | GUI-mode Setup wizard (`wizard.c`, `install.c`) with its art. |
| `boot/bootdata/txtsetup.sif`, `boot/bootdata/packages/reactos.dff.in` | Their file list, and the cabinet list generated from it. |

**Porting rules:**
- Port the UI flow, layout and screen strings: the XP wording is public knowledge, and ReactOS reproduces it under
  the GPL.
- Replace the NT-specific code (disk I/O, registry hives, boot sectors) with Linux equivalents.

## 3. Media tooling: decision

| Tool | Verdict |
|---|---|
| **live-build** (Debian's official build system for live images; `config/` tree with package lists, hooks and `includes.chroot`; outputs a hybrid ISO through xorriso) | **Use it.** Scriptable, reproducible, runs headless on the Debian VM, and supports both BIOS and UEFI boot. The debian-user post (2025-09, KoruX) uses it with Calamares, and the `nodiscc/debian-live-config` template is a good starting point for the `config/` layout. We replace Calamares with our own Setup. |
| **Cubic** (Custom Ubuntu ISO Creator) | Not suitable. It is an interactive GTK wizard that remasters an existing ISO, with no headless mode documented. Fine for experiments, unusable for a repeatable build. |
| **udib** | Not suitable as the base. It injects a `preseed.cfg` into a debian-installer netinst ISO's initrd and repacks with xorriso. That keeps debian-installer's user interface, which is not what we want. It is useful only as a reference for the xorriso repack flags. Our own unattended mode (§4.6) replaces preseeding. |

An earlier preseed exists in `D:\VirtualBox_VMs\debian_preseed.cfg`; it is how the current VM was installed. Use it as
the reference for partitioning defaults and the package selection.

## 4. Architecture

### 4.1 ISO layout (built by live-build)

```
/boot/grub/, /isolinux/          BIOS + UEFI boot (hybrid ISO, bootable from USB via dd)
/live/vmlinuz, initrd.img
/live/filesystem.squashfs        THE TARGET SYSTEM: Debian + WineExplorer, complete and configured
/setup/layout.inf                file list generated from filesystem.squashfs (§4.3)
/setup/txtsetup.sif              text-mode data: product, defaults, disk-space needs, components
/setup/eula.txt                  licence text (Debian/GPL/Wine LGPL summary; ours, not Microsoft's)
/setup/art/                      open art; genuine packs are added only by `--genuine` builds (§4.7)
/pool/                           .debs for the optional components (Windows Components analog)
```

The boot environment and the payload are **the same squashfs**: Setup runs from the live root and copies that root
to disk, the way live installers do. This avoids building and shipping the system twice, and it makes the live entry
free.

**Boot menu** (GRUB, themed as plain text in the spirit of XP's "Press any key to boot from CD…"):
1. *Install Debian with WineExplorer*: the default. Kernel argument `wexsetup=textmode`.
2. *Start WineExplorer from CD*: a live session for testing; GDM autologin into the WineExplorer session.
3. *Install (unattended)*: `wexsetup=textmode wexunattend=/setup/unattend.sif`.
4. Rescue shell, and memory test if available.

### 4.2 Stage 1: text-mode Setup (`installer/textsetup/`, C, new code)

- **Startup:** a systemd unit in the live system starts it on `tty1` when `wexsetup=textmode` is on the command line.
  Nothing graphical runs.
- **Screen:** draw it with **ncursesw** on the Linux virtual terminal.
  - Use a VGA-like console font (`Lat2-VGA16` / `default8x16` through `setfont`), 80×25 blue background, gray
    status bar, yellow progress bar.
  - Port ReactOS usetup's screen layout and strings. Keep the screen flow identical to XP's (§2.2, stage 1), with
    these substitutions:
    - Partitioning uses **libfdisk / libblkid** (util-linux), with a GPT label on UEFI and MBR on BIOS.
    - The partition list shows XP's columns: disk and partition sizes in MB, file system, label, "Unpartitioned
      space".
    - "Format the partition using the NTFS file system" becomes **ext4**: quick (`mkfs.ext4`) or full (adds
      `-c`). FAT/NTFS options are grayed or left out.
    - In UEFI mode, create the EFI System Partition automatically (FAT32, 512 MB), as Windows Vista and later do,
      and say so in one status line.
    - Swap: a swap file on the root partition, sized like XP's pagefile (1.5 × RAM, capped). No dialog.
- **Copying files:** read `/setup/layout.inf` and copy each file from the mounted squashfs to the target. The
  progress bar runs by bytes; the status bar shows "Copying: <name>". Preserve owners, modes, xattrs, symlinks,
  hardlinks and device nodes.
  - Do it in-process with `copy_file_range` / `sendfile`, not by spawning cp/rsync.
  - Check each file's size against the list, and report errors with XP's retry/skip/quit screen.
- **"Initializing your configuration":**
  - Write `fstab` (UUIDs) and a fresh `machine-id`.
  - Install GRUB into the target: `grub-install` plus `update-grub` in a chroot, the one place where running tools
    is acceptable.
  - Regenerate the initramfs.
  - Set the setup-pending flag `/etc/wineexplorer/setup/stage=gui`.
  - Copy `unattend.sif` if one was given.
- **Finish:** the 15-second reboot countdown, then eject the media (`eject`) and reboot.
- **Repair (R) and Recovery Console:** gray these out in v1.

### 4.3 The file list (`installer/tools/mklayout.py`)

- **When:** it runs at ISO build time (a live-build binary hook) over the finished chroot.
- **What it writes:** `/setup/layout.inf` in XP's *structure*, with our own content:
  - `[SourceDisksNames]`: one medium, the squashfs.
  - `[WinntDirectories]`: numbered Linux directories (1 = `/`, 2 = `/usr`, 3 = `/usr/local/lib/wine`, …).
  - `[SourceDisksFiles]`: `relative/path = 1,,size,,,,,dirid,,disposition,`.
  - A separate `[Links]` section for symlinks, and one for special files.
- **`txtsetup.sif`:**
  - Carries `[SetupData]` (product name "Debian GNU/Linux with WineExplorer", default target).
  - Carries `[DiskSpaceRequirements]`, the computed total plus headroom. Text-mode Setup refuses a partition that is
    too small, with XP's message.
- **Optional components:** `installer/setupinfs/sysoc.inf` lists the optional components, each an apt package set in
  `/pool`, for example LibreOffice, Firefox ESR, Steam/Proton, Samba. They are installed in GUI-mode Setup and
  later from Add/Remove Windows Components.

### 4.4 Stage 2: GUI-mode Setup (`wine/programs/setup`, Wine program `setup.exe`)

- **Startup:**
  - On the first boot of the installed system, `wex-setup.service` runs when `/etc/wineexplorer/setup/stage=gui`
    exists, ordered `Before=display-manager.service`.
  - The service starts Xorg on vt7 and a dedicated, locked system user `wexsetup` with its own Wine prefix.
  - That user runs `wine explorer /desktop=Setup,<native size> setup.exe -newsetup`, without the shell.
  - Do not run Wine as root.
- **Privileged work** goes over D-Bus through `wexhost`. Extend `wexhost` where needed, and document new calls in
  `docs/DBUS_PLAN.md`.
  - **Authorization:** `/etc/polkit-1/rules.d/49-wexsetup.rules` grants the `wexsetup` user exactly these actions.
    It is installed by stage 1 and deleted at the end of stage 3.
  - **Locale and keyboard:** `org.freedesktop.locale1` `SetLocale`, `SetX11Keyboard` and `SetVConsoleKeyboard`.
  - **Time:** `timedate1` `SetTimezone`, `SetNTP`, `SetTime` (`WexTimedate*` exists).
  - **Computer name:** `hostname1` `SetStaticHostname` / `SetPrettyHostname` (`WexHostname*` exists).
  - **Administrator password:** sets **root's** password through AccountsService, or a small root helper on the
    system bus if AccountsService cannot set root.
  - **Network:** NetworkManager; Typical = DHCP on all wired interfaces (`WexNet*` exists).
  - **Components:** installed by a root helper using **PackageKit** (`org.freedesktop.PackageKit`), not apt on a
    command line.
- **The window:** the XP billboard, cloned.
  - Left column: the five steps and the time estimate; right side: rotating panels.
  - Panel text is ours, describing WineExplorer and Debian features, in XP's voice.
  - The estimate comes from measured durations of the remaining tasks, like XP's `winntbb` estimate.
  - Wizard pages, centred over the billboard, in XP order:
    - **Regional and Language Options:** Customize… / Details… open our `intl.cpl` pages if they exist, else gray.
    - **Personalize Your Software:** writes RegisteredOwner and RegisteredOrganization into the default prefix
      template (`/etc/skel/.wine`), and into `/etc/wineexplorer/owner`.
    - **Your Product Key:** leave the page out. No key, and nothing to grayed-out.
    - **Computer Name and Administrator Password:** a NetBIOS-style name check plus the hostname rules.
    - **Date and Time Settings.**
    - **Networking Settings:** Typical / Custom. Custom opens our network properties (`ncpa.cpl`) if available.
    - **Workgroup or Computer Domain:** writes the workgroup to Samba's `smb.conf` if Samba is selected, else stores
      it in `/etc/wineexplorer/workgroup`. Domain join: gray.
- **Then the automatic stages, each with real work:**
  - *Installing Devices*: `udevadm settle`; list the devices that have no driver (wexhost `WexHardwareList`, the same
    report Device Manager uses) for the setup log.
  - *Installing Network*.
  - *Copying files*: the optional components.
  - *Completing installation*:
    - Build the default Wine prefix template in `/etc/skel/.wine` (`wineboot`, the theme, the XP resource packs if
      present).
    - Register file associations.
    - Write the setup log `/var/log/wineexplorer-setup.log` (XP's `setupact.log` equivalent).
    - Remove temporary files.
  - Finally set `stage=oobe` and reboot.
- **Build and stock Wine:** a new program needs `configure.ac` + `autoconf` + `config.status` on the VM; copy
  `configure` back to the repo. XP's GUI-mode Setup was `%windir%\system32\setup.exe`; Wine has no `setup.exe`, so the
  new program adds no stock-Wine change. Do not register it in `App Paths`.

### 4.5 Stage 3: OOBE (`wine/programs/oobe`, `oobe.exe`)

- **Startup:** `wex-oobe.service` runs at `stage=oobe`, like stage 2: full screen, no shell.
  - First the XP "Display Settings" prompt: "Windows will automatically adjust your screen resolution…", using
    XRandR through `WexDisplaySet`.
- **Pages:** XP's sequence, rendered natively (Win32/GDI+), not HTML. Use the same layout, title bar band, Back/Next
  buttons, animated background and title music.
  - **Welcome.**
  - **Help protect your PC:** turns `unattended-upgrades` on or off.
  - **Checking your Internet connectivity.**
  - **Activation / registration:** leave both pages out.
  - **Who will use this computer?:** creates up to five users through AccountsService. The first becomes an
    administrator (`sudo` group), as XP made OOBE users Computer Administrators.
  - **Thank you!**
- **Finish:**
  - Write the GDM configuration: autologin only if exactly one user without a password, as XP showed the Welcome
    screen otherwise.
  - Set the default session, reusing `scripts/setup-default-session.sh` logic.
  - Create each user's prefix from `/etc/skel/.wine`.
  - Remove the polkit rule, the setup user and the flags, then start the display manager.
- **First logon:** the existing WineExplorer session (`scripts/wine-explorer-session.sh`) plays XP's "Welcome"
  logon.

### 4.6 Unattended mode (needed for automated tests)

- **File:** `unattend.sif` in `winnt.sif` syntax (XP's answer-file format; the section and key names are public
  documentation).
  - `[Unattended]`: `UnattendMode=FullUnattended`, `TargetDisk`, `Repartition=Yes`.
  - `[GuiUnattended]`: `TimeZone`, `AdminPassword`, `OEMSkipRegional`.
  - `[UserData]`: `FullName`, `OrgName`, `ComputerName`.
  - `[Networking]`, `[Components]`.
  - A WineExplorer extension `[WexOobe]` that lists users.
- **Behavior:** every stage reads it and skips its pages, while still showing progress screens, as XP did.

### 4.7 Art and genuine resources

  it again with `VBoxManage sharedfolder add … --transient` after a reboot) to fetch:
  - `winntbb.dll` (billboard)
  - `syssetup.dll` (wizard header and watermark)
  - `msoobe.exe` and `%windir%\system32\oobe\` (images, `title.wma`, `*.htm` for reference only)
  - `setupldr.bin` text strings (for reference only)
  - `XP_FILES` for the DLLs.
  - A file-copy path for the OOBE folder.
- **Packaging.** `installer/build-iso.sh --genuine` adds the packs to `/setup/art/xp/` *only for the user's own local
  build*. The default ISO uses the open art:
  - ReactOS `base/setup/reactos/res`, `dll/win32/syssetup/resources` and `base/setup/usetup` art.
  - Our own redraws where ReactOS has nothing.
- **Fonts.** Tahoma for the GUI stages comes from `scripts/fonts.py` (genuine, local builds only). The open fallback
  is the one the shell already uses.

### 4.8 Packaging WineExplorer for the ISO

- **The Wine build.** `installer/package-wine.sh` builds the WineExplorer Wine (`wine/`, as on the VM) and makes a
  `.deb` with `make install DESTDIR=… && dpkg-deb`.
  - Package `wineexplorer`, installed under `/usr/local` exactly as the VM is, so the session script needs no
    change.
  - Depends on the runtime libraries in today's VM `apt-mark showmanual` list. Runtime only: drop the `-dev`
    packages, gcc and mingw.
- **The session package.** `wineexplorer-session` carries `scripts/wine-explorer-session.sh`,
  `wine-explorer.desktop`, `20-wineexplorer-modesetting.conf`, the GDM defaults and the setup units.
- **Inclusion.** live-build pulls both from `config/packages.chroot/`.

## 5. Repository layout to create

```
installer/
  build-iso.sh                 entry point; runs on the Debian VM as root (via scripts/vm.sh ssh); --genuine, --unattended
  package-wine.sh
  live-build/config/           package-lists/, hooks/live/, hooks/normal/, includes.chroot/, bootloaders/
  textsetup/                   C sources + Makefile (ncursesw, libfdisk, libblkid)
  setupinfs/                   txtsetup.sif.in, sysoc.inf, unattend.sif.example, eula.txt
  tools/mklayout.py
  systemd/                     wex-textsetup.service, wex-setup.service, wex-oobe.service, polkit rule
wine/programs/setup/           GUI-mode Setup + billboard
wine/programs/oobe/            OOBE
docs/INSTALLER.md              how it works, how to build, how to test
```

## 6. Milestones and acceptance tests

Test everything in a **new** VirtualBox VM, `WineExplorer-Install-Test`, created by a script with `VBoxManage`:
- Run it twice, with BIOS firmware and with EFI.
- Use a 20 GB disk and 4 GB of RAM.
- Drive it with `keyboardputscancode` / `keyboardputstring`, the way `scripts/xp_run.py` drives the XP VM.
- Take screenshots with `VBoxManage controlvm … screenshotpng`.

Do not touch `WineExplorer-VM`: it is the development machine. Build on it, test elsewhere.

| # | Milestone | Done when |
|---|---|---|
| M1 | live-build skeleton + packages | `build-iso.sh` produces a hybrid ISO; the *Start from CD* entry boots into the WineExplorer session in BIOS and EFI VMs |
| M2 | File list | `mklayout.py` output covers every file in the squashfs (count and total bytes match `unsquashfs -lls`) |
| M3 | Text-mode Setup | Every XP text-mode screen exists; partition create/delete works; installs to an empty disk; the system boots to the setup-pending stage. Screenshots side by side with XP references |
| M4 | GUI-mode Setup | Billboard + all wizard pages; settings really applied (check `hostnamectl`, `timedatectl`, `localectl`, root password); components install from `/pool` offline |
| M5 | OOBE | Users created; autologin rule as specified; first logon shows the WineExplorer desktop with a working prefix |
| M6 | Unattended | `Install (unattended)` goes from boot to desktop with no keystrokes; the automated test passes in BIOS and EFI |
| M7 | Docs + polish | `docs/INSTALLER.md`; `STOCK_CHANGES.md` updated if stock Wine was touched; ISO size and install time recorded |

**Reference screenshots.** The XP VM is already installed, so its Setup cannot be captured from it.
- Ask the user for an XP installation ISO.
- With it, record every Setup screen in a throwaway VM and save the PNGs to `references/screenshots/xp-setup/`, as
  the other references are.
- Until then, use ReactOS Setup's screens for layout and XP's wording from §2.2.

## 7. Open questions for the user (ask before M3)

1. Is an XP installation ISO available, to capture reference screenshots of every Setup stage?
2. Disk layout: one ext4 root partition (XP-like simplicity), or a separate `/home`? Btrfs snapshots?
3. "Administrator password": set the root password (closest to XP), or keep root locked and make the first OOBE user
   a sudoer only (the Debian default when no root password is given)?
4. Which optional components ship in `/pool`, and how big may the ISO grow (one DVD, 4.7 GB)?
5. Product name and branding on the screens: "Debian GNU/Linux with WineExplorer", or another name?
