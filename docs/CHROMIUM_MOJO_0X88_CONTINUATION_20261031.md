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
