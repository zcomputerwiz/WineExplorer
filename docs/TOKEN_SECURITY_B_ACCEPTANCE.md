<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Token Security Checkpoint B Acceptance Report

**Date:** 2026-10-22  
**Work orders:** `docs/TOKEN_SECURITY_CHECKPOINT_B_WORKORDER.md`, `docs/TOKEN_SECURITY_B_FIX_WORKORDER.md`  
**Supersedes:** the 2026-10-18 report for `gemini/token-security-b` at `77089c00`. Its PASS statuses are author
claims, not evidence: six of its nine source hashes did not match the pushed files, B2 had no authorization
implementation behind it, and its B4 "fix" was in the wrong program (see section 3).  
**Test environment:** Ubuntu 24.04.5 LTS, Linux 6.18.44-fc-v140 x86_64, 4 cores, container (no VM, no
installed image), gcc 13.3.0, MinGW-w64 GCC 13-win32, owned Xvfb display per run, tests run as the unprivileged
user `wextest`, B3 fixture run as root in `/tmp`.

---

## 1. Revisions

| Item | Value |
| :--- | :--- |
| Wine base (`codex/token-security-correctness`) | `ae5fec1e11c6ae42e2ba73a6e9b6368d40a34d92` |
| Gemini Wine submission | `404d23f1c4c21937ed3314955a70bc1da2200310` (`gemini/token-security-b`) |
| Wine at this report | `c01c57ef7bee041af75ab4c233b9712a0a03567d` (`ccr-08223ee4-p830bf`) |
| Parent gitlink at this report | `c01c57ef7bee041af75ab4c233b9712a0a03567d` |
| Parent submission | `77089c00d8b3641eac4ae67f4e5ff016e1e1712e` (`gemini/token-security-b`) |

The Wine commits on top of the submission are `shell32` (`d03f9b04`), `kernelbase` (`21777b96`), `ntdll`
(`09c26b23`), `explorer` (`cdf641d1`) and `conhost` (`c01c57ef`).

The Wine tree was assembled with `scripts/assemble.sh` and configured with
`configure --enable-archs=i386,x86_64` (WoW64). The assembly does not touch the files hashed below: the hashes
of the assembled build tree equal the hashes of the pushed blobs.

---

## 2. Gate matrix

| Gate | Status | Evidence |
| :--- | :--- | :--- |
| A1 Single-source assembly, WoW64 build | **PASS** | Clean build of both Gemini's `404d23f1` and `c01c57ef`; section 4.1 |
| A2 Focused token probe, both architectures | **PASS** | section 4.2; also PASS on the unmodified submission |
| A3 Non-focused upstream baseline | **PARTIAL** | `advapi32 security` 3691 tests, 0 failures; `ntdll info` 2687 tests, 0 failures; `shell32 shlexec` 1803 tests with 2 failures that also occur on the unmodified submission and on stock Wine `master`, so they are inherited; section 4.3 |
| A4 Runner sentinel isolation | **PASS** | success, failure, timeout, signal; section 4.4 |
| A5 Disjoint restriction and allocation failure | **PARTIAL** | Wine behaviour tested (`GroupCount == 0`, still restricted, access denied); no native Windows control run, so the "native contract" is unverified |
| B1 Authorization and identity design | **PARTIAL** | Design written in `ELEVATION.md`; nothing implemented |
| B2 Real authorization | **PARTIAL** | Helper configuration audit implemented and tested for ten registry states on two architectures (section 4.5). Not implemented: polkit/D-Bus mechanism, request records, valid and invalid administrator authentication, cancel and timeout cases, concurrent A/B, Unicode and quoted argument cases |
| B3 Machine writes, migration, rollback | **PARTIAL** | Unix-tier capture, rollback, tamper refusal, failure handling and path validation pass in a disposable fixture (section 4.6). NOT RUN: ordinary non-elevated Win32 write denial, approved installer, second user after restart, wineboot/save/merge |
| B4 Fresh-prefix diagnostic | **PASS** | The division by zero was in `conhost.exe`, not explorer; fixed there; reproduced on the unfixed build and absent on the fixed build (section 4.7) |
| B4 installed desktop (X11 and Wayland, ISO, restored VM) | **NOT RUN** | needs an installed image |
| Checkpoint C (Chromium and Edge sandbox) | **NOT RUN** | gated on B |

---

## 3. What changed since the submission

| ID | Finding | Resolution |
| :--- | :--- | :--- |
| F1 | Reported source hashes did not match the pushed files | Hashes below are taken from the pushed blobs and the same build tree (4.8) |
| F2 | Gitlink pin and environment were misreported | Section 1 |
| F3 | B2 marked PASS without an authorization implementation | B2 is PARTIAL |
| F4 | A5 claimed a native contract without a native control | A5 is PARTIAL |
| F5 | `SHELL_ExecuteW` returned `SE_ERR_NOASSOC` instead of `ERROR_BAD_FORMAT` for `ERROR_ELEVATION_REQUIRED` with the helper unset | Fixed in `d03f9b04`. Note: this path is not reachable through the public API, because with the helper unset `kernelbase` never reports `ERROR_ELEVATION_REQUIRED`, so no test can fail on the old code; the change only restores the pre-submission result |
| F6 | `kernelbase` parsed every target manifest before reading the helper | Helper is read first; unset returns early (`21777b96`). Results are unchanged for all ten tested states |
| F7 | Helper paths were untested outside shell32 `runas`, and three parsers disagreed | `ntdll` reduced to an unset test (`09c26b23`); ten states tested through `CreateProcessW` and `ShellExecuteExW` (4.5). Not covered: a key whose read is denied by its DACL (Wine does not enforce key DACLs for reads); the `ntdll` invalid-helper branch has no independent public entry point because `kernelbase` refuses first |
| F8 | Metadata was captured after the first `chown` | Capture first; the old script left the root directory owned by the machine group, which the new test and a manual run of the old script confirm |
| F9 | Backups sat in the group-writable tree and root rollback trusted them | Root-only backup directory outside the machine root; rollback refuses non-canonical or outside paths; tamper test passes |
| F10 | Write-denial test passed whenever `su nobody` failed | Positive controls, exact `Permission denied` match; the Unix tier is reported separately from Windows enforcement |
| F11 | Weak path validation | Canonical absolute path required; system directories rejected |
| F12 | Promotion hid `cp` failures before deleting the source | Failures abort before any deletion |
| F13 | B4 test passed when explorer exited early | Rewritten; fails on early exit, missing probe result and unhandled exceptions. It does not reproduce the conhost crash by itself; the harness check below does |
| F14 | Unexplained `systray.c` guards | Reverted. They were aimed at the wrong program, did not stop the crash, and the `delete_icon` change skipped `hide_icon` (dock removal and the displayed-icon count) |

---

## 4. Evidence

All commands were run on the revisions in section 1.

### 4.1 Build

```bash
configure --enable-archs=i386,x86_64 && make -j4      # Gemini's 404d23f1 and c01c57ef, separate build directories
```

Both builds finished with `Wine build complete.` The first attempt failed at the end because
`wayland-scanner` was missing (`No rule to make target 'dlls/wexhost/xdg-shell-client-protocol.h'`); installing
`libwayland-dev`, `wayland-protocols` and `libwayland-bin` fixed it.

### 4.2 Focused token probe

```bash
bash scripts/check-token-security.sh --wine-build BUILD --output-dir NEW --focused --all
```

Exit 0 on both builds. Markers on both architectures: `deny_only_acl_and_object_open=PASS`,
`elevation_helper_errors_and_child_handle=PASS`, `elevation_helper_states_manifest_child=PASS` (new),
`shipping_consent_has_no_approval_bypass=PASS`.

### 4.3 Upstream suites (non-focused)

```bash
bash scripts/check-token-security.sh --wine-build BUILD --output-dir NEW --all
```

| Suite (x86_64) | Result |
| :--- | :--- |
| `advapi32 security` | 3691 tests executed, 171 todo, 0 failures, 28 skipped |
| `ntdll info` | 2687 tests executed, 21 todo, 0 failures, 1 skipped |
| `shell32 shlexec` | 1803 tests executed, 39 todo, **2 failures**, 1 skipped (also i386, same counts) |

The two failures are `shlexec.c:2303` and `shlexec.c:2305`, the `SEE_MASK_CLASSNAME` `.exe` case. They occur on
the unmodified submission build and on stock Wine `master` (`081042ed`, built unmodified with the same
configuration: 1788 tests, 2 failures, both architectures), so they are inherited and not caused by these changes.

The harness previously failed `ntdll info` because child processes print their own summary line with 0 tests;
the rule now requires assertions to have run overall and no failures in any process. This does not weaken the
failure check.

### 4.4 Runner sentinel

```bash
bash scripts/test-runner-sentinel.sh --wine-build BUILD --output-dir NEW
```

```text
success PASS
failure PASS
timeout PASS
signal PASS
```

### 4.5 Elevation helper states

`helper_states()` in `scripts/test_token_security_regressions.cpp` runs a child whose manifest requires
administrator rights (`token_manifest_child`, exit code `100 + elevation type`) from a limited-token process
(`TokenElevationTypeLimited`, checked by the test).

| Helper state | `CreateProcessW` | `ShellExecuteExW` (no verb) |
| :--- | :--- | :--- |
| absent key, absent value, empty string, zero-length `REG_SZ` | starts, child reports full token (102) | starts, 102 |
| `REG_DWORD` | `ERROR_ELEVATION_REQUIRED`, no process | `ERROR_DATATYPE_MISMATCH` (1629) |
| odd length | `ERROR_ELEVATION_REQUIRED` | `ERROR_INVALID_DATA` (13) |
| whitespace only | `ERROR_ELEVATION_REQUIRED` | `ERROR_BAD_PATHNAME` (161) |
| control character | `ERROR_ELEVATION_REQUIRED` | `ERROR_BAD_PATHNAME` (161) |
| oversized | `ERROR_ELEVATION_REQUIRED` | `ERROR_FILENAME_EXCED_RANGE` (206) |
| mock helper approves | `ERROR_ELEVATION_REQUIRED` | starts, 102 |

Both architectures passed. The same table passes on the unmodified submission, so these tests confirm the
submission's helper logic and the refactor in `kernelbase`/`ntdll`; they do not fail on the old code.

### 4.6 B3 machine root (Unix tier, disposable fixture, root)

```bash
sudo bash scripts/test_b3_machine_root.sh
```

```text
b3_unix_group_write_allowed=PASS (Unix tier only: group members write without Windows elevation)
b3_unix_outsider_write_denied=PASS
b3_backup_dir_not_writable_by_group=PASS
b3_tampered_backup_refused=PASS
b3_failed_restore_fails_rollback=PASS
b3_metadata_capture_and_rollback=PASS
b3_rollback_without_backup_refused=PASS
b3_path_validation_and_safety=PASS
```

The first result means a member of the machine group can write the shared tree through Unix permissions alone,
without any Windows-side elevation. The default group is `wexsys`, falling back to `sudo`. Whether that is
acceptable is a design decision for the Windows-side enforcement work, which is NOT RUN here.

The old script, run by hand on a fixture root with mode 750 and owner `root:root`, left it `750 root:daemon`
after rollback.

### 4.7 B4: the fresh-prefix division by zero

The message `wine: Unhandled division by zero at address 000000014000C8CC` appears in the stderr of the token
probe's fresh prefixes. The address is in **`conhost.exe`**, not `explorer.exe`:

```text
update_window  programs/conhost/window.c:629
console->active->max_width = (r.right - r.left) / console->active->font.width;
```

The font cell size is zero when `ERR("Unable to find a valid console font")` has fired, and `update_window`
divides by it. `c01c57ef` skips the computation when either size is zero.

Reproduction and result with the harness (`--focused --all`, which now fails on any `Unhandled` line found in the
probe's stderr after `wineserver -w`):

| Build | Runs | Unhandled-exception lines (one per architecture log) |
| :--- | :--- | :--- |
| Submission `404d23f1`, with Gemini's `systray.c` guards | 1 | 2: the guards did not stop it |
| Submission with `systray.c` reverted to `ae5fec1e` | 11 | 2 in 10 runs, 0 in 1 run: intermittent |
| `c01c57ef` | 6 | 0 in every run |

With the harness check in place the unfixed build exits 1 (`Owned-prefix cleanup needs inspection`) and
`c01c57ef` exits 0.

Reproduction is not guaranteed in other environments or by `scripts/test_b4_explorer_fresh.sh` alone (a console
program in a new console plus the tray balloon path, with default logging, does not trigger it here). That
script passes before and after the fix and is kept as the explorer-side smoke test. A direct trigger for the
missing console font is not yet isolated.

### 4.8 Hashes (SHA-256)

Sources, from the pushed blobs (`git show REV:path | sha256sum`); the build tree files are identical:

| File | SHA-256 |
| :--- | :--- |
| `wine/server/token.c` | `7f1267cbafd62e034c2c74a91135a7ce67cc5190ac43d6f24ec379867a2fe628` |
| `wine/dlls/advapi32/tests/security.c` | `ca5b9affa53f58c6d46e797dd39ae63a1ff34f2593bd5df73919d16b6733f629` |
| `wine/dlls/shell32/shlexec.c` | `ea71c8a432502f791db9f0e3392965385f3e9cdcc2104e3000fd1ebd4029e109` |
| `wine/dlls/kernelbase/process.c` | `97982ed2e14494367fbdbc5a53f483340c184876cce10dcf41f0f0bd9a8f1011` |
| `wine/dlls/ntdll/loader.c` | `12bc347bf5586ccf6515e5e5ad7a7866070acb6ce115a5d11159dd7a49377636` |
| `wine/programs/explorer/systray.c` | `19582c9b087ab113400e95b52be6676f07b0782294b911d48d11bca7a7bf76e9` |
| `wine/programs/conhost/window.c` | `5d6f9e4e38deb261db9e04c9493059f2b6177764d3bb8fb56e786ec836b7a34d` |
| `scripts/setup-machine-root.sh` | `7108912c973b11140a7e848203990a3adc7ef6ff6fdcec174c4218b3d0982b85` |
| `scripts/test_b3_machine_root.sh` | `d315dde3f466ed67d8844ba90cbe14691a04823e06056097a06969564bd775e7` |
| `scripts/test_b4_explorer_fresh.sh` | `aa912361da0e88cf8d35195b49db289f789431b7f9e0a31c9aab5e1c817d8e55` |
| `scripts/tray_balloon_probe.cpp` | `4b17dda34150a06f694c0550712ba6da1ec23955042cc20e67a04b95fb43c955` |
| `scripts/test_token_security_regressions.cpp` | `3be153deac881169639b385db77d0a4d857900ea2cd03c426e4a593054c550c2` |
| `scripts/check-token-security.py` | `d52be6a3ccaa4e695e6ee5674caf1bf451f3a428cb4c5943027c3344489a22c7` |
| `scripts/token_manifest_child.cpp` | `494106ce49d1351bde58094e8c3d08bb1e41b434b0bcc9753e1a9853f095243a` |
| `scripts/token_manifest_child.manifest` | `6a30fd593a81ae08de2ef916274af98b54207e06393c5f1edd12edea154236fe` |
| `scripts/token_manifest_child.rc` | `ebb7dad0ffe5a522eae78e3eb8682a0d5eb598cbbbb060e4284fa9d2477f3915` |

Binaries from the `c01c57ef` WoW64 build tree:

| Binary | SHA-256 |
| :--- | :--- |
| `wine` | `bf827bf3a3aef1144091269dbc03cd24ac7da15a592b9f4c5387d50340036b95` |
| `server/wineserver` | `2b6845110f25d96c68f8872a7c67a61909cb56407f8b11416e4fa9ec0bc0ac54` |
| `advapi32_test.exe` x86_64 | `9ffc73a2498d38a87b3aa1aae5fcb350c1fb930e64d2d8f639ba3ffd23ba23da` |
| `advapi32_test.exe` i386 | `95f483d45a4703a95d33222171e5619b7d3d34c1a702017b3b94cb447a21358e` |
| `shell32_test.exe` x86_64 | `b74a499ccca45667d8f9d0524636ea712227e1d8eb2102b5a24307759ee1afa5` |
| `shell32_test.exe` i386 | `92ff1eb0f293459905f80efcfc9f849914dfefffaf86541ccf453e168981992c` |
| `ntdll_test.exe` x86_64 | `b1307d969e3956b113316e5046b5f6f51101c4d422a46653548e953eff07a666` |
| `ntdll_test.exe` i386 | `db6d4b86e1f280c19e394f5ebceb72ffb2dc9fb35e7d60f1e3ff8b524530160a` |
| `conhost.exe` x86_64 | `a2834106688718f7cc6407a6fb026660e66bb6e657850f2a1780c5d4a79e92fb` |
| `explorer.exe` x86_64 | `ba8da28f27787e731dc32d1d2ad2d00f93cbeb278c512d3d73bfdfee2ec3f661` |
| `consent.exe` x86_64 | `95ed96b399167a110afff472ba1c0880eeb23ccbcc764c6daaf94af84c0c07d3` |

---

## 5. Open items

- B2 real authorization (polkit mechanism, request binding, administrator authentication) is unimplemented.
- B3 Windows-side checks and the installed-image, X11/Wayland and second-user gates need a designated
  installed image.
- A5 needs a native Windows control for repeated and disjoint restrictions.
- The key-DACL-denied helper read cannot be tested while Wine ignores key DACLs for reads.
- Checkpoint C has not started.
