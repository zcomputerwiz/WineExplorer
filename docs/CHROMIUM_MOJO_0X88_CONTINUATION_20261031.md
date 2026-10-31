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
