<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Next bounded step: discover the owned stack tool before attaching anything

## Starting point

Owned-thread diagnostic succeeded: parent independently viewed the white front,
rehashed114 files and checked17 actual binaries/11 deployed sources, namespace,
VirGL/budget and clean shutdown. Native56 threads and independently selected
Linux56 tasks captured in0.139243s, no inventory errors. GUI/CrBrowserMain,
Chrome_IOThread, BrokerEvent and several COMSTA threads have verified Win32 IDs.
Linux observations (49 pipe_read,1 poll,6 futex) are NOT Win32 ID mappings, stack
traces, a deadlock proof or a causal diagnosis. HTTP again delivered637 HTML bytes
without scene.js/readiness. No production fix justified yet.

Next useful evidence is selected thread stacks in ONLY that owned browser. Do
not improvise an attach now: the exact existing WineDbg executable/dependencies
have not yet been inventoried. First perform the read-only discovery below.

## Authorized milestone: ONE metadata-only guest boot

Luna implements/executes this bounded read-only discovery; parent reviews results
before authorizing debugger execution or any further browser launch. Reuse the
already reviewed C: controller/launcher/stopper and owned D:-backed VirGL guest,
with a fresh edge-stack-discovery-r1-20261023 run. No new framework or VM.
Check live account usage before launch; do not begin if95% five-hour/99% weekly.

Before boot verify current host launcher/controller/budget/stopper hashes against
accepted owned-thread preparation; public seed and D: disk/known-host paths must
match. Parent/source worktree C:, all private artifacts D:, leave D: master alone.
Use the900s owned launcher/90s cleanup reserve and exact process identity/QMP
shutdown. Require650 rawUTC seconds before guest discovery. SSH commands bounded
to30s each, discovery total<=120s. Stop safely on failure; no retry boot.

Read and preserve: guest free space (>=1GiB before any staging/creation), all17
known binary hashes from the accepted prior preparation, and metadata/hash of
these EXACT candidate paths under the existing BUILD:

- programs/winedbg/x86_64-windows/winedbg.exe
- dlls/dbghelp/x86_64-windows/dbghelp.dll
- dlls/dbghelp/dbghelp.so (optional if absent; do not search arbitrary directories)

For each record exact resolved path, regular-file/symlink identity, size/SHA256,
and PE machine where applicable. Reject escapes outside the owned clean-source
build; missing file is explicit unavailable evidence, not authority to build or
download a replacement. Do not execute these files, Wineboot or any browser.
Read available source-checkout Git HEAD/status metadata (bounded output), plus
hashes of programs/winedbg/{winedbg.c,tgt_active.c,stack.c,dbg.y}, from the exact
owned clean-source directory. Record unavailable metadata honestly; never change
its Git/build state. Parent can compare source hashes to Wine4315be83 Git objects.
No privileged writes, installed prefix/profile changes, package installation,
payload transfers, screenshots, filesystem cleanup or guest network changes.

Only if a tiny private helper is needed, edit via apply_patch in ignored C: build
control directory and record its hash/source in D: metadata. No general remote
file inspector/path arguments: fixed explicit paths/known hashes only. Reuse
existing SSH identity/strict-known-host settings; never print private key bytes.
Preserve stdout/stderr, actual commands, budget/identity/backend/result records.
Always exact-owned SIGINT -> QMP powerdown -> QEMU/Xvfb/Windows controller exit
proof; no inference of graceful shutdown merely from process disappearance.
Commit AND push the metadata report (no binary/vendor payload) and stop for review.

## Later stack milestone — NOT execution authority

After parent reviews discovery, plan one explicit diagnostic opt-in reusing the
current original HTTP fixture/security/backend and timeout-only observation.
Fresh identity selection on THAT run, never reuse historical numeric IDs. Select
only GUI/main, unique Chrome_IOThread/BrokerEvent and bounded named COMSTA owners
from the verified native inventory, <=8; ambiguous/missing ownership fails closed.
WineDbg command script may contain only attach NUMERIC_OWNED_PID, explicit bt
NUMERIC_OWNED_TID commands, detach and quit. No bt all/info thread/global process
commands, expression evaluation, breakpoints, privilege enabling or security flags.
Preserve IDs/creation times before/after and exact source/binary/helper hashes.

Wine4315be83 stack.c bt all deliberately attaches across processes. Its normal
parser exit detaches known debuggees, but attach does NOT disable kill-on-exit.
Microsoft documents target suspension and possible target termination if debugger
exits: https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocess
A later explicit plan must address timeout/detach semantics and calibrate against
an original disposable child, with no unreviewed binary patch. A timeout may not
be called harmless, a normal waiter stack may not be called the root cause, and
debugger-affected rendering may not count as acceptance. No debugger now.

Never merge master, bypass sandbox, spoof capabilities, publish vendor/art/runtime,
rebuild loaded binaries, touch OOBE/token/other worker VMs or consume reset credits.
At95% safely checkpoint/commit/push and reschedule; weekly99% stop until reset.
