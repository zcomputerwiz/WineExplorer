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
