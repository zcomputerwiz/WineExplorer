<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# UWP track: where to resume (written 2026-11-13, updated 2026-11-14)

Read this first when the UWP work is picked up again. The status table is in `README.md` of this folder; the measured
behaviour is in `tests/uwp/u1_1/README.md` and `tests/uwp/u1_3/README.md`.

## Where everything is

| What | Where | Commit |
|---|---|---|
| Wine code | repository `zcomputerwiz/wine`, branch `codex/uwp-runtime`; worktree `C:/Users/Owner/.codex/worktrees/uwp-runtime/wine` | fe9776eb |
| Wine integration branch | `development` (all UWP code) | fe9776eb |
| WineExplorer UWP branch | `claude/uwp`, worktree `D:/DeepThought/Projects/WineExplorer-uwp` (probes and docs since 2026-11-14; `master` gets it by an agreed merge) | this commit |
| Probes, runners, docs | this repository, `master`: `tests/uwp`, `docs/uwp`, `docs/STOCK_CHANGES.md` | this commit |
| Lab helper scripts | `tests/uwp/lab-helpers` (copies of the session's scratch scripts) | this commit |

Nothing is uncommitted and no job is running on the guest.

## Done, and how it was tested

U1.0 to U1.5 (package identity and activation, view lifecycle, presentation and input, package information, package
dependencies, `ApplicationData`, storage files with `FileIO`, `PathIO`, file streams, `DataReader`, `DataWriter`).

Each was measured on Windows 11 with `tests/uwp/u1_1/view.exe` (signed package, one application per mode) and the
`expected-*.txt` lists were written from that. The same binaries were then run on Wine x64 and x86 in the owned U1
guest, candidate `u11-2`:

* `guest-u11-present.sh u11-2 RUN`: all probe modes, with a display (Xvfb, 3D) and without. Last run `u15-run4`: pass,
  storage 87 checks.
* `guest-all.sh u11-2 TAG` and `guest-final.sh u11-2 TAG`: Wine tests of `windows.storage` (599), kernel32 `version`
  (541), `windows.ui` (12, 82), `twinapi.appcore` (73), the U1.0 fixture with 14 context negatives, the installer
  suites (63, 59). Last tag `u15c`: pass. The `rpcrt4` `server` test has two `np_basic` failures that are older than
  this work.
* `guest-wintypes.sh u11-2 RUN`: `wintypes` (1252) and `windows.storage.applicationdata` (12). Last run `wt-1`: pass.

The candidate was built from ee46bbaf plus the stream commit; it does not contain 0e5f4da1 (advapi32, another
session), which is under the stream commit on `development`. Different DLLs, but not built together by me.

## Next steps, in order

1. **Done 2026-11-14:** package resources (U1.6), 43 checks on Windows and Wine (`ViewResources`).
2. **Suspend and resume** (U1.1c). The probe `ViewSuspend` is built. It needs a person at the Windows machine:
   `run-present.ps1 -Mode suspend -Manual`, then minimize the window, wait about 12 s, restore it, wait about 8 s.
3. Installer signature check (so that Store-signed packages install), server-side compatibility gate.
4. Smaller gaps: thumbnails, file queries, `KnownFolders`, pickers, `GetParentAsync`, `MapChanged` events of the
   settings, `GetPublisherCacheFolder`.
5. `Windows.UI.Xaml`: `PLAN_XAML.md`. **Not started on purpose**, see the decisions below.
6. After UWP: the Copilot MSI (`E:\CopilotStableEnterpriseX64.msi`), then the Office installers from `E:` (2013, 2016,
   2021, 2024; success is: installer completes, Word opens and saves a document; media and keys stay out of the
   repository).

## Open decisions for the owner

* Next piece: MRT/PRI (can be done unattended) or suspend/resume (needs the owner once).
* XAML (`PLAN_XAML.md`): is the Calculator the first target and may the installed Store package be used as a test
  oracle; is a .NET dependency acceptable if the native route stalls; XAML before or after Copilot and Office.

## Things that expire or need care

* **Test certificates.** The Windows oracle runs sign with two throwaway certificates (subject
  `CN=WineExplorer U1 Oracle`, thumbprints 2ACF46FB... for x64 and 839622FF... for x86). They expire
  **2026-11-14 19:35**. After that `package.ps1` makes a new one, and the owner has to import its public
  `oracle.cer` into `Cert:\LocalMachine\TrustedPeople` once from an elevated PowerShell. The old ones stay installed
  until the owner asks to remove them.
* **Shared checkout.** `D:\DeepThought\Projects\WineExplorer` local `master` was lined up with `origin/master` on
  2026-11-14 (`git reset --keep`; the old head was a95f3581, the system tier commit 7e7a02a2 is on
  `claude/system-tier-elevation`). Its working tree still has uncommitted files that are not from the UWP track
  (four files under `docs/uwp` from the earlier Codex work, the `wine` and `wexcomp` submodule checkouts, untracked
  scratch files): leave them.
* **Coordination.** Announce every push to Wine `development` to the other sessions. Before a push to WineExplorer
  `master` that moves the `wine` gitlink or touches `scripts/`, `installer/`, `docs/TOOLING.md` or
  `docs/STOCK_CHANGES.md`, send them the commit and agree who pushes. `development` moved to 5ec3c009 on 2026-11-14 (the system tier session merged
  `claude/run-dialog`, `shell32` only, on top of the UWP work); `codex/uwp-runtime` has that merged (a146951f, not
  built on the guest yet). The gitlink of `master` still pins 3ea5d0cb; the system tier session will propose moving it.

## The owned guest

QEMU guest "U1", controller `scripts/lab/uwp/owned-lab.py` (run through WSL as root: `status`, `ssh`, `push`,
`restart`; `ssh` and `push` stop after 55 s, so start long jobs detached with `setsid nohup` and poll a file).
Workspace `/home/debian/uwp-u1-20261111`; my files in `claude/` there, logs in `claude/logs`.

* Candidate `build/candidates/u11-2`: `source` and `build` are the tree described above, `phase` is BUILT. Rebuild in
  place: `lab.sh delta NAME` (tar of the changed files of the worktree), then `guest-u1r2-build.sh u11-2 <tar>`, or
  `fastbuild15.sh <tar>` to compile only `windows.storage` and `wintypes`.
* Run directories left in the candidate (results, each with a Wine prefix): `u15-run2` to `u15-run4`, `ui-u15b`,
  `ui-u15c`, `regress-u15b`, `regress-u15c`, `u1-run-u15b`, `u1-run-u15c`, `install*-u15b`, `install*-u15c`, `wt-1`.
  They may be deleted when space is needed: the build disk (`/dev/vdb`, 157 G) fills with such prefixes; it was at
  70 % when this was written, and a full disk makes the suites fail with "No space left on device".
* The other candidates (`merged-a524eaf3`, `r0a-1`, `r0b-1`, `r4-1`, `r4-clean-1`) are from the earlier Codex work;
  `merged-a524eaf3` is the origin that `guest-u1r2-build.sh` copies from.
* Fixture on the guest: `claude/u11fix`, pushed by `u11-fixture.sh` from the built probes of this repository.

## Traps found on the way

* The interface names of the WinRT headers are macros (`IAsyncOperation_StorageFile` and the like). Passing one
  through a wrapper macro before `##` pasting expands it and breaks the member names.
* Bash heredocs in the tool shell break on C text with quotes or backticks and collapse backslashes: write such text
  with an editor tool or a patch script file.
* `git fetch origin` in the Wine worktree does not move `origin/development` reliably: use `git ls-remote origin` or
  `git fetch origin development`.
* widl crashes on winmd generation when `include/windows.storage.idl` gets a class built on a collection: define such
  classes in the DLL's own `classes.idl`.
* A folder object must not keep its directory open: Wine removes a directory only when the last handle closes.
* The tool shell cannot send mouse or keyboard input to the Windows desktop: modes that need input are run by the
  owner (`run-present.ps1 -Manual`).
