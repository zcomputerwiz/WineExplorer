<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Vintner Boot Integration: Where to Resume

Date: 2026-11-14. Branch: `claude/vintner-boot-integration`. Plans: `VINTNER_BOOT.md`, `SAFE_MODE_WORKORDER.md`;
test reference: `SAFE_MODE.md`. Read `TOOLING.md` first.

## 1. State

- The branch is master `3ab37342` merged in, with wine pinned at `development` `5ec3c009` (2026-11-14), handed to
  session 0 to merge to master. Master itself pins wine `3ea5d0cb83b` until session 0 merges.
- Gitlinks: wine `5ec3c009`, wexcomp `1f8e5d3a`, vintner `d3fe892`.
- Wine: `5ec3c009` = UWP U1.5 (`9484da1a`) + `0e5f4da1` (LookupAccountSid and the caller's mapped SID) + the fork's
  `claude/run-dialog` (`3ea5d0cb83b`: the shell32 commits `042fc59aab4`, `68ab2d5dab3` for the Run dialog and
  `1d6c6a65acf` for a folder's "DDE failure"). Development has since moved to `fe9776eb` (UWP headers and
  resources); session 0 moves to it after.
- Nothing is uncommitted here except ` M wine`: the `wine` working tree is the E1 checkout and is not this work's.
  Never `git add wine`; a gitlink change goes through `git update-index --cacheinfo 160000,<sha>,wine`.

## 2. Done and tested

On the ISO `wex-pn` (2026-11-14, master `120e3896` with wine `5ec3c009`; the branch adds only its own docs and test
script to that), BIOS and UEFI, all passing; the same chain passed on `wex-mg` (wine `3ea5d0cb83b`) on 2026-11-13:

| Stage | Phases |
|---|---|
| install | zero-input install and installed-disk checks |
| defaults | safe, safenet, safecmd, nowine, bootlog, startnormal, oschoices, normal, lastgood, vfailed, lastgoodgen, with the probes |
| accounts | `accounts` |
| classic logon dialog (C4) | `classic` (caption 5 66 202, Luna) |
| Run dialog | `rundialog` (eight command lines, the folder without "DDE failure.") |
| Wayland | `waylanddisplay`, `waylandsafe`, `waylandcmd` |
| Startup and Recovery (D5) | `startrec` |

Flaky, not failing: on UEFI the menu key (F8 held 25 seconds) sometimes misses Vintner Boot's menu. `open_menu`
retries once; on `wex-pn` `oschoices` missed both tries and passed when run again. Five single misses in earlier
logs. `startrec` once lost a key of the password (UEFI); it now retypes once. `scripts/test_safemode.py`: 149.

## 3. Running or half done

Nothing. No VM and no build is running.

## 4. Next steps, in order

1. Wait for session 0 to merge the branch to master (it waits for the UWP session's agreement).
2. Pin Vintner Boot `6f30413` or later on the branch, with the install and defaults stages, and hand it over the
   same way. Since `d3fe892`: docs, a CI script fix, and `vintner-platform` Recommends `grub-common` (Vintner #96;
   an apt install with recommends also brings `os-prober`). The vintner binary is unchanged. Setup installs the
   package with `dpkg -i` (`installer/textsetup/install-vintner.sh`), which ignores Recommends, and the image already
   has `grub-common`, so the install should not change; the install stage's `check-vintner.py` confirms it.
3. Continue `SAFE_MODE_WORKORDER.md` (the open items after C4 and D5).

How merges work (owner, 2026-11-14): I move pins on my own branch as needed; the session 0 session merges into
master (and other branches) and moves the pins there. I send it the commit, the pins and what was tested, and say
what touches `scripts/`, `installer/`, `docs/TOOLING.md` or `docs/STOCK_CHANGES.md`. Session 0 runs on another
machine; the Vintner session shares this one (WSL Debian), so tell it before VM runs.

## 5. Open decisions for the owner

None.

## 6. What is left on the machines

- WSL Debian (VM tests): `/var/tmp/vmtest-gpt/wex-pn-bios` and `-uefi` (the installed disks, about 9 GB each; keep
  as the base) and the overlays `wex-p{t,a,c,r,w,s}-{bios,uefi}` (evidence in each `safe-mode/`; may be deleted).
  Logs: `/var/tmp/wex-iso/safemode-wex-p?-*.log`, `run-wex-pn-*.log`; `D:\WSL\wex-iso\pn-chain.log`. Probes:
  `/mnt/d/WSL/wex-iso/probes`.
- `D:\WSL\wex-iso\wex-pn.iso`: the tested ISO. `wex-mg` and `wex-c5` (disks and ISOs) are deleted.
- WSL wexbuild (builds): `/root/WineExplorer` is the ISO clone, its `wine` at `5ec3c009` (detached; `configure`
  and friends modified by the build, not to be committed). `/root/WE-safemode`, `/root/wine-stage` and
  `/root/wine-vbi.bundle` are older and may be deleted after a look.
- The session's wrapper scripts are copied to `D:\WSL\wex-iso\session-scripts` (not in the repository; each has
  the old scratch directory in its `S=` line, to be changed before use):
  `wexbuild-iso-master.sh REV NAME` (ISO of a commit; the gitlink's commit must be in `.git/modules/wine`),
  `wsl-install-run.sh NAME vintner` (install on both firmwares), `wsl-safemode-snap.sh SRC TEST installed PHASE...`
  (a Safe Mode run from a snapshot of the checkout; `WEX_FWS=uefi` for one firmware),
  `wsl-mg-chain.sh` (the whole chain of section 2), `wexbuild-wine-merge.sh` (a Wine merge in a worktree of the
  ISO clone, brought here with a bundle).
- Use `wsl --terminate NAME`, never `wsl --shutdown` (other sessions use the same WSL).
