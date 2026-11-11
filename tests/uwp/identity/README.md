<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1-M2 unpackaged package identity observation probe

Status (2026-11-11): the probe builds and runs on the Windows host for both
`x64` and `x86`; every case is observed as unpackaged and the captured records
validate. This is **test preparation** for the U1 track: it records how the
plain Win32 package-identity ABI reports an unpackaged process, through every
self-owned handle form. It is **not** a Wine implementation, **not** a packaged
acceptance, and it makes **no** Wine claim. The Wine-side assertions below are
prospective and remain NOT-RUN.

Updated 2026-11-11 (U1-M2R): the build/run and fault-check drivers are now
fail-closed. They create fresh owned output directories, never delete
preexisting state, refuse root/source/project/foreign/reparse destinations,
validate the run bound, and bound every wait. The captured `out/` evidence is
preserved and hash-verified (see "Output safety" and "Hashes").

Updated 2026-11-11 (U1-M2R2): the output destination is now enforced to be a
fresh direct `out-*` child of this test directory, so the local `.gitignore`
always covers it; nested or non-`out-*` destinations are refused. The
sentinel checks no longer delete the fresh `fault-*` diagnostic child created
by a fault-check run (it is left in place, gitignored under `out/`); they
delete only the exact disposable paths they created themselves, never
inferring ownership from a path appearing since a snapshot. The .NET process
helpers run with `CreateNoWindow`.

## Purpose and scope

`identity_probe.cpp` is an original plain Win32/C++ ABI console probe (no
C++/WinRT, no projection headers). It queries the package identity of **this**
process only, through:

| # | API | handle form | status domain |
|---|-----|-------------|---------------|
| 1 | `GetCurrentPackageFullName` | current | Win32 LONG |
| 2 | `GetCurrentPackageFamilyName` | current | Win32 LONG |
| 3 | `GetCurrentPackagePath` | current | Win32 LONG |
| 4 | `GetPackageFullName` | process pseudo-handle | Win32 LONG |
| 5 | `GetPackageFamilyName` | process pseudo-handle | Win32 LONG |
| 6 | `GetPackageFullName` | opened self-process handle | Win32 LONG |
| 7 | `GetPackageFamilyName` | opened self-process handle | Win32 LONG |
| 8 | `GetPackageFullNameFromToken` | opened current process token | Win32 LONG |
| 9 | `GetPackageFamilyNameFromToken` | opened current process token | Win32 LONG |
| 10 | `RtlQueryPackageIdentity` | opened current process token | NTSTATUS |

It uses valid, bounded buffers and the two-call length-then-value pattern. It
never targets unrelated processes or tokens, never performs invalid-pointer or
crash experiments, and prints no package name, path, token credential or user
identifier. If the process unexpectedly has package identity it reports the
context (API + handle form) and stops without printing the identity.

## Files

* `identity_probe.cpp` - the probe (GPL-2.0-or-later).
* `build-run.ps1` - preview-default build/run driver; `-Execute` builds x64 and
  x86 with the static CRT in a fresh gitignored output directory (a unique
  `out-<timestamp>-<pid>` by default, or a fresh direct `out-*` child named by
  `-OutDirectory`) and runs only these probes, each with a bounded wait,
  capturing exit, raw records and hashes. Fail-closed: it never deletes,
  refuses preexisting output and root/source/project/foreign/nested/non-`out-*`
  /reparse destinations, and validates the run bound (see "Output safety").
* `validate.ps1` - results validator (see below).
* `fault-check.ps1` - fault-checks the validator against truncated, duplicate
  and altered synthetic copies of the real captured records. It writes to a
  fresh unique `fault-<timestamp>-<pid>` child, never deletes a preexisting
  fault tree or the input records, and refuses foreign/reparse input
  directories.
* `sentinel-check.ps1` - sentinel preservation/refusal checks for the two
  drivers (existing output, source/root/foreign/nested/non-`out-*`
  destinations, reparse points, preexisting fault tree, run bounds);
  preview/refusal paths only, disposable owned copies only.
* `.gitignore` - ignores the generated `out/`, `out-*/` and `sentinel-*/` trees.
* `out/` (generated, ignored) - the captured evidence:
  `build/<arch>/identity_probe.exe`, `records_<arch>.jsonl`,
  `stderr_<arch>.txt`, `build_<arch>.log`, `manifest.json`, and `fault/`
  (validator fault-check copies). Preserved and hash-verified (see "Hashes").
* `out-*/` (generated, ignored) - fresh per-run output directories created by
  `build-run.ps1 -Execute`.

## Record schema

One JSON object per line (JSONL) on stdout. Fields:

* `schema` - `wex-uwp-identity/1`.
* `arch` - `x64` or `x86`.
* `api` - the queried API name.
* `handle` - `current`, `process_pseudo`, `process_opened` or `token_opened`.
* `status_domain` - `win32` (Win32 LONG) or `ntstatus` (NTSTATUS); the two are
  never conflated.
* `status_dec` / `status_hex` - the observed status in decimal and `0x` hex.
* `in_len` / `out_len` - buffer size used in the value call, and the length
  reported by the API (both 0 when the query failed before a value call).
* `result` - `no_package`, `unexpected_packaged` or `not_run`.
* `reason` - empty, or for `not_run`: `declaration_unverified`,
  `export_missing`, `handle_open_failed`, `insufficient_buffer` or
  `unobserved_status`.

## Commands

Run from this directory. Preview first (no `-Execute`), then execute.
`-Execute` creates a fresh unique `out-<timestamp>-<pid>/` by default; pass
`-OutDirectory` to name a fresh directory (it must not already exist, must be
a direct child of this test directory, and must be named `out-*` so the local
`.gitignore` covers it):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-run.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-run.ps1 -Execute
```

Validate the captured records (both architectures):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\validate.ps1 .\out\records_x64.jsonl .\out\records_x86.jsonl
```

Fault-check the validator against synthetic copies of the real records (writes
to a fresh unique `out\fault-<timestamp>-<pid>\`, never touching the captured
`out\fault\`):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\fault-check.ps1 -Directory .\out
```

Run the sentinel preservation/refusal checks (preview/refusal paths only):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\sentinel-check.ps1
```

The driver links `kernel32.lib advapi32.lib windowsapp.lib` (the `*FromToken`
imports are provided by `windowsapp.lib`, matching `tests/uwp/u1_0`). It
performs no trust-store, certificate, package-installation or policy change and
touches nothing outside its own fresh output directory.

## Toolchain (actual, recorded by the driver)

* **OS**: Microsoft Windows 11 Pro, version `10.0.26300`, build `26300`.
* **Compiler**: `Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35228`
  for x64 and for x86 (`cl.exe`, VS 2022 Build Tools, toolset v143).
* **Linker**: `Microsoft (R) Incremental Linker Version 14.44.35228.0`.
* **SDK**: `10.0.26100.0`
  (`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0`).
* **CRT**: static (`/MT`), `/std:c++17 /EHsc`.
* **vcvarsall**: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat`.

A non-fatal `'vswhere.exe' is not recognized` line appears in each build log;
`vcvarsall.bat` continues and the environment initializes correctly, and both
builds exit 0.

## Observed results (Windows 11, actual)

Both `x64` and `x86` produced 10 records each, exit 0, no timeout, empty
stderr. Every Win32 case was observed as unpackaged with the same status; the
two architectures are identical in their observations.

| API | handle | domain | status_dec | status_hex | result |
|-----|--------|--------|-----------|------------|--------|
| `GetCurrentPackageFullName` | current | win32 | 15700 | 0x00003D54 | no_package |
| `GetCurrentPackageFamilyName` | current | win32 | 15700 | 0x00003D54 | no_package |
| `GetCurrentPackagePath` | current | win32 | 15700 | 0x00003D54 | no_package |
| `GetPackageFullName` | process_pseudo | win32 | 15700 | 0x00003D54 | no_package |
| `GetPackageFamilyName` | process_pseudo | win32 | 15700 | 0x00003D54 | no_package |
| `GetPackageFullName` | process_opened | win32 | 15700 | 0x00003D54 | no_package |
| `GetPackageFamilyName` | process_opened | win32 | 15700 | 0x00003D54 | no_package |
| `GetPackageFullNameFromToken` | token_opened | win32 | 15700 | 0x00003D54 | no_package |
| `GetPackageFamilyNameFromToken` | token_opened | win32 | 15700 | 0x00003D54 | no_package |
| `RtlQueryPackageIdentity` | token_opened | ntstatus | (not run) | (not run) | not_run |

`15700` is `APPMODEL_ERROR_NO_PACKAGE` (`0x3D54`). The opened self-process
handle (`OpenProcess` with `PROCESS_QUERY_INFORMATION |
PROCESS_QUERY_LIMITED_INFORMATION`) and the opened current process token
(`OpenProcessToken` with `TOKEN_QUERY`) both opened successfully, so cases 6-9
ran rather than recording a handle-open error.

### Hashes (SHA-256)

The full captured `out/` evidence set (19 files), verified byte-for-byte
unchanged by the U1-M2R repair:

| artifact | SHA-256 |
|----------|---------|
| `out/build/x64/identity_probe.exe` | `915E1B95FBE3860CCF386E16D5D42EB65B7113C05F4E11B41674FFBA9DE5D0CE` |
| `out/build/x64/identity_probe.obj` | `078C06A982E11ECE01A15FD0A6FD1BBAD84FC08B091117154F26FD2FCAF0247B` |
| `out/records_x64.jsonl` | `BA28AB85616067397E489643455733DFE7BD28BFC875E7E081160D0A686CD17A` |
| `out/stderr_x64.txt` | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `out/build_x64.log` | `B8AB4E5A749B3CAFFA3DB85B9FA1A12F6A0F4CDAAB9BE886F22D2BFE95177829` |
| `out/build/x86/identity_probe.exe` | `E5886423BE0E3F1AAED52B690E446ED05FAC4FF5CB20334ACEE4A9D1646DFE0D` |
| `out/build/x86/identity_probe.obj` | `18BDED4D256F3453DF51EAA2FA9231779B969171902039780B060589A977E00F` |
| `out/records_x86.jsonl` | `CD12A068A70B36159C5DE70D60E20C24A3791ABEC7134A6DF5D8257F3B2FB89B` |
| `out/stderr_x86.txt` | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `out/build_x86.log` | `C9A38C3192E9749441529F719A9BE944B5DB13546BEB27C83D1338CF63DA79BB` |
| `out/manifest.json` | `B9E08C9468C1525F6648C243A5611ED2E757E90FBEC9EF15976566F5EDEF1C62` |
| `out/fault/clean/records_x64.jsonl` | `BA28AB85616067397E489643455733DFE7BD28BFC875E7E081160D0A686CD17A` |
| `out/fault/clean/records_x86.jsonl` | `CD12A068A70B36159C5DE70D60E20C24A3791ABEC7134A6DF5D8257F3B2FB89B` |
| `out/fault/truncated/records_x64.jsonl` | `8CBDC4B373FE332CDB06AA46DD203FAA308DC93648F64111328DDD963192A500` |
| `out/fault/truncated/records_x86.jsonl` | `CD12A068A70B36159C5DE70D60E20C24A3791ABEC7134A6DF5D8257F3B2FB89B` |
| `out/fault/duplicated/records_x64.jsonl` | `2DA248B7F39DC2992E1CAF7D41622B1572EAABC67191059E6959D0F386F980BB` |
| `out/fault/duplicated/records_x86.jsonl` | `CD12A068A70B36159C5DE70D60E20C24A3791ABEC7134A6DF5D8257F3B2FB89B` |
| `out/fault/altered/records_x64.jsonl` | `1EC1705167DD316AEF6FF934CAFDB12A7CB947C3EA9DF12F7A980DA1647C41C1` |
| `out/fault/altered/records_x86.jsonl` | `CD12A068A70B36159C5DE70D60E20C24A3791ABEC7134A6DF5D8257F3B2FB89B` |

A fresh `build-run.ps1 -Execute` (U1-M2R) rebuilt both probes and re-ran them;
the resulting `records_<arch>.jsonl` are byte-identical to the captured
evidence above (same SHA-256), confirming the observations are deterministic.
The rebuilt executables differ in hash (MSVC build metadata is not
bit-reproducible); the records are the evidence of record.

## Validation results

* `validate.ps1` on `records_x64.jsonl` + `records_x86.jsonl`: **VALID** (20
  records, both architectures; all present, unique, well-formed and
  consistently unpackaged), exit 0.
* `fault-check.ps1 -Directory out`: **PASS** (writes to a fresh unique
  `out\fault-<timestamp>-<pid>\`; the captured `out\fault\` tree and the input
  records are left byte-for-byte unchanged) -
  * clean copy: accepted (exit 0);
  * truncated copy (last x64 line cut mid-record): rejected (malformed JSON and
    the omitted `RtlQueryPackageIdentity|token_opened` case);
  * duplicated copy (one x64 record appended again): rejected (duplicate
    `x64|GetCurrentPackageFullName|current`);
  * altered copy (first x64 `status_dec` changed by one): rejected
    (`status_hex` inconsistent with `status_dec`, and status != expected).

The validator rejects malformed JSON, missing/wrong-typed fields, an
unsupported schema, a missing or unexpected architecture, duplicate
(arch, API, handle) records, omitted or extra cases, and inconsistent
unpackaged observations (any `unexpected_packaged`, a Win32 case not observed
as `APPMODEL_ERROR_NO_PACKAGE`, or the Rtl case not observed as
`STATUS_NOT_FOUND` or an explicit NOT-RUN). The expected unpackaged values are
parameters (default `15700` / `-1073741275`) so an observed discrepancy is
surfaced, not absorbed.

## Output safety (U1-M2R)

The build/run and fault-check drivers are fail-closed and never delete
preexisting state:

* **Fresh owned output only.** `build-run.ps1 -Execute` creates a fresh unique
  `out-<timestamp>-<pid>/` by default, or a fresh direct `out-*` child named by
  `-OutDirectory`. It never deletes or reuses an existing directory: a
  preexisting destination is refused (exit 3), so the captured `out/` evidence
  and any caller data are left untouched.
* **Destination policy.** The destination must be a fresh direct child of this
  test directory named `out-*`, so the local `.gitignore` (`out-*/`) always
  covers it. Filesystem roots, the source directory, the project (repo) root,
  foreign (outside-repo) destinations, nested destinations and non-`out-*`
  names are refused, as are reparse points (symlinks/junctions) in the
  destination or any ancestor.
* **Bounded waits.** `-TimeoutSeconds` must be a positive integer of at most
  300 (otherwise exit 4). The probe run, the post-kill wait and the output
  reads are all bounded; a probe that cannot be killed or whose output cannot
  be read is a failed run, never a hang. The build is bounded as well.
* **Fault-check is read-only on its input.** `fault-check.ps1` copies the
  captured records into a fresh unique `fault-<timestamp>-<pid>/` child; it
  never moves or deletes the input records (verified by hash) and never touches
  a preexisting fault tree such as the captured `out\fault\`. It refuses
  foreign/reparse input directories (exit 2).
* **Sentinel checks.** `sentinel-check.ps1` exercises the preview/refusal paths
  only (no build, no probe run, no deletion of preexisting data) and verifies:
  existing output is refused with its sentinel file intact; the source
  directory, project root, filesystem root and foreign destinations are
  refused; a junction destination is refused with its target intact; a nested
  destination and a non-`out-*` direct child are refused; invalid run bounds
  are rejected; the default preview creates nothing; a junction and a foreign
  fault-check input are refused; a directory without the record files is
  refused; and a fault-check on the captured `out/` passes while leaving the
  preexisting `out\fault\` tree byte-for-byte preserved. The fresh
  `fault-<timestamp>-<pid>` diagnostic child created by that run is left in
  place (gitignored under `out/`); the script deletes only the exact
  disposable paths it created itself, never inferring ownership from a path
  appearing since a snapshot. All 15 checks pass.

## NOT-RUN and limitations

* **`RtlQueryPackageIdentity` is NOT-RUN** on the Windows host, explicitly, not
  a silent pass. The installed Windows SDK `10.0.26100.0` does not declare it
  (there is no `ntintsafe.h`, and a search of every installed SDK header on
  2026-11-11 found no prototype), so there is no local declaration to verify
  against and no call is made; the probe records
  `result: not_run, reason: declaration_unverified`. The probe carries a
  compile-time flag that is **false**; it does **not** enable a call (there is
  no implementation behind it) and no `PackageIdentityInfo` ABI is assumed. A
  real call is a separate future order; for reference, the Wine ntdll
  declaration it would have to match is
  `RtlQueryPackageIdentity(token, fullname, fullname_size, appid, appid_size,
  packaged)` returning `NTSTATUS` (`wine/include/winternl.h`,
  `wine/dlls/ntdll/rtl.c`).
* **No Wine run.** This order is Windows-host test preparation only. No Wine
  build, prefix or runtime was used, and no Wine behavior is claimed.
* **No packaged case.** The process is unpackaged; the packaged positive path
  (and the "unexpectedly packaged" stop) was not exercised.
* **Unobserved error paths are not asserted.** The probe records whatever
  status it observes; it does not invent expectations for error paths that did
  not occur (e.g. handle-open failure, insufficient buffer).
* **Host-specific.** The observations are from one Windows 11 host and one
  SDK/MSVC toolchain; they are a versioned comparison point, not a universal
  contract.

## Observed Windows values vs prospective Wine assertions

These are separate and must not be conflated.

**Observed on Windows 11 (actual, this run):** every Win32 package-identity
query of an unpackaged process - current, explicit self (pseudo and opened
handle), and opened current process token - returns `APPMODEL_ERROR_NO_PACKAGE`
(`15700` / `0x3D54`). `RtlQueryPackageIdentity` was not run (unverified local
declaration).

**Prospective Wine assertions (NOT-RUN, to be checked later):** Wine is
expected to return the same `APPMODEL_ERROR_NO_PACKAGE` from the current-package
queries and `STATUS_NOT_FOUND` (`0xC0000225`) from `RtlQueryPackageIdentity`
for an unpackaged process (per the current source noted in
`docs/uwp/EXECUTION_PLAN.md`). This has **not** been verified against a Wine
build in this order; it is a target for the later Wine baseline work, to be
confirmed with the same probe bits and recorded with full build/prefix/hash
evidence.
