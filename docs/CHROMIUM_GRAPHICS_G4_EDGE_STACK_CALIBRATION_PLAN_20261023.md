<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Calibrate a selected-process stack probe before using it on Edge

## Evidence and purpose

Owned-thread R1 supplied verified native GUI/main, Chrome_IOThread, BrokerEvent
and COMSTA thread identities, not their call stacks. Metadata-only discovery found
existing x64 WineDbg and dbghelp PE files, no optional dbghelp.so. No debugger ran.
Parent independently matched both PE hashes to the existing owned HOST build;
Makefile srcdir=../../wine. Four debugger source hashes in that host wine/ tree
match Wine4315be83 Git objects exactly; debugger/dbghelp paths are clean. Overall
host Wine HEAD is af80a466, NOT4315be83. This is narrow existing-build provenance,
not a reproducible rebuild attestation or a claim of matching whole source HEAD.
Guest source/Git at the initially selected paths was unavailable and is preserved.
Read discovery report/current HEAD before using these findings.

- WineDbg SHA25671b6beda29d24a525143c074956c125f67e8bd2b81f9e9730108280b8666b7f9
- dbghelp PE SHA2565338dbd72c7b9d7fb6e4d3dcbb799542739308a53c409f46040c6b32fd6e3302

First prove normal attach, explicitly selected numeric-thread backtraces, detach
and target responsiveness against an ORIGINAL disposable child, not Edge. A
debugger affects execution and may kill its target on exit/timeout; neither a
waiter stack nor debugger-affected frames count as consumer/graphics acceptance.

## CURRENT authorized offline milestone: small building blocks ONLY

Luna implements the following two pieces and tests; no boot, Wine/debugger/Edge
execution, calibration orchestration or further guest access yet. Parent reviews
before integration. Keep this small enough to finish safely before usage95%.

1. Original native two-thread waiter source under scripts/. Exact --wait NONCE32
   and --release NONCE32 modes only; reject arbitrary PID/path/command inputs.
   --wait creates a unique nonce-scoped manual-reset event with collision failure,
   names its main/worker threads when supported, emits ONE bounded strict JSON
   readiness record with own WinPID/main/worker TIDs/process creation time/nonce,
   then both wait <=30s for that event. Check all API results and flush readiness
   only when the worker is actually ready. Parent can later run --release for
   that exact nonce; worker joins bounded, success exit0 only after release.
   No external networking, target process lookup, payload read or debug privilege.
   No indefinitely running thread/process on timeout/failure. Normal --wait must
   not silently reuse a pre-existing named event. Native API behavior unproven
   until later real calibration; cross-compilation is not execution evidence.
2. Tiny Python command generator/validator, no subprocess or attachment yet.
   Accept a strict calibration identity record with exact positive nonboolean
   uint32 WinPID and TWO distinct WinTIDs, positive uint64 creation time,32hex nonce
   and exact schema. Return ONLY a WineDbg command file containing attach PID,
   bt MAIN_TID, bt WORKER_TID, detach, quit, each as generated numeric tokens and
   newline. No strings from callers in command tokens; reject duplicates/overflow,
   bools, missing/unknown fields, malformed nonce/IDs, trailing content/injection.
   No bt all/info thread, global process command, expression/breakpoint or fallback.
   This is preparation, not PID ownership proof or executable debugger authority.

Use apply_patch, actual MinGW -Wall -Wextra cross-compile into a fresh D: private
artifact directory, original source hash/PE metadata and warning/error logs.
Executable Python tests should cover exact output and every negative above.
If adding native API mock tests, label them honestly; do not present synthetic
records as Wine API proof. Run the current170 Linux controls in fresh private
loopback namespace and original Node fixture/controller controls. Preserve all
existing behavior and unused M wine; do not integrate into Edge runner now.
Commit AND push passing building blocks and an explicit NOT_RUN/NOT_INTEGRATED
report; return for parent review. No PREPARED_FOR_LIVE claim without later runner.

## Later milestones — NOT current execution authority

After review, integrate ONE healthy calibration using the existing owned
compositor/namespace/installed-prefix/control helpers, not a new browser framework.
Fresh source/binary/helper inventory including both discovered tool hashes, the
original waiter source and compiled PE. Recheck all17 existing binaries, installed
Edge payload before/after, prefix ownership, no other owned consumer running,
>=1GiB guest free before any creation; no reinstall/prefix copy/forced update.
Keep original private network/sockets/backend, rawUTC650/435 guards,900 launcher/
90 cleanup reserve,285 runner/300SSH and identity/QMP/Windows shutdown proof.

Calibrator must validate exact original child PE/hash, run nonce/creation identity
and separately owned Linux Popen PID/start/prefix before attach, with a bounded
readiness handshake. Use only generated explicit numeric commands with exact
known WineDbg PE; local/empty symbol path, no symbol downloads or security flags.
Bound debugger startup/collection/kill/reap and logs within a declared <=10s cap.
Require explicit attach success, backtrace sections for BOTH selected TIDs,
detach completion and unchanged target identity; then release original waiter and
require exit0. A debugger timeout may terminate its owned child: record failed
calibration and preserve evidence, never call it harmless or retry automatically.
No Edge launch, broad debugger or privileges if calibration fails. Parent reviews
normal detach/response evidence BEFORE the later full-Edge stack integration.

Later Edge integration would select fresh native identities in THAT run, never
historical PID/TID constants, and <=8 explicitly named owned threads. Revalidate
before/after, no cross-domain ID assumption. Always diagnostic-only; preserve the
existing timeout/front/HTTP/pixel/cleanup behavior, no rendering pass from stacks.
Any deliberate debugger-timeout fault calibration needs its own reviewed plan.

Work in owned C: graphics worktree, private artifacts D:, main D: master/other
agents untouched. No master merge, sandbox bypass, token/OOBE imports, capability
spoof, vendor/art publication, loaded-build rebuild or reset-credit use. At95%
five-hour safely checkpoint/commit/push and reschedule; weekly99% stop until reset.
