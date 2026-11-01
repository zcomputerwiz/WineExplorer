# Mojo token continuation: corrected native baseline

The pinned Chromium `AccessToken::FromCurrentProcess(false, TOKEN_ADJUST_DEFAULT)`
calls `FromProcess`, which adds `TOKEN_QUERY` to the `OpenProcessToken` request.
The probe now requests `TOKEN_QUERY | TOKEN_ADJUST_DEFAULT` (`0x88`) before
`RevertToSelf`, rather than `0x80`. Its failure diagnostic names the same mask.
Token restrictions, DACL construction, transport ordering, and return codes
were not changed.

## Native baseline

The corrected x86_64 PE was compiled with the existing WSL MinGW compiler:

```text
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -o <private-output>/chromium-mojo-token-0x88-x64.exe tests/probes/chromium_mojo_bootstrap_probe.c -ladvapi32 -luserenv
```

SHA-256: `8ca51fa79d67407cdf983d54b8fe445e2964fddfb75241aae3b477c6197bd2f9`.

On the native host, `all` returned exit 0; `plain`, `service_restricted`, and
`network_appcontainer` each reported PASS, with `failures=0 overall=PASS`.
The sandboxed modes verified the probe invitation payload and completed the
delayed integrity-level change before reverting the thread token.

The same source was also compiled with `i686-w64-mingw32-gcc` using the same
flags/libraries. Its x86 PE SHA-256 is
`9d27ae910a940b1cb4415bff2fda7ab087cd8a408f223630bc08fd3a567fac92`.
The native x86 `all` also returned exit 0 and all three modes reported PASS,
`failures=0 overall=PASS`. This covers native 32-bit behavior, not guest
WoW64 startup, which remains blocked below.

This is the probe's result, not proof of actual Chromium invitation acceptance
or browser startup. The probe's return-code names are not independent evidence
of browser fatal codes.

## Prior guest attempts and remaining prerequisites

The earlier r2/r3 guest attempts used the old PE, timed out after 150 seconds,
and emitted no probe output. They do not establish any token-mode verdict.
Run B in r2 did not select the patched server. Run B in r3 selected an owned
scratch patched server, but still timed out before output. The actual preserved
guest binaries discovered were under `build/wow64/loader/wine` and
`build/wow64/server/wineserver`; their layout must be inspected rather than
assuming an i386-only sub-build or an absent top-level launcher.

Only `server/token.c` differs between Wine `ce87bac0` and `f2e67abe`; protocol
definitions are unchanged. This source comparison is not enough to prove
the guest's complete client/server/Unix/PE runtime provenance.

Before a new matrix: verify minimal executable startup, fresh-prefix
initialization, module closure, actual ELF architecture, display/session
configuration, and matched server identity. Whole token-object security
descriptor preservation still needs native `DuplicateTokenEx` contract checks;
the current Wine candidate remains unaccepted.

Preserve old logs/builds and use only fresh, exclusively owned paths and
exact-owned cleanup. Do not enable automatic inbox checks.

## r4 startup diagnosis: blocked before the matrix

The fresh `mojo-token-recon-r4` guest inspection established that the preserved
build has only `build/wow64`, not a top-level `build/loader`. The real loader
and server are ELF x86-64, and the loader's `--version` immediately reports
`wine-11.20`. This refutes the static i386-sub-build hypothesis; changing to an
absent top-level loader is not a solution.

The guest's Unix `ntdll.so` and both i386/x86_64 `ntdll.dll` hashes match the
reconciliation host build exactly. The evidence also records loader/server,
core Unix/PE modules, program hashes and ELF dependencies. This is targeted
runtime provenance, not a claim that every guest module is verified.

With an exclusively owned fresh prefix, `WINEARCH=win64`, `DISPLAY=` and
`XDG_RUNTIME_DIR=/run/user/1000`, bounded `wineboot.exe -u` timed out at 90
seconds (exit 124). The logs show missing display-driver/window creation,
COM/RpcSs marshaling errors and setupapi copy error 1812. A subsequent x64
minimal console executable also timed out at 25 seconds (exit 124), with no
stdout or stderr. The x86 startup case and corrected Mojo A/B matrix were
not run because initialization did not complete.

The setupapi error is not an established root cause: the source
`SetupDefaultQueueCallbackW` COPYERROR handler returns `FILEOP_SKIP`.
The guest had neither a running display server nor installed `Xvfb`/`strace`.
No packages, images or replacement runtimes were installed. Further execution
must first establish a working guest session and successful prefix startup;
the results do not justify attributing the timeout to token access.

The preserved server remained
`0323c006eab58eb9ba975675e2345750bf061aab53ec56954d82952ad13b67ab`.
After fetching logs, the owned prefix server was stopped and its processes
verified absent; only the owned prefix was removed, restoring 2.5 GiB free.
The exact launcher PID received TERM. Its result was `stop_reason=signal`,
`graceful_powerdown=true`, `qemu_exit_status=0`. No QEMU, Xvfb or wineserver
remained on the WSL host. Preserved trees and old r2/r3 evidence were untouched.

Raw startup/native logs and targeted closure evidence are under
`docs/evidence/mojo-token-recon-r4-*`. The full local process archive is
`mojo-r4-logs.tar.gz` in the coordinating session's private artifacts.

## Descriptor candidate review

Codex's `TOKEN_DUPLICATION_SD_PARENT_REVIEW_20261031.md` on
`codex/g4-r1-completion` (`cabe66a7`) identifies a conformance conflict:
upstream `test_token_security_descriptor` explicitly expects a custom source
object-DACL marker not to be inherited by `DuplicateTokenEx` with NULL
security attributes. NULL attributes create a default object descriptor.
Source integrity propagation is independently covered by `test_token_label`;
it does not imply whole owner/group/DACL/SACL inheritance.

Therefore the blanket NULL-descriptor copy in `f2e67abe` is withheld from
acceptance. The separate SID-matching/default-DACL changes are not rejected
by this finding, but still need their own regressions. Any descriptor
preservation required by lowbox creation or process assignment must be
established at that exact boundary, not granted by generic duplication or a
self-token exemption.

### Candidate correction

Wine `e1a90b94` restores the entire `token_duplicate` function exactly to
`ce87bac0`, not just the blanket-copy branch. The original independent
mandatory-label propagation also runs for NULL descriptors; simply deleting
the new else branch would have left this behavior incorrectly conditional
on an explicit descriptor. The AllApplicationPackages/default-DACL-user
changes elsewhere in `server/token.c` remain untouched.

The function was compared directly with the baseline source and the existing
WoW64-configured build's `server/wineserver` target was rebuilt.
New server SHA-256:
`72d6a692200c94d530e3c51f74c90086e8dfd116bede7b2e8c72968942e86644`.
The compiler reports the pre-existing `req_filter_token` variable-sized
`struct group` allocation warning, outside the changed function. No unrelated
allocation changes were made.

This corrected server has not been staged or run in the guest. Old host/guest
artifacts with the `853c9f0c...` hash still represent the rejected descriptor
candidate and must not be mislabeled as this correction. Guest startup and
the separate restricted/cross-process regressions remain prerequisites for
acceptance.

### Native descriptor oracle: measured token state versus object security

`tests/probes/token_duplicate_sd_probe.c` uses disposable tokens only.
Compile it with `x86_64-w64-mingw32-gcc` or `i686-w64-mingw32-gcc`,
`-O2 -Wall -Wextra`, and `-ladvapi32`; run the resulting PE without arguments.
Both native architectures returned exit 0: all 114 named assertions passed,
with no skipped cases. The log records token type, object DACL markers,
separate `TokenDefaultDacl` markers, token/object integrity, owner/group
presence, and handle inheritance. It does not print the user's SID.

| Case, repeated for primary and impersonation tokens | Native observation |
| --- | --- |
| NULL security attributes | Source custom object-DACL marker absent; new object does not use the source token's distinct `TokenDefaultDacl` marker |
| Non-NULL attributes, NULL descriptor | Same descriptor behavior; requested inheritable handle honored |
| Explicit descriptor with a second object-DACL marker | Supplied marker honored, source marker absent |
| Distinct `TokenDefaultDacl` | Token state marker preserved in every duplicate, independently of object DACL |
| Explicit low object mandatory label | `TokenIntegrityLevel` remains Medium (`0x2000`); object label is Low (`0x1000`) |
| NULL-attributes duplicate of that low-object-label token | Both token integrity and new object label are Medium; the source low object label is not inherited |

The last two cases materially refine the upstream-label review: an explicitly
supplied low **object** label is not ignored natively. It is independent of
`TokenIntegrityLevel`. The first oracle run incorrectly equated these values
and failed exactly `primary.explicit_low_label.object_integrity` and
`impersonation.explicit_low_label.object_integrity` (exit 1). Correcting that
expectation and adding the subsequent NULL-attributes duplication case
established the distinction rather than suppressing the failure.

Final PE SHA-256 values:

- x64: `3c3840dca0058a0d5dceae336ddbe1113575cee9c0ca30be1240b31aac5028f9`
- x86: `6343627ed847e72158b538755a30363a53d49b71d7a793c7ee01525bd5734548`

Raw final logs are `docs/evidence/token-duplicate-sd-native-x64.log` and
`token-duplicate-sd-native-x86.log`.

The rollback `e1a90b94` is therefore only a baseline restoration, **not**
complete native descriptor conformance: its source-object-label propagation
and source-default-DACL-based label initialization need comparison against
these named assertions in a working guest. Any further correction must
separate public duplication, effective creator defaults, internal lowbox/
process assignment, and token integrity state. No unverified implementation
of those additional boundaries has been made.

The delegated Qwen oracle job exceeded its 600-second deadline after reading
references; it produced no source file or execution results. The coordinating
agent implemented and executed this bounded oracle directly. This was not
reported as a successful helper run.

## Recovered runtime A/B execution

Parent recovery `42e727eb` provides an independent R6 runtime with the ten
required WinMD resources, resolved Unix dependencies, a stopped initialized
prefix and the existing private Wayland/VirGL recipe. The coordinator reused
that environment and cloned the stopped prefix independently for each run.
No preserved runtime or prefix was overwritten.

| Probe, on both x64 and x86 | Recovered baseline `0323c006...` | Candidate `72d6a692...` (`e1a90b94`) |
| --- | --- | --- |
| Minimal startup | PASS, exit 0 | PASS, exit 0 |
| Mojo plain | PASS | PASS |
| Mojo service_restricted | Child exits early with code 1 | PASS |
| Mojo network_appcontainer | Child exits early with code 1 | PASS |
| Mojo overall | Exit 1, two failing modes | Exit 0, all three modes PASS |
| Native-backed descriptor oracle | Exit 1, eight named failures | Exit 1, same eight named failures |

Every invocation records the actual live wineserver `/proc` executable and
hash. Both runs verified actual outer VirGL before starting the probes.
The unchanged descriptor failures are six `.default_marker_not_object`
assertions for primary/impersonation NULL attributes, NULL descriptor, and
NULL-after-explicit-low cases, plus the two `.explicit_low_label.object_integrity`
assertions. Source custom object-DACL marker absence and token integrity
assertions do not fail. These results support fixing public creation defaults
and object-label handling without reinstating blanket source-SD copying.

This is an actual probe mode flip, not actual Chromium/browser validation.
The candidate still fails the focused descriptor contract. Chromium and Edge
have not been executed in this continuation.

Raw stdout/stderr and per-run identities are in
`docs/evidence/mojo-recovered-*`, including `mojo-recovered-comparison.json`.
All owned prefix servers and compositors stopped before log collection.
Only the owned cloned prefixes and scratch hardlinked runtime were removed;
the scratch server was unlinked before replacement, leaving the recovered
baseline hash unchanged.

The initial runner accidentally used `candidate` for both scratch runtime and
result directory, raising `FileExistsError` after baseline completion. No
candidate probe ran on that attempt. The retry used `candidate-run`, loaded
the retained baseline JSON, and explicitly ran candidate-only. All baseline
evidence was preserved rather than repeated or overwritten.

## Public descriptor correction: matched guest evidence

The narrow public/internal duplication boundary preserves explicit mandatory
object labels and initializes omitted public object defaults without inheriting
the source TokenDefaultDacl. Omitted labels follow source token integrity
state, not an explicitly lowered source object label. Wine topic `59f4bc31`
contains the correction. Internal callers retain
their prior source-label behavior. Descriptor-setting/allocation failures now
propagate rather than returning a partially initialized token.

In fresh VM run `mojo-recovered-fixed-r7`, baseline and corrected server used
independent clones of the stopped recovery prefix and private Wayland/VirGL.
Both architectures passed minimal startup. The baseline repeated its two
sandboxed Mojo failures and eight named descriptor failures. The correction
passed all three Mojo modes (exit 0) and all **114 unique named descriptor
assertions** (exit 0) on both x64 and x86, with no skips. Each invocation verified
one live server with its exact expected hash. Candidate SHA-256:
`196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800`.
Raw output and identities: `docs/evidence/mojo-fixed-*`.

The Wine build succeeded with the existing, unrelated `req_filter_token`
variable-sized-group array-bounds warning. No broader effective-creator
owner/group equivalence or internal assignment conformance is claimed.
All owned cloned prefixes and the scratch runtime were removed after process
absence checks; the preserved baseline hash is unchanged. Launcher shutdown
was graceful (`fallback_used=false`, `qemu_exit_status=0`), with no host
QEMU/Xvfb/wineserver processes remaining. Actual Chromium and Edge validation
remains outstanding.

## Durable runtime packaging prerequisite

`scripts/g4_combined_r6_deploy.py` is the tracked archive implementation used
by the local R6 trial. It parses the actual staged `wow64/loader/wine.inf`
`[WinmdFiles]` section, validates nonempty regular nonsymlink source resources
under the explicitly supplied original build `include/` before writing any
archive, and places them under
`wow64/include/` in the consumer's in-tree runtime. Resource content hashes
are included in the same runtime manifest; the launcher symlink remains a
symlink rather than being dereferenced into an incompatible runtime layout.

The fixture suite `python -m unittest discover -s scripts -p
test_g4_combined_r6_deploy.py` is independent of ignored private trial files
and a populated Wine submodule. Ten cases cover exact resource placement,
hashes, launcher-link preservation, metadata authority, missing/empty files,
and rejecting path escapes. Recovery identified ten missing WinMD resources
as the setupapi initialization hang; this guards against redeploying that
incomplete runtime. The staged runtime's own `include/` was measured to be
missing all ten resources, so the local trial explicitly supplies
`/mnt/d/DeepThought/Builds/wine-reconciliation/build/include`, without modifying
the read-only staging tree. That explicit production preflight passes all ten
required resources, totaling 111104 bytes. A real launcher-symlink case runs
on WSL; Windows skips only that case when symlink privilege is unavailable.
The actual deployed recipe remains authoritative, not
the fixture's ten-name sample.

## Fresh actual-Chromium validation mode

`check-g4-chromium-native-exit.py --token-fixed-r7` binds an independent
`chromium-token-fixed-r7-20261031` output/profile/stage and candidate runtime
`chromium-token-fixed-r7-20261031-wine/build/wow64`. Its prefix is the dedicated
`chromium-token-fixed-r7-20261031-prefix` clone; the preserved browser prefix
is never used for execution or modified. The native helper must be rebuilt
with that exact `WEX_EXIT_OUTPUT` and `WEX_DIAGNOSTIC=1`. Runtime/image/manifest,
native held-handle exit and prefix ownership checks remain unchanged.

The mode retains verify-r6's scoped security/network diagnostics, production
sandbox settings, private loopback fixture and actual VirGL verification.
The owned full host runtime staging copy unlinks its server before overlaying
the validated `59f4bc31` server, packages the ten required metadata resources,
and hashes every deployed runtime file against the host manifest. Existing
consumed verify-r6 paths are not reused.

Run the mode and related contract fixtures under WSL:
`python3 -B -m unittest test_g4_chromium_native_exit
test_g4_chromium_observation_tolerance test_g4_userenv_view`, from `scripts/`.
These Unix-dependent fixtures require `os.geteuid`; they are not Windows-native
tests. The fresh-mode wiring passes all 34 fixtures. Actual browser outcomes
must still be measured; the earlier standalone probe success is not acceptance.

The first r7 browser attempt was rejected before acceptance by
`Invalid Chromium held-handle binding`: the tracked helper compiled
`WEX_DIAGNOSTIC=1` to level-2 scoped VLOG, while the adapter required level 3.
The recorded environment also retained the installer's older `WINESERVER`,
despite selecting the candidate loader. Neither successful orchestration nor
the standalone token probes makes this a valid candidate browser verdict.
The original journal and logs remain preserved; owned prefix/runtime clones
were removed and the VM shut down gracefully.

The fresh `--token-fixed-r8` retry explicitly sets candidate `WINE` and
`WINESERVER` in the execution environment. `WEX_DIAGNOSTIC=3` now emits the exact
level-3 command expected by the adapter; existing level-2 builds retain their
original command. A real compiled C contract test compares the entire command
against the Python adapter, and fixtures verify stale installer runtime values
are overridden and mismatched VLOG is still rejected. All 36 related WSL
fixtures pass. The helper's image/flags/held-handle checks are not weakened.

Independent review using the user-selected GPT-6.1 Sol found no blocking
correctness/regression issue in Wine `59f4bc31`, accepting the measured public
descriptor contract and synthetic Mojo results narrowly. Broader effective
creator/internal assignment contexts and allocation-failure execution remain
unproved, not demonstrated blockers. This review does not accept the browser.

The r8 actual browser run verified all 1778 deployed runtime files
(1883823875 bytes), correct candidate runtime environment, exact level-3 native
command binding, and actual outer/inner VirGL. Chromium created the network
service with `--service-sandbox-type=network` and the storage service with
`--service-sandbox-type=service`. However, the held browser process remained
alive at the 45000 ms native wait, and the private fixture recorded **zero
requests**. The helper reported native timeout; the collector surfaced
`Linux helper failed/exit mismatch`. No successful native exit, page load,
or full browser acceptance is claimed.

Raw journal/log/NetLog and the outcome summary are
`docs/evidence/chromium-token-r8-*`. Prefix processes and compositors stopped,
the owned prefix/runtime clones were removed, and the guest shut down
gracefully with exit 0 and the preserved baseline server hash unchanged.
Post-token startup diagnosis is the remaining Chromium prerequisite; Edge
execution remains gated on actual Chromium validation.

Local Qwen's bounded log diagnosis identifies the next concrete failure:
network PID 620 and storage PID 636 each log
`Terminating current process after 15 seconds with no connection` after
starting their utility loops. No renderer is created. This proves the browser
connection did not complete; it does not identify why or prove complete service
initialization. The WSALookupService error alone is not a proven cause.

The initial fixed `--token-wait-r9` diagnostic attempted a bounded
`winedbg --command "bt all"` snapshot after five seconds. It timed out after
eight seconds with empty output. The browser observer exited with Linux code
84 and no terminal native journal event; this run is inconclusive and must
not be interpreted as a browser exit code or root-cause evidence. Its owned
runtime/prefix were cleaned and the guest shut down gracefully.

The fresh `--token-wait-r10` observer replaces the debugger with read-only
`/proc` observations scoped by the exact private `WINEPREFIX`: process command
lines and thread names, wait channels and current syscalls. Missing/denied
fields are recorded explicitly. It launches no process, attaches no debugger,
captures no memory dump or environment secrets, and preserves the normal
browser command. Runtime, field, process and thread quotas remain bounded.

The r10 read-only snapshot completed with 17 prefix processes, including the
candidate server's exact hash. Utility main/IO threads were waiting in Linux
`pipe_read`; this by itself does not distinguish ordinary Wine server waits
from the invitation failure. The browser process's prefix-identity read was
denied (`PermissionError`), so its omission is **not** evidence that the
browser was absent. The exact native journal again records a 45-second
timeout and the fixture records zero requests. Cleanup and shutdown succeeded.

The next fixed transport diagnostic adds only `+file,+sync` to the Wine debug
channels, retaining the same level-3 Chromium command and all sandbox/identity
checks. The synthetic Mojo probe does exercise named pipes and IOCP; it is
not a complete Chromium broker/child invitation implementation. Diagnostics
must discriminate the actual pipe/create/open/read/write path rather than
assuming the probe contains no transport.

The r11 full `+file,+sync` trace exceeded the unchanged 1-MiB calibration
stderr quota after about one second during loader startup. That run has no
terminal native record and cannot decide the browser failure. Its trace must
not be presented as a browser crash or as a connection verdict. The follow-up
will use pipe-only diagnostic instrumentation rather than raise the evidence
quota or drop unrecorded trace output.

The r12 `wex_mojo` diagnostic rebuilt only kernelbase for both architectures.
The isolated archive still has 1778 files (1883848566 bytes); only those two
DLLs differ and the server remains `196f24db...`. Original staging hardlinks
were verified unchanged. The bounded trace is 76189 bytes, with 106 pipe
creation/open records. It reaches successful crashpad pipe creation/open and
COM/RPC pipe startup, but no Chromium log or broker Mojo pipe was produced.
The bound child was still alive at 45 seconds with zero fixture requests.
This **earlier stall differs from r8/r10's utility connection failure**; it
does not establish the cause of that failure.

After that observation, the same owned runtime passed x64/x86 startup,
all three synthetic Mojo modes on each architecture, and both descriptor
oracles (six programs, all exit zero). These regressions also exercise the
instrumented APIs, including failed pipe opens; they are not browser
acceptance. The diagnostic changes logging only, with exact failed NT status
and no read of undefined output handles/information. Cleanup, preserved
baseline hash, and graceful QMP shutdown were independently verified.

The r13 diagnostic uses the original corrected runtime (not the pipe-only
DLL overlay) and a fixed root-scoped read-only snapshot. The helper verifies
root execution, private mount/network namespaces, canonical fixed paths and
the candidate server hash before reading `/proc`. It retains the existing
eight-second read budget with a ten-second outer timeout and never stores
environment contents. It again reaches sandboxed network/storage children,
then the alive 45-second native timeout with zero HTTP requests.

Root access eliminates the permission errors, but the exact-prefix filter
still omits the main browser. **Correction to the earlier PID interpretation:**
Linux PID1118 is the native-exit helper, not the main browser. Its
`pipe_read` is not the browser's wait state. Sixteen matching processes are
captured, including the utility children; the browser's omission does not
prove absence. Resolving the Wine-to-Linux process identity is still required
before attributing its wait. Cleanup and graceful shutdown were verified.

The r14 diagnostic resolves that identity gap with Wine `65f8cc3a`'s opt-in
`wex_process` startup/thread mapping. The full 1778-file manifest has exactly
one change from the original corrected runtime: Unix `ntdll.so`. Both
architectures' PE modules, original kernelbase and server `196f24db...` remain
unchanged. Forty-one focused observer/contract checks cover binding, live
thread identity, quotas, deadlines and the complete native command.

This run binds Windows PID248 to Unix PID1118. Unlike r13, PID1118 here is
the browser, verified against its exact executable, fresh profile and thread
group. At five seconds the main thread and `StackSamplingPr` thread1162 are
both in `pipe_read`, each reading 16 bytes. This proves the observed process
and waits, not the waited object or a hang in the last logged token call.
Only those two browser threads were mapped, and no Chromium log or utility
connection appeared. The browser remained alive at 45 seconds with zero
HTTP requests: **another earlier pre-logging stall, not r13's later utility
failure**. Six x64/x86 startup, three-mode Mojo and descriptor programs then
passed on that same runtime. All owned threads were checked before clone
removal; the preserved baseline and graceful QMP shutdown were verified.
Chromium acceptance and the subsequent Edge comparison remain outstanding.
