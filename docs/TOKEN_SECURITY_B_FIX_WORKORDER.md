<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Token security checkpoint B: review fixes

Date: 2026-10-22. Owner: Claude (session `ccr-08223ee4-p830bf`). Status: planned, not started.

This order fixes the defects found while reviewing Gemini's checkpoint B submission. It is an implementation
order, not an acceptance report. Status words below (PASS, PARTIAL, NOT RUN, BLOCKED) follow
`TOKEN_SECURITY_CHECKPOINT_B_WORKORDER.md`.

## Inputs

| Item | Value |
| :--- | :--- |
| Parent submission | `WineExplorer` `gemini/token-security-b` at `77089c00d8b3641eac4ae67f4e5ff016e1e1712e` |
| Wine submission | `wine` `gemini/token-security-b` at `404d23f1c4c21937ed3314955a70bc1da2200310` (the parent gitlink pins this) |
| Wine base | `ae5fec1e11c6ae42e2ba73a6e9b6368d40a34d92` (`codex/token-security-correctness`) |
| Parent base | `92cb0684` |
| Governing order | `docs/TOKEN_SECURITY_CHECKPOINT_B_WORKORDER.md` |

Work on `ccr-08223ee4-p830bf` in both repositories. Fast-forward each branch to the submission above first. Both
branches currently sit at an ancestor of the submission, so this needs no force-push. Push Wine first, then the
parent gitlink. Never push to `gemini/*`, `codex/*` or `master`.

## Review findings this order fixes

| ID | Area | Defect |
| :--- | :--- | :--- |
| F1 | Evidence | Six of nine reported source SHA-256 values (`token.c`, `security.c`, `shlexec.c`, `process.c`, `loader.c`, `systray.c`) do not match the pushed blobs. The tested binaries cannot be tied to the pushed source. |
| F2 | Evidence | The report labels `ae5fec1e` as the gitlink pin. The real pin is `404d23f1`. The header kernel (6.1.0-26) contradicts §4 (6.12.111+deb13). |
| F3 | Status | B2 is marked PASS, but only the helper-configuration audit exists. No polkit/D-Bus mechanism, request binding, or real authentication was built or tested. |
| F4 | Status | A5 claims "native" disjoint-restriction semantics without a native Windows control. |
| F5 | shell32 | `SHELL_ExecuteW`: when `CreateProcess` fails with `ERROR_ELEVATION_REQUIRED` and the helper is `HELPER_UNSET`, no branch sets `retval`. It returns `SE_ERR_NOASSOC` (31) instead of the earlier `ERROR_BAD_FORMAT`. |
| F6 | kernelbase | `is_elevation_required` now parses the target manifest before reading the registry, so the helper-unset (stock) path pays a `CreateActCtxW` on every limited-token `CreateProcess`. |
| F7 | Tests | Only shell32 explicit `runas` has negative helper tests. The kernelbase manifest path, the ntdll loader path, denied reads, oversized and control-character values are untested. The three parsers disagree on whitespace and control characters. |
| F8 | B3 | Metadata is captured after the first mutation: `ensure_machine_prefix_links` runs `chown` on the root, and promotion `mv`, `wineboot -u` and theme copies also run first. |
| F9 | B3, security | The stat and ACL backups live inside the group-writable machine root. Rollback runs as root and trusts their paths, so a machine-group member can make root `chown`, `chmod` or `setfacl` arbitrary files. |
| F10 | B3 | The write-denial test passes whenever `su nobody` fails for any reason. It never shows that the interactive user's own group can write, and it records no Windows-side enforcement. |
| F11 | B3 | Path validation rejects only `/`, `/var` and `/etc`. |
| F12 | B3, pre-existing | Promotion runs `cp -a … 2>/dev/null \|\| true` and then `rm -rf` on the source, so it can lose data. |
| F13 | B4 | `test_b4_explorer_fresh.sh` prints PASS when explorer exits early. It sets `WINEDEBUG=-all`, greps for a build-specific address, uses the shared `$DISPLAY`, and has no failing baseline. |
| F14 | B4 | The systray change adds NULL guards and reorders `delete_icon` without an identified root cause. The work order called the address a lead, not a cause. |

Out of scope: implementing real elevation authorization (polkit mechanism, request records, standard-user
authentication). That remains open B2 work. This order only records it honestly (step 7).

## Preconditions

1. **Usage gate.** Building Wine as WoW64 is long and expensive. Read `~/.claude/state/usage.json` before steps 1
   and 6. It is absent in this container, which counts as UNKNOWN. Per `~/.claude/CLAUDE.md`, ask the user to hold,
   to delegate the build to Jules, or to explicitly override.
2. **Toolchain.** The container has `gcc` and `bison` but no MinGW. Install `gcc-mingw-w64-i686`,
   `gcc-mingw-w64-x86-64`, `flex`, Wine build dependencies, `Xvfb`, `acl` and `util-linux` (`unshare`,
   `runuser`). If the network policy blocks this, read `read_documentation` topic `environment.network` and
   mark the build gates BLOCKED. Do not fall back to `--enable-win64`.
3. **Test users.** Create two unprivileged users: `wextest` (in the machine group) and `wexout` (not in it). Run
   Wine tests as `wextest`, never as root.
4. **No VM.** The VM in the report (`wineuser@127.0.0.1:2222`) is not reachable here. Installed-image, real X11
   or Wayland desktop, and second-user-after-restart gates stay NOT RUN in this order.

## Steps

Commit after each working step. Each step lists its acceptance check.

### Step 0: prepare branches and record the baseline

- Fast-forward both `ccr-08223ee4-p830bf` branches to the submission SHAs.
- Copy this file into the parent branch and commit it.
- Recompute SHA-256 of the nine files from the pushed blobs (`git show SHA:path | sha256sum`) and save them as the
  F1 evidence.

Accept: both branches equal the submission SHAs plus this document. The F1 table is reproducible from the
commands.

### Step 1: clean WoW64 build of the submission, unchanged

Fresh tree at `404d23f1`:

```
configure --enable-archs=i386,x86_64
make
```

Build in an owned directory, and build the advapi32, ntdll and shell32 test executables. Then hash the six Wine
sources and the five binaries named in the report.

Accept: the build succeeds, and the source hashes equal the step 0 values. This gives a source-tied baseline for
F1. A configure or build failure is recorded verbatim and stops the order.

### Step 2: Wine code fixes (F5, F6, F7 alignment)

Make one Wine commit per fix. Keep each change small and list it in `docs/STOCK_CHANGES.md`.

1. **F5, `dlls/shell32/shlexec.c`.** In the `ERROR_ELEVATION_REQUIRED` branch, add a final `else` for
   `HELPER_UNSET` that sets `retval = ERROR_BAD_FORMAT`. This matches the pre-submission fallthrough.
2. **F6, `dlls/kernelbase/process.c`.** Move the helper audit back ahead of the manifest check. If the helper is
   unset, return `FALSE` before `CreateActCtxW`. If it is configured or invalid, check the manifest as now. The
   result for every input stays the same; the stock path regains its early exit.
3. **F7, one rule for all three parsers.** The helper is UNSET only when:
   - the key or the value is absent, or
   - the type is `REG_SZ` or `REG_EXPAND_SZ`, the length is even, and the data is empty or starts with NUL.

   Every other result, read errors included, is not unset. `ntdll` and `kernelbase` only act on "unset or not",
   so drop their whitespace and control-character scans (they cannot change the outcome). Keep the detailed
   errors in shell32 only, because only shell32 reports them. Remove the stray blank line added in `loader.c`.

Accept: Wine builds with no new warnings. The `advapi32` `security` test results match step 1. The `shell32`
`shlexec` test has no new failures.

### Step 3: regression tests for the helper paths (F7)

Extend `scripts/test_token_security_regressions.cpp` and add a manifest child built with the same toolchain:
`requireAdministrator` manifest, exits with 37 and reports its `TokenElevationType`. Get the limited token the
same way the existing `shell_helpers()` cases do.

For each helper state, assert the result on both i386 and x86_64. States: absent key, absent value, empty
string, `REG_DWORD`, odd length, oversized (more than `MAX_PATH` characters), whitespace only, a control
character, read denied by key DACL, and a valid mock helper.

| Path | UNSET | INVALID | CONFIGURED |
| :--- | :--- | :--- | :--- |
| `CreateProcessW` on the manifest child (kernelbase) | Child runs and reports `TokenElevationTypeFull` (ntdll silent elevation) | Fails with `ERROR_ELEVATION_REQUIRED`, no process | Fails with `ERROR_ELEVATION_REQUIRED`, no process |
| `ShellExecuteExW` on the manifest child, no verb | Child launches | Fails, `SE_ERR_ACCESSDENIED` and the shell32 error, `hProcess == NULL` | Mock helper runs, then the real target launches once |
| `ShellExecuteExW`, `runas` (existing cases) | Kept | Kept, plus oversized, control character and denied read | Kept |
| `RtlCreateUserProcess` on the manifest child (bypasses kernelbase, ntdll loader only) | Child reports Full | Child reports Limited (no silent elevation) | Child reports Limited |

Every case restores the original `Helper` value and key DACL, including on failure. Every negative case checks
that no orphan child process is left behind.

Accept: all cases pass on both architectures with the step 2 build. On the step 1 build, the shell32 `UNSET` +
`ERROR_ELEVATION_REQUIRED` return-code case fails, proving the test catches F5.

### Step 4: B3 script fixes (F8 to F12)

In `scripts/setup-machine-root.sh`:

1. **F11.** Require an absolute path with `realpath -m` equal to the input. Reject `/` and the system directories
   `/bin /boot /dev /etc /home /lib /lib32 /lib64 /opt /proc /root /run /sbin /srv /sys /tmp /usr /var /var/lib`.
   Do this before any other action, rollback included.
2. **F9.** Move the backups to a root-only directory outside the machine root:
   `${WEX_BACKUP_DIR:-/var/lib/wineexplorer/backup}/<machine-root-hash>/`, mode `0700`, owner `root:root`.
   Refuse to use the directory if it is a symlink, is not owned by root, or is group or other writable.
   - Rollback rejects any backup entry whose path, after `realpath`, is outside the machine root.
   - Rollback uses `chown -h` and does not follow symlinks.
   - Rollback applies ACLs only to validated paths, by filtering the `getfacl` dump.
3. **F8.** Capture stat and ACL metadata immediately after validation, before step 1 of the script. If a backup
   already exists, keep it (first capture wins). Rollback lists entries created after capture and leaves them in
   place.
4. **F12.** Remove `2>/dev/null || true` from the promotion copies. Delete each source only after its copy
   succeeds.
5. Do not suppress any `chown`, `chmod` or `setfacl` failure. Rollback exits non-zero and does not print
   "completed successfully" if any restore command fails.

In `scripts/test_b3_machine_root.sh`:

- Assert the stat and ACL of the root directory, `drive_c`, a nested file with an extended ACL, and a file with
  a space in its name. Assert each before apply and after rollback.
- **F10.** Run the write checks with `runuser -u USER --`, and include a positive control: the same user can write
  to its own temporary directory. `wexout` must fail to write in the machine root. `wextest` must succeed; record
  this as the Unix-tier result, which is group membership and not Windows elevation.
- Tamper test: replace the backup with an entry pointing at a file outside the fixture, then run rollback. It must
  refuse, and the outside file must keep its stat.
- Path tests: `/`, `/home`, `/var/`, `/var/lib`, `relative/path` and `/tmp/../etc` must all be rejected.
- Failure test: make one restore fail (for example, an immutable file if `chattr` works, or a read-only bind
  mount). Rollback must exit non-zero.

Accept: the test passes as root in a disposable fixture under `/tmp`. Each negative case is shown to fail on the
submission's script first, except where the old script crashes first; record that case as such. The
Windows-side checks (non-elevated Win32 write denied, approved installer succeeds) are NOT RUN here because they
need an installed image.

### Step 5: B4 root cause and honest test (F13, F14)

1. Rewrite `scripts/test_b4_explorer_fresh.sh`:
   - Use an owned `Xvfb` display and a private `WINEPREFIX`.
   - Keep the default `WINEDEBUG`; add `+seh` only while investigating.
   - Drive the tray balloon path with a small helper that calls `Shell_NotifyIconW` with `NIF_INFO`, then deletes
     the icon.
   - Fail if explorer exits early, and fail on `Unhandled exception`, `divide by zero` or
     `EXCEPTION_INT_DIVIDE_BY_ZERO`. Do not grep for addresses.
   - Shut down with `wineserver -k` and check that the prefix's server is gone.
2. Run the new test against the step 1 build with `systray.c` taken from the base (`ae5fec1e`). It must fail,
   showing the crash. If it cannot reproduce the crash, record B4 as BLOCKED (cannot reproduce) and keep the
   original stderr evidence; do not report PASS.
3. With the crash reproduced, symbolize the faulting address in that build's `explorer.exe`. Find the actual
   division and fix it.
4. Revert the submission's NULL guards and the `delete_icon` reorder unless the root cause shows that a guard is
   needed. Keep only those with a written reason in `docs/STOCK_CHANGES.md`.

Accept: the test fails on the base `systray.c` and passes on the fix, on both architectures of explorer where
applicable. The commit message names the root cause.

### Step 6: rebuild and full evidence run

On a clean tree of the final Wine and parent SHAs, rebuild as in step 1. Run as `wextest`:

```
bash scripts/check-token-security.sh --wine-build BUILD --output-dir NEW_DIR --focused --all
bash scripts/check-token-security.sh --wine-build BUILD --output-dir NEW_DIR --all
bash scripts/test-runner-sentinel.sh --wine-build BUILD --output-dir NEW_DIR
```

Also run the step 3 regressions, step 4 (as root, fixture only) and step 5. Hash every source and binary from the
same tree that was built.

Accept: every listed command exits 0, and the hashes in the report equal `git show` of the pushed blobs. A
failure is recorded with its output, not hidden.

### Step 7: correct the documents (F2, F3, F4)

Rewrite `docs/TOKEN_SECURITY_B_ACCEPTANCE.md` with a per-case matrix:

| Gate | Expected status after this order |
| :--- | :--- |
| A1 to A4 | PASS only if step 6 proves it |
| A5 | PARTIAL: Wine behaviour tested; native Windows contract unverified |
| B1 | PARTIAL: design written, not implemented |
| B2 | PARTIAL: helper audit done and tested; real authorization not implemented |
| B3 | PARTIAL: Unix-tier capture, rollback and denial tested in a fixture; Windows-side and installed checks NOT RUN |
| B4 | PASS or BLOCKED, from step 5 |
| C | NOT RUN |

Also:

- Fix the SHA table. State the real gitlink pin and one consistent environment line.
- Add a note that the original hashes did not match the pushed source, and that the old results are superseded.
- Change `docs/ELEVATION.md` §6 D: "Fully supported and validated" becomes "designed; not implemented".
- Update `docs/STOCK_CHANGES.md` for steps 2 and 5, and `docs/TOKEN_SECURITY.md` where it repeats the old
  claims.

Accept: no PASS appears without a command and output in the report.

### Step 8: push and review

1. Push Wine `ccr-08223ee4-p830bf`.
2. Point the parent gitlink at it and push the parent branch.
3. Open one ready-for-review PR per repository, against `gemini/token-security-b`, and watch both.
4. Report both SHAs and the report path to the user.

## Risks

- A WoW64 Wine build on 4 cores can take one to two hours, and two clean builds are planned (steps 1 and 6).
- Step 5 may not reproduce the crash outside the original VM. The order then ends B4 as BLOCKED, not PASS.
- Reverting the systray guards may expose a different crash. Treat that as new evidence for step 5, not a reason
  to keep unexplained guards.
