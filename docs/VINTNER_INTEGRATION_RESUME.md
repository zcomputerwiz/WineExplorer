<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Vintner Boot Integration: Where to Resume

Date: 2026-11-14 (evening). Branch: `claude/vintner-boot-integration`. Plans: `VINTNER_BOOT.md`,
`SAFE_MODE_WORKORDER.md`; test reference: `SAFE_MODE.md`. Read `TOOLING.md` first.

## 1. State

- The branch equals master and `development` at `2162596c` (2026-11-14), plus this note: wine `1052fab3`, wexcomp
  `1f8e5d3a`, vintner `6f30413`. Everything of this branch is on master: the Run dialog and DDE fixes (via Wine
  `development`), D5 Startup and Recovery, C4's classic logon, D4 Debugging Mode, the test harness
  (`installer/tests/run-chain.sh`, `build-test-iso.sh`, `nbd-connect.sh`) and `scripts/vmtest.py`'s memory settings.
- Nothing is uncommitted here except ` M wine` and ` M vintner`: those working trees are not this work's (the
  `wine` one is the E1 checkout). Never `git add` them; a gitlink change goes through
  `git update-index --cacheinfo 160000,<sha>,PATH`.

## 2. Done and tested

On the ISO `wex-mm` (2026-11-14, `2162596c`: Wine `1052fab3`), BIOS and UEFI, all passing, with
`run-chain.sh -j 2 --iso` (53 minutes, the install included): install, then

| Stage | Phases |
|---|---|
| defaults | safe, safenet, safecmd, nowine, bootlog, startnormal, oschoices, normal, lastgood, vfailed, lastgoodgen, with the probes |
| accounts | `accounts` |
| classic logon dialog (C4) | `classic` (caption 5 66 202, Luna) |
| Run dialog | `rundialog` (eight command lines, the folder without "DDE failure.") |
| Wayland | `waylanddisplay`, `waylandsafe`, `waylandcmd` |
| Startup and Recovery (D5) | `startrec` |

`debugmode` (D4) passed on both firmwares on `wex-vb` (`0738040c`'s files). Earlier passes: `wex-vb` (vintner
`6f30413`), `wex-pn` (wine `5ec3c009`), `wex-mg` (wine `3ea5d0cb83b`).

Flaky, not failing: on UEFI the menu key (F8 held 25 seconds) sometimes misses Vintner Boot's menu; `open_menu`
retries once, and a stage that still misses passes when run again (`run-chain.sh --fws uefi NAME defaults`).
`scripts/test_safemode.py`: 151.

Owner-requested application tests on throwaway overlays (nothing kept, nothing in the repository; findings in
session 0's `docs/KNOWN_BUGS.md`): the Zune theme MSI (B4 per-machine installs, B5 Windows version, B6 `.theme`
association; the theme itself applies and looks right), Copilot (installs; B7 crash at start), Edge 156.0.4314.58
(installs; headless renders; windowed start crashes, B10), Chromium 1736300 (installs per user and runs windowed
with the sandbox and WebGL: the baseline).

## 3. Running or half done

Nothing. No VM and no build is running. (The WSL restart for `.wslconfig`'s `autoMemoryReclaim=dropcache` was done
on 2026-11-14.)

## 4. Next steps, in order

1. When session 0 sends the Wine commit with its IVirtualDesktopManager (B10 lead), retry the windowed Edge start:
   the Edge MSI is `D:\Projects\ChromiumLab\installers\edge-156.0.4314.58\` (SHA-256 `784051EB...F2FB`), on a
   throwaway overlay with wineuser in `wexsys` (B4's workaround, never in an image).
2. When session 0's B6 (`.theme` association, desk.cpl `/Action:OpenTheme`) lands, retry the Zune installer's last
   step (`D:\Projects\apps\ZuneDesktopTheme.msi`; it needs B4's workaround and Windows XP SP2 for msiexec, B5).
3. Pin Vintner Boot `87d2f60` or later (Secure Boot tier B: `vintner-platform` carries `shimx64.efi` and
   `mmx64.efi`, and `vintner-core` refuses a payload without them; build both from one commit) with the full chain.
4. Run the chain for each Wine pin session 0 proposes (next: Wine `51c29e50`, the UWP XAML loader).
5. `SAFE_MODE_WORKORDER.md` has one item left, C4's RID 500 and `wex-is-admin`, which is E1's. Ask the owner what
   comes next.

How merges work (owner, 2026-11-14): I move pins on my own branch as needed; the session 0 session merges into
master (and other branches) and moves the pins there. I send it the commit, the pins and what was tested, and say
what touches `scripts/`, `installer/`, `docs/TOOLING.md` or `docs/STOCK_CHANGES.md`. Session 0 runs on another
machine; the Vintner session shares this one (WSL Debian), so tell it before VM runs. Bugs found along the way go
to session 0, which records them in `docs/KNOWN_BUGS.md` (owner, 2026-11-14).

## 5. Open decisions for the owner

None.

## 6. What is left on the machines

- WSL Debian (VM tests): `/var/tmp/vmtest-gpt/wex-mm-bios` and `-uefi` (the installed disks of `2162596c`, about
  9 GB each; keep as the base). Results: `/var/tmp/wex-chain/wex-mm/`. Probes: `/mnt/d/WSL/wex-iso/probes`.
  `msitools` is installed (reading MSIs).
- `D:\WSL\wex-iso\wex-mm.iso`: the tested ISO. Older ISOs and disks are deleted.
- WSL wexbuild (builds): `/root/WineExplorer` is the ISO clone, its `wine` at `1052fab3` with a current build and
  `build/wow64/.wex-test-iso-stamp`. `/root/WE-safemode`, `/root/wine-stage` and `/root/wine-vbi.bundle` are older
  and may be deleted after a look.
- How to test a commit: `installer/tests/build-test-iso.sh REV NAME` in wexbuild, then
  `installer/tests/run-chain.sh --iso /mnt/d/WSL/wex-iso/NAME.iso NAME` in Debian (`docs/TOOLING.md`, the section
  added 2026-11-14). The old wrappers in `D:\WSL\wex-iso\session-scripts` are kept only for `wexbuild-wine-merge.sh`.
- `%USERPROFILE%\.wslconfig` has `autoMemoryReclaim=dropcache`. Use `wsl --terminate NAME`, never `wsl --shutdown`
  (other sessions, and another Windows account's Claude session, use the same WSL).
