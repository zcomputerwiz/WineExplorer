<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Offline delay-import candidates after native Chromium R1

Status: **OFFLINE_STATIC_CANDIDATES_ONLY**. No VM/SSH/Wine/Windows PE/browser
execution, build, new preparation, payload change or production stub. R1 remains
preserved. Its0xc06d007f is consistent with Visual C++ delayed GetProcAddress
failure, following Microsoft's [delay-load exception documentation](https://learn.microsoft.com/en-us/cpp/build/reference/error-handling-and-notification?view=msvc-170).
The actual unresolved DLL/procedure is still unproven. Retained helper stderr/
stdout and Chromium log contain no explicit delayed-import failure name; the
WSALookupServiceBegin warning does not establish that cause.

Read-only cached156.0.8078.0 PE import tables compared against the current owned
`/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64` AMD64 exports:

| Cached image | Normal imports | Delayed imports |
|---|---:|---:|
| chrome.exe |254|116|
| chrome.dll |536|772|
| chrome_elf.dll |192|39|

All982 normal entries and923/927 delayed entries resolve to an export in this
static model. Four delayed entries lack a named export, representing three
distinct candidates:

| Candidate | Importing image | Static evidence |
|---|---|---|
| USERENV!DeriveAppContainerSidFromAppContainerName | chrome.exe and chrome.dll | Absent from owned userenv export table and pinned userenv.spec. |
| USER32!GetPointerDevice | chrome.dll | Absent export; pinned user32.spec:628 is a commented stub, not an export. |
| api-ms-win-power-setting-l1-1-0!PowerReadACValue | chrome.dll | Owned API-set maps to powrprof.dll; absent export. Pinned powrprof.spec exports PowerReadACValueIndex, a different name. |

Candidate presence does not show the call was reached. Export presence does not
show implementation quality (exports may be stubs). Static resolution includes
ordinals, recursive forwarders and the owned .apiset schema; pinned loader
HashedLength/final-revision matching and default/host-specific target rules were
used. No final unresolved mapping/forwarder/DLL or absent ordinal remains in
these tables. Runtime DLL search/loaded modules, dynamic API lookups and effective
security are not attested. This is an owned host-build reference, not whole-build
source or guest loaded-file proof.

Primary source context is exact Wine4315be83d75644f13500381dcec94dcdc5fa904e:
[API-set lookup/target](https://github.com/zcomputerwiz/wine/blob/4315be83d75644f13500381dcec94dcdc5fa904e/dlls/ntdll/loader.c#L685),
[user32.spec](https://github.com/zcomputerwiz/wine/blob/4315be83d75644f13500381dcec94dcdc5fa904e/dlls/user32/user32.spec#L628),
[userenv.spec](https://github.com/zcomputerwiz/wine/blob/4315be83d75644f13500381dcec94dcdc5fa904e/dlls/userenv/userenv.spec),
[powrprof.spec](https://github.com/zcomputerwiz/wine/blob/4315be83d75644f13500381dcec94dcdc5fa904e/dlls/powrprof/powrprof.spec#L25).
Each of the three missing export names was independently checked with installed
GNU objdump's `-p` data inspection, exit0. No target DLL was executed.

Private complete records under
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-delay-import-research-r1-20261024/`:
`inventory.json` (all imports/exports/API-set targets,69 input hashes, parser
warnings), `delay-candidates.json`, scripts, exact pinned source bytes/hashes,
three objdump outputs, original-log hash/search record and summary/seal.
Inventory SHA256`764f9ef9a88c5882d981b5f6dc252a1944d7fa4ca11b4ad086558870a0f340e2`;
candidate SHA256`124dcaea413f3d141c9033f9fc89bb19c505100996f2d2e4269c94ea6cdbbe15`.
The23-file seal retains the initial, superseded inventory too: its apparent
API-set/.drv/.cpl unknowns were corrected against pinned mapping/path rules,
not promoted to missing APIs. Final parser warnings concern large chrome.dll
section geometry and the schema DLL's zero entrypoint; retained, not suppressed.

Parser [pefile2024.8.26](https://pypi.org/project/pefile/2024.8.26/) is MIT;
wheel SHA76f8b485dcd3b1bb8166f1128d395fa3d87af26360c2358fb75b80019b957c6f
matches retained PyPI metadata. Host ensurepip was unavailable; the verified
wheel was extracted into the private D: venv site-packages, without OS package
changes. Reads capped512MiB/file,2GiB total,150s; final enumeration1.65s.
No vendor bytes committed. No unrelated unit reruns; static parser/objdump
checks and `git diff --check` passed.

Smallest next discriminator, for parent planning only: add `warn+module` to one
future bounded diagnostic's Wine logging. Pinned [LdrGetProcedureAddress](https://github.com/zcomputerwiz/wine/blob/4315be83d75644f13500381dcec94dcdc5fa904e/dlls/ntdll/loader.c#L2066)
emits the failed name/ordinal/module at2090 on the module channel declared at40;
this avoids full trace+module. Selective SEH may corroborate the exception.
R1's fixed output/profile now exists: a separately reviewed output-only native
contract override/fresh-path build would be needed, preserving API sequence,
normal arguments and existing evidence. No such code/preparation/run is made or
authorized here. Stop for parent review; no speculative API/security fix.
