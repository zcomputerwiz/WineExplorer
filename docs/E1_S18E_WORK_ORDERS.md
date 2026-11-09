# E1-S18e work orders for Qwen

Mechanical slices of the S18e remediation (`docs/E1_S18E_REMEDIATION.md`, findings in
`docs/E1_S18_REVIEW.md`). Run one order per job, in the order listed (later orders touch the same
files). Claude reviews and commits each result; design work (R5, R7 ownership, R8 duplex channel,
R9 adapter phases, R10 consumed-record install, R14 per-request leases, R13 filtered-admin) is not
in these orders.

## Rules for every order

* Read `docs/TOOLING.md` and `CLAUDE.md` first. Touch only the files the order names.
* Never pass C escapes (`\\`, `\n`) through shell heredocs, `sed` or `python -c`. Use the edit tool.
* No commits, no pushes, no VM, no `/etc/wineexplorer`. Run tests only in WSL distro `wexbuild`.
* Every behaviour change gets a test that fails before the change and passes after it.
* Never make a check pass by relaxing it, adding a default value, or guessing an identity.
  Never regenerate or edit the production-object baselines (`/tmp/main_prod*_baseline.o`): if the
  byte-identical check fails, your change touched the shipping broker (often a field added to
  `struct request` outside `#ifdef WEXELEVATE_E1S18`); fix the change and report it.
  If something cannot be done as written, stop and say so in the report.
* Stop and report after at most 45 minutes, finished or not.
* Report: files changed, each change in one line, test output (last lines), anything not done.

Broker test command (from PowerShell):

```
wsl -d wexbuild -u root --exec bash -c "cd /mnt/d/DeepThought/Projects/WineExplorer && bash scripts/test_broker_e1s18.sh 2>&1 | tail -n 25"
```

It must end with `ALL E1-S18 broker integration tests passed successfully.` It also runs the
tests under ASan/UBSan and checks that the production `main.c` object is byte-identical to the
reviewed baseline: none of these orders may change `src/programs/wexelevate/main.c`.

## WO-1 Exact decoding (R15)

Files: `src/programs/wexelevate/cred_dialog.c`, `src/programs/wexelevate/e1s18_broker.c`,
`src/programs/wexelevate/test_broker_e1s18_main.c`, `src/programs/wexelevate/test_cred_dialog_main.c`.

1. `cred_dialog.c` lines 141 and 222: an S1 answer must be exactly `WEX_S1_ANSWER_LENGTH` bytes
   (reject shorter and longer), not `len < WEX_S1_ANSWER_LENGTH`.
2. `e1s18_broker.c` line 765: same, `answer_len == WEX_S1_ANSWER_LENGTH`.
3. `e1s18_broker.c` line 894: the evidence buffer must be exactly
   `sizeof(struct wex_s1_evidence_record)` bytes.
4. `e1s18_broker.c` line 279: the policy value enables only when it is exactly 1
   (`return val == 1;`); 2, 0xffffffff and so on stay disabled.
5. `e1s18_broker.c` line 1091: the S10 response SID must be exactly 28 bytes (the fixed
   mapped-SID size the ntdll wrapper enforces, `wine/dlls/ntdll/sec.c` around line 191) and pass
   `IsValidSid` before any use; on native builds check the length only.

Tests: one case each for an answer one byte long and one byte short, evidence one byte longer,
policy values 0/1/2, SID lengths 27/28/29.

## WO-2 NEGOTIATE token output (R10, first part)

Files: `src/programs/wexelevate/e1s18_broker.c`, `src/programs/wexelevate/test_broker_e1s18_main.c`.

`default_issue_token` (line 1003) calls `pNtCreateWexAccountToken( WEX_TOKEN_NEGOTIATE, &context,
NULL, ...)`. The real ntdll export rejects a NULL token output with STATUS_INVALID_PARAMETER before
reaching the server (`wine/dlls/ntdll/sec.c` lines 149-154). Pass the address of a local
`HANDLE negotiate_token = NULL` and deny (close the context, return an error) if it is not still
NULL after a successful NEGOTIATE. Make `fake_NtCreateWexAccountToken` in the test return
STATUS_INVALID_PARAMETER (0xc000000d) for a NULL token pointer on every operation, as ntdll does.
Tests: the success path still passes; a fake that sets the token on NEGOTIATE makes the request deny.

## WO-3 Broker asks the server for the storage generation (op 12)

Files: `src/programs/wexelevate/e1s18_broker.c`, `src/programs/wexelevate/e1s18_broker.h`,
`src/programs/wexelevate/test_broker_e1s18_main.c`.

The server now has `WEX_HKU_STORAGE_QUERY` (12) in `wine/include/wine/wex_profile.h`; read the
comment there. Change the broker to use it:

1. Add `query_storage` to the broker ops table (next to `load_profile`), with a default
   implementation that calls `pNtLoadWexProfileEx( WEX_HKU_STORAGE_QUERY, req->s10_context,
   token, 0, record, 48, &key, &managed, &generation )` where `record` is built exactly like the
   LOAD record in `default_load_profile` but with the storage generation field (offset 32) zero.
   Success only when status is 0, key is NULL, managed is 1 and generation is nonzero; then store
   it in `req->storage_generation`. Anything else denies.
2. In `e1s18_handle_standard_request`, call it after token issuance and before `load_profile`.
3. Stage 2 no longer produces a storage generation: remove the STORAGE_QUERY step from
   `e1s18_transport_redeem_live` (it keeps OPEN, REDEEM, CLOSE) and remove its
   `out_storage_generation` parameter and the `storage_gen` handling in `default_redeem_evidence`.
   Keep every other Stage 2 check.
4. Test fake `fake_NtLoadWexProfileEx`: handle op 12 (check access 0, 48-byte record with
   storage field 0, return a configurable generation, NULL key, managed 1). LOAD must receive the
   generation returned by op 12 (assert it in the fake).

Tests: success; op 12 failure denies and LOAD is never called; op 12 returning 0 or a key denies;
the generation given to LOAD equals the queried one (use a value other than 1, e.g. 0x5e1f).
Update the tests that set `storage_generation` through Stage 2.

## WO-4 Retire the S6 storage query (after WO-3)

Files: `src/dlls/wexhost/e1s6.c`, `src/include/e1s6_protocol.h`, `tools/e1s6/evidence.py`,
`tools/e1s6/test-evidence.py`, `tools/e1s6/e1s6-lab.py`.

The storage generation now comes from the wineserver, so the S6 `E1S6_STORAGE_QUERY` (10)
operation has no user. Keep the number reserved with a comment ("retired, never reuse"), make
wexhost reject it like an unknown operation, and delete `Service.storage_generation`, the
`query_storage` hook, the `STORAGE_QUERY` branch in `Connection.serve` and its tests (keep a test
that operation 10 is denied). Keep the `storage` audit reason only if something still uses it.
Tests: `bash scripts/test_evidence.sh --disposable-guest` (root, wexbuild) must end with
`E1S8_EVIDENCE OK`; wexhost must still build (`make -C /root/WE-prefetch/build/wow64
dlls/wexhost` after `bash unsloth-tmp/s9-sync.sh`).

## WO-5 Random nonce and channel id (R4, part)

Files: `src/programs/wexelevate/e1s18_broker.c`, `src/programs/wexelevate/e1s18_broker.h`,
`src/programs/wexelevate/test_broker_e1s18_main.c`.

1. Line 1776 fills the attempt nonce with `i ^ 0xa5`. Replace with 32 random bytes from
   `BCryptGenRandom( NULL, buf, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG )` on Windows and
   `getrandom( buf, 32, 0 )` (full length required) on native builds. Failure, or an all-zero
   result, denies the request before any dialog starts.
2. Line 730 derives the channel id from the nonce (`nonce[i] ^ 0x5c`). Make it 16 independent
   random bytes from the same function.
3. Add a test hook `e1s18_set_test_random( const uint8_t *bytes, size_t len )` next to
   `e1s18_set_test_clock` so existing tests stay deterministic, plus a hook to force an RNG
   failure.
4. In `default_redeem_evidence`, take `now` again after the transport call returns, so evidence
   that expires during the call is rejected.

Tests: RNG failure denies with no dialog spawned; nonce and channel id differ between two requests
(without the hook); evidence that expires during the (fake) transport call is rejected.

## WO-6 Launch preparation inputs (R11, part)

Files: `src/programs/wexelevate/launch_prep.c`, `src/programs/wexelevate/launch_prep.h`,
`src/programs/wexelevate/e1s18_broker.c`, `scripts/test_launch_prep.sh` and its test source.

1. `launch_prep.c` around lines 579-612: when the account lookup fails, the code invents
   `HOME=/home/bob` and `USER=bob`. Fail with an error status instead.
2. The Windows environment block is built as narrow strings but passed to CreateProcessAsUserW with
   CREATE_UNICODE_ENVIRONMENT. Build it as UTF-16LE: each `NAME=value` converted from UTF-8,
   NUL-terminated, the block ending in an extra NUL; invalid UTF-8 fails; size limit stays. Report
   the size in bytes.
3. `e1s18_broker.c` line 1316 maps the caller's cwd with `& 0x7f`. Instead convert it from
   UTF-16 to UTF-8 exactly (reject unpaired surrogates and truncation) and deny if it does not fit.

Tests: lookup failure denies; a non-ASCII variable round-trips into the UTF-16 block and the block
ends with two NUL characters; a non-ASCII cwd is converted exactly; an overlong cwd denies.
`bash scripts/test_launch_prep.sh` and the broker test must pass.

## WO-7 Dialog handle inheritance (R8, part)

Files: `src/programs/wexelevate/e1s18_broker.c`.

`e1s18_spawn_consent_dialog` (line 513) starts consent.exe with `bInheritHandles = TRUE` and no
list, so two dialogs can inherit each other's pipe ends. Use `STARTUPINFOEXW` with
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` containing only that dialog's pipe write handle, and
`EXTENDED_STARTUPINFO_PRESENT`. Check every attribute call and free the list on all paths. Do not
change the dialog protocol (that is R8 design work). Check: two-ABI MinGW lab compile in the broker
test.

## WO-8 Confirmed child termination on rollback (R7, item 2)

Files: `src/programs/wexelevate/e1s18_broker.c`, `src/programs/wexelevate/e1s18_broker.h`,
`src/programs/wexelevate/test_broker_e1s18_main.c`.

`default_terminate_child` ignores the results of TerminateProcess and of the five-second wait,
and the `fail:` path of `e1s18_handle_standard_request` then closes the process handle and
unloads the profile as if the child were gone.

1. Change the ops hook to `BOOL (*terminate_child)( HANDLE process )`, returning TRUE only when
   the child is confirmed dead: call TerminateProcess (its failure alone does not decide, the
   process may already have exited), then require `WaitForSingleObject( process, 5000 ) ==
   WAIT_OBJECT_0`.
2. In the `fail:` path, when termination is not confirmed: do not close `pi.hProcess` or
   `pi.hThread` (keep the references; add a `ponytail:` comment that a supervisor-owned reaper
   is the R7 design follow-up), do not unload the profile (the child may still use the hive),
   still close the remote handle and everything else, and return `ERROR_CHILD_NOT_COMPLETE`
   instead of the original error.
3. Fake: add a switch to make `fake_terminate_child` report failure.

Tests: unconfirmed termination returns ERROR_CHILD_NOT_COMPLETE, closes no process handle, does
not unload, still closes the remote handle; the confirmed path and the existing stage matrix
behave as before.

## WO-9 Probe tests: the caller's job and debugger do not reach the elevated process (R13)

Files: `scripts/wexelevate_probe.c`, `scripts/test_sysrun_server.sh`. Do not edit anything under
`wine/` or `src/`.

The wineserver now (wine/server/process.c, `new_process`, look for `isolate`) skips the
designated parent's job and debugger when the verified broker creates a process for an explicit
token. Add two probe modes, modelled on `attack_mode` (same "child <out> hold <tag>" command, the
same `wexelevate_run(..., WEXELEVATE_WANT_HANDLE, &process, &pid)`, the same wait for `ready=1`
and the same release file at the end):

1. `jobparent <image> <out> <tag>`: create a job with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` and no
   breakaway permission, assign the probe itself to it, then request the elevated "hold" child.
   Checks: `IsProcessInJob( process, job, &in )` gives FALSE; `IsProcessInJob( process, NULL, &in )`
   gives FALSE; after `TerminateJobObject( job, 1 )` and a short wait the elevated process is still
   running (`WaitForSingleObject( process, 0 ) == WAIT_TIMEOUT`). Note that `TerminateJobObject`
   also ends the probe itself: do every check and write every result line before it, then call
   it, and let a second probe instance (or the harness) check that the elevated process survived
   by its tag (see how `callerdeath_mode` finds processes by tag with a toolhelp snapshot). Choose
   whichever is simpler and say which.
2. `debugparent <image> <out> <tag>`: start a second copy of the probe in a new mode
   `debugparent-inner <image> <out> <tag>` with `DEBUG_PROCESS` (so children of the inner copy
   would be debugged too), run a debug loop that records the process ids of every
   CREATE_PROCESS_DEBUG_EVENT and continues every event with DBG_CONTINUE (DBG_EXCEPTION_NOT_HANDLED
   for exceptions other than the initial breakpoint). The inner copy requests the elevated "hold"
   child and writes its pid to a file. Check: the debugger never saw a create-process event for
   that pid, and the elevated process reaches `ready=1`.

Harness: next to `c4_caller_death_leaves_no_child` in `scripts/test_sysrun_server.sh`, add
`c4_caller_job_not_inherited` and `c4_caller_debugger_not_inherited` with the same `bp` pattern,
for both ABIs (64 and 32) like other cases that run both. Both probe modes must compile with the
existing two MinGW compile lines of the harness (-Wall, no new libraries unless already linked).

Check (compile only here; Claude runs the system-role harness): compile the probe for both ABIs
the way `scripts/test_sysrun_server.sh` does, with `-Wall -Werror`, and run `bash -n` on the
script.

## WO-10 Passwordless administrator never authenticates

Files: `tools/e1s7/test-client.py` (and `scripts/test_pam_worker.sh` only if a new fixture account
needs creating or removing there).

The S7 worker calls `pam_authenticate( pam, PAM_DISALLOW_NULL_AUTHTOK )`
(`tools/e1s7/pam-worker.c`, stage 3), so an account with an empty password must be refused even
if the PAM stack says `nullok`. Nothing tests it. Add a disposable fixture account in the
administrator group whose password is removed (`passwd -d`), make the lab PAM stack used by the
test include `pam_unix.so nullok` for that run, and check that an empty answer, a single space
and a wrong password are all refused for it, and that a normal account with a password still
works. Remove the account at the end (only if the test created it). Run
`bash scripts/test_pam_worker.sh --disposable-guest wex-e1s-test` as root in wexbuild; it must
pass.

## WO-11 Symlinked machine hives for an unmapped system account (for Safe Mode C7)

Files: `scripts/test_sysrun_server.sh` only.

Section "3. (iii)" checks that a desktop user's prefix whose `system.reg`/`userdef.reg` links point
into the machine root never overwrites the machine file and still reads HKLM from the machine
root. Add the same checks for an unmapped system account, as the greeter will use: create a
disposable account with a UID below 1000 (`useradd --system`), shell `/usr/sbin/nologin`, a home
under `/var/lib/<name>`, in neither `sudo` nor the system group, not in the accounts map. Give it
the same skeleton prefix at `<home>/.wine` the harness gives desktop users (the system role is only
available for `<passwd home>/.wine`, see `wine/dlls/ntdll/unix/sysrun.c` around line 121). Run the
3(iii) steps as that account (`runuser -u <name> -s /bin/sh -- ...`, since its login shell is
nologin), add one more check that the server it gets is the launcher-started one (the same way
3(iii) or section 2 recognises it), and remove the account and its home at the end only if the
harness created it. New result names: `c2_symlinked_hives_system_account_<check>`.
Check: `bash -n` on the script; Claude runs the system-role suite.

## WO-12 registry.c reads admin_group like accounts.c

Files: `wine/server/registry.c` (`admin_group_name`, `user_is_administrator`),
`scripts/test_sysrun_server.sh`.

Two parsers of `/etc/wineexplorer/sysrun.conf` disagree: `wine/server/accounts.c` (`read_policy`)
treats a second `admin_group=` line or an invalid group name as an invalid configuration, while
`registry.c` takes the first line. Make `registry.c` match: more than one `admin_group=` line, an
empty or invalid name (same rule as `valid_name` in accounts.c; copy the check, do not link
accounts.c) means nobody is an administrator; no key means `sudo`; an unsafe file (not a regular
root-owned file, group/other writable, or a symlink) keeps today's behaviour. Add a harness case
next to the existing `admin_group=` override case: a configuration with two `admin_group=` lines
gives a session member of that group the standard (non-administrator) token shape. List the change
in `docs/STOCK_CHANGES.md` (you may edit that file too).
Check: Claude builds and runs the suite; you run `bash -n` on the script.

## WO-13 Consent dialogs at 640x480

Files: `scripts/test_credential_dialog.sh` only.

Safe Mode runs at 640x480. Run the existing headless dialog cases once more on an Xvfb screen of
640x480x24, and add a check that the dialog window lies fully inside the screen (window position
and size from `xwininfo` or `xdotool getwindowgeometry`, whichever the wexbuild distro has; if
neither, install nothing and report it). Keep the existing 24-bit run unchanged.
Check: run `bash scripts/test_credential_dialog.sh` as root in wexbuild; it must pass.

## WO-14 One canonical S1 encoder for S5 and S18 (groundwork step 1)

Files: new `src/programs/wexelevate/e1s_encode.h` (GPL header, see `scripts/add-license.py`),
`src/programs/wexelevate/e1s.h`, `src/programs/wexelevate/e1s18_broker.c`,
`src/programs/wexelevate/e1s18_broker.h`, `src/programs/wexelevate/test_broker_e1s18_main.c`,
`scripts/test_broker_e1s18.sh` only if a new file must be compiled in. Read
`docs/E1_S18E_GROUNDWORK.md` section 1 and `docs/E1_S1_CONTRACTS.md` "Encoding and negotiation"
first.

1. Move the pure encoding from `e1s.h` into `e1s_encode.h`: `struct e1s_bytes`, `e1s_uint`,
   `e1s_put`, `e1s_blob`, `e1s_string`, and two functions that produce the exact byte sequences of
   the caller binding record and of the frozen request (the field order `e1s_binding_digest` and
   `e1s_freeze` use today), with no hashing, no Win32 calls and no allocation the caller cannot see.
   SID validity and the hash are passed in (a function pointer or done by the caller).
2. `e1s.h` keeps its functions and behaviour but builds its bytes through `e1s_encode.h` (BCrypt and
   `IsValidSid` stay there). The S5 digests must not change.
3. `e1s18_compute_digests` is replaced: the standard-user branch builds the binding and the frozen
   request through `e1s_encode.h` and hashes them with the existing `sha256_*` (domain tags
   "E1S1-BIND\0", "E1S1-REQ\0", "E1S1-ENV\0"). The inputs it does not have in production
   (generations, prefix, host process, opener/writer ids, desktop route, channel, image file
   identity) come from new fields of `struct request` (or a small context struct) that are zero
   today; any zero required input makes the request deny with ERROR_NOT_SUPPORTED before a dialog
   starts. Tests set them through a test hook.
4. Tests: a golden-vector test that the shared encoder yields the same request and binding bytes
   (and SHA-256) as a fixed hand-checked vector; each missing input denies before any dialog; the
   full pipeline tests keep passing with the hook-supplied context.

Check: the broker test command must pass, including the byte-identical production `main.c` check
(this order must not change production objects). Also compile the lab PE broker (the MinGW step).

## WO-15 One standard-user request in flight (groundwork step 3)

Files: `src/programs/wexelevate/e1s18_broker.c`, `src/programs/wexelevate/test_broker_e1s18_main.c`.

The S6 transport in wexhost is process-global, so two standard-user requests must never overlap.
Add a broker-wide "standard request in flight" slot taken at the start of
`e1s18_handle_standard_request` (before the prompt queue) and released on every exit. A second
request while the slot is taken returns ERROR_BUSY at once (no waiting, no dialog, no transport
call). Use the queue's existing lock type (`wex_queue_lock_t`) or a plain interlocked flag; no new
dependency. Tests: two concurrent requests (pthread in the native test) give one success path and
one ERROR_BUSY with no dialog/transport call counted for the second; the slot is free again after
success, after each failure stage and after cancellation.
Check: the broker test command must pass (ASan/UBSan included).

## WO-16 The broker's clock is the verifier's CLOCK_MONOTONIC (groundwork step 2, part 1)

Files: `src/dlls/wexhost/wexhost.spec`, `src/dlls/wexhost/wexhost.c`, `src/dlls/wexhost/wexhost_unix.h`,
`src/dlls/wexhost/unixlib.c`, `src/dlls/wexhost/e1s6.c`, `src/programs/wexelevate/e1s18_broker.c`,
`src/programs/wexelevate/test_broker_e1s18_main.c`.

S1 says all times are nanoseconds of the verifier host's `CLOCK_MONOTONIC`. The broker uses
`GetTickCount64` (`e1s18_get_monotonic_time` in `e1s18_broker.c`), which is a different clock.

1. Add a wexhost export `DWORD WINAPI WexMonotonicNs( UINT64 *now )` following exactly the
   pattern of `WexE1s6Transport`: spec line, PE wrapper in `wexhost.c`, params struct and enum entry
   in `wexhost_unix.h` (with a `C_ASSERT` on its size), the Unix function (in `e1s6.c`) calling
   `clock_gettime( CLOCK_MONOTONIC )`, and entries in BOTH call tables in `unixlib.c` (the native
   table and the WoW64 thunk table, keeping their order identical to the enum). It returns
   ERROR_SUCCESS and a nonzero value, or an error and 0 on clock failure or overflow.
2. In the PE build of the broker, `e1s18_get_monotonic_time` gets the time through
   `GetProcAddress( LoadLibraryW( L"wexhost.dll" ), "WexMonotonicNs" )` (cache the pointer; load
   once) instead of `GetTickCount64`; a missing export or a failure returns 0, which the callers
   already treat as a denial. Native builds keep `clock_gettime( CLOCK_MONOTONIC )`. The test clock
   hook stays first.
3. Set the request's absolute deadline once at the start of the standard-request handling (inside
   the WO-15 slot, before the prompt queue), store it in `struct request` (new field if needed), and
   use that same value everywhere a deadline is computed today (the 120 s prompt deadline and the
   evidence expiry checks must not extend it).
Tests: a clock failure at admission denies before any dialog; the prompt step and the evidence step
see the admission deadline, not a later one (advance the test clock between stages).
Check: the broker test command must pass; build wexhost for both ABIs
(`bash unsloth-tmp/s9-sync.sh` then `make -C /root/WE-prefetch/build/wow64 -j16 dlls/wexhost`
as root in wexbuild) and report the result.

## WO-17 The verifier keeps the broker's deadline (groundwork step 2, part 2)

Files: `tools/e1s6/e1s6-lab.py`, `tools/e1s6/test-evidence.py`.

`Transaction` uses its own five-second setup deadline (`self.deadline = time.monotonic_ns() +
5_000_000_000`) for the whole transaction, and the PAM start packet already carries the broker's
`expiry` (read at the `struct.unpack_from("<16sQQQII", start, 72)` line). Keep the five-second
limit only until the PAM start packet arrives; then require `now < expiry <= now + 300 s` (deny
otherwise) and set `self.deadline = expiry` for everything after, including `live()` and the
evidence binding. Tests in `test-evidence.py` (or a small new test in the same style): an expiry
in the past, an expiry more than 300 s ahead, and a valid expiry that becomes the deadline.
Check: `bash scripts/test_evidence.sh --disposable-guest` as root in wexbuild ends with
`E1S8_EVIDENCE OK`, and `python3 -I -m py_compile tools/e1s6/e1s6-lab.py` passes.
