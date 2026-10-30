<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: callers use the broker (plan step 7)

Date: 2026-10-24. Status: scoped. Part of `ELEVATION_BOUNDARY_PLAN.md` (design D4, step 7). Builds on the broker
(`ELEVATION_BROKER_WORKORDER.md`), consent (`ELEVATION_CONSENT_WORKORDER.md`) and the secure desktop
(`ELEVATION_SECURE_DESKTOP_WORKORDER.md`): with the launcher and a helper configured, an administrator's request to
`\\.\pipe\wexelevate` shows click consent on `WinSta0\Winlogon` and starts the program with the full token.

## Why

Nothing calls the broker yet. With a helper configured, `runas`, `requireAdministrator` manifests and per-machine
MSI installs still take the old linked-token routes, which enforcement closed, so they fail. This step routes them
through the broker, which makes elevation usable end to end and lets the S2/S3/S4 `WAITING` assertions run.

## Decisions (main session; small stock changes, gated)

- **E1. One client, in WineExplorer.** The client code exists (`src/include/wexelevate.h`, `wexelevate_run()`).
  Export it once from `wexhost.dll` (`WexElevateRun`), which already carries broker support (the lease). Stock Wine
  callers load `wexhost.dll` by name only while elevation is enforced (`is_elevation_helper_unset()` false) and
  call the export; if the DLL or export is missing they fail with `ERROR_NOT_SUPPORTED`, never fall back to the old
  route. With the helper unset, every caller keeps today's behavior byte for byte.
- **E2. shell32 (`dlls/shell32/shlexec.c`).** Under enforcement:
  - verb `runas`, and a `CreateProcess` that fails with `ERROR_ELEVATION_REQUIRED` (740, `requireAdministrator`,
    or `highestAvailable` for an administrator), go to the broker with the resolved image, the full command line,
    the working directory and the show state;
  - `SEE_MASK_NOCLOSEPROCESS` gets the broker's process handle; `ShellExecuteEx` errors map the broker's codes
    (1223 cancel stays 1223 / `SE_ERR_ACCESSDENIED` per native `ShellExecuteEx`; 50 for a standard user until the
    over-the-shoulder work order; 21 when no consent policy applies);
  - the old helper/consent-exit-code path stays only for the unset-helper case.
- **E3. Manifests in CreateProcess (kernelbase).** Already: `requireAdministrator` returns 740 for a limited token
  (Windows behavior); `highestAvailable` runs limited from `CreateProcess` and elevates only through
  `ShellExecute`, as on Windows. Verify, do not change unless a test shows a difference.
- **E4. msi.** Windows elevates a per-machine install once, through the Installer service. Here: when `msiexec`
  (or a program calling `MsiInstallProduct`) runs a per-machine install (`ALLUSERS=1`, or `ALLUSERS=2` resolved to
  per-machine for an administrator) on a limited administrator token under enforcement, it relaunches the same
  `msiexec` command line through the broker once (one consent) and returns its result; the elevated `msiexec`
  then runs everything, including deferred custom actions, with the full token, so `custom.c`'s linked-token route
  is not needed under enforcement and is left for the unset-helper case. Per-user installs do not elevate.
  Test against a real MSI before settling; if the relaunch breaks a common case, stop and report.
- **E5. Others.** `ntdll/loader.c` `elevate_token` stays (never runs when enforced); explorer already runs limited
  (Q3); `winetest` stays test-only and documented; `consent.exe` is the broker's UI.
- **E6. Standard users** get the broker's 50 (`ERROR_NOT_SUPPORTED`) until over-the-shoulder elevation exists;
  `highestAvailable` simply runs limited for them, as on Windows.

## Scope

1. `WexElevateRun` export in wexhost; shell32 and msi callers (E1, E2, E4); verify E3.
2. Tests (fixture with launcher, helper set, consent test hook, secure desktop):
   - `runas`, `requireAdministrator` and `highestAvailable` through `ShellExecuteEx` as a filtered administrator
     (elevates after Approve, cancel gives 1223 and no process), a standard user (50; `highestAvailable` runs
     limited) and an already elevated process (runs directly, no second prompt);
   - 32-bit and 64-bit callers and targets;
   - `SEE_MASK_NOCLOSEPROCESS` handle waits and returns the exit code;
   - two real, freely licensed installers that require administrator rights, one 32-bit and one 64-bit (for
     example 7-Zip's and Notepad++'s installers; download in this container, never commit them), install into
     the machine root's `Program Files` after Approve and are visible to another user; a per-machine MSI
     (build a minimal one with Wine's own tools or `msitools` if available) installs with one consent;
   - the S2, S3 and S4 `WAITING` assertions that can now run in this container are run (others stay for the
     local validation round), with `WEX_BROKER_AVAILABLE=1` where the tests use it;
   - regression: the shell32 `shlexec` and msi test suites before and after (helper unset: identical), the full
     harness, focused, B4 both modes, `test_sysrun_server.sh` full.
3. Docs: `STOCK_CHANGES.md` for every stock change; Results; plan status.

## How the work is split

Planning and review: the main session. Implementation: a Sonnet agent. Anything security-relevant not decided
above: stop and report.

## Results

(none yet)
