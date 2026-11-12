<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1-R0B PackageFullNameFromId Native Observation Probe and Driver Suite

## Status
Status (2026-11-11): The probe compiles and executes cleanly on the Windows host
for both `x64` and `x86` using MSVC toolset v143 with static CRT (`/MT`) under
Windows SDK 10.0.26100.0. All 29 test cases per architecture pass native observation
capture and validate cleanly against the independent validator (`validate.ps1`).

Following Codex reviews in `docs/uwp/REVIEW_PACKAGE_NAME.md`:
1. **Asynchronous Bounded Execution & Bounded Tool Queries**: `build-run.ps1`
   executes child processes and compiler/linker banner queries with concurrent
   asynchronous pipe reads (`ReadToEndAsync`), timed process waits, bounded kill
   waits, and bounded output capture completion. Tool version queries require
   successful finite process wait and stream drain across both `stderr` and
   `stdout`, matching the known `cl` / `link` version banner patterns (rather
   than accepting option headers such as `C/C++ COMPILER OPTIONS`) and recording
   `TIMED_OUT`, `READ_TIMED_OUT`, or `MISSING` truthfully on failure. Any
   nonzero probe exit code, process timeout, unconfirmed kill, or missing/timed-out
   tool banner fails the run.
2. **Fail-Closed Output Destination Policy**: `build-run.ps1` enforces a strict
   fresh, direct child `out-*` policy for both explicit (`-OutDirectory`) and
   default destinations. It verifies no reparse points (symlinks/junctions) exist
   anywhere in the ancestor chain and never deletes or overwrites existing evidence.
3. **Value Initialization & Explicit NA for Missing Pointers**: Observation records
   are strictly value-initialized. Real parameter fields are measured directly at
   API calls. Tests with a missing length pointer (`null_length`) serialize
   `in_len` and `out_len` as explicit `"NA"` strings rather than uninitialized
   scalar numbers.
4. **Checked-Sentinel Semantics Distinct from Content Comparisons**: Sentinel flags
   are separated: `sentinel_checked` indicates whether a canary buffer was actually
   initialized and compared; `sentinel_intact` is true only when an initialized
   canary remains untouched. Result string comparisons against expected references
   are separate content assertions.
5. **Full OS UBR, Tool/SDK, Source & Artifact Hashes**: `build-run.ps1` records
   full OS build including UBR (`26300.9974`), compiler/linker banners, source
   file SHA256, build/run commands, and SHA256 hashes of all executables and
   record files in `manifest.json`.
6. **Independent Per-Case Validator & Comprehensive Mutation Fault-Checker**:
   - `validate.ps1` derives every case's expected `ret`, `in_len`, `out_len`,
     exact result string (`str`), reference `expected` string, `sentinel_checked`,
     and `sentinel_intact` independently from the case specification and literal
     captured native publisher constants. It never trusts `record.expected` or
     skips checks when `record.expected` is empty.
   - `validate.ps1` enforces real string/boolean/scalar types and `INT32`/`UINT32`
     bounds (`[0..4294967295]`).
   - `fault-check.ps1` verifies that `validate.ps1` accepts the clean capture and
     rejects all 11 faulted variants (`missing`, `truncated`, `duplicated`,
     `altered_ret`, `empty_success_str`, `altered_expected_and_actual`,
     `wrong_success_out_len`, `short_capacity`, `excess_capacity`, `wrong_type`,
     and `altered_param_na`) while preserving input evidence and prior fault trees
     byte-for-byte.

## Purpose and Scope
`package_name_probe.cpp` is a standalone Win32 C++ observation probe for the Windows
Application Model API `PackageFullNameFromId` exported by `kernelbase.dll` / `kernel32.dll`.
It queries native behavior without host Python, registry modifications, certificates,
or package installations.

### Test Matrix (29 Cases per Architecture)

| Category | Test Case Name | Target API Condition | Expected Ret | In Len | Out Len | Canaries Checked | Expected String |
|---|---|---|---|---|---|---|---|
| `buffer_capacity` | `null_buffer_len_0` | `buffer = NULL`, `len = 0` | 122 | `0` | `42` | No | `""` |
| `buffer_capacity` | `sentinel_len_0` | Non-null buffer (sentinels), `len = 0` | 122 | `0` | `42` | Yes | `""` |
| `buffer_capacity` | `sentinel_len_1` | Non-null buffer (sentinels), `len = 1` | 122 | `1` | `42` | Yes | `""` |
| `buffer_capacity` | `sentinel_len_req_minus_1` | Non-null buffer (sentinels), `len = 41` | 122 | `41` | `42` | Yes | `""` |
| `buffer_capacity` | `exact_capacity` | Non-null buffer (sentinels), `len = 42` | 0 | `42` | `42` | Yes | `TestPackage_1.2.3.4_x86_res_1234567890abc` |
| `buffer_capacity` | `extra_capacity` | Non-null buffer (sentinels), `len = 52` | 0 | `52` | `42` | Yes | `TestPackage_1.2.3.4_x86_res_1234567890abc` |
| `resource_id` | `resource_id_null` | `resourceId = nullptr` | 0 | `39` | `39` | No | `TestPackage_1.0.0.0_x86__1234567890abc` |
| `resource_id` | `resource_id_empty` | `resourceId = L""` | 0 | `39` | `39` | No | `TestPackage_1.0.0.0_x86__1234567890abc` |
| `resource_id` | `resource_id_nonempty_res` | `resourceId = L"res"` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x86_res_1234567890abc` |
| `resource_id` | `resource_id_nonempty_resourceid` | `resourceId = L"resourceid"` | 0 | `49` | `49` | No | `TestPackage_1.0.0.0_x86_resourceid_1234567890abc` |
| `publisher_derivation` | `derive_publisher` | `publisher = L"publisher"` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x86_res_0mk95qz5wh294` |
| `publisher_derivation` | `derive_CN=Test` | `publisher = L"CN=Test"` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x86_res_hz258y3tkez3a` |
| `publisher_derivation` | `derive_CN=Microsoft Corporation...` | `publisher = L"CN=Microsoft Corporation..."` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x86_res_8wekyb3d8bbwe` |
| `publisher_derivation` | `derive_AppX Test Publisher` | `publisher = L"AppX Test Publisher"` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x86_res_q1jy3mp9kw2c8` |
| `publisher_derivation` | `derive_MyPublisher123` | `publisher = L"MyPublisher123"` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x86_res_j9x1bp7cpjcpy` |
| `publisher_id` | `explicit_13_chars` | `publisherId = L"1234567890abc"` (13 chars) | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x86_res_1234567890abc` |
| `publisher_id` | `explicit_12_chars` | `publisherId = L"1234567890ab"` (12 chars) | 87 | `0` | `0` | No | `""` |
| `publisher_id` | `explicit_14_chars` | `publisherId = L"1234567890abcd"` (14 chars) | 87 | `0` | `0` | No | `""` |
| `publisher_id` | `both_publisher_and_id_null` | `publisher = NULL`, `publisherId = NULL` | 87 | `0` | `0` | No | `""` |
| `architecture` | `arch_intel` | `PROCESSOR_ARCHITECTURE_INTEL` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x86_res_1234567890abc` |
| `architecture` | `arch_amd64` | `PROCESSOR_ARCHITECTURE_AMD64` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_x64_res_1234567890abc` |
| `architecture` | `arch_arm` | `PROCESSOR_ARCHITECTURE_ARM` | 0 | `42` | `42` | No | `TestPackage_1.0.0.0_arm_res_1234567890abc` |
| `architecture` | `arch_arm64` | `PROCESSOR_ARCHITECTURE_ARM64` | 0 | `44` | `44` | No | `TestPackage_1.0.0.0_arm64_res_1234567890abc` |
| `architecture` | `arch_neutral` | `PROCESSOR_ARCHITECTURE_NEUTRAL` | 0 | `46` | `46` | No | `TestPackage_1.0.0.0_neutral_res_1234567890abc` |
| `architecture` | `arch_invalid` | `processorArchitecture = 9999` | 87 | `0` | `0` | No | `""` |
| `parameters` | `null_id` | `packageId = nullptr`, `&len` (`0`) | 87 | `0` | `0` | No | `""` |
| `parameters` | `null_length` | `packageId = &id`, `len = nullptr` | 87 | `"NA"` | `"NA"` | No | `""` |
| `parameters` | `null_name` | `id.name = nullptr`, `&len` (`0`) | 87 | `0` | `0` | No | `""` |
| `parameters` | `empty_name` | `id.name = L""`, `&len` (`0`) | 87 | `0` | `0` | No | `""` |

## Files
* `package_name_probe.cpp`: C++ observation probe testing `PackageFullNameFromId`.
* `build-run.ps1`: Build and execution driver compiling x64 and x86 binaries with `/MT` and executing with bounded async pipe reads and timeouts.
* `validate.ps1`: Independent validator verifying JSONL records, strict string/scalar/bool types, `INT32`/`UINT32` bounds, completeness, checked-sentinel semantics, and per-case conformance.
* `fault-check.ps1`: Independent fault-checker exercising `validate.ps1` against clean and 11 faulted synthetic variants without deleting prior fault trees or mutating input evidence.
* `sentinel-check.ps1`: Comprehensive driver refusal and path preservation tester (preview/refusal only).
* `.gitignore`: Excludes `out/`, `out-*/`, and `sentinel-*/` directories.

## Commands

Run from this directory (or directly in an active PowerShell session):

```powershell
# Preview build and run commands
& .\build-run.ps1

# Build and execute into a fresh unique output directory
& .\build-run.ps1 -Execute

# Validate captured records
& .\validate.ps1 .\out-20261111-154654-33504\records_x64.jsonl .\out-20261111-154654-33504\records_x86.jsonl

# Fault-check validator (1 clean + 11 mutation fault variants)
& .\fault-check.ps1 -Directory .\out-20261111-154654-33504

# Run driver refusal and preservation checks
& .\sentinel-check.ps1
```

## Fresh Verified Evidence (`out-20261111-154654-33504`)
* **Directory**: `tests/uwp/package-name/out-20261111-154654-33504`
* **OS**: Microsoft Windows 11 Pro, version `10.0.26300`, build `26300`, UBR `9974` (`26300.9974`, `26H2`)
* **Source SHA256**: `32AE81161F730AA3D7E3BC2CB54FCAA9CF11ED14CC50972081C15DE7A5758DA9`
* **x64 Tool Versions**: `cl` = `Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35228 for x64`, `link` = `Microsoft (R) Incremental Linker Version 14.44.35228.0`
* **x64 Executable**: `build\x64\package_name_probe.exe` (`D5A9897E6D42819D271109AD02D1F70F0007EEF32B26C4AE20781C6DEE5C5351`)
* **x64 Records**: `records_x64.jsonl` (`084895BE76DC6B260532F4793B96485DF6A628B3CFD42DAB27D85B2298E0B912`)
* **x86 Tool Versions**: `cl` = `Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35228 for x86`, `link` = `Microsoft (R) Incremental Linker Version 14.44.35228.0`
* **x86 Executable**: `build\x86\package_name_probe.exe` (`F2FD037791D7653497CE9E4457FAE113E8EDA136F30D1EEE39B85AEFF412B1D9`)
* **x86 Records**: `records_x86.jsonl` (`42368FDBBD805FC8069F2AA3C0EC337B870F888C6191E424B90DCE3B4C39DB4F`)
* **Validation Result**: PASS (58/58 records across both architectures valid, unique, well-formed, and independently conformant; record hashes match prior verified captures `out-20261111-115621-14216` and `out-20261111-154345-31960`).
* **Counterexample Verification**: Running `validate.ps1` against the retained counterexample in `unsloth-tmp/uwp-u1-20261111/package-validator-review-20261111-152821-14884` exits `1` (`INVALID: 4 violation(s)` on `resource_id|resource_id_null`).

## Historical Evidence Preserved
Prior capture directories (`out-20261111-111906-26876`, `out-20261111-112140-12868`, `out-20261111-112543-31240`, `out-20261111-115621-14216`, `out-20261111-154345-31960`) remain preserved unmodified.

## Limitations
* **Native Observation Only**: This suite tests and documents native Windows 11 host behavior (`PackageFullNameFromId`). Live Wine runtime tests are owned by Codex.
