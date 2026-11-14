# System tier, Group Policy, enforced elevation: where to resume

Written 2026-11-13 by the system tier / elevation session, when its usage limit was near. This is the state to
pick up from; the work itself is described in [SYSTEM_TIER_WORK_ORDERS.md](SYSTEM_TIER_WORK_ORDERS.md),
[GROUP_POLICY.md](GROUP_POLICY.md) and [E1_PRODUCTION_PLAN.md](E1_PRODUCTION_PLAN.md). Delete this file when it
is no longer true.

## Branches and trees

- **WineExplorer:** this session works on `claude/system-tier-elevation`, in its own worktree
  `D:\DeepThought\Projects\WineExplorer-systier`. It does not push to `master`: a merge to `master` is agreed
  with the other sessions first (the commit and what was tested go to both; who pushes and when is agreed).
  The owner's words: "Work on your branch and coordinate with the other sessions for merges to main."
- **Wine:** `development` stays the shared integration branch; every push is announced to the other sessions.
  The worktree's `wine` is a worktree of the same Wine repository on the local branch `claude/system-tier-wine`
  (also pushed under that name as a backup of the merge below).
- **Other sessions:** "Reusable - clear every time" (UWP; also commits in `D:\DeepThought\Projects\WineExplorer`,
  the shared checkout on local `master`) and "Wine Explorer Integration" (branch
  `claude/vintner-boot-integration`, another machine). Never reset or rebase the shared checkout. Its local
  `master` still holds this session's commit 7e7a02a2 under two UWP commits; the UWP session lines it up with
  origin once told this session has left it.
- **Runner scripts** (untracked): `unsloth-tmp/` in the worktree, copied from the shared checkout with the
  path changed. Run them as `wsl.exe -d wexbuild -u root -- bash /mnt/d/DeepThought/Projects/WineExplorer-systier/unsloth-tmp/NAME`.
  They have not been run from the new path yet: the first run there is the check that the copy works
  (`pol_run.sh`: build and normal suite, about 25 minutes; `st_t62_run.sh`: everything, about an hour).

## State of the code

| What | Where | Tested |
|---|---|---|
| Group Policy steps 1 to 4, the lab domain, the end-to-end test | `master` | suites, the lab domain, an installed machine |
| Enforced elevation, opt-in image (`WEX_ELEVATION=1`), administrator's elevation | `master` | `installer/tests/elevation.sh` 6 checks, `session0.sh` 16 |
| Shared document folders, New Folder message | `master` | normal suite; the folders on an installed image |
| Test hooks behind `WEX_E1_TEST_HOOKS` (dialog, broker); the suite check for user names | this branch (7e7a02a2) | normal 347, lab 421, broker test |
| Wine: a saved Volatile Environment key is dropped at hive load (2ebf65d5) | Wine `development`, pinned by `master` through 3ea5d0cb83b | normal suite |
| Wine: `LookupAccountSid` knows the caller's mapped SID (0e5f4da1) | Wine `development`, **not pinned anywhere** | normal 347 and lab 421 on 32ed531b plus it |
| Wine: merge of `claude/run-dialog` into `development` (5ec3c009) | local, and pushed as `claude/system-tier-wine`; **not on `development`** | built; normal suite was running when this was written |

## Next actions, in order

1. Finish the check of Wine 5ec3c009 (build passed; rerun `pol_run.sh` from the worktree if the result of the
   run in the shared checkout is lost). If it passes: `git push origin claude/system-tier-wine:development` in
   the worktree's `wine` (a fast-forward of `development` from 9484da1a; if `development` moved, merge again),
   tell both sessions the commit, and tell the UWP session that the shared checkout is free.
2. Pin that Wine commit on this branch (`git add wine`), run the full sequence (`st_t62_run.sh`), push the
   branch.
3. Propose to both sessions: `master`'s `wine` gitlink from 3ea5d0cb83b to that commit. It brings the UWP
   runtime work and the user name fix into images. The Integration session offered its Safe Mode chain on an
   ISO of the commit (about 1.5 hours) as the check; ask it to run it.
4. Confirm the user name fix on an installed image (`%USERNAME%` and `%USERPROFILE%` of the installed user with
   session 0; on the last image they were empty and `C:\users`). Image scripts: `unsloth-tmp/el-chain2.sh`
   (fresh clone of `master`; change it to check out this branch), look script `el-check-guest.sh`.
5. Hook separation, the rest ([E1_PRODUCTION_PLAN.md](E1_PRODUCTION_PLAN.md), "Progress"): `wine/server`
   (`sysrun.c`, `token.c`), `src/dlls/wexhost/e1s6.c`, `wex_profile_storage.c`, the three broker setters left
   alone. Mechanical: Qwen did the broker part well with a written list and no shell.
6. Not looked at under enforcement: changing the display resolution, installing a program, a second user's
   first logon.

## Decisions waiting for the owner

- The PAM stack, lockout defaults and unlock, the audit record, the built-in Administrator, and lifting the
  "DO NOT ENABLE" gate ([E1_PRODUCTION_PLAN.md](E1_PRODUCTION_PLAN.md), "Decisions that are the owner's").
  Needed at steps 3 and 4 of that plan, not before.
- Whether this session's Wine commits should also go to a branch instead of `development` (asked 2026-11-13,
  not answered; `development` is used as before).

## Machines and disks left as they are

- **wexbuild (WSL):** `/root/WE-prefetch` (main build), `/root/owned-r8` (lab build), `/root/WE-s0` (ISO clone,
  at 09f5c938), ISOs `/root/out/wex-s0.iso` (no enforcement, 159d820e) and `/root/out/wex-el.iso` (enforcement,
  09f5c938); installed disks `/root/vmtest-s0/wex-s0` and `/root/vmtest-el/wex-el` with their test overlays
  (`-s0test`, `-adtest`, `-eltest`; each has a throwaway root password file). All VMs stopped.
  `/var/tmp/wex-ad-lab/admin_password.txt` is the lab domain's throwaway password, copied from Debian.
- **Debian (WSL):** the lab domain, `/var/tmp/wex-ad-lab` (controller `dc1`, member `member1`, both stopped;
  start with `installer/tests/ad/dc-create.sh`).
- No Qwen or Antigravity job is running.
