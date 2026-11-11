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

## R8 orders (steps 2 and 4 of docs/E1_S18E_R8_REVIEW.md)

The frozen contract is `docs/E1_S18E_R8_SPEC.md` (v5.1; Codex cleared steps 2 and 4 in
`docs/E1_S18E_R8_SPEC_REVIEW.md` "v5 check"). Read sections 1, 2, 4, 5, 6, 7 and 11 first. Where
this order and the spec differ, the spec wins: stop and report the difference. Do not invent
fields, enum values, lengths or messages. Nothing here may touch the server, wexhost, consent's
UI, PAM, evidence or token formats, or S1 record kinds.

## WO-18 E1R8 C codec (R8 step 2, part 1)

Files: new `src/programs/wexelevate/e1r8_codec.h` and new
`src/programs/wexelevate/test_e1r8_codec_main.c` (GPL headers, see `scripts/add-license.py`),
`scripts/test_broker_e1s18.sh` (compile and run the new test).

1. `e1r8_codec.h`: header-only, portable C (`stdint.h`, `string.h` only; no Win32, no allocation),
   compiling for native gcc and both MinGW ABIs like `e1s_encode.h`. Explicit little-endian
   put/get helpers; never `memcpy` a struct onto the wire.
   * `struct e1r8_header` (decoded values) and `struct e1r8_tuple` (sealed tuple fields, section 2)
     with encode/decode of the 76-byte common header, the 160-byte tuple tail, the 204-byte
     pre-endorsement tuple and the 220-byte sealed tuple, at the section 4 offsets.
   * Type constants 1-9 and 16-27 and the enum values of section 5/6 (desktop_mode 1/2, RESULT
     status 1/2, DONE status 1/2, CANCEL reason 1, RELEASE outcome 1/2).
   * One encoder and one decoder per message type in the section 5 and 6 tables, each taking
     `(buf, len)` and the expected header values, returning 0 or a negative error, checking the
     exact total length for that type (fixed, or the min..max range with the string lengths
     consistent), magic, version 1, type, reserved 0, enum values, string rules (section 4:
     account names ASCII S4 grammar 1..63 like `wex_valid_account_name`; display strings UTF-16LE,
     even length, 0..520, valid surrogate pairs, no NUL or control characters; verifier owner
     `:<digits>.<digits>` 4..255), refused RESULT with every other body field zero/empty, and no
     trailing bytes. ANSWER (type 7) carries the unchanged 156-byte S1 kind-2 record: decode it
     with the fields of `struct wex_s1_answer_record` read field by field, and apply section 5
     (Confirm: answer_kind 2, round 2, index 0, empty payload; Cancel: answer_kind 3, round 0,
     index 0, empty payload, challenge zero; answer_kind 1 refused; `required_caps` within a mask
     argument; present fields equal the tuple).
   * S7 version 2 PROMPT (section 7 step 4): encode and decode the exact 178-byte prompt.
   * The secret password answer (section 7 step 5): encode/decode an S1 kind-2 record with
     `answer_kind` 1, round 1, index 0 and a payload of 1..4096 bytes (total 157..4252) that passes
     a strict RFC 3629 UTF-8 check (section 4: no truncated or missing continuation, no overlong,
     no surrogates, nothing above U+10FFFF, no C0, C1, F5..FF, no NUL).
2. `test_e1r8_codec_main.c` (native, links nothing else): for every message type a round trip at
   minimum and maximum length, then every section 11 codec vector (one byte short and long, wrong
   magic/version/type/reserved, unknown enum values, each bad string kind, bad S1 fields, refused
   RESULT with a nonzero field, the UTF-8 list, 4096 accepted and 4097 refused, empty password
   payload refused). A `golden` argument prints, one per line, `<type> <hex>` for each message
   type encoded from this fixed input: request_id 0x0102030405060708, attempt 1, nonce bytes
   0x10..0x2f, channel_id 0x30..0x3f, binding_digest 0x40..0x5f, request_digest 0x60..0x7f,
   deadline 0x1122334455667788, generations 1..6, caller_session 7, desktop_route 0x80..0x8f,
   desktop_mode 1, dialog_id 0x90..0x9f, account "admin", account_key 0xa0..0xaf,
   evidence_ref 0xb0..0xbf, challenge 0xc0..0xdf, verifier owner ":1.42", requester "Caller",
   image "x.exe", publisher "" (empty), pid 4242,
   uid 0, password "pässword" in UTF-8. These lines are WO-19's cross-check input.
3. `scripts/test_broker_e1s18.sh`: build the test natively with `-Wall -Wextra -Werror` and under
   ASan/UBSan, run both, and compile `e1r8_codec.h` once with each MinGW compiler (`-fsyntax-only`
   on a one-line `.c` including it, or `-c` the test file if it builds for PE).

Check: the broker test command passes (byte-identical production check included).

## WO-19 E1R8 Python codec (R8 step 2, part 2; after WO-18)

Files: new `tools/e1s6/e1r8.py`, new `tools/e1s6/test-e1r8.py` (GPL SPDX header like
`test-evidence.py`), `scripts/test_broker_e1s18.sh` (one cross-check step).

1. `e1r8.py`: standard library only (`struct`, no third-party packages). The same header, tuple,
   enum, type and string rules as WO-18, with `encode_<name>(...) -> bytes` and
   `decode_<name>(data, expected) -> dataclass` for every section 5 and 6 type, plus the S7 v2
   PROMPT and the secret password answer. Decoders raise one exception class, `E1R8Error`, on any
   rejection, with the reason in the message. UTF-8 is checked by an explicit byte-level validator
   implementing section 4, not by `bytes.decode` alone.
2. `test-e1r8.py` (`unittest`): the same vector list as WO-18 item 2, plus a test that reads the
   WO-18 `golden` lines from a file path given in `E1R8_GOLDEN` (skip if unset) and asserts that
   `e1r8.py` encodes byte-identical messages from the same fixed input and decodes each line back
   to that input.
3. `scripts/test_broker_e1s18.sh`: after the WO-18 test, write its `golden` output to
   `$FIXTURE/e1r8-golden.txt` and run `E1R8_GOLDEN=$FIXTURE/e1r8-golden.txt python3 -I
   "$REPO/tools/e1s6/test-e1r8.py"`; fail the script if it fails.

Check: the broker test command passes; `python3 -I -m py_compile tools/e1s6/e1r8.py` passes. Do not
import `e1r8.py` from `e1s6-lab.py` or any service yet.

## WO-20 Two explicit-list pipes and INIT/ACK (R8 step 4; after WO-18)

Files: `src/programs/wexelevate/e1s18_broker.c`, `src/programs/wexelevate/e1s18_broker.h`,
`src/programs/wexelevate/test_broker_e1s18_main.c`, `src/programs/consent/consent.c`,
`scripts/test_broker_e1s18.sh` only if a file must be compiled in. Read spec section 5 and
`e1s18_spawn_consent_dialog` (e1s18_broker.c, `#if defined(_WIN32) && defined(WEXELEVATE_E1S18)`).

1. Broker (lab branch only, all inside the existing `WEXELEVATE_E1S18` guards):
   * Create two anonymous pipes with non-inheritable handles: `to_d` (B writes, D reads) and
     `from_d` (D writes, B reads). Make only D's two ends inheritable, just before
     `CreateProcessW`; pass exactly those two in `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. B's own ends
     are never inheritable. After `CreateProcessW` succeeds close B's copies of D's two ends; on
     any failure close all four. Replace `/channel <h>` with `/in <h> /out <h>`.
   * INIT inputs the server and verifier will supply later (dialog_id, verifier owner, the
     generations, caller_session, desktop_route) come from a new context in the request that is
     zero in the lab today. A zero required input denies with ERROR_NOT_SUPPORTED before any pipe
     or process is created (the WO-14 pattern); tests set it through a test hook. Never fill a
     default. desktop_mode is 1 when `secure`, else 2.
   * Write INIT (WO-18 encoder; requester, image and publisher from the request's existing
     validated display strings, converted to UTF-16LE within the 520-byte bounds, else deny)
     and read exactly one 156-byte ACK, each in a worker thread doing exact-length
     `WriteFile`/`ReadFile` loops. The main thread waits on {worker done, D process, caller,
     `stop_event`, `lease_event`} with the existing consent timeout. Any other outcome cancels:
     `CancelSynchronousIo` on the worker, `TerminateProcess` D, wait for D (bounded 5 s), join the
     worker (bounded 5 s), close all handles; never return while a worker can still touch a freed
     buffer (if the join times out, fail closed and keep the buffer allocated; report it).
     An ACK that fails decoding, has wrong digests or dialog_id, is short, long, or late cancels.
   * After a valid ACK the existing flow continues unchanged: D's existing S1 Confirm/Cancel record
     arrives on `from_d` and is verified as today. No other message types in this order.
2. consent.exe (`/broker /credentials` mode only). `consent.c` ships: every change below goes inside
   `#ifdef WEXELEVATE_E1S18`, and without that define `consent.c` must compile to today's behaviour
   (the existing `/channel` path stays in the `#else`). In the lab build: parse `/in` and `/out` instead of `/channel`
   (reject both missing, duplicated or equal); before showing anything, read exactly one INIT from
   `/in` (bounded total size: the INIT maximum), decode it with `e1r8_decode_init_learn` (WO-18)
   passing the existing `/request` and `/token` arguments and attempt 1, keep the
   tuple and fill `data->nonce`, `data->binding_digest` and `request_digest` for the S1 record
   from it (today nothing sets them, so they stay zero), write ACK to `/out`, then
   continue as today, writing its S1 record to `/out`. Clear inheritance on both handles at start;
   consent spawns no child. Malformed or missing INIT: exit with the existing failure code without
   showing the dialog.
3. Tests (native lab test with fake pipes/threads where the Win32 path cannot run, plus the MinGW
   compile): oversized INIT, partial INIT then EOF, a writer blocked by a reader that never reads
   (cancellation via stop_event and via caller exit unblocks it), broken pipe (D exits before ACK),
   ACK short/long/wrong digest/wrong dialog_id, cancellation at each wait, an unrelated inheritable
   handle in the broker that D must not receive, B's copies of D's ends closed after success, all
   four closed after a failed `CreateProcessW` (test hook), zero context input denies before any
   pipe is created.

Check: the broker test command passes (ASan/UBSan and byte-identical production check included);
both MinGW lab broker compiles pass; `consent.c` compiles with both MinGW compilers with and without
`-DWEXELEVATE_E1S18` (`-Wall -Werror`, the include paths and libraries of
`scripts/test_credential_dialog.sh` lines 186-209); report the commands you used.

## WO-21 Tests for R8 step 3a-3c: dialog protection, loader pin, dialog image (for Antigravity)

Read docs/E1_S18E_R8_STEP3_PLAN.md (3a-3c) and the server code it names first: `new_process`,
`wex_dialog_pipes_valid`, `wex_handle_allowed`, `wex_dialog_pass_credentials` (wine/server/process.c),
`wex_dialog_fd_message` (request.c), `is_dialog_image` (sysrun.c). The server code is done; this order
adds a lab hook, two probe modes and harness cases. Do not change any wine/server file.

Files: `src/programs/wexelevate/main.c` (inside `#ifdef WEXELEVATE_TEST_APPROVAL` only),
`scripts/wexelevate_probe.c`, `scripts/test_sysrun_server.sh`.

1. Test broker hook (main.c, `#ifdef WEXELEVATE_TEST_APPROVAL` only, next to `secure_desktop_test`):
   when `HKLM\Software\WineExplorer\ElevationTest` DWORD `R8DialogTest` is 1, 2 or 3, the broker,
   once at service start before it serves requests, starts one child and writes a result file:
   * image: REG_SZ `R8DialogImage` (a full Windows path), extra arguments REG_SZ `R8DialogArgs`;
   * two pipes with `CreatePipe` and non-inheritable handles; mode 1 and 2 narrow the child's read end
     to `FILE_GENERIC_READ` and its write end to `FILE_GENERIC_WRITE` with `DuplicateHandle` (no
     `DUPLICATE_CLOSE_SOURCE`; close the originals); mode 2 instead passes the un-narrowed ends;
   * modes 1 and 2: `STARTUPINFOEXW` with `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` holding exactly those two
     handles, `EXTENDED_STARTUPINFO_PRESENT`, `bInheritHandles` TRUE; mode 3: no handle list,
     `bInheritHandles` FALSE (the unprotected control);
   * command line: `"<image>" <args> /in <read handle> /out <write handle>`;
   * result: REG_SZ `R8DialogResult` names a Windows file path; write `create_error=<GetLastError or 0>`
     and `pid=<child pid or 0>` lines; keep the broker's own pipe ends open until the child exits.
   The production `main.c` object must stay byte-identical (the broker test command checks it).
2. Probe modes (wexelevate_probe.c, follow the existing mode style):
   * `r8child <pidfile>`: write `pid=<GetCurrentProcessId()>`, then sleep 40 s and exit 0;
   * `openpid <pid> <outfile>`: `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)` and write
     `error=<0 or GetLastError()>`.
3. Harness (test_sysrun_server.sh), inside the R13 block that runs the test broker as the recognised
   `wexelevate.exe` (the `ln -f "$SYS32/wexelevate_test.exe" "$SYS32/wexelevate.exe"` part), each case
   setting the registry values with `reg_append` and restarting with `el_down`/`el_up`, both ABIs
   where the probe has both:
   * **protected**: mode 1, image the probe `r8child`; the caller probe `openpid` of the child pid as
     `$ADM` gets error 5 (access denied); PASS name `r8_dialog_protected_open_denied_<arch>`;
   * **control**: mode 3, same image; `openpid` gets error 0; `r8_unprotected_control_opens_<arch>`;
   * **bad rights**: mode 2; `create_error=5`, `pid=0`, no child process; `r8_dialog_unnarrowed_refused`;
   * lab build only (when the lab server has the trace, see below): the server log (run with
     `WEX_EL_LOG`) has `wex-r8: dialog <id> protected`, `wex-r8: dialog <id> pin pid=<n>` where `<n>` is
     the real Unix pid of the child (find it with `el_proc`), `image consent=0` for the probe child,
     and for a further mode 1 run with image `C:\windows\system32\consent.exe` and args
     `/broker /credentials`, `image consent=1`; PASS names `r8_pin_is_child_pid_<arch>`,
     `r8_image_probe_not_dialog`, `r8_image_consent_is_dialog`.
   Restore the registry and the broker exactly as the R13 block does afterwards.
4. Lab trace: the lab server objects get `-DWEX_R8_SERVER_TEST`. In the lab-tree build used by the
   harness (see unsloth-tmp/wo4-verify.sh for the pattern) rebuild `server/process.o server/request.o
   server/sysrun.o server/handle.o` with `CFLAGS="-g -O2 -DWEX_E1S6_TRANSPORT_TEST -DWEX_R8_SERVER_TEST"`;
   the normal build has no trace, so the trace cases are skipped there and only the protected,
   control and bad-rights cases run.

Check: the broker test command passes; `WINE_BUILD_DIR=/root/WE-prefetch/build/wow64 bash
scripts/test_sysrun_server.sh` as root in wexbuild passes with the new cases (was 320 PASS); report the
PASS count and the new PASS lines. Run long commands in the background and wait for them; before a
wexbuild run check nothing else runs (`pgrep -fa 'test_e1s|e1s11-final|make -C|test_sysrun_server'`)
and afterwards run `bash unsloth-tmp/clean-wexbuild-fixture.sh`. Never use VM B.

## WO-22 R8 3e-1: kernel pin for every process (lab only; for Antigravity)

Design: docs/E1_S18E_R8_3E_DESIGN.md section 9 point 3. Read it, docs/E1_S18E_R8_STEP3_PLAN.md (3b) and
the 3b code first: `wex_dialog_pass_credentials` and `new_process` in wine/server/process.c,
`wex_dialog_fd_message`, `pidfd_pid` and `receive_fd` in wine/server/request.c. Claude reviews the result
before anything is committed.

Files: wine/server/process.c, process.h, request.c (and the place where the first client process is
created from the master socket, find it), scripts/test_sysrun_server.sh, scripts/wexelevate_probe.c only
if a probe mode is needed. Everything new is inside `#ifdef WEX_E1S6_TRANSPORT_TEST`; a build without
that define must produce exactly today's behaviour. Do not touch the protected-dialog rules (3a/3b).

1. In lab builds, when the system role is enabled (`system_role_enabled()` / `sysrun_active`, follow how
   process.c already tests it), set SO_PASSCRED and SO_PASSPIDFD on the server end of every new process
   socket: in `new_process` (the existing helper may be reused) and where the first client process is
   created. Failure leaves the process without a pin; it never denies the creation (only protected
   dialogs deny).
2. `receive_fd` for a process that is not a protected dialog, in lab builds: keep today's fd handling
   exactly, and additionally parse SCM_CREDENTIALS and SCM_PIDFD. Every received pidfd is closed unless it
   becomes the pin: the first transfer whose pidfd names the live process its credentials name
   (`pidfd_pid`) sets `wex_pin_pidfd/pid/uid` (CLOEXEC); later pidfds are closed. Never leak an fd: with
   SO_PASSPIDFD the kernel attaches a pidfd to every message.
3. A helper `int process_pin_matches( struct process *process )` (process.c, lab only): the pin exists,
   `pidfd_pid(pin) == wex_pin_pid` (still alive) and `wex_pin_pid == process->unix_pid`. Not used yet;
   3e-3 will call it.
4. Trace under `WEX_R8_SERVER_TEST`: `wex-r8: process <id> pin pid=<pid>` when a non-dialog pin is set.
5. Tests in test_sysrun_server.sh, lab tree only (the lab build compiles server objects with
   `-DWEX_E1S6_TRANSPORT_TEST -DWEX_R8_SERVER_TEST`; see unsloth-tmp/r8-3bc-trace.sh for building such
   a tree and its `wine` symlink): (a) a caller probe's pin pid equals its real Unix pid
   (`r8_caller_pin_is_unix_pid_<arch>`, both ABIs); (b) no fd leak: count the wineserver's open fds
   (`/proc/<wineserver pid>/fd`) before and after a probe mode that opens and closes 2000 files (add
   `fdchurn <n>` to the probe if no such mode exists), the count may differ by at most 5
   (`r8_pidfd_no_leak`); (c) the normal (non-lab) build still passes everything it passed (325).

Check: build the main tree and a lab tree as in unsloth-tmp/r8-3bc-trace.sh (with both defines on
server/process.o server/request.o server/sysrun.o server/handle.o and any other object you change),
run the system-role suite against both (`WINE_BUILD_DIR=<tree> timeout 3300 bash
scripts/test_sysrun_server.sh`), report both PASS counts and the new PASS lines. Run long commands in
the background; before a wexbuild run check nothing else runs; afterwards run
`bash unsloth-tmp/clean-wexbuild-fixture.sh`. No commits. Never use VM B.

## WO-23 R8 3e-2/3e-3: caller desktop and the BINDING operation (lab only; for Antigravity)

Design: docs/E1_S18E_R8_3E_DESIGN.md sections 9 and 10 (read both; section 10 amends 9). Read the
existing `get_pipe_caller_binding` handler, `struct pipe_writer`, `retain_consumed_writer` and
`clear_pipe_writer` (wine/server/named_pipe.c), `sysrun_e1s6_endorse` (sysrun.c), `process_pin_matches`
(process.c), `read_policy`/`read_protected`/`parse_map` (accounts.c) and `e1s_prefix_id` (registry.c).
Claude reviews the result before anything is committed.

Files: wine/server/named_pipe.c, protocol.def (one reply field, then regenerate with
`tools/make_requests` as Wine does: include/wine/server_protocol.h, server/request_handlers.h,
server/request_trace.h), sysrun.c/.h, accounts.c/.h, registry.c (only to expose `e1s_prefix_id` to
the lab code), wine/dlls/ntdll/unix (the existing `NtQueryWexPipeCallerBindingEx` path: pass a reply
buffer for the new data; both ABIs), scripts/test_sysrun_server.sh, scripts/wexelevate_probe.c.
All new server behaviour inside `#ifdef WEX_E1S6_TRANSPORT_TEST`; without it the reply data is always
empty and everything else is unchanged.

1. Desktop at write (3e-2): `struct pipe_writer` gains `struct desktop *desktop` (retained, NULL if the
   writing thread has none); set where a message's writer is recorded (`get_thread_desktop_quiet(current)`,
   take a reference), moved with the writer into `consumed_writer`, released in `clear_pipe_writer`.
2. protocol.def: `get_pipe_caller_binding` REPLY gains `VARARG(binding,bytes);` (empty unless op 1).
3. BINDING (op 1): the request payload is exactly 8 bytes: magic `E1R8`, version u16 = 1, op u16 = 1
   (little-endian). Any other payload keeps today's S6 meaning. On op 1, after all the existing
   authority and writer checks (factor them into one function used by both paths):
   * the caller is the verified broker (`sysrun_process_is_broker`) with a current S6 registration;
   * the opener has a live pin (`process_pin_matches( client->client_process )`);
   * the consumed writer has a retained desktop;
   * no binding exists yet on this pipe end (one BINDING per connection);
   * create a binding record owned by the server end: five fresh 16-byte random IDs (`/dev/urandom`,
     all-zero rejected) for prefix_id, host_process_id, opener_id, writer_id, desktop_route; retained
     references to the opener process, the writer process and the desktop; `server_gen` and
     `broker_gen` from the broker's S6 registration (expose a sysrun.c helper; deny if unset);
     `map_gen` read now from the protected accounts map (new accounts.c helper reusing
     read_policy/read_protected/parse_map; deny on any error); `root_gen` and `policy_gen` from a new
     sysrun.c helper keeping a server-global random tag per path that changes when the stat tuple
     (st_dev, st_ino, st_size, st_mtim, st_ctim) of the machine root directory, respectively the
     protected sysrun.conf, differs from the last one seen; `caller_session` = opener's session id;
   * the reply data is exactly 124 bytes, little-endian, in this order: prefix_id, host_process_id,
     opener_id, writer_id, desktop_route (16 each), server_gen, root_gen, broker_gen, map_gen,
     policy_gen (u64 each), caller_session (u32).
   The record is released with the server end (disconnect, close, destroy). Denials use the existing
   `E1S6_BINDING_AUDIT` sites with new names.
4. ntdll: `NtQueryWexPipeCallerBindingEx` takes an optional output buffer for the binding data (keep the
   existing callers working; follow how the request payload is passed today), both ABIs.
5. Tests (lab tree; the lab build compiles named_pipe.o sysrun.o accounts.o registry.o process.o
   request.o handle.o with `-DWEX_E1S6_TRANSPORT_TEST -DWEX_R8_SERVER_TEST`, see
   unsloth-tmp/r8-3bc-trace.sh): a probe mode (in the S6 test broker path the harness already uses for
   transport tests) that runs op 1 and checks the reply: 124 bytes, all IDs nonzero and distinct,
   generations nonzero, session equals the caller's; a second op 1 on the same pipe is denied; a
   payload of 7 or 9 bytes or a wrong magic/version/op is denied; with the map's generation changed
   between two requests map_gen differs; touching sysrun.conf changes policy_gen; the normal build
   still passes 325 and its reply data is empty.

Check: main and lab trees built, both suites run (report both PASS counts and new PASS lines), the S6
transport suite (`WINE_BUILD_DIR=<lab> bash scripts/test_e1s6_transport.sh`, was 302 PASS) still
passes, the broker test command passes. Long runs in the background, nothing else running in
wexbuild, `bash unsloth-tmp/clean-wexbuild-fixture.sh` afterwards. No commits. Never use VM B.

Result (2026-11-09, Antigravity; Claude reviewed and ran the final check): normal suite 325 PASS (no
binding data); lab S6 transport suite passes with 20 R8 lines on both ABIs (`r8_binding_op1`,
`r8_binding_map_gen_changed`, `r8_binding_policy_gen_changed`, `c4_e1s6_r8_impostor`); broker test and
E1R8 codec cross-check (11 cases) pass. Review fixes: the S13 lease reuses a prefix ID that BINDING
already created; the test restores accounts.map before its checks can fail. Known limit: if the reply
fails after the record is created, the connection cannot retry BINDING (lab only). The Antigravity job
stalled after its work and was cancelled; a leftover `wexgreeter` account from a killed run broke the
normal suite until the fixture cleanup removed it.

## WO-24 R8 3d-1/3d-2: ENDORSE_DIALOG and RECHECK operations (lab only; for Antigravity)

Contract: docs/E1_S18E_R8_SPEC.md sections 2-4 (tuple layout and offsets, server endorsement, rechecks);
design: docs/E1_S18E_R8_3E_DESIGN.md sections 9 and 10. Builds on WO-23 (`struct pipe_binding`, the
op 1 path in `get_pipe_caller_binding`, wine/server/named_pipe.c) and 3a-3c (`wex_creator`,
`wex_protected`, `wex_dialog_image`, `wex_pin_*`, `process_pin_matches`, wine/server/process.c/.h).
Claude reviews the result before anything is committed. Not in this order: the ENDORSED message to the
verifier (WO-25) and the verifier asking for rechecks.

Files: wine/server/named_pipe.c, process.c/.h (one helper), sysrun.c/.h only if a helper is missing,
scripts/wexelevate_probe.c, scripts/test_sysrun_server.sh. No protocol or ntdll change (the WO-23 reply
VARARG and the extended `NtQueryWexPipeCallerBindingEx` structure carry everything). All new server code
inside `#ifdef WEX_E1S6_TRANSPORT_TEST`.

1. Helper `process_unstarted_suspended( struct process * )` in process.c: the created-suspended proof of
   `process_e1s_profile_committable` (lines from `e1s_profile_dead` through the single suspended first
   thread) without the profile list and lease checks. Do not change `process_e1s_profile_committable`.
2. `struct pipe_binding` gains, at BINDING (op 1): a retained reference to the opener's primary token
   and its token id and modified id. It also gains the endorsement: `int endorsed`, the retained D
   process, D's primary token with its token id and modified id, D's pin pid, `dialog_id[16]` and the
   sealed tuple (220 bytes). `clear_pipe_binding` releases all of it.
3. ENDORSE_DIALOG (op 2). Request payload exactly 216 bytes: the 8-byte header (`E1R8`, version 1, op 2),
   D's process handle as u32 (an `obj_handle_t` in the caller's table), then the 204-byte pre-endorsement
   tuple. Reply data exactly 16 bytes: `dialog_id`. Run the existing writer checks first (shared
   function). Deny with a distinct audit site name unless every check holds:
   * caller is the verified broker (`sysrun_process_is_broker`), the current thread has no token
     (`!current->token`), `sysrun_get_s6_generations` succeeds;
   * this pipe end has a binding and it is not endorsed yet (one endorsement per binding);
   * D: `get_process_from_handle( handle, PROCESS_QUERY_LIMITED_INFORMATION )`; `D->wex_creator ==
     current->process`; D is a protected dialog (`wex_protected[0]` set); `D->wex_dialog_image`;
     `process_unstarted_suspended( D )`; `token_service_account( D->token ) == SERVICE_ACCOUNT_SYSTEM`;
     `process_pin_matches( D )`; `D->session_id == binding->caller_session`; D's desktop object (from
     `D->desktop` via the desktop lookup the server already uses for a process's desktop handle) equals
     `binding->desktop` (pointer equality);
   * the tuple, read at the spec offsets: server_gen, broker_gen, map_gen, root_gen, policy_gen,
     caller_session and desktop_route equal the binding record; `desktop_mode` is 1 or 2; deadline is
     nonzero; the current generations still equal the record (`sysrun_get_s6_generations`,
     `accounts_get_map_generation`, `sysrun_get_root_generation`, `sysrun_get_policy_generation`);
     verifier_gen is not checked by the server (section 10);
   * the opener is alive (`!is_terminating`, `running_threads > 0`), its primary token is still the
     retained token, and its token id and modified id are unchanged.
   Then assign `dialog_id` (16 random bytes, nonzero, different from the five binding IDs), retain D,
   D's token and ids, the pin pid and the sealed tuple (pre-endorsement tuple + dialog_id), set
   `endorsed`, and return `dialog_id`.
4. RECHECK (op 3). Request payload exactly 24 bytes: header (op 3) and `dialog_id`. No reply data. Deny
   unless: the caller checks of step 3 hold; the binding is endorsed and `dialog_id` matches; D is
   alive, its token is still the retained token with unchanged ids, its session and desktop are
   unchanged, its pin matches; the opener checks of step 3 hold; all five generations still equal the
   record. RECHECK does not require D to be suspended (D runs after endorsement).
5. Tests (lab tree as in WO-23, both ABIs). New probe mode `r8endorse <result file>` in the broker role
   (installed as `wexelevate_test.exe` like `r8binding`): BINDING on its own pipe, then create
   machine-root `C:\windows\system32\consent.exe` (64-bit; `syswow64` for the 32-bit probe) with
   `CREATE_SUSPENDED` and an explicit handle list of two narrowed pipe ends (copy mode 1 of
   `r8_dialog_test`, src/programs/wexelevate/main.c), build the 204-byte tuple from the BINDING reply
   (any nonzero request_id, nonce, channel_id, digests, deadline, verifier_gen; desktop_mode 1), and
   write one `name=PASS|FAIL` line per check:
   * ENDORSE succeeds with a nonzero 16-byte dialog_id; a second ENDORSE on the same pipe is denied;
   * RECHECK with that dialog_id succeeds; RECHECK with a changed dialog_id is denied;
   * on a fresh pipe and fresh suspended consent.exe each: a tuple with one changed generation is denied
     (one case per generation: server, root, broker, map, policy); a changed desktop_route is denied;
     a changed caller_session is denied; desktop_mode 3 is denied; payload sizes 215 and 217 are denied;
   * a resumed consent.exe (ResumeThread before ENDORSE) is denied; the probe's own image as D (not
     consent.exe) is denied; ENDORSE without a prior BINDING is denied;
   * after a successful ENDORSE, `touch` of sysrun.conf (the harness does it between two probe phases,
     as WO-23 does) makes RECHECK deny; after TerminateProcess of D, RECHECK is denied.
   Terminate every consent.exe the probe created before it exits. Harness: one `ok r8_endorse_<name>_<arch>`
   per line, both ABIs, restoring any file it changes before a check can fail (WO-23 pattern).

Check: the normal (main) build suite still passes 325 and contains no `r8_endorse` lines; the lab S6
transport suite (`WINE_BUILD_DIR=<lab> bash scripts/test_e1s6_transport.sh`) passes with all the new
lines; `bash scripts/test_broker_e1s18.sh` passes. Report both PASS counts, the new lines, and the diff
summary. Long runs in the background, nothing else running in wexbuild, `bash
unsloth-tmp/clean-wexbuild-fixture.sh` before and after each suite. When the check is done, write the
report and end the job; do not wait or loop afterwards. No commits. Never use VM B.

Result (2026-11-10, Antigravity, 106 minutes; Claude reviewed): normal suite 325 PASS (no r8_endorse
lines); lab S6 transport suite 365 PASS with 38 `r8_endorse_*` lines (19 per ABI); broker test and
codec cross-check pass. Review: payload sizes and tuple offsets match spec section 4; every retained
reference is released on all paths. Known limits (lab only): ENDORSE and RECHECK still require the
consumed writer (a later read on the same pipe clears it and RECHECK then denies); the generation
checks are duplicated between ops 2 and 3. The job also reverted an unrelated uncommitted file
(tests/uwp/u1_0/run-wine.sh), restored by Claude.

## WO-25 R8: the broker fills its R8 context from S6, BINDING and ENDORSE (lab branch; for Qwen)

Goal: replace the zero R8 context (WO-20) with live inputs, so `e1s18_spawn_consent_dialog`
(src/programs/wexelevate/e1s18_broker.c) runs the server-side protocol of WO-23/WO-24. Code and native
unit tests only; the in-Wine end-to-end run is a later order. Claude reviews before commit.

Files: src/programs/wexelevate/e1s18_broker.c, e1s18_broker.h, test_broker_e1s18_main.c,
src/include/e1s6_protocol.h, src/dlls/wexhost/e1s6.c (step 2 only), scripts/test_broker_e1s18.sh only if a
new test needs a build line. Everything new stays under `WEXELEVATE_E1S18` (the shipping broker object
must not change: compare the production `main.c`/broker objects as WO-20 did with
unsloth-tmp/wo20-consent-obj.sh, or state why not possible).

1. Four new members of `struct e1s18_ops` (the existing test-override table), with default Win32
   implementations and native stubs returning `ERROR_NOT_SUPPORTED`:
   * `r8_open( req, unsigned char reg[E1S6_OPEN_SIZE], UINT64 *channel )`: `WexE1s6Transport( req->pipe,
     E1S6_OPEN, ... )` as in the existing redeem path (e1s18_broker.c around line 1149);
   * `r8_owner( req, UINT64 channel, char owner[256] )`: the new transport op of step 2;
   * `r8_binding( req, unsigned char out[124] )`: `NtQueryWexPipeCallerBindingEx` on `req->pipe` with the
     extended structure (Size = sizeof the extension, input = 8-byte header `E1R8`, version 1, op 1,
     output buffer 124 bytes; layout of `WEX_PIPE_CALLER_BINDING_EXT` in wine/dlls/ntdll/sec.c and
     `R8_PIPE_CALLER_BINDING_EXT` in scripts/wexelevate_probe.c); success only with exactly 124 bytes back;
   * `r8_endorse( req, HANDLE dialog, const unsigned char tuple[204], unsigned char dialog_id[16] )` and
     `r8_recheck( req, const unsigned char dialog_id[16] )`: ops 2 (216-byte payload: header, the dialog
     handle as u32, the tuple) and 3 (24 bytes: header, dialog_id), same call; endorse succeeds only with
     exactly 16 nonzero bytes back.
2. wexhost: transport operation `E1S6_OWNER_QUERY 11` (header define with a comment): on an open,
   bound channel only, copies the verifier's unique bus name recorded at OPEN (`owner`, e1s6.c) into a
   256-byte buffer, NUL-terminated; any other state denies like an unknown operation.
3. `e1s18_r8_prepare( req )` (portable, called at the top of `e1s18_spawn_consent_dialog` instead of
   relying on a preset context): `r8_open`, then `r8_binding`, then `r8_owner`; parse the BINDING reply
   (prefix_id, host_process_id, opener_id, writer_id, desktop_route at 0/16/32/48/64; server_gen 80,
   root_gen 88, broker_gen 96, map_gen 104, policy_gen 112; caller_session 120) and the broker
   registration (first 112 bytes of the OPEN record: server_gen at 48, broker_gen at 56, verifier_gen at
   64; confirm these against the `registration(...)` calls in tools/e1s6/e1s6-lab.py and e1s6.c lines
   461-468 before coding, and report any difference instead of guessing). Deny (`ERROR_ACCESS_DENIED`) unless the BINDING server_gen and broker_gen equal the
   registration's and every value is nonzero; the verifier owner must pass
   `e1r8_valid_verifier_owner`. Fill `s_r8_context` from them (root_gen, map_gen, policy_gen,
   caller_session, desktop_route from BINDING; verifier_gen and the owner from S6), leaving `dialog_id`
   zero. Close the S6 channel (`E1S6_CLOSE`) on every path after OPEN. Tests keep using
   `e1s18_set_r8_context` (a preset complete context skips prepare only when a test flag says so; no
   production path skips it).
4. `e1s18_build_r8_tuple` builds the pre-endorsement tuple without requiring `dialog_id`; the sealed tuple
   is completed by step 5. `e1s18_r8_context_complete` checks everything except `dialog_id`.
5. In `e1s18_spawn_consent_dialog`: create D with `CREATE_SUSPENDED` (keep everything else of the
   WO-20/3a creation: two narrowed ends, explicit handle list); encode the 204-byte pre-endorsement tuple
   with the existing codec (`e1r8_codec.h`); `r8_endorse` with `pi.hProcess`; on success put the
   returned `dialog_id` into the tuple and the context, encode INIT from the sealed tuple, then
   `ResumeThread( pi.hThread )` (close the thread handle only after resuming), then the existing INIT/ACK
   worker. After a valid ACK, `r8_recheck`; a failed endorse or recheck terminates D (`r8_stop_worker`
   pattern) and returns `ERROR_ACCESS_DENIED`. Every early return closes all four pipe handles, both
   process and thread handles, leaves the secure desktop if entered, and never leaves D suspended.
6. Native tests (test_broker_e1s18_main.c, using `e1s18_set_ops`): prepare fills the context from fake
   OPEN/BINDING/owner data; prepare denies when BINDING server_gen or broker_gen differs from the
   registration, when any BINDING value is zero, when the owner is invalid, when OPEN/BINDING/owner fails
   (and closes the channel after a successful OPEN); the pre-endorsement tuple bytes equal the codec
   golden layout with the context values; the sealed tuple carries the endorsed dialog_id. Keep every
   existing test passing.

Check: `bash scripts/test_broker_e1s18.sh` passes (native, ASan, both MinGW compiles, codec cross-check);
report the new test names. Do not run Wine or anything in wexbuild other than the compilers and this
script. No commits. Never use VM B.

Amendment (Claude, after reading the S6 control path for WO-26): the verifier keeps its R8 attempt
records per S6 session, and ENDORSED travels on that session's control socket, so `e1s18_r8_prepare`
must not close the S6 channel. It keeps the channel in the attempt and closes it when the attempt ends
(every exit of `e1s18_spawn_consent_dialog` after a successful prepare).

Result (2026-11-10, Qwen, 36 minutes; Claude reviewed): six ops (`r8_open`, `r8_owner`, `r8_binding`,
`r8_endorse`, `r8_recheck`, `r8_close`), `E1S6_OWNER_QUERY 11`, `e1s18_r8_prepare`, D created suspended,
endorsed, resumed, rechecked after ACK; native tests for prepare and the tuples. Claude's fixes: the
amendment (prepare keeps the channel, `e1s18_r8_release` closes it after the dialog call, a second
attempt while one is in flight is refused), a failed `ResumeThread` terminates D, a pointer-sign cast in
wexhost. `bash scripts/test_broker_e1s18.sh` passes (native, ASan, both MinGW, production objects
byte-identical, codec cross-check); wexhost e1s6.c compiles cleanly with and without the lab define. Not
run in Wine yet (WO-27).

## WO-26 R8: ENDORSED from the server to the verifier (lab only; for Antigravity, after WO-25)

Contract: docs/E1_S18E_R8_SPEC.md section 6 (ENDORSED, type 16, 244 bytes: 76-byte header, 160-byte
tuple tail, D's pin pid u32, uid u32; D's pidfd as SCM_RIGHTS) with this amendment, which the order also
writes into the spec: the S6 control socket is shared with the broker (it passes the handle), so the
endpoint alone does not authenticate the server. ENDORSED therefore carries, like the S6 endorsement
(wine/server/sysrun.c `sysrun_e1s6_endorse`), explicit SCM_CREDENTIALS with the server's pid, euid and
the effective system gid (sent under `sysrun_raise`), the kernel's SCM_PIDFD of the sender (the
verifier sets SO_PASSPIDFD), and exactly one SCM_RIGHTS fd (D's pidfd). The verifier checks the sender
exactly as `endorsement()` does (tools/e1s6/e1s6-lab.py: uid, system gid, wineserver executable inode,
the session's server pid and pin).

Files: wine/server/sysrun.c/.h, process.c/.h, named_pipe.c, tools/e1s6/e1s6-lab.py,
docs/E1_S18E_R8_SPEC.md (section 6 amendment only), scripts/wexelevate_probe.c,
scripts/test_sysrun_server.sh. Server code inside `#ifdef WEX_E1S6_TRANSPORT_TEST`. Do not touch
src/programs/wexelevate, src/dlls/wexhost or src/include (WO-25 owns them).

1. Server: at a successful S6 endorsement, retain a duplicate of the control socket fd on the broker
   process (`e1s6_control_fd`, replacing and closing the previous one; closed when the process is
   destroyed).
2. Server, in ENDORSE (WO-24 op 2), after all checks and before publishing the endorsement: build
   ENDORSED with the R8 header (magic `E1R8`, version 1, type 16, length 244, request_id, attempt,
   reserved 0, nonce, channel_id from the pre-endorsement tuple), the tuple tail (bytes 60..203 of the
   tuple plus the new dialog_id), D's pin pid and D's real uid; open a fresh pidfd for D
   (`pidfd_open`), send one `sendmsg` with the three control messages on `e1s6_control_fd`
   (`MSG_DONTWAIT | MSG_NOSIGNAL`), close the pidfd. A missing control fd or a short send denies the
   endorsement with an audit site, and nothing is retained.
3. Verifier (e1s6-lab.py): watch each session's control socket outside `endorsement()`; a packet that
   starts with `E1R8` and type 16 is ENDORSED: one `recvmsg` with `MSG_CMSG_CLOEXEC`, exactly 244 bytes,
   no truncation, exactly SCM_CREDENTIALS + SCM_PIDFD (kernel) + one SCM_RIGHTS fd, no other ancillary
   data; sender checks as above; the received fd is a pidfd whose fdinfo `Pid:` equals the body pid
   (alive) and whose process's real uid equals the body uid; header fields valid (version 1, length 244,
   reserved 0); request_id/attempt not already recorded; at most 8 records per session (spec section 1).
   Store the record (tuple tail, pid, uid, pidfd owned by the record, closed when the session ends) and
   audit `E1R8_ENDORSED ok request=<hex> pid=<n>`; any failure audits a distinct denial and closes every
   received fd. An ENDORSED arriving while `endorsement()` waits on the same socket must not be lost
   (dispatch by magic in one receive path).
4. Tests: in the WO-24 `r8endorse` lab case, after the successful ENDORSE the harness finds exactly one
   `E1R8_ENDORSED ok` line in the verifier log with the consent process's Unix pid
   (`r8_endorsed_to_verifier_<arch>`); an ENDORSE on a fresh pipe after the probe closed its S6 channel
   is denied and adds no `E1R8_ENDORSED` line (`r8_endorsed_needs_channel_<arch>`); the existing 38
   `r8_endorse` lines still pass.

Check: normal suite 325 (no new lines), lab S6 transport suite passes with the new lines, broker test
passes. Long runs in the background, nothing else running in wexbuild, `bash
unsloth-tmp/clean-wexbuild-fixture.sh` before and after each suite. Never run `git checkout`, `git
restore`, `git stash` or any command that discards changes: other agents edit other files of this
tree. Write the report and end the job when done. No commits. Never use VM B.

Result (2026-11-10, Antigravity, about 70 minutes; Claude reviewed): normal suite 325; lab S6 transport
suite 369 PASS with `r8_endorsed_to_verifier` and `r8_endorsed_needs_channel` on both ABIs and the 38
`r8_endorse_*` lines; broker test passes. Claude's fix: D's pidfd is a duplicate of its kernel pin, not
`pidfd_open` by pid (pid reuse); re-run of the lab suite after it: 369 PASS. The verifier does not yet
compare ENDORSED's tail with records of its own (it has none before the attach step).

## WO-27 R8 end to end in Wine: a standard user's request reaches an endorsed, acknowledged dialog (lab; for Antigravity, after WO-26)

Goal: the first in-Wine run of the standard-user chain as it exists: request, S6 OPEN, BINDING, the S1
context, consent.exe created suspended, ENDORSE, ENDORSED at the verifier, INIT, ACK, RECHECK. Product
first (Claude, 2026-11-10): one positive run per caller ABI and only the negative case listed; no matrix.
Claude reviews before commit.

Files: src/programs/wexelevate/e1s18_broker.c/.h, test_broker_e1s18_main.c (keep it passing; adjust
only what the moved gate changes), wine/server/named_pipe.c (step 2), scripts/wexelevate_probe.c and
scripts/test_sysrun_server.sh (the BINDING length and the new case), unsloth-tmp/wo23-build-lab.sh (lab
build of the broker and consent.exe). Lab defines only; the shipping broker and consent objects stay
byte-identical (`bash scripts/test_broker_e1s18.sh` checks the broker; unsloth-tmp/wo20-consent-obj.sh
the consent object).

1. Broker: run `e1s18_r8_prepare` before the WO-14 S1 gate in `e1s18_handle_standard_request` (after the
   nonce) and fill `s_s1_context` from the same live data instead of leaving it zero: generations (root,
   server, broker, map, policy from BINDING; verifier from S6), prefix, host_process, opener, writer,
   desktop (= desktop_route) from BINDING, channel from the S6 broker registration (offset 88, 16 bytes),
   host_uid from BINDING (step 2), the image identity (volume serial, file index, size, last write) from
   the image the broker froze (`freeze_image`, main.c; use what it already holds), and the deadline from
   the request (`E1S18_DEADLINE`). The prompt path then reuses the prepared context and the open
   channel (prepare is not run twice; `e1s18_r8_release` still closes the channel when the attempt
   ends, on every exit of `e1s18_handle_standard_request`).
2. Server: the BINDING reply gains `host_uid` u64 at offset 124 (the opener's native uid from its pin's
   credentials, `wex_pin_uid`), so the reply is 132 bytes; update the WO-23 probe check and the broker
   parse (`r8_binding` expects 132). Nothing else in the server changes.
3. Lab trace: under the lab define, the broker writes one line per milestone to its log
   (`r8: prepare ok`, `r8: endorse ok`, `r8: ack ok`, `r8: recheck ok`, or `r8: <step> failed <error>`).
4. Lab build: build the broker (`programs/wexelevate`) with `-DWEXELEVATE_E1S18` and consent.exe
   (`programs/consent`) with `-DWEXELEVATE_E1S18` in the lab tree (MinGW objects: find how the Wine
   makefiles pass flags to PE objects, as wo23-build-lab.sh does for the Unix lab objects; report the
   exact command). The main build stays as it is.
5. Test (lab S6 transport suite, both caller ABIs): with the lab verifier running and
   `HKLM\Software\WineExplorer\Elevation\EnableStandardUserElevation` set in the fixture's machine hive,
   the standard user `wexstd` asks the lab broker to elevate a program (use the probe mode the existing
   standard-user cases use; report which). Pass when the broker log has `prepare ok`, `endorse ok`,
   `ack ok` and `recheck ok` in that order and the verifier log has one `E1R8_ENDORSED ok` with the
   consent.exe pid (`r8_e2e_ack_<arch>`). After `recheck ok` the dialog waits for input: the harness
   closes it (or lets the consent deadline expire) and the request ends with a denial; the case passes
   on the milestones, not the final status. Negative (one): with the verifier stopped, the request is
   denied and no consent.exe process was created (`r8_e2e_no_verifier_denied`).
6. If the chain stops earlier, do not add workarounds in the server or verifier: report the first
   failing milestone, its error and the log lines around it.

Check: normal suite 325, lab S6 transport suite passes with the new lines and every earlier line, broker
test passes. Same rules as WO-26 (background runs, clean fixture before and after, never discard changes,
no commits, never VM B, end the job after the report).

Result (2026-11-10; Antigravity 138 minutes, then Claude): the standard-user chain runs end to end in Wine:
`r8_e2e_ack_64` and `r8_e2e_ack_32` (prepare, endorse, ENDORSED at the verifier, ack, recheck, in order)
and `r8_e2e_no_verifier_denied` pass; lab S6 transport suite 372 PASS, normal suite 325, broker test
passes. The first run stopped at ENDORSE (`dialog-desktop-mismatch`): 3e decision 4 was wrong (corrected
in the 3e design, section 10). Claude's further fixes: the secure desktop stays the default (Antigravity
had switched it off); the probe joins WinSta0\Default like an interactive program (`runinteractive`, and
the WO-24 cases, now desktop_mode 2); the lab build keeps the E1S18 broker and consent as separate
`-e1s18` files and the case swaps them into the build tree in place for its own duration (Wine loads
system32 builtins from the build tree by module name; the server recognises them by the machine-root
entry); the case restarts the verifier after its negative step (the fixture lock is reused); the
`E1S1-REQ` tag rewrite that dodged the installed-tree check is reverted. Open: run earlier in the suite,
the case leaves state that makes the later `c4_refused_consent_is_error_cancelled` time out (1460); it
now runs last in the C4 section. Checked by hand in VM B (2026-11-10: labwc on the real display, the
wexadm shell running, WinSta0 held as in a real session): a refused admin elevation ends with 1223 in
14 seconds, so this is a leftover of the lab fixture, not a product hang; not pursued further.

## WO-28 R8 account selection and worker start: SELECT_OK, SELECT, START, WORKER_READY, ATTACH_OK (lab; for Antigravity)

Goal: extend the WO-27 end-to-end chain from `recheck ok` to `attach_ok sent`: after the recheck, B and D
agree the account (SELECT_OK, SELECT), B starts the verifier's worker for it (START), V spawns W parked with
W_SETUP and answers WORKER_READY, and B sends ATTACH_OK to D. Product first (Claude, 2026-11-10): one
positive end-to-end run per caller ABI plus the negatives named below; no matrix. Claude reviews before commit.
Contract: docs/E1_S18E_R8_SPEC.md sections 4-7 and 9 (messages, encodings, states; section 7 step 1 for W).

Read first: the WO-25..WO-27 sections and their code (e1s18_broker.c `e1s18_spawn_consent_dialog` after the
recheck; consent.c, the `WEXELEVATE_E1S18` path after the ACK; tools/e1s6/e1s6-lab.py, the session class with
its `pam-start` phase and `worker_factory`; tools/e1s7/supervisor.py and pam-worker.c, the S7 worker and its
supervisor; e1r8_codec.h for the message codecs (add any missing one there with a codec test case, and to
tools/e1s6/e1r8.py with the golden cross-check).

Files: src/programs/wexelevate/e1s18_broker.c/.h and test_broker_e1s18_main.c, e1r8_codec.h and
test_e1r8_codec_main.c, src/programs/consent/consent.c (lab path only), src/dlls/wexhost/e1s6.c only if START
needs a transport operation, tools/e1s6/e1s6-lab.py, tools/e1s6/e1r8.py and test-e1r8.py, tools/e1s7/ (W
bootstrap and W_SETUP), scripts/test_sysrun_server.sh (the WO-27 case), scripts/test_broker_e1s18.sh only for
build lines. Everything new under `WEXELEVATE_E1S18` or the lab gates; shipping objects byte-identical.

1. B, after `recheck ok`: write SELECT_OK (76 bytes) to D, read SELECT (account:str, S4 grammar, 81..143
   bytes) within the attempt deadline, resolve the account through S4 (the map lookup the broker already uses
   for account keys; an unknown name gives the zero key, the decoy of spec section 6), send START on the open S6
   channel (account:str, account_key:16), wait for WORKER_READY, then write ATTACH_OK (76) to D. One trace line
   per milestone: `r8: select ok`, `r8: start sent`, `r8: worker ready`, `r8: attach_ok sent`.
2. D (consent, lab path): after the ACK, enable the account field only after SELECT_OK; on OK send SELECT
   with the entered name; keep the password field disabled (it is enabled only after READY, a later order).
   Lab test hook, compiled only with `WEXELEVATE_E1S18`: if
   `HKLM\Software\WineExplorer\ElevationTest\R8AutoSelect` names an account, D sends SELECT with it without
   input (the headless harness cannot type); never in a shipping build.
3. V (e1s6-lab.py): accept START only in state `ENDORSED` for the attempt whose ENDORSED record it holds
   (request_id, attempt), validate it (spec section 4), and spawn W through the e1s7 supervisor, parked in
   bootstrap: a SOCK_SEQPACKET pair for the secret socket with SO_PASSCRED and SO_PASSPIDFD on both ends
   (checked), W's supervisor control socket as a separate socketpair, and W_SETUP as W's first control message
   (244 bytes: tuple tail from the ENDORSED record, D's pid and uid, D's pidfd as the only SCM_RIGHTS; spec
   section 6 transfer rules). W checks W_SETUP and the pidfd (fdinfo `Pid:`, real uid), then waits without
   reading secret data. V moves to `WORKER_READY` and sends WORKER_READY (76) to B on the S6 data socket. Audit
   `E1R8_WORKER_READY request=<hex> pid=<W pid>`.
4. Test: extend `r8_e2e_case`: set `R8AutoSelect` to the fixture's admin account; pass on the new milestones
   in order plus one `E1R8_WORKER_READY` line (`r8_e2e_attach_ok_<arch>`, both ABIs). Negatives: a START for an
   attempt V holds no ENDORSED record for is refused (verifier audit, no worker)
   (`r8_start_without_endorsed_refused`); an unknown account name still yields WORKER_READY with a decoy (zero
   key) and no evidence (`r8_select_unknown_account_is_decoy`). Native codec tests for any new codec.

Check: normal suite 325, lab S6 transport suite passes with every earlier line and the new ones, broker test
and codec cross-check pass. Same rules as WO-26/27: background runs, clean fixture before and after, never
discard changes (no git checkout, restore, stash or reset), no commits, never VM B, put multi-part guest or WSL
commands in script files (PowerShell strips inner double quotes), and if the chain stops before
`attach_ok sent`, report the first failing milestone with its log lines instead of working around it. End the
job after the report.

Result (2026-11-10; Antigravity about 150 minutes, stopped at the user's request; Claude finished it): the
chain runs to `attach_ok sent` on both caller ABIs (`r8_e2e_attach_ok_64/32`, with the worker parked after
W_SETUP and `E1R8_WORKER_READY` at the verifier); `r8_start_without_endorsed_refused_64/32` and
`r8_select_unknown_account_is_decoy` pass; lab S6 transport suite 375 PASS, normal suite 325, broker test and
codec cross-check pass. Where Antigravity stopped: the positive chain worked, its START negative ran as a plain
user process and never reached the START check. Claude's fixes: that negative now runs from the verified
broker-role `r8binding` probe through the transport (`E1S6_R8_START 12`, defined in e1s6_protocol.h); the
verifier's hardcoded source-tree import paths and its silently optional evidence service are gone (the fixture
installs `e1r8.py` and `evidence.py` next to the verifier, imports are mandatory, `supervisor.py` is pinned
again); the case's `rm -rf` of the whole libexec directory (launcher and running verifier) is replaced by
removing its own three files; the broker traces whether the selected account resolved (`r8: account
known|decoy`) and the cases assert it. Known limits (lab): the broker resolves the account by reading
accounts.map through the Z: drive (must become a server lookup before leaving the lab; marked in the code); the
verifier sends WORKER_READY when W is spawned, without an acknowledgement that W accepted W_SETUP; D blocks its
dialog thread while it waits for ATTACH_OK; an ENDORSED for a dialog that already died drops the whole S6
session (`endorsed-dialog-alive`).

## WO-29 R8 steps 5-8 and the launch in one push: password, result, confirm, token, profile, program (lab; Claude)

Owner direction (2026-11-10): the product working end to end comes before harness work; build the rest of the
password path in one push to a first elevated launch, then review and add negatives once on the whole chain.
No per-step work orders, no additions to the fake-based broker test.

Result (2026-11-10): **a standard user's request, with an administrator's account and password, starts the
program under the administrator's token in Wine (lab build), for 64- and 32-bit callers.** The launched program
reports elevation type 2 (full), integrity 12288 (high), Administrators enabled, Program Files writable and the
administrator account's own SID as its user. Lab S6 transport suite 378 PASS (`r8_e2e_elevated_64/32`,
`r8_wrong_password_refused`, `r8_select_unknown_account_is_decoy` now refused through the whole conversation,
`r8_e2e_no_verifier_denied`); broker test and codec cross-check pass. The chain stays lab only and disabled.

What was built:

* **Dialog side** (`consent.c`, `src/dlls/wexhost/e1s6.c`; `E1S6_DLG_*` in `e1s6_protocol.h`): after ATTACH_OK
  the dialog calls the verifier's `DialogAttach` on its own system-bus connection to the owner named in INIT,
  takes the secret socket end and the worker's pidfd, compares the tuple tail, sends ATTACH_ACK, waits for the
  broker's READY and the worker's 178-byte prompt (sender pid, uid and pidfd checked against the worker's pin),
  sends the password as the S1 kind-2 record on that socket only, then takes RESULT, answers Confirm with the
  challenge and waits for DONE. `PR_SET_DUMPABLE` 0 is set and checked before the attach. The operations ride
  on the existing `WexE1s6Transport` export (no new export or Unix call table entry).
* **Worker** (`tools/e1s7/pam-worker.c`, `--bootstrap`): after W_SETUP it takes the broker's START unchanged
  (account and key), then ATTACH_ACK from the pinned dialog, W_ACK, PROMPT_RELEASE, the version 2 prompt and
  the one answer, and continues on the existing normalized PAM path. The account's uid and map generation come
  from the protected map itself; the selected key must be the account's current incarnation.
* **Verifier** (`tools/e1s6/e1s6-lab.py`, `supervisor.py`): `DialogAttach` (caller must be the endorsed dialog:
  same pid and uid on the bus, pin alive; a refused call changes nothing), W_ACK -> DELEGATED and
  PROMPT_RELEASE, the worker's private result -> evidence record (kind 3) and RESULT, one redemption, RELEASE ->
  RELEASED. From ENDORSED the session lives until the broker's attempt deadline instead of the 5 s setup bound.
  The dialog's death ends its attempt until it is redeemed or refused.
* **Broker** (`e1s18_broker.c`): DELEGATED, recheck, READY, the verifier's RESULT, the final-account check
  (section 8), RESULT with a one-use challenge, Confirm, recheck, redemption, DONE, RELEASE; then the existing
  stages with their real inputs: the evidence record's checks, token issue, storage query, profile load,
  environment, suspended create, child check, lease, commit, resume. Stage tracing (`r8: <stage> failed <n>`,
  `r8: launched`).
* **Lab build** `scripts/lab/r8-lab-build.sh`: one lab tree with the S6, R8, S10 token and S13 profile switches
  (the earlier R8 lab tree had no token or profile code, so nothing past the dialog could have run in it).

Found on the way (each cost one run):

* The launch-preparation stage was scaffolding in the real broker: fixed uid 1000, a made-up SID and profile
  path, and a host-side routine that needs file descriptors a PE process does not have. On the live path the
  environment is now the issued token's own block (`CreateEnvironmentBlock`), nothing from the caller.
* The broker took the dialog's clean exit after DONE for a cancellation (the dialog can leave before the
  writer thread returns); the pipe helper now lets the writer's own result decide.
* The verifier must not fail a session for an ENDORSED whose deadline is out of range (a lab probe sends one):
  it only does not extend the session.
* After a WSL restart WSLg mounts `/tmp/.X11-unix` read-only and Xvfb cannot start: run the suite in a private
  mount namespace with an owned directory bound over it (`unshare --mount --propagation private`).
* A failed R8 case leaves the lab broker and consent in the build tree's normal file names; rebuild the lab
  tree before the next run.

**Lab stand-ins and deviations that must go before this leaves the lab** (none is hidden; each is marked in
the code):

1. **The broker installs its own consumed record** before token issue (`WEX_TOKEN_TEST_INSTALL`, lab servers
   only). The server-side receipt from the verifier (R10) is not built, so today the broker vouches for itself.
   This is the main open security item of the chain.
2. **No product provisioning of the administrator's profile** in the requesting user's prefix: only the lab
   tool (`tools/wex-profile` with its test switch) can create one; the test case runs it. A first elevation on
   a real machine would stop at the storage query.
3. **The verifier is the lab script** (`e1s6-lab.py`), with its lab bus name and policy file; no installed
   service exists. The R8 path also bypasses `evidence.py`'s throttling and attempt accounting (the record is
   built directly): repeated wrong passwords are limited only by PAM.
4. Redemption uses two lab frames (types 28 and 29) with the record checked by the broker; DONE is written
   before the launch commit; the working-directory containment rule of launch preparation is not applied on the
   live path; the broker still reads `accounts.map` through the Z: drive.
5. Dialog: the password field is enabled before READY (typed before the single OK click; it is sent only after
   the worker's authenticated prompt); one attempt per dialog (a wrong password ends the request); the dialog
   thread blocks during the check, which the worker pads to about 4 s for every outcome, success included; the
   verifier does not watch the dialog's bus name (it watches the pin).
6. The broker's waits on the verifier (DELEGATED, RESULT) block inside the transport call: caller death or
   broker stop is noticed only when the verifier cancels or the deadline passes.
7. `R8AutoPassword` (lab hook next to `R8AutoSelect`, compiled only with `WEXELEVATE_E1S18`) types a throwaway
   fixture password for the headless run.

Not done yet: review of the whole chain with the security-relevant negatives added once (attach by a foreign
process, replayed Confirm, D or W death per state), the interactive check of the dialog in a VM, R4 rest, R5,
R7, R14, then S19 and S20.

## WO-30 Review of the WO-29 chain and its negative cases (Claude, 2026-11-10)

Review of the whole WO-29 diff (dialog, wexhost, broker, verifier, worker). No blocking defect in the password
path: the password travels only between the dialog and the worker, the worker checks the sender of every packet
against the pinned dialog, the verifier burns the evidence before comparing the reference, and a dead dialog
ends the attempt. The WO-29 stand-ins are unchanged and still decide whether this may leave the lab.

Fixed by the review:

* Verifier `DialogAttach`: the caller's identity (bus pid and uid, live pin) is checked before the attempt's
  state and tuple, so a foreign caller learns nothing about the attempt.
* wexhost: a refused `E1S6_DLG_ANSWER` is wiped whatever its size (it was wiped only above 512 bytes).
* Broker: the retained evidence record is cleared at the start of every dialog step, so a record can never
  outlive its attempt.

Negative cases added to the lab suite (security-relevant only):

* `r8_foreign_attach_denied`: another process of the same user calls `DialogAttach` with the live request id.
  Refused at `attach-sender-pid`, nothing attached, the dialog keeps running.
* `r8_dialog_killed_no_launch`: correct password, dialog killed while the worker holds its answer back. No
  authenticated result, no redemption, no program.

Result: lab S6 suite 380 PASS, normal suite 325 PASS, broker test and codec cross-check pass.

Noted, not changed: the broker waits for the verifier inside the transport call and so does not itself see the
dialog or the caller go away during the password check; the verifier's dialog pin ends the attempt instead
(covered by the second case). The caller leaving during that window is not covered by a test.

## WO-31 R10: the verifier's receipt replaces the broker's self-install (lab; Claude, 2026-11-10)

Removes WO-29 stand-in 1. Design: docs/E1_S18E_R10_DESIGN.md with the corrections of
docs/E1_S18E_DESIGN_REVIEW.md items 2 and 3, in the simplest form that keeps their point: the server takes
the consumed record from the verifier, never from broker bytes.

* Verifier (`tools/e1s6/e1s6-lab.py`): at redemption, after burning the evidence and before answering the
  broker, it sends RECEIPT (E1R8 type 30: header, evidence reference, the 220-byte record, canonical account
  name) on the S6 control socket with its kernel credentials. Because it is sent first, it is already queued
  at the server when the broker asks; no status query or acknowledgement is needed.
* Server (`wine/server/named_pipe.c`, op 4): see docs/STOCK_CHANGES.md. Sender, framing, every evidence
  field against the sealed tuple, time and generations are checked; the kind 4 input is built from the
  sealed tuple and the receipt; the retained opener process and token are the caller. One attempt per
  binding; a failure burns the context.
* Broker (`e1s18_broker.c`): NEGOTIATE, then op 4 with the context handle, then ISSUE. The fixture install
  is gone. The policy generation in its ISSUE input is the attempt's (it was a literal 1, which only the
  fixture accepted).

First run finding: the record's root generation is the verifier's own instance value and the server's is a
path generation; they cannot be compared. The server keeps its own value (from the sealed tuple) in the
kind 4 input and checks the verifier generation against the tuple instead.

Negative case added: `r8_no_receipt_no_token` (the lab verifier withholds the receipt through
`WEX_E1S6_LAB_NO_RECEIPT=1`; the broker has its evidence copy, the server refuses at `receipt-receive`,
token issue fails, nothing starts).

Result: lab S6 suite 381 PASS, normal suite 325 PASS, broker test and codec cross-check pass.

Still open from the R10 design: a revocable transaction owner that outlives the evidence (the verifier
retires the attempt at RELEASE before the token is issued; the installed record is validated by the
verifier's live pin and unchanged map, root and policy generations only), NEGOTIATE before redemption, and
the receipt's root generation having no server-side counterpart.

## WO-32 First-use provisioning of the administrator's profile (lab; Claude, 2026-11-11)

Removes the WO-29 stand-in "only the lab tool can create the administrator's profile". The test no longer
prepares a profile; the first elevation creates it.

* Helper (`tools/wex-profile/wex-profile-provision.c`, root-owned, installed next to the verifier; denies
  in a normal build like the rest of S12): arguments are the account name, its key, the map generation, the
  standard user's uid and the number of the prefix descriptor. It looks the account up again in the
  protected accounts map (administrator, same key and generation, not the user itself; the SID comes from
  there), takes the user's group from the password database, requires the descriptor to be a directory
  owned by that user, opens the fixed root-protected seed edge by edge, then gives up root permanently and
  calls `wex_profile_prepare`. An existing profile is validated and reused. The path helpers shared with
  the lab driver moved to `tools/wex-profile/anchor.h`.
* Server: ENDORSED carries the prefix directory as a second descriptor (docs/STOCK_CHANGES.md).
* Verifier (`tools/e1s6/e1s6-lab.py`): pins the helper at startup; checks the received prefix (directory,
  owned by the caller's uid) and keeps it with the attempt; after the worker reports success and before the
  result goes to the broker it runs the helper (empty environment, only the two descriptors, 10 s bound)
  and logs `E1R8_PROFILE`. A failure ends the attempt: without a profile nothing can be launched.

First run finding: the seed path macro was named like the storage status `WEX_PROFILE_SEED` and broke the
enum when defined on the command line; it is `WEX_PROFILE_SEED_PATH`.

Cases: `r8_e2e_elevated_64` requires `reused=0`, `r8_e2e_elevated_32` `reused=1`;
`r8_provision_refuses_bad_input` (wrong account key stops at `site=account`, a directory of another user
at `site=prefix`, nothing is created).

Open: the helper runs synchronously in the verifier's main loop; a second elevation while the profile is
loaded by the server was not tried (R14); the seed in the lab is the test's own file, the projection of
`/etc/skel/.wine/user.reg` is unchanged S12 work; the helper's result line is not bound to the attempt
beyond its exit status.

## WO-33 Review R14: a second elevation while the first program runs (lab; Claude, 2026-11-11)

Before: the server refused every LOAD for an account whose hive was loaded, so a second elevated program
could not start until the first had ended and the hive was unloaded.

* Server (`wine/server/registry.c`, docs/STOCK_CHANGES.md): a LOAD for the exact account of a loaded,
  committed hive joins it with its own lease. Live users were already counted per SID, so unload and the
  idle reaper need no change beyond returning a failed joiner's lease.
* Verifier: the provisioning helper reports busy (9) when the profile is locked by the server that has it
  loaded; that is accepted, nothing was created, and the server's own LOAD decides.

Case: `r8_second_elevation_while_first_runs` (the first elevated program holds; the second is launched as
the elevated administrator; the server logs the joined lease; the first is released afterwards).

Not covered: a joiner whose setup fails after LOAD (the lease return path is untested), three or more
concurrent logons, a join while another setup is still uncommitted (refused, the caller would have to
retry), the reaper unloading the hive after both programs have ended.

## WO-34 A wrong password leads to a further attempt (lab; Claude, 2026-11-11)

Before: one refused password ended the request; the person had to start the program again. The spec
(section 1) allows attempts 1..3 per request, each with a fresh dialog, on the one admission deadline.

* Broker (`e1s18_broker.c`): after a refused RESULT (and only then) the dialog step starts again with the
  next attempt number and a new channel id, up to three attempts and only while at least 10 s of the
  deadline are left. The S10 input carries the attempt that was redeemed.
* Server (`wine/server/named_pipe.c`, docs/STOCK_CHANGES.md): a binding may be endorsed again for the next
  attempt once the previous dialog is gone.
* Dialog (`consent.c`): `/attempt N` from the broker; after a refusal it says "Please try again" on
  attempts 1 and 2. Lab hook `R8AutoPassword2` is typed on a further attempt.
* Verifier: a further ENDORSED needs the next attempt number and every earlier attempt refused or retired.

Cases: `r8_retry_after_wrong_password` (wrong, then right: one refusal, one redemption, the program runs
as the elevated administrator); `r8_wrong_password_refused` now requires exactly three refused attempts
in one request and no launch.

Open: the 120 s deadline covers all three attempts including about 4 s of padding each; nothing limits
the number of requests (throttling belongs to the installed verifier); the nonce is the request's, only
the channel id and dialog are new per attempt; a retry after Cancel or a transport failure is not offered.

## WO-35 The real password window, driven by the keyboard (lab; Claude, 2026-11-11)

Until now every R8 run typed the password through the lab hook, without a window. New case
`r8_typed_by_keyboard`: no hook; xdotool on Xvfb (the host keyboard, as in the secure-desktop section)
types the account and a wrong password, then the account and the right password into the fresh dialog;
the program runs as the elevated administrator. `WEX_R8_SHOTS=dir` keeps screenshots of each step.

The window path worked on the first run. The screenshots showed two defects, both fixed in `consent.c`:

* The window named `setup.exe` (the default of an absent `/cmd`). It now shows the image from INIT, the
  one the broker bound into the attempt; an INIT without a usable image ends the dialog.
* The message box after a refusal was a black rectangle: a second window is not presented on the secure
  desktop. There is no message box any more. The fresh dialog of a further attempt shows "The user name
  or password is incorrect. Please try again."; an outcome that ends the request is shown in the dialog
  itself for 2.5 s.

Open: the account name is not carried into the fresh dialog (typed again); the INIT publisher and
requester are still empty (the dialog checks the signature itself); Cancel and the close button in the
window were not driven; the UI thread still blocks while the verifier answers.
