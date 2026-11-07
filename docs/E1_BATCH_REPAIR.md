<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1 combined-build and independent-review repair

2026-11-06. Working-tree edits only; no commit, build, WSL, VM, PAM or Wine
execution. Inputs read: `ChromiumLab/e1s-baseline/s9-final-report.md` in full and
`ChromiumLab/guest-sysrole/s678-review-antigravity.md` in full, plus S4/S5/S6/S7/
S8/S10 package notes, CLAUDE.md, TOOLING.md and STOCK_CHANGES.md. Each review
item was checked against its code before editing it. Earlier reported passes
are historical evidence; they do not validate these edits.

## Confirmed combined-build/test items

| Item | Verdict and repair | Files | Still unverified |
| --- | --- | --- | --- |
| S7 `R_OK` compile collision | Real. All enum members and references renamed `RES_*`; numeric audit values preserved. No old `R_*` tokens remain in worker or script. All native variants retain strict warning flags. Disabled-worker checks require exit 1, preventing missing executable/exit 127 from being a false PASS. | `tools/e1s7/pam-worker.c`, `scripts/test_pam_worker.sh` | GCC compile/link of disabled, fixed, test worker and PAM module against installed libpam-dev; all real PAM tests. |
| S6 retained token assignment | Real. Explicit `(struct token *)grab_object(...)`; fd duplicates use CLOEXEC, and gated read/send/format comparisons have explicit appropriate sizes. Reviewed every test-gated sysrun/S10 token block and other S6 server files; other retained object assignments already cast properly. | `wine/server/sysrun.c` | Fresh all-server-lab-defines build with NO `-fpermissive`, no new warnings; normal server build too. |
| S10 WINBOOL and casts | Real mixed SDK include-path bug. Include the dependency-free byte header by source-relative path; documented MinGW commands use only `-Isrc/include`, never `-Iwine/include`. Both GetProcAddress results cast through `(void *)`. | `scripts/e1s10_token_probe.c`, `docs/E1_S10_TOKEN.md` | Both MinGW compilers with `-Wall -Wextra -Werror`; S10 runtime matrix. |
| S6 fixture refuses existing role/users | Correct refusal, not a code defect. Handoffs explicitly require root in clean wexbuild or a throwaway VM, NEVER shared VM B. Removed the harness's suggestion to remove an existing role; no ownership checks weakened. TOOLING records the prior VM B deletion and re-provision via `guest-sysrole/push.sh`. | S6/S7/S10 docs, TOOLING.md, sysrun/S6/S7 scripts | Clean-fixture root runs and cleanup; no VM B state touched here. |
| `token.c:2104` array-bounds warning | Real undersized declared C object allocation. Reserve at least `sizeof(struct group)` for the restricted null-SID group; logical SID/count unchanged. | `wine/server/token.c`, STOCK_CHANGES.md | Rebuild warning disappearance, advapi32 security regression controls. |

## All eight independent-review findings

| Finding | Verdict after inspection | Change / files | Limits and next validation |
| --- | --- | --- | --- |
| 1. D-Bus Open closes reply sockets | The early original close exists, but **not a demonstrated EBADF bug under the documented API**. Upstream UnixFd owns a dup, contrary to the review's ownership premise. Requested conservative lifetime change still applied. | `tools/e1s6/e1s6-lab.py`: retain originals through synchronous serialization, close on a high-priority next-turn callback; cancellation removes callback and closes them too. | Do not retain until transaction end: that masks EOF. Real Open, disconnect, serialization failure and fd counts still need the deployed dbus-python. [Upstream ownership contract](https://dbus.freedesktop.org/doc/dbus-python/dbus.types.html#dbus.types.UnixFd). |
| 2. Removed wexhost sync_account_map | **Intentional S4 correction, not a bug.** Unprivileged code could not perform root-only synchronization. | S4 and STOCK_CHANGES explicitly explain removal from `user_create`; provisioning and root path/timer/oneshot own synchronization. No wexhost call restored. | Non-systemd/custom paths still need equivalent explicit root lifecycle integration; asynchronous sync cannot replace retirement/rename coordination. |
| 3. executable_fd inherited by PAM helpers | **Real.** pass_fds clears CLOEXEC for exec, and the worker did not restore/close its executable pin. | `supervisor.py` passes `--executable-fd`; `pam-worker.c` validates distinctness and closes it before NSS/PAM, restoring CLOEXEC on retained transaction fds. Test module checks every remaining nonstandard fd is CLOEXEC. | Fresh actual worker/module/helper runs and fd leak checks required. |
| 4. Unlocked requests lookup in serve | **Real lock-discipline defect.** CPython's atomic dictionary lookup does not make the multi-step ownership check synchronized. | `evidence.py`: remove redundant unlocked precheck; call locked `Service.cancel` directly. | Real cancellation/expiry races still unrun. No claim of a reproducible torn CPython dictionary object. |
| 5. Account-existence prompt/timing oracle | **Real.** Unknown selections skipped prompts/PAM and returned immediately. | Default S7 asks one fixed masked prompt before lookup; known/unknown/ineligible outcomes release four seconds after answer. Bounded backend overrun sends the same failure. Private failure-only DECOY=6; S8 rejection now shares normal worker admission, active slot, attempts, throttle and ready path. Supervisor/S8 reject successful decoys. Tests compare real PAM classes and protect decoy quotas. | Fixed single-password lab policy only. Unsupported backend shapes fail closed; raw multi-round tests require a separate root-only test override compiled out of fixed builds. Three samples/class, identical full prompt transcript, first-prompt spread <=250 ms, results 3.9..4.25 s post-answer with spread <=250 ms. Unrun. General MFA needs an equivalent decoy policy. NSS resolution before the S18 factory, audit/storage/transport faults, quotas, scheduler overload, cancellation and exhausted original deadline are not constant-time guarantees; S18 must validate whole-path timing too. |
| 6. dup without CLOEXEC | **Real descriptor-hygiene defect.** | `sysrun.c:e1s6_get_fd`: `fcntl(F_DUPFD_CLOEXEC)`, atomic CLOEXEC on duplication. | Native lab build and fd lifecycle stress unrun. |
| 7. Cancellation audit loses IDs | **Real.** | `evidence.py`: log each matched request with its object, actual request ID and attempt; cancel-all logs each. Unmatched IDs emit no fake request=0 cancellation. Component test covers multiple requests and foreign connection isolation. | Audit output/races/storage limits unrun. |
| 8. Numeric proc exe lookup | **Partly real.** Existing post-stat live pidfd check already rejected the described PID-reuse substitution. The directory was still reopened by numeric PID. | `e1s6-lab.py`: derive/check PID from the kernel-issued pin, bracket one proc-directory open with live pin checks, retain it with server pin, open exe relative to it and compare fstat device/inode with expected pinned binary. Added live/wrong/dead-pin cases. | A pidfd's self/fd link is anon_inode, not an executable path. No PID-only fallback. This does not freeze later exec, memory changes or same-inode content mutation; trusted root executable policy remains required. Kernel race/fd-count tests unrun. |

## Static exclusion and checks performed

* Root and Wine `git diff --check`: PASS. Untracked touched files also checked
  for trailing whitespace/line-ending damage separately.
* Git Bash `bash -n` on changed sysrun/S6/S7 shell scripts: PASS; scripts were
  parsed, not executed.
* Exact existing broker/consent grep assertions: PASS (grep exit 1, no stderr).
  Expanded grep and independent rg checks over src/installer/tools/Wine build
  and install files: PASS, no occurrences of any of
  `WEXELEVATE_TEST_APPROVAL`, `WEXCONSENT_TEST_HOOK`,
  `WEX_E1S6_TRANSPORT_TEST`, `WEX_E1S10_TOKEN_TEST`, `WEX_E1S7_LAB`,
  `WEX_E1S7_TEST`, `WEX_ACCOUNTS_TEST` in the selected build-file extensions.
* Reviewed gated implementations and stubs. S4 production maintenance/parser,
  S5 existing-path hardening and S10 protocol/export/denial scaffolding remain
  intentional; disabled lab authorization, factory, prompt and fixture machinery
  are not enabled in production. Native S8 defaults disabled and is not installed.
* Byte scans, ASCII and UTF-16 like `has_str`, of saved **old baseline** artifacts
  (no executable run): shipping brokers, both ABIs, lack `wexelevate_stub`,
  `E1S1-REQ`, `WexE1s6Transport`; test brokers contain all three. Normal server
  lacks `wexelevate_test.exe` and `E1S token context (private)`; S6/S10 servers
  have the S6 marker and only S10 has the token-context marker. Normal wexhost.so
  lacks `org.wineexplorer.E1S6Lab`; S6/S10 versions contain it. Old disabled PAM
  binary lacks `E1S7PAM`. **These scans do not validate rebuilt repair artifacts.**
* Harness now repeats normal/lab runtime string checks with positive controls
  and verifies required files exist; S7 checks fixed-versus-test override strings.
  New binary checks and installed-image/nm checks await Qwen.

Python parsing was not run: `where.exe python python3 py` returned
`INFO: Could not find files for the given pattern(s).` C includes, prototypes,
wire offsets, signal masking, timer release race, ownership and cleanup were
reviewed manually. No compiler, generator or runtime result is inferred.

## Qwen next, exclusively in wexbuild / throwaway VM

1. Preserve/sync the full uncommitted batch and these repairs. Fresh normal and
   lab WoW64 objects; both architectures. Force sysrun/token rebuild with the
   S6/S10 flags in the isolated lab only, **no -fpermissive**. Confirm no new
   warnings, including the restricted-group allocation. Regenerate/compare
   protocol artifacts as already documented; no protocol edit in this repair.
2. Compile every S7 script variant with its existing strict flags and libpam;
   compile the S10 probe with both corrected MinGW commands and `-Werror`.
   Parse all changed Python, then run S8 component tests.
3. As root, run the sysrun/S6 provisioning fixtures on a clean machine without
   an installed role. Provision S2 disposable identities/password input locally
   for S7 and run `test_pam_worker.sh --disposable-guest wex-e1s-test` there.
   NEVER remove VM B's role or accounts to run these tests.
4. Require real S6 acceptance/Open/EOF/impostor controls before S10's owned S6
   infrastructure matrix. Run all existing S4/S5 controls and both ABIs'
   pipe/security/wex_token regression tests. Repeat strings/nm/installed-artifact
   exclusion against newly built normal artifacts, with positive test controls.
5. Collect prompt/timing results without secrets; do not weaken tolerance or
   accept missing binaries/skips as PASS. Validate backend overrun, unsupported
   transcript, deadline, cancellation, process-group reaping, PID churn and fd
   counts. All remain unverified here.

## Tooling failures retained

`exec_command` returned exit 0/empty output even for `pwd` and
`Write-Output 'probe'; [Console]::WriteLine('console')`; cause not exposed.
Direct Node filesystem/child-process calls produced visible inspection results.
No empty shell output was counted as validation.

`rg --files -g AGENTS.md -g !wine/**` hit
`rg: ./unsloth-tmp: Access is denied. (os error 5)`; replaced by explicit scoped
instruction-path searches. An exploratory `rg -n struct group|sub_auth[` scan
failed with `error: unclosed character class`; fixed literal/scoped reads were
used. Exploratory gate scans naming root `configure.ac`/`Makefile.in` and later
`wine/Makefile.in` failed with `The system cannot find the file specified. (os
error 2)`; successful final scans use real directories and `wine/configure.ac`.
No incomplete scan was counted as a pass.

One combined documentation patch failed atomically with
`apply_patch verification failed: Failed to find expected lines` because S8's
text begins `can never produce evidence` rather than `It can never produce
evidence`. Confirmed no partial changes; reapplied against exact context.
All edits used the patch tool; no C escapes passed through shell heredocs.
No VM/distro/service/fixture state was changed, and nothing was committed.
