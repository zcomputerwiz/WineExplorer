<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Gemini): WoW64 build scripts, ISO and install test at the current pin

Status: done, 2026-10-15. Assignee: Gemini.

`CLAUDE.md` requires every Wine build (dev, test, ISO) to be a WoW64 build (`configure --enable-archs=i386,x86_64`)
so 32-bit Windows programs run. The docs say so, but some scripts still build 64-bit only, and no ISO has been built
since the rule or since master moved to Wine `f0108dc` (Wayland L1 plus the Vista+ msstyles reader). This work order
brings the scripts, the package and the ISO in line and proves a 32-bit program runs on an installed system.

## Working alongside Codex — boundaries

Codex is on Wayland L2 (`docs/WAYLAND_L2_WORKORDER.md`): `src/dlls/wexhost`, `src/dlls/wexshell/linux_windows.c`,
the Wine fork's `codex/wayland-l2`, the compositor, and the Proton/dev VMs. Do not touch those or the main checkout
`D:\DeepThought\Projects\WineExplorer`.

- **Environment:** WSL `wexbuild` (`wsl -d wexbuild -u root`). Use your checkout `/root/WineExplorer`: switch it to a
  new branch `gemini/wow64-iso` from `origin/master` and run `git submodule update`. Do not use `/root/verify-*`, `/root/up-v4`,
  `/root/uxtheme-dlls` or `/root/shots` (Claude's review trees).
- **Git:** run fetches with `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never` and a `timeout`: a non-interactive fetch
  can otherwise wait forever on a hidden credential prompt (this happened 2026-10-14). Push your branch only; do not
  merge into or push `master`, never force-push, one change per commit.
- Keep work, ISO trees and VM disks under `/root` (`/tmp` is cleared when the distribution stops; never `/mnt/d`).
  Run multi-step commands from script files.
- Rules from `CLAUDE.md`: GPL headers on new files; never pass C escapes through heredocs/`sed`/`python -c`; no
  Microsoft material in the repository.

## Tasks

### 1. Scripts build WoW64
- `scripts/refactor/build-assembled.sh` and `scripts/refactor/build-recursive.sh` still configure with
  `--enable-win64`: change them to `--enable-archs=i386,x86_64`. Search the whole repository (scripts, installer,
  docs, CI files) for any other `--enable-win64`, `build/wine64` or 64-bit-only assumption and fix it.
- `installer/package-wine.sh` defaults to `BUILD_DIR=build/wine64`: default to `build/wow64` and check that the
  package contains both `lib/wine/x86_64-windows` and `lib/wine/i386-windows` (and the Unix side); fail clearly if the
  build is not WoW64. Keep its existing safety checks.
- `docs/BUILDING.md` and `docs/INSTALLER.md`: commands and directory names match.

### 2. ISO at the current pin
In `wexbuild`: `scripts/assemble.sh` → configure (WoW64) in `build/wow64` → `make -j16` →
`installer/package-wine.sh` → `installer/build-iso.sh --clean` (`WORK_DIR`/`OUT_ISO` under `/root`). Record the
master and Wine commits, times and package/ISO sizes.

### 3. Install test and a 32-bit program
- `installer/tests/install-interactive.sh <iso> /root/install-test-password.txt <name>` must pass unmodified (it
  already checks Bob's desktop and the Recycle Bin).
- Add a 32-bit check to the test (or a small helper it calls): after logon, run a 32-bit program on the installed
  system and confirm it draws a window — build a tiny 32-bit test program with `i686-w64-mingw32-gcc` at test time
  (keep its source in `installer/tests/`), and also start Wine's own 32-bit `notepad` from `syswow64`. Use the
  existing `scripts/vmtest.py` tools (`clicktext`, `wait`, `shot`, `sh`). Keep a 64-bit program check next to it.
- Run the unattended test too (`installer/tests/install-unattended.sh`).

### 4. XP time zone list (small, independent)
`src/include/wextimezone.h` (shared by setup and `timedate.cpl`) has fewer entries than XP's list (~75). Complete
it with XP's display names and order, each mapped to IANA through CLDR `windowsZones.xml` (keep the Unicode notice
already in the file). Unattended `TimeZone` keeps accepting both forms. Check both the setup page and
`timedate.cpl` show the full list.

## Acceptance

Report in `docs/WOW64_ISO.md`, with per task what changed, commits, commands, timings and results, and every failed
command with its cause and the fix. Screenshots of the 32-bit program on the installed desktop and of the time
zone list go in `docs/wow64-iso/` (open art only). Then:
- both install tests pass on the new ISO;
- the 32-bit and 64-bit program checks pass on the installed system;
- no `--enable-win64` remains in the repository outside historical notes.

Set this file's status line to done and push `gemini/wow64-iso`. The user or Claude merges.
