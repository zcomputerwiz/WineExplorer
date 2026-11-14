<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Vintner Boot Integration: Where to Resume

Date: 2026-11-14. Branch: `claude/vintner-boot-integration`. Plans: `VINTNER_BOOT.md`, `SAFE_MODE_WORKORDER.md`;
test reference: `SAFE_MODE.md`. Read `TOOLING.md` first.

## 1. State

- Master and `development` are `1a1f393f` (2026-11-14): this branch's work up to `ef0dc06f` is in them, with wine
  `5ec3c009` (Wine `development`, which contains the Run dialog and DDE commits), wexcomp `1f8e5d3a` and vintner
  `6f30413`.
- On the branch since then, waiting for session 0 to merge to master: the test harness (`df456f23`, merge of
  `claude/harness-speed`): `installer/tests/run-chain.sh`, `build-test-iso.sh`, `nbd-connect.sh`, and the memory
  settings in `scripts/vmtest.py`. Docs: `TOOLING.md` (its own section at the end) and `SAFE_MODE.md`.
- Nothing is uncommitted here except ` M wine` and ` M vintner`: those working trees are not this work's (the
  `wine` one is the E1 checkout). Never `git add` them; a gitlink change goes through
  `git update-index --cacheinfo 160000,<sha>,PATH`.

## 2. Done and tested

On the ISO `wex-vb` (2026-11-14, `289c2080`: master `152d6095` with vintner `6f30413`), BIOS and UEFI, all passing
(install, defaults and `startrec` with the old wrappers; then every stage again with `run-chain.sh -j 3` on its
installed disks). The same chain passed on `wex-pn` (wine `5ec3c009`, vintner `d3fe892`) and on `wex-mg` (wine
`3ea5d0cb83b`, 2026-11-13):

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
logs. `startrec` once lost a key of the password (UEFI); it now retypes once. `scripts/test_safemode.py`: 150.

`run-chain.sh` on `wex-vb`, three jobs at a time: defaults 21 and 25 min (BIOS, UEFI), startrec 8 and 8, wayland 4
and 5, classic 3 and 3, accounts 3 and 3, rundialog 2 and 2; all stages in about 35 minutes, against 79 one job at a
time. With Wine unchanged, `build-test-iso.sh` spends only the ISO's 11 minutes.

## 3. Running or half done

- The WSL restart for `%USERPROFILE%\.wslconfig` (`autoMemoryReclaim=dropcache`, written 2026-11-14 with the owner's
  OK): after the Vintner session says its Secure Boot row in `vbtest` is finished, terminate Debian, vbtest and
  wexbuild one by one (never `wsl --shutdown`), check the setting, and tell the Vintner session WSL is back.

## 4. Next steps, in order

1. The harness is on master (`3a08b9b7`). D4 (Debugging Mode on the serial port) is on the branch, tested with the
   `debugmode` phase on both firmwares, handed to session 0 for master.
2. `SAFE_MODE_WORKORDER.md` has one item left, C4's RID 500 and `wex-is-admin` for the Administrator account, which
   is E1's (not this branch's). Ask the owner what comes next.

How merges work (owner, 2026-11-14): I move pins on my own branch as needed; the session 0 session merges into
master (and other branches) and moves the pins there. I send it the commit, the pins and what was tested, and say
what touches `scripts/`, `installer/`, `docs/TOOLING.md` or `docs/STOCK_CHANGES.md`. Session 0 runs on another
machine; the Vintner session shares this one (WSL Debian), so tell it before VM runs.

## 5. Open decisions for the owner

None.

## 6. What is left on the machines

- WSL Debian (VM tests): `/var/tmp/vmtest-gpt/wex-vb-bios` and `-uefi` (the installed disks, about 9 GB each; keep
  as the base) and some `wex-vb-STAGE-FW` overlays (may be deleted). Results: `/var/tmp/wex-chain/wex-vb/`. Probes:
  `/mnt/d/WSL/wex-iso/probes`.
- `D:\WSL\wex-iso\wex-vb.iso`: the tested ISO. `wex-pn`, `wex-mg` and `wex-c5` (disks and ISOs) are deleted.
- WSL wexbuild (builds): `/root/WineExplorer` is the ISO clone, its `wine` at `5ec3c009` with a current build
  (`build/wow64/.wex-test-iso-stamp` is written by `build-test-iso.sh` from its first run on). `/root/WE-safemode`,
  `/root/wine-stage` and `/root/wine-vbi.bundle` are older and may be deleted after a look.
- How to test a commit now: `installer/tests/build-test-iso.sh REV NAME` in wexbuild, then
  `installer/tests/run-chain.sh --iso /mnt/d/WSL/wex-iso/NAME.iso NAME` in Debian (`docs/TOOLING.md`, the section
  added 2026-11-14). The old wrappers in `D:\WSL\wex-iso\session-scripts` are kept only for `wexbuild-wine-merge.sh`
  (a Wine merge in a worktree of the ISO clone, brought here with a bundle).
- Use `wsl --terminate NAME`, never `wsl --shutdown` (other sessions use the same WSL).
