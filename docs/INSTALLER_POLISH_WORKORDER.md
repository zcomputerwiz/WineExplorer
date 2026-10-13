# Work order (Gemini): installer polish — setup screens, time zones, Automatic Updates, boot

Status: done, 2026-10-13. Taken from Codex's "Next work" items 1 and 3 (`docs/CODEX_HANDOFF.md`); Codex is told
these are Gemini's. Background: `docs/INSTALLER.md`, `docs/BUILDING.md`, `installer/tests/install-interactive.sh`.

## Working alongside Codex — boundaries

Codex works in the main checkout `D:\DeepThought\Projects\WineExplorer` (it has **uncommitted changes there**) and on
the Proton and dev VMs. Do not touch any of those.

- **Your environment:** WSL2 distribution **`wexbuild`** (Debian 13.5): Wine build dependencies (`apt-get build-dep
  wine`), MinGW, autoconf, textsetup libraries, live-build/debootstrap/xorriso/GRUB, QEMU/KVM (`/dev/kvm`), OVMF,
  tesseract, imagemagick, parted, Xvfb, xdotool; git identity and the Windows credential manager configured. Enter
  with `wsl -d wexbuild -u root`. A verified checkout exists at `/root/WineExplorer` (cloned with
  `--recurse-submodules`, built and packaged as described under *Verification of the environment* below). Run
  multi-step commands from a script file (PowerShell quoting breaks `$`, `|`, quotes).
- **Your branch:** `gemini/installer-polish` in `/root/WineExplorer`, from `origin/master`. Push it to
  `origin` (`zcomputerwiz/WineExplorer`); **do not merge into or push `master`** — the user/Claude merges when
  Codex is not mid-change. Never force-push; one change per commit; GPL headers on new files.
- **Out of scope (Codex's):** logonui, the greeter, LightDM, W4–W7, wexhost, wexshell/explorer, the Wine fork
  (`wine/` submodule). If a task seems to need one of these, stop and write it into your report instead.
- **Microsoft material:** XP VM screenshots or XP files may be looked at for reference but never committed (see
  `CLAUDE.md`, `docs/ASSETS.md`). Use XP's *wording and layout*, not its bitmaps.
- Rules from CLAUDE.md: never pass C escapes (`\\`, `\n`) through heredocs/`sed`/`python -c` (write files with an
  editor tool or a script file, grep the touched lines after); commit after each working step.

## Tasks

### 1. GUI setup (`src/programs/setup`) visual parity
- Pages become **Wizard97 pages with headers** like XP's GUI setup: header title + subtitle per page (Regional and
  Language Options, Personalize Your Software, Computer Name and Administrator Password, Date and Time Settings,
  Networking Settings, Workgroup or Computer Domain), `PSH_WIZARD97 | PSH_HEADER`, `PSP_USEHEADERTITLE |
  PSP_USEHEADERSUBTITLE`; header bitmap from open art (`scripts/open_assets.py`, ReactOS) or none.
- During setup the dialog uses the **classic look** (no visual style) like XP setup; check the frame and the
  `X` button match the classic style instead of the light theme seen in `docs` screenshots.
- The billboard's step list (left pane) must advance: "Collecting information" → "Dynamic update" → "Preparing
  installation" → "Installing Debian & Wine" → "Finalizing installation", matching what setup is doing (today the
  arrow stays on "Dynamic update" during the pages).
- XP has an empty name field (Next is disabled until a name is entered); today it is prefilled "Wine User".

### 2. Date and Time page: XP time zones
- Reuse **`src/dlls/timedate.cpl/timezone.c`**'s XP list (`(GMT-05:00) Eastern Time (US & Canada)` …) instead of raw
  IANA names: move the table and its Windows→IANA map into a small shared header/source both use (no copy-paste).
- Complete the Windows→IANA map for every entry using **CLDR `windowsZones.xml`** (Unicode License v3 — compatible;
  record the CLDR version and keep its notice). Unattended `TimeZone` accepts either form.
- The page shows date and time pickers and the "Automatically adjust clock for daylight saving changes" box as XP
  does; the checkbox maps to the zone choice (Linux handles DST by zone) — gray it out if it has no effect.

### 3. Automatic Updates applied (`src/programs/oobe`, `installer/scripts/wex-oobe.sh`)
- OOBE records the choice but nothing applies it. OOBE runs unprivileged: have it write the choice to its stage
  directory (`/etc/wineexplorer/setup/automatic-updates`: `on`/`off`), and `wex-oobe.sh` (root) applies it:
  `unattended-upgrades` installed via the live-build package list; `on` → `/etc/apt/apt.conf.d/20auto-upgrades` with
  `APT::Periodic::Update-Package-Lists "1"; APT::Periodic::Unattended-Upgrade "1";`, `off` → both `"0"`.
- Unattended: `[Unattended]`/`[Data]` key for automatic updates (XP's answer file used `AutomaticUpdates=Yes/No` in
  `[Data]`); document it in `unattend.sif.example`.

### 4. Text setup (`installer/textsetup`) wording
- Format screen and partition list use XP wording, not device paths: "Setup will install Debian GNU/Linux with
  WineExplorer on partition C: Partition1 [New (Raw)] 15000 MB ( … MB free) on 20480 MB Disk 0 at Id 0 on bus 0 on
  atapi [MBR]." — adapt the bus/Id part to what Linux knows (virtio/scsi/nvme, disk index); keep device names only
  in logs.
- The restart screen's countdown bar: XP shows a red/brown bar counting down without a percentage.

### 5. Installed system boot
- Installed GRUB: no Debian theme, no visible menu when only one OS is installed (XP boots straight through):
  `GRUB_TIMEOUT_STYLE=hidden`, `GRUB_TIMEOUT=0` (Shift/Esc still shows the menu), no `desktop-base` GRUB theme,
  and fix the missing `unicode.pf2` warning Codex logged (either `GRUB_TERMINAL=console` or install the font).
- The live CD menu stays as it is (it already has "Continue Setup from the hard disk").

### 6. Docs and the Recycle Bin check
- Fresh install must show the Recycle Bin icon on the first user's desktop (fixed upstream in our fork,
  `a417c3be`); the install test already waits for it.
- Update `docs/INSTALLER.md` (what changed, new answer-file key, how to run the test in `wexbuild`).

## Acceptance

In `wexbuild`: build (assemble → configure → make), package, `build-iso.sh --clean`, then
`installer/tests/install-interactive.sh` passes unmodified except for OCR anchors that legitimately changed with the
new wording (say which in the report). Add screenshots of each changed screen to `docs/installer-polish/` (open art
only) and write `docs/INSTALLER_POLISH.md`: per task what changed, commits, test results, anything left. Then set this
file's status line to done. Also run an unattended install with `AutomaticUpdates=No` and check
`/etc/apt/apt.conf.d/20auto-upgrades` on the installed disk (`qemu-nbd` while the VM is off).

## Verification of the environment (Claude, 2026-10-13)

In `wexbuild`, from a fresh `git clone --recurse-submodules` of `master` at `c73773f9` (Wine fork `d0a0af06819`):
`scripts/assemble.sh` → `wine/configure --enable-win64` → `make -j16` (13 min) → `installer/package-wine.sh` →
`installer/build-iso.sh --clean` (`WORK_DIR=/root/iso-work/work`, `OUT_ISO=/root/iso-work/wineexplorer.iso`,
1.4 GB) — all succeeded, ~30 min end to end; logs in `/root/verify-logs/`. Then
`installer/tests/install-interactive.sh /root/iso-work/wineexplorer.iso /root/install-test-password.txt
wexbuild-check` **passed in 6.5 min** (LightDM and greeters installed, accounts correct, Bob's desktop 6 s after
logon); screenshots in `/tmp/vmtest/wexbuild-check/steps/`. The test password is in
`/root/install-test-password.txt` (outside Git; never commit it).

The first configure warned "x86_64 PE compiler supporting C++17 not found"; `g++-mingw-w64-x86-64/-i686` were then
installed and configure rerun without the warning — the existing `build/wine64` is configured but **not yet rebuilt**
with the C++ modules: run `make -j16` once before your first change. The installed system already uses LightDM and
our greeters (Codex's work) — leave them alone as stated above.

`/tmp` is cleared when the WSL distribution stops; keep work under `/root`. Put ISO work and VM disks under `/root`
too (plenty of space), never on `/mnt/d`.
