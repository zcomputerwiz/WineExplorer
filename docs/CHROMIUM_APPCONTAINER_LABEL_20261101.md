<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium's network service sandbox fails on a prefix without the AppContainer registration: labels in SetNamedSecurityInfoW()

Date: 2026-11-01. Status: cause found and repaired in the Wine tree; nothing is pushed. The repair is `1cc71ccd` (advapi32)
on the Wine branch `copilot/first-thread-data-alignment`. The test run that validated it exposed a second, separate bug that
only 32-bit callers see, repaired in `67317644` (wow64). This is the third failure of a series; the other two are
[CHROMIUM_FIRST_THREAD_DATA_ALIGNMENT_20261101.md](CHROMIUM_FIRST_THREAD_DATA_ALIGNMENT_20261101.md) (the stall that started
the regression hunt) and [CHROMIUM_MISSING_WINE_FONTS_20261101.md](CHROMIUM_MISSING_WINE_FONTS_20261101.md) (the R6 runtime
package lacks Wine's TrueType fonts). The gap is in stock advapi32 and has the same conditions in both Wine bases (R5
`eba89375`, R6 `66caeb46`): it is not part of the token or AppContainer work and not a regression of the new base. It stays
hidden while the prefix already holds the network service's AppContainer registration, as the lab VM's live prefix did, and
shows on a prefix that does not. No document in the series is an acceptance record for Chromium's rendering, security or
token behavior: the sandbox is on in every run quoted here.

## Result

- The network service's sandbox needs an AppContainer profile. Chromium creates the profile's `AC` directory and sets its
  DACL and its Low mandatory label in one `SetNamedSecurityInfoW( dir, SE_FILE_OBJECT, DACL | LABEL | UNPROTECTED_DACL, ... )`
  call. Wine's advapi32 opened the directory without `WRITE_OWNER`, which the Wine server requires for
  `LABEL_SECURITY_INFORMATION` (as Windows does), so the call failed with `ERROR_ACCESS_DENIED` (5).
- Chromium rolled the profile back and could not launch the sandboxed network service. By Chromium's fallback logic (see
  Mechanism; the restart itself is inferred, not read from a command line) it then restarted the service without the sandbox
  and stored a "failed launch" marker for Chromium 156 in `Local State`. In that run the fixture page never reached the
  network stack.
- `1cc71ccd` changes two conditions in `dlls/advapi32/security.c`: `SetNamedSecurityInfoW()` also asks for `WRITE_OWNER` when a
  label is set, and `GetNamedSecurityInfoW()` also asks for `READ_CONTROL` when a label is read. The new test
  `test_named_security_info_label` fails four checks without it and passes with it, on x86_64 and i386.
- The i386 test run also showed a crash in wow64 `NtCreateToken()` on a NULL optional parameter, repaired in `67317644`
  (the same change as upstream `9f4469dbfb`).
- Local Chromium runs with the fix and no registration in the prefix at the start (w7, w9): the label call succeeds,
  `CreateAppContainerToken()` is called, `index.html` and `scene.js` are served, the network service starts with
  `--service-sandbox-type=network`, and neither a "crashed or was terminated" line nor the `Local State` marker appears.
- Limits: validated in local runs only, not in the lab VM. Wine does not store labels, so the repaired calls succeed but
  have no effect on access checks and `GetNamedSecurityInfoW()` returns no label afterwards (Open items).

## Symptom

Local runs of the pinned Chromium 156 build with the sandbox on, in a WoW64 build of the Wine branch, 40 s each, with a new
`--user-data-dir` for every run. w7 to w9 traced `-all,+timestamp,+pid,+tid,+winediag,+process,+security`; w6's log has
`security`, `advapi` and relay lines for the advapi32 and ntdll security calls and no `process` lines.

| Run | Wine tree | `AppContainerRegisterSid` | `index.html` / `scene.js` reach the network stack | "Network service crashed" lines | `Local State` marker | Network service |
|---|---|---|---|---|---|---|
| w6 | without `1cc71ccd` | new (hr 0), unregistered 7 ms later | no / no | 1 | `network_service_failed_launch_major_version`: 156 | restarted, unsandboxed (inferred) |
| w7 | with `1cc71ccd` | new (hr 0) | yes / yes | 0 | absent | `--service-sandbox-type=network` |
| w8 | with `1cc71ccd` | exists (`0x800700b7`, left by w7) | yes / yes | 0 | absent | `--service-sandbox-type=network` |
| w9 | with `1cc71ccd`, new prefix | new (hr 0) | yes / yes | 0 | absent | `--service-sandbox-type=network` |

- "Reach the network stack" counts the `NotifyBeforeURLRequest` lines of `chromium.log` for the two fixture URLs. w6 logs 11
  such lines, none for the fixture (only component update URLs, from the restarted service); w7 to w9 log 24 to 25 and one for
  each fixture URL. The fixture server logged `index.html` 200, `scene.js` 200 and the `favicon.ico` 404 in w7 to w9. w6 has
  no server summary.
- `CreateAppContainerToken()` is traced twice in each of w7 to w9 and never in w6. `AppContainerUnregisterSid()` is traced
  once in w6 (the rollback) and never in w7 to w9.
- w8 ran on the prefix that w7 had left registered, so Chromium got `ERROR_ALREADY_EXISTS` and never made the label call. All
  four runs use the same package name (`cr.sb.netC443...`) whatever their user data directory, so the registration outlives
  a run's profile. w8 is the state of the lab VM's live prefix: the VM runs `chromium-prep-tfont1`, `-tfix1` and `-tdiag1`
  (`chromium-native-evidence\helper.stderr.log`) log `AppContainerRegisterSid returning hr=0x800700b7` as well. This state
  works with and without the fix. Only w7 and w9 exercise the repair.
- The unsandboxed relaunch in w6 is inferred, not read from a command line (w6 has no `+process`): the marker, Chromium's
  fallback logic (Mechanism), the missing token calls and a restarted network service process. w7 to w9 show the sandboxed
  command line, `--utility-sub-type=network.mojom.NetworkService --service-sandbox-type=network`.
- `netlog.json` is a 382-byte stub in every run and does not discriminate. `Unknown error 10042` lines turned into
  `net::ERR_FAILED` (20 in w6, 76, 44 and 44 in w7 to w9) occur in the failing and the passing runs alike and do not
  discriminate either.

## Mechanism

Chromium's `AppContainerBase::CreateProfile()` (`sandbox/win/src/app_container_base.cc`) runs under a lock. The w6 trace of
the failing run, with Wine's seconds clock in front:

| Time | Step of `CreateProfile()` | Wine call | Result |
|---|---|---|---|
| 55540.575 | register the package SID | `AppContainerRegisterSid( sid, L"cr.sb.net<hash>", L"Chrome Sandbox" )` (trace line 4967) | hr 0; the mapping `S-1-15-2-...` is stored below `Software\Classes\Local Settings\...\AppContainer\Mappings` (line 5031) |
| | create the profile directory | `C:\users\root\AppData\Local\Packages\cr.sb.net<hash>\AC` | not traced |
| 55540.579 | read its DACL | `GetNamedSecurityInfoW( dir, SE_FILE_OBJECT, DACL )` | 0 |
| 55540.579 | add the package entry | `SetEntriesInAclW` (all access, object and container inherit) | 0 |
| 55540.579 | write the DACL and the Low label | `SetNamedSecurityInfoW( dir, SE_FILE_OBJECT, 0x20000014, 0, 0, dacl, sacl )` (line 5068) | 5 |
| 55540.580 | (inside that call) | `NtSetSecurityObject( handle, 0x20000014, ... )` | `c0000022`, `STATUS_ACCESS_DENIED` |
| 55540.583 | roll back | `AppContainerUnregisterSid` (line 5082) | hr 0; Chromium also deletes the directory and returns null |

`0x20000014` is `DACL | LABEL | UNPROTECTED_DACL` (0x4, 0x10, 0x20000000). The label is written by
`CreateAppContainerDirectory()` through `SecurityDescriptor::WriteToFile()` (`base/win/security_descriptor.cc`), which ends in
`SetNamedSecurityInfo()`. The Chromium files and functions are named from indexed snapshots that are newer than the pinned
build (`7d9824099030`), so no line numbers are given for them.

The seven other `SetNamedSecurityInfoW()` calls of w6 (DACL only, `0x20000004` and `0x4`, on the profile folders `Default\Cache`,
`Default\Shared Dictionary`, `Default\Network` and `SODA\1.2.12`) all return 0. A DACL needs only `WRITE_DAC`; the label call is
the only one that fails.

**Wine's side.** `SetNamedSecurityInfoW()` opens the object itself, with an access mask computed from the information it was
asked to set: `WRITE_OWNER` for the owner and the group, `WRITE_DAC` for the DACL, `ACCESS_SYSTEM_SECURITY` for the SACL, plus
`READ_CONTROL` for a file's DACL (`dlls/advapi32/security.c:2424`; the code is shared by services, registry keys and files).
The label was not in that list, so `DACL | LABEL` opened the directory with `WRITE_DAC | READ_CONTROL` only. The server asks for
`WRITE_OWNER` for the owner, the group and the label (`set_security_object()`, `server/handle.c:730`), and refused with
`STATUS_ACCESS_DENIED`. Windows has the same rule: the probe below shows `SetSecurityInfo()` on a handle without
`WRITE_OWNER` failing with 5. `GetNamedSecurityInfoW()` had the mirror gap: `get_security_object()` always requires
`READ_CONTROL` (`server/handle.c:749`), advapi32 requested it only for the owner, the group and the DACL
(`dlls/advapi32/security.c:2837`), so reading only the label failed with 5.

**Why only a prefix without the registration.** `AppContainerRegisterSid()` (`dlls/kernelbase/security.c:1810`, token series)
keeps the mapping in the prefix's `HKCU` hive and returns `HRESULT_FROM_WIN32( ERROR_ALREADY_EXISTS )` for a complete existing
mapping (line 1870). Chromium's `CreateProfile()` then returns the existing profile without touching the directory, so the
label call is never made (w8). The failing first start rolls the registration back and nothing persists, so every start in a
prefix without the registration repeats the failure until the label call works (w6 and then w7 on the same prefix). The lab
VM's live prefix already held the registration, which is why the VM runs never showed it.

**Chromium's persisted fallback.** `NetworkProcessLaunchWatcher::OnServiceCrashed()` (`chrome/browser/net/
system_network_context_manager.cc`) treats a network service that dies before its first IPC (`crashed_pre_ipc()`) as a failed
sandbox launch. It records the pref `network_service_failed_launch_major_version` (the Chromium major version) in `Local State`
and, with the feature `kRestartNetworkServiceUnsandboxedForFailedLaunch`, restarts the service without the sandbox.
`HasFailedPreviousRecentLaunch()` keeps it that way for every start of the same major version. Consequence for the harness: a
user data directory that a failing run has written keeps the network service unsandboxed for all of Chromium 156 even after
Wine is fixed, so every run needs a new, empty user data directory. The R6 harness already does this (its
`--user-data-dir` is `<output>\profile` below a per-run output directory); do not reuse a run directory after a failing run.

## Fix

Two conditions in `dlls/advapi32/security.c` (`1cc71ccd`), so a label is handled like the owner and the group:

```
SetNamedSecurityInfoW():
    if (SecurityInfo & (OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|LABEL_SECURITY_INFORMATION))
        access |= WRITE_OWNER;
GetNamedSecurityInfoW():
    if (info & (OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|LABEL_SECURITY_INFORMATION))
        access |= READ_CONTROL;
```

The server is unchanged. The new test `test_named_security_info_label` (`dlls/advapi32/tests/security.c`, called from the test
list) follows the results measured on Windows (probe table below). Recorded in `STOCK_CHANGES.md`.

## Validation

All Wine builds are WoW64 (`--enable-archs=i386,x86_64`); the 32-bit checks run the 32-bit program in the same build.

Checks of `test_named_security_info_label` (line numbers in `dlls/advapi32/tests/security.c`):

| Check | Without the fix | With the fix |
|---|---|---|
| `SetNamedSecurityInfoW( dir, DACL \| LABEL \| UNPROTECTED_DACL )`, as Chromium calls it (4203) | 5 | 0 |
| `SetNamedSecurityInfoW( dir, LABEL )` (4206) | 5 | 0 |
| `SetNamedSecurityInfoW( file, LABEL )` (4209) | 5 | 0 |
| `GetNamedSecurityInfoW( file, LABEL )` (4214) | 5 | 0 |
| `SetSecurityInfo()` on a handle with `WRITE_DAC` must fail with `ERROR_ACCESS_DENIED` | passes | passes |
| `SetSecurityInfo()` on a handle with `WRITE_OWNER` must succeed | passes | passes |

Whether the label comes back is `todo_wine` (4215): Wine's file objects do not store it. The ACE checks that follow are guarded
and skipped.

The advapi32 `security` unit in the same tree, `1cc71ccd` reverted and applied, then with `67317644` (failures of the unit's
summary line, run as root):

| Wine tree | x86_64 | i386 |
|---|---|---|
| `1cc71ccd` reverted | 12 | 20 |
| `1cc71ccd` | 8 | 16 |
| `1cc71ccd` and `67317644` | 8 | 8 |

The four label checks are the difference between the first two rows on both architectures. The eight further i386 failures in
the second row (lines 8908 to 8930, `test_NtCreateToken`: `status 0xc0000005`) are the wow64 crash below. The eight that
remain are the same in every row: lines 3676 and 3755 (`todo_wine` checks that succeed unexpectedly), 4005, 4014, 4024 and
4057 (files whose DACL should deny `CreateFile` open with error 0) and 6512 and 6524 (owner and token user in the default
DACL). I attribute them to running as root, the only user available in the lab WSL; a non-root run was not tried.
`test_admin_elevation` (8714, 8719) failed in one old, long-lived scratch prefix and passes in a new one.

Native comparison with `scripts/appcontainer-label-probe.c` (a non-elevated process on a native Windows host; "before" is the
clean upstream `66caeb46` build, the 64-bit and the 32-bit probe gave identical results on every column):

| Call | Windows host | Wine before | Wine with `1cc71ccd` |
|---|---|---|---|
| `GetNamedSecurityInfoW( dir, DACL )` | 0 | 0 | 0 |
| `SetEntriesInAclW` | 0 | 0 | 0 |
| `SetNamedSecurityInfoW( dir, DACL \| LABEL \| UNPROTECTED_DACL )` | 0 | 5 | 0 |
| read the label back from `dir` | 0, `S:AI(ML;OICI;NW;;;LW)` | 5 | 0, no label |
| `SetNamedSecurityInfoW( dir, LABEL )` | 0 | 5 | 0 |
| `SetNamedSecurityInfoW( file, LABEL )` | 0 | 5 | 0 |
| read the label back from `file` | 0, `S:AI(ML;;NW;;;LW)` | 5 | 0, no label |
| `SetNamedSecurityInfoW( dir, LABEL )` with the system integrity level | 1299 | 5 | 0 |
| `SetSecurityInfo( handle with WRITE_DAC, LABEL )` | 5 | 5 | 5 |
| `SetSecurityInfo( handle with WRITE_OWNER, LABEL )` | 0 | 0 | 0 |

The repaired calls now match Windows except for two differences that this change leaves alone (Open items): the label is not
stored, and a label above the caller's integrity level is accepted (Windows: 1299, `ERROR_INVALID_LABEL`).

Local Chromium runs: the table under Symptom. The w7 and w9 runs sit on a prefix without the registration and show the repair
end to end; w8 shows that the repaired build also works when the registration exists.

**wow64 `NtCreateToken()`** (`67317644`, `dlls/wow64/wow64_private.h`): `token_user_32to64()`, `token_owner_32to64()`,
`token_primary_group_32to64()` and `token_default_dacl_32to64()` now return NULL for a NULL input, as upstream `9f4469dbfb`
does. `wow64_NtCreateToken()` (`dlls/wow64/security.c`) passes all four, and the ntdll side treats only the default DACL as
optional (`dlls/ntdll/unix/security.c:41`), so a 32-bit caller's NULL default DACL was dereferenced and `NtCreateToken()`
failed with `STATUS_ACCESS_VIOLATION`. It was found because `test_NtCreateToken` failed on i386 only; Chromium (64-bit) does
not depend on it. The 32-bit probe and the i386 test run are the regression checks for the series.

**Review.** The local Qwen agent reviewed the three Wine commits (`2ebf5f72`, `1cc71ccd`, `67317644`) and found no bug. Its
claims were checked against the code and the tests like any other result.

## Reproduce

Test, in a WoW64 build tree `BUILD` with a scratch prefix `PREFIX` (not a long-lived one):

```
cd BUILD && make -j8 dlls/advapi32/all dlls/advapi32/tests/all
WINEPREFIX=PREFIX WINEDEBUG=-all LANG=C.UTF-8 BUILD/wine BUILD/dlls/advapi32/tests/x86_64-windows/advapi32_test.exe security
WINEPREFIX=PREFIX WINEDEBUG=-all LANG=C.UTF-8 BUILD/wine BUILD/dlls/advapi32/tests/i386-windows/advapi32_test.exe security
```

Look for failures between `security.c:4151` and `4260`: there must be none. Run as root, the unit also reports the eight
failures listed under Validation, with or without the fix. To see the bug, revert the two conditions above and run again.

Probe, on Wine and on a native Windows host (build commands in the file header):

```
x86_64-w64-mingw32-gcc -O2 -Wall -o appcontainer-label-probe64.exe scripts/appcontainer-label-probe.c -ladvapi32
i686-w64-mingw32-gcc   -O2 -Wall -o appcontainer-label-probe32.exe scripts/appcontainer-label-probe.c -ladvapi32
```

Chromium, after every Wine change in advapi32, kernelbase or the server, and always on a prefix without the registration (a
new scratch prefix for every run, created headless with `WINEDLLOVERRIDES="mscoree,mshtml="`):

1. Start Chromium with the sandbox on and a new, empty `--user-data-dir`, with
   `WINEDEBUG=-all,+timestamp,+pid,+tid,+winediag,+process,+security`.
2. `AppContainerRegisterSid` must return `hr=0x00000000` (a prefix that returns `0x800700b7` skips the label call and proves
   nothing about this bug), `CreateAppContainerToken` must appear, `AppContainerUnregisterSid` must not.
3. `chromium.log` must have no `Network service crashed or was terminated`, and `<user-data-dir>\Local State` must not contain
   `network_service_failed_launch_major_version`.
4. The process for `--utility-sub-type=network.mojom.NetworkService` must carry `--service-sandbox-type=network`.

## Open items

- Validated in local runs only. The lab VM's live prefix already held the registration (its runs log `0x800700b7`), so the VM
  never reached the label call; how the registration got there (an earlier run or the harness itself) was not traced. The
  whole chain (alignment, fonts, label) has not been run in the VM on a prefix without the registration. The local run
  directories (w6 to w9) were scratch and are not in the repository; the counts above are from them.
- Wine stores no label on files: after the repair `SetNamedSecurityInfoW()` succeeds and `GetNamedSecurityInfoW()` returns no
  label (the `todo_wine` at line 4215). The Low label on the profile directory therefore has no effect on access checks in
  Wine. Implementing it belongs to the file object code in the server and is not part of this change.
- Wine does not check the privilege needed to set a label above the caller's integrity level (Windows returns 1299). Not
  changed.
- The eight failures of the advapi32 `security` unit run as root remain; they are the same with and without these commits.
  A non-root run was not tried.
- The R6 harness needs a new user data directory per run and should check the signals in Reproduce, so that a profile left by
  a failing run cannot hide a repaired build (a run that skips the label call looks healthy on a prefix that has the
  registration).
- The unsandboxed relaunch in w6 is inferred (above). The Chromium source references are from snapshots newer than the pinned
  build, so function names are given and line numbers are not.
- Nothing is pushed. Rebase or merge of the Wine branch into the line you ship is your decision.
