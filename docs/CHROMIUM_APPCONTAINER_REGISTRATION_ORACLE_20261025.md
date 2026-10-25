<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# AppContainer registration: original Windows oracle and proposal

Completed the parent80fb32cf work order on branch
`codex/chromium-appcontainer-registration-oracle`, based on5823aed6. No VM,
SSH, browser, Wine run/build/patch, profile/token/ACL operation, privilege change
or R5 retry. Parent C branch/main D master/submodules/other workers untouched.

Original stdlib ctypes oracle defaults to help without DLL loading or mutation.
Explicit `--run --output <fresh canonical D: JSON>` generates two independent
`WexGraphicsOracle.` random identities and uses legitimate Derive package SIDs.
Both initial lookups must prove absence before registration; access denial or
unknown lookup failure aborts. No arbitrary SID/name mutation parameters.
Only successful fresh registrations become owned cleanup targets. Finally
unregisters those targets and confirms missing mappings, including on primary
failure. Derive memory uses FreeSid; converted SID strings use LocalFree;
successful lookup strings use AppContainerFreeMemory. Output ownership on
unexpected failed/non-NULL lookup is not guessed.

Explicit signatures follow Chromium's exact source revision
[7d982409 app_container_base.cc](https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/sandbox/win/src/app_container_base.cc),
declarations39–42 and wrappers51–79. Its CreateProfile handles already-exists.
Microsoft documents the Derive/FreeSid pairing in
[DeriveAppContainerSidFromAppContainerName](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-deriveappcontainersidfromappcontainername).
No Microsoft implementation disassembly or imported implementation was used.

## Actual native Windows observations

Windows10.0 build26300, AMD64,64-bit Python3.13; parent PID5792, separate native
Python child PID26488, child0/reaped. All four kernelbase exports were available.
The child inherited the ordinary environment and only derived/read both names;
its returned PID matches the owned Popen PID. This proves cross-process visibility
in this user's current session, not reboot persistence or cross-user isolation.

| Operation | Actual unsigned HRESULT | Observation |
| --- | --- | --- |
| Missing lookup, both fresh SIDs | 0x80070002 | NULL output |
| Fresh A registration | 0x00000000 | Lookup returns exact A moniker |
| Duplicate A, changed display text | 0x800700b7 | Subsequent moniker unchanged |
| B lookup before registration | 0x80070002 | A registration did not register B |
| Fresh B registration | 0x00000000 | A and B independently readable |
| Separate child lookup, both | 0x00000000 | Exact owned monikers, allocated/free pairs |
| Unregister A | 0x00000000 | A becomes missing; B remains readable |
| Lookup A / second unregister A | 0x80070002 | Missing lifecycle |
| Finally unregister B | 0x00000000 | Following lookup0x80070002/NULL |

LastError is recorded separately: both fresh successful registrations left122,
while their HRESULT was S_OK; it must not be treated as failure. Successful
lookup buffers: five parent/two child, all released by AppContainerFreeMemory.
Each process freed its two derived SIDs and two converted strings. Native exit0,
sequence_complete/cleanup_complete=true, both mappings confirmed absent; no
cleanup errors. Default help and mocked tests did not mutate registrations.

Actual command, once:

```text
C:/Users/Owner/AppData/Local/Programs/Python/Python313/python.exe -B scripts/g4_appcontainer_registration_oracle.py --run --output D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/appcontainer-registration-oracle-r1-20261025/native-result.json
```

Private evidence root is that output's directory: native JSON/stdout/stderr/exit,
Linux and Windows mocked-test logs, source snapshots, read-only pinned Wine
source references, audit/summary and13-file hash seal. Native-result SHA256
`121f7d21798fd107711767180e7445b7cd6e0301771c509bc08bc59c0038a114`.
No raw registry dump or proprietary payload committed.

`python3 -B -m unittest test_g4_appcontainer_registration_oracle -v`:
**17 PASS,0.012s,no skips** on WSL. Native Windows Python discovery of the same
module: **17 PASS,0.006s,no skips**. Tests use the actual flow/binder with fake
APIs: default no load, exact signatures/deallocators, collision/permission refusal,
failed registration not owned, error cleanup/primary preservation, child-read
persistence mismatch, timeout/owned kill/reap failures, unavailable exports and
fresh-output guards. Production DLL APIs are never invoked on non-Windows.
`git diff --check` passes. No unchanged consumer suite rerun.

## Proposed bounded Wine implementation — not implemented

- At pinned4315be83 `kernelbase.spec`40/42–44 are commented missing exports.
  Add real stdcall FreeMemory(ptr), LookupMoniker(ptr ptr), RegisterSid(ptr wstr
  wstr), UnregisterSid(ptr), with implementations in kernelbase/security.c and
  tests under kernelbase/tests. Keep Derive in the already reviewed userenv work.
- Persist a genuine per-user SID→moniker/display-name mapping using existing
  kernelbase registry APIs, not a process-only dictionary. A Wine-owned HKCU
  namespace is a proposed storage choice, not an observed Windows registry path.
  Pinned registry.c566 uses create_key/NtCreateKey with returned disposition;
  userenv_main.c41–64 demonstrates typed/size-checked WCHAR registry reads.
  Existing security.c uses GetProcessHeap allocation/free idioms.
- Serialize lookup/register/unregister with a cross-process, user-scoped lock;
  require an explicit cross-session/abandoned-lock policy. Under that lock,
  use atomic key creation disposition to distinguish duplicates, publish complete
  UTF16 values, and roll back only newly owned partial keys on write failure.
  Existing registrations must never be overwritten/deleted by failed registration.
  Test concurrent same-SID writers: one success, one already-exists, intact data.
- Lookup allocates an independent NUL-terminated UTF16 buffer; FreeMemory pairs
  with that allocator. Check registry type/length/terminator and allocation/error
  paths. Return genuine lookup/unregister failure for absent mappings, propagate
  access/I/O errors, and test persistence in a separate process. No success stub.
- This run covers valid matching package SIDs and ASCII monikers only. It does
  not establish NULL-input HRESULTs, non-NULL failure sentinel behavior, Unicode
  normalization/length limits, SID/moniker mismatch, duplicate display metadata
  changes, or concurrent/reboot behavior. Preserve UTF16 without an invented
  normalization policy; resolve these unobserved contracts before fixing their
  exact error behavior. No invalid-pointer probe was run.

Registration is identity/profile metadata, not token creation, capability ACL
enforcement or sandbox isolation. Existing userenv CreateAppContainerProfile
remains E_NOTIMPL at4315; do not hide that or infer full AppContainer security.
The R5 missing-export/breakpoint correlation is still not symbolized crash proof.
STOP parent review before production implementation or another browser trial.
