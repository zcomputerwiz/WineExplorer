<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge navigation stall: narrowly owned thread observation

## Reviewed starting evidence

Parent independently inspected the white HTTP failure front, rehashed all107
evidence files, checked actual17 binary/deployed10 source entries, private-lo
start/end and graceful QMP/QEMU/Xvfb/Windows controller exit0 from715360ea.
HTTP delivered the complete637-byte original HTML; no scene.js request or sampled
renderer/readiness followed. FILE/LO-down and FILE/LO-up failures remain preserved.
No file-only, sandbox/token/API or graphics cause has been established.

Next objective: identify which browser-owned threads exist and their observed
wait state, so a later stack/navigation probe targets a demonstrated owner rather
than guessing. FIRST implement a non-attaching inventory, OFFLINE; parent reviews
before one diagnostic. This is not a production fix or a new acceptance gate.

## Bounded implementation: no debugger yet

Reuse the current runner, observer and original HTTP mode. Add an explicit
owned-thread diagnostic option requiring startup-diagnostic, HTTP and verified
private-loopback flags BEFORE output creation. Default FILE/HTTP modes remain
unchanged. Capture only after readiness failure, BEFORE owned-prefix cleanup;
retain primary timeout, existing whole-front capture and every cleanup failure.
No readiness retry, browser restart or changed transport/backend/security.

Extend the existing native observer with a separate read-only mode. Python must
first select exactly ONE visible top-level window with this run's exact nonce in
its title/URL, exact installed Edge image, valid HWND/owner and geometry. Reject
zero/multiple candidates, malformed identities and helper/child/foreign images.
Pass the selected numeric HWND/Win32 PID and nonce explicitly; native code must
independently revalidate HWND owner/image/nonce, process creation time before and
after observation and thread ownership. Use QUERY-only process/thread handles,
Toolhelp filtered to that Win32 PID, GetThreadDescription if available and bounded
records (<=128 threads, bounded names). Include GUI TID, Win32 PID/TIDs, creation
time and observation errors; do not truncate silently or claim missing names are
stacks. No OpenThread suspend/context/write/debug rights, attach, token privileges,
process mutation, remote execution or payload reads. Preserve query failures.

Separately select Linux browser-main from the existing run-owned Popen/sample
identity: exact PID/start ticks, retained prefix, exact installed image, this
run's app URL AND fresh profile, no --type child. Revalidate before/after.
Optionally read ONLY that Linux process's /proc/PID/task entries (comm/stat/wchan
and syscall when permitted), each bounded and with Linux TID/start identity.
Do not equate Win32 IDs to Linux IDs or infer a mapping from numeric coincidence;
label the two inventories independently. Permission-denied/unavailable syscall
or wchan is explicit limited evidence, never a reason to escalate or fail cleanup.
No /proc/* scan beyond existing prefix sampler; no stacks/memory/dumps collected.

Whole observation <=5 seconds, within existing total285/cleanup55 reserve. Output
one bounded JSON diagnostic record and raw native stdout/stderr. Strict parse,
schema/identity/count checks; any ambiguity or observer timeout yields a retained
observation error and NO broader fallback. Diagnostic remains passed=false/exit1.
No third-party services, downloads, installer runs or debugger dependency needed.

## Why not WineDbg bt all

Parent inspected actual guest-pinned Wine4315be83 source READ-ONLY in D: Wine Git
objects (the unused C: submodule lacks that object). stack.c backtrace_all_active
enumerates/attaches across processes; info thread also inventories prefix-wide.
Never use bt all, a global thread command or broad debugger attach for this task.
dbg_attach_debuggee does not disable debugger kill-on-exit. Microsoft documents
that DebugActiveProcess suspends the target and debugger exit can terminate it:
https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocess
Thus any later debugger stage needs its own selected-PID/TID, binary provenance,
timeout/detach/owned-target-failure plan and parent review. Not authorized here.

## Tests and deliverables

Executable tests: flags/proof rejected before output; default modes unchanged;
unique owned selection vs zero/multiple/foreign/child/wrong nonce; strict numeric
and schema limits; native owner/image/creation-time/TID mismatches; Linux PID reuse,
wrong argv/prefix/profile, thread disappearance, denied queries; observer timeout,
malformed/oversized output, cleanup error and primary-error preservation. Use
actual native compilation if available; mock controls must be labelled as such.
Do not claim mocked native API tests exercise actual Windows/Wine behavior.
Run current146 Linux controls in fresh isolated netns, Node fixture, PowerShell
controller and appropriate real X-socket control. No live browser/guest now.

Work ONLY C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer,
codex/g4-r1-completion. Leave D: master and all submodules/other workers untouched;
preserve unstaged unused M wine, stage exact files. Add the small explicit source
inventory to the existing private procedure, no new framework. Fresh immutable
PREPARED_NOT_RUN manifest, exact hashes/commands, original HTTP byte hashes,
17 expected binaries, all actual host helpers/seeds/installed reference, no host
run yet. All private test storage D:, same retained authorized Edge installation.
Commit AND push, return for independent parent review BEFORE one live diagnostic.
Live gates remain >=1GiB guest free BEFORE transfer/create,650/435 rawUTC guards,
inner/outer VirGL, isolated sockets/lo,285 runner/300 SSH/900 launcher/90 shutdown
reserve, exact identity SIGINT/QMP/child/controller exits. No speculative security
patch, bypass, capability spoof, master merge or vendor/art publication. At95%
five-hour used safely checkpoint; weekly99% stop. Never consume reset credits.
