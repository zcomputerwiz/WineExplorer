<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Vintner Boot Integration: Where to Resume

Date: 2026-11-13. Branch: `claude/vintner-boot-integration`. Plans: `VINTNER_BOOT.md`, `SAFE_MODE_WORKORDER.md`;
test reference: `SAFE_MODE.md`. Read `TOOLING.md` first.

## 1. State

- `claude/vintner-boot-integration` and master were equal at `ef0f1e54` (pushed 2026-11-13). Master has since
  gained UWP commits (`dc2b79b1`: docs and `tests/uwp` only); merge master into the branch before the next proposal.
- Gitlinks at `ef0f1e54`: wine `3ea5d0cb83b`, wexcomp `1f8e5d3a`, vintner `d3fe892`.
- Wine: `3ea5d0cb83b` is the fork's branch `claude/run-dialog` (pushed) = `2ebf65d5` merged with the shell32 commits
  `042fc59aab4`, `68ab2d5dab3` (Run dialog) and `1d6c6a65acf` (a folder's "DDE failure"). It is not in `development`
  yet: session 0 holds a merge of it (`5ec3c009`, fork branch `claude/system-tier-wine`).
- Nothing is uncommitted here except ` M wine`: the `wine` working tree is the E1 checkout and is not this work's.
  Never `git add wine`; a gitlink change needs the owner's word and goes through
  `git update-index --cacheinfo 160000,<sha>,wine`.

## 2. Done and tested

On the ISO `wex-mg` (built from `258d5355`, the first merge with master; the later commits change only docs,
`tests/uwp` and the test script), BIOS and UEFI, all passing:

| Stage | Phases |
|---|---|
| install | zero-input install and installed-disk checks |
| defaults | safe, safenet, safecmd, nowine, bootlog, startnormal, oschoices, normal, lastgood, vfailed, lastgoodgen, with the probes |
| accounts | `accounts` |
| classic logon dialog (C4) | `classic` (caption 5 66 202, Luna) |
| Run dialog | `rundialog` (eight command lines, the folder without "DDE failure.") |
| Wayland | `waylanddisplay`, `waylandsafe`, `waylandcmd` |
| Startup and Recovery (D5) | `startrec` |

`startrec` failed once on UEFI (the prompt called the typed password incorrect: a lost key), passed unchanged on a
second run, and passed on both firmwares with the retry of `ef0f1e54`. `scripts/test_safemode.py`: 149.

## 3. Running or half done

Started 2026-11-14, for session 0's proposal to move master's wine gitlink to `development` `5ec3c009`: an ISO
`wex-pn` of the local, unpushed test commit `b11876e4` (branch `claude/test-wine-5ec3c009`, worktree
`D:\Projects\WE-pin`: master `120e3896` with wine `5ec3c009`), then the whole chain of section 2. Logs:
`D:\WSL\wex-iso\iso-pn.log` (ends with `ISO DONE`) and `D:\WSL\wex-iso\pn-chain.log` (ends with `overall=0` when
everything passed). If it was interrupted, run it again: `wexbuild-iso-master.sh b11876e4 wex-pn` in wexbuild, then
`wsl-pn-chain.sh` in Debian (both in `session-scripts`). The result goes to session 0; afterwards remove the
worktree and the test branch.

## 4. Next steps, in order

1. Merge master into the branch (expect `docs/STOCK_CHANGES.md` to auto-merge).
2. When session 0 proposes moving master's wine gitlink to a `development` commit (it will send the commit and its
   results), run the chain of section 6 on an ISO of that commit, both firmwares (about 1.5 hours), and report.
3. Pin Vintner Boot `96186d6` or later (docs and a CI script fix since `d3fe892`) as an ordinary update, with the
   install and defaults stages.
4. Continue `SAFE_MODE_WORKORDER.md` (the open items after C4 and D5).

Master pushes are agreed with the session 0 and UWP sessions first (the commit and what was tested), when they move
the wine gitlink or touch `scripts/`, `installer/`, `docs/TOOLING.md` or `docs/STOCK_CHANGES.md`.

## 5. Open decisions for the owner

- Who moves master's wine gitlink to `development` once it contains `3ea5d0cb83b`, and whether the Safe Mode chain
  must pass on that commit first (offered; not started).

## 6. What is left on the machines

- WSL Debian (VM tests): `/var/tmp/vmtest-gpt/wex-mg-bios` and `-uefi` (the installed disks, about 9 GB each; keep
  as the base) and the overlays `wex-g{t,a,c,r,w,s}-{bios,uefi}` (evidence in each `safe-mode/`; may be deleted).
  Logs: `/var/tmp/wex-iso/safemode-wex-g?-*.log`, `run-wex-mg-*.log`. Probes: `/mnt/d/WSL/wex-iso/probes`.
- `D:\WSL\wex-iso\wex-mg.iso`: the tested ISO. `wex-c5` (disks and ISO) is deleted.
- WSL wexbuild (builds): `/root/WineExplorer` is the ISO clone, its `wine` at `3ea5d0cb83b` (detached; `configure`
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
