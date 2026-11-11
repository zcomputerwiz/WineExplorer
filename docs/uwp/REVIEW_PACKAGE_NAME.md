<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R0B review follow-ups (Codex)

The 41-line runtime fix addresses the three inspected helper defects, but is
not accepted or built yet. Native evidence exists, with defects below that
must be repaired before it is an acceptance oracle. Do not overwrite old
outputs; regenerate corrected evidence into a fresh ignored directory.

## Runtime fatal path

`dlls/kernelbase/version.c` introduced a returning, empty `SymCryptFatal`.
This is rejected even though inherited `ntdll/sec.c` has that pattern. A new
cryptographic invariant failure must fail closed and cannot return a package
identity. AGY job `5c1dd234` owns that narrow correction. User-mode XMM save/
restore no-ops are explicitly supported by the existing SymCrypt user-mode
environment; the fatal callback is not a harmless optional hook.

## Native driver and observation records

Only parent `tests/uwp/package-name/` needs edits for these corrections; do not
touch the runtime/fatal worker's files or its optional README update while it
is active. Assign this package after that worker completes.

1. The driver calls synchronous stdout/stderr `ReadToEnd()` before its timed
   `WaitForExit`, so the claimed 30-second deadline cannot interrupt a hung
   probe. Use concurrent asynchronous pipe reads, timed process wait, bounded
   kill/read completion and truthful failure recording (the accepted identity
   driver provides a local pattern). Any nonzero probe exit must fail the
   driver; the current driver prints RUN OK and leaves allOk true.
2. Explicit output destination validation lacks reparse/symlink ancestor
   checks and the default destination bypasses validation. Use the same fresh,
   direct ignored-child policy for both. Never delete previous evidence.
3. `Observation obs;` leaves scalar fields uninitialized in parameter cases.
   Actual retained x64 records include `null_id in_len=32758,
   out_len=1580202200`, `null_length in_len=32758` and `null_name out_len=7`.
   These numbers came from unassigned record fields, not API observations.
   Value-initialize records and capture input/output length at the actual call;
   represent a missing length pointer as explicitly not applicable. Do not
   label `sentinel_intact=true` unless a sentinel was actually initialized and
   compared. A result/expected string comparison is a separate assertion.
4. Add a small validator for complete unique per-architecture cases, sane
   typed fields, ret/length/string and checked-sentinel semantics. It must
   reject truncated/duplicate/altered records. Distinguish observation capture
   from conformance assertions; the observer currently exits 0 regardless of
   unexpected ret values and the driver alone cannot prove all cases passed.
5. Capture full OS build including UBR, compiler/SDK and executable/record
   hashes. Preserve old native artifacts as historical flawed evidence;
   corrected x64/x86 runs must be fresh. Existing output capacity/publisher
   observations have assigned fields, but never claim the defective parameter
   fields were validated native behavior.

Run only our native probes/PowerShell checks on Windows; no WSL/VM/Wine,
installation, certificate, registry mutation or tool download. Codex owns live
Wine tests after a clean baseline. No staging/commits by workers.

## Second review (2026-11-11)

The corrected native capture in `out-20261111-115621-14216` contains 58 records
and passes its validator, but that validator still trusts record.expected and
does not check successful lengths generally. Codex reproduced false acceptance
after changing x64 `resource_id/resource_id_null` to empty str/expected and
out_len 999999; validator printed VALID and exited 0. Counterexample retained
under `unsloth-tmp/uwp-u1-20261111/package-validator-review-20261111-152821-14884`.
Require independently fixed expected values/lengths and meaningful mutation
tests before accepting the evidence harness. Running the validator directly
in the current PowerShell shell produced output; an earlier nested Windows
PowerShell diagnostic returned no validator output and was not treated as proof.

The runtime candidate itself built successfully for i386/x86_64 from the clean
base plus the three R0B files. Wine kernel32 version tests passed 200 checks
per architecture, zero failures and two skips; matching-server cleanup passed.
This establishes those scoped runtime regressions, not full package support.
