# WineExplorer Installer & Live System

This document describes the Windows XP–style installation system and live environment for Debian 13 (trixie, amd64) with the WineExplorer desktop shell.

---

## 1. Architecture and Stages

The installation media is a bootable hybrid ISO image created using Debian's `live-build` tool. The boot environment and installation payload share the same unified root squashfs (`/live/filesystem.squashfs`).

### The Three Setup Stages
1. **Stage 1: Text-mode Setup (`installer/textsetup/`)**
   - Boots directly on `tty1` via `wex-textsetup.service` when `wexsetup=textmode` is present on the kernel command line.
   - VGA 80×25 console with classic Windows XP blue background, white text, gray bottom status bar, and yellow progress bar.
   - Partitioning with MBR or GPT, formatted with ext4.
   - Copies files from squashfs, writes `fstab`, installs GRUB, and sets `/etc/wineexplorer/setup/stage=gui`.
   - Finishes with a 15-second red reboot countdown and media eject.

2. **Stage 2: GUI-mode Setup (`wine/programs/setup/`)**
   - On the first boot, runs `setup.exe -newsetup` over the billboard (`winntbb.dll`).
   - Runs under a dedicated locked system user `wexsetup` on Xorg vt7 before display manager.
   - Features the 5-step billboard on the left, time remaining estimation, rotating product panels on the right, and centered wizard pages (Regional, Owner, Computer Name, Date/Time, Network).
   - Performs hardware settle, configures default `/etc/skel/.wine` template, writes setup log, and sets `stage=oobe`.

3. **Stage 3: Out-of-Box Experience (OOBE) (`wine/programs/oobe/`)**
   - Displays display resolution adjustment prompt.
   - Native Win32 wizard welcoming the user, setting up updates, testing network connectivity, and creating user accounts.
   - First user is added to `sudo` administrative group.
   - Configures GDM autologin (if single passwordless user) and transitions to the desktop.

---

## 2. Boot Menu Entries

The hybrid ISO includes both BIOS and UEFI GRUB configurations:
1. **Install Debian with WineExplorer (Default)**: boots with `wexsetup=textmode` into Stage 1.
2. **Start WineExplorer from CD**: boots live session into GDM autologin and launches the WineExplorer shell directly for testing.
3. **Install (unattended)**: boots with `wexsetup=textmode wexunattend=/setup/unattend.sif`.
4. **Rescue shell & Advanced options**.

---

## 3. How to Build the ISO

### Prerequisites on Debian Build Host
```bash
sudo apt-get install -y live-build squashfs-tools xorriso isolinux syslinux-common \
    mtools dosfstools grub-pc-bin grub-efi-amd64-bin
```

### Building the Packages & ISO
Run as root or via sudo:
```bash
# Default build (open artwork fallbacks)
sudo ./installer/build-iso.sh

# With genuine XP resources (extracted from user XP VM)
sudo ./installer/build-iso.sh --genuine

# With unattended install default
sudo ./installer/build-iso.sh --unattended

# Clean previous build artifacts before building
sudo ./installer/build-iso.sh --clean
```

The output ISO is written to `installer/wineexplorer.iso`.

---

## 4. How to Test

Automated testing is conducted in an isolated VirtualBox VM (`WineExplorer-Install-Test`):

```bash
# Create BIOS VM and attach ISO
python installer/test-vm.py create bios installer/wineexplorer.iso

# Start VM headless or with GUI
python installer/test-vm.py start --gui

# Capture screen
python installer/test-vm.py shot build/test_screen.png

# Send keys (e.g. Enter, F8, C, D)
python installer/test-vm.py key enter
python installer/test-vm.py key f8

# Test UEFI mode
python installer/test-vm.py create efi installer/wineexplorer.iso
python installer/test-vm.py start --gui

# Clean up test VM
python installer/test-vm.py destroy
```

---

## 5. File Structure

```
installer/
  build-iso.sh                 # Main build orchestration script
  package-wine.sh              # Debian packaging script for wineexplorer & session
  test-vm.py                   # VirtualBox test VM automation helper
  live-build/config/           # live-build configuration tree
    package-lists/             # Package lists for target system
    includes.chroot/           # Root filesystem overlay
    hooks/live/                # Binary ISO hooks (GRUB menu, mklayout)
  textsetup/                   # C source for Stage 1 text-mode Setup
  setupinfs/                   # txtsetup.sif, sysoc.inf, eula.txt, unattend.sif
  tools/                       # mklayout.py (generates layout.inf from squashfs)
  systemd/                     # wex-*.service units and polkit rule
wine/programs/setup/           # Stage 2 GUI Setup source
wine/programs/oobe/            # Stage 3 OOBE source
```
