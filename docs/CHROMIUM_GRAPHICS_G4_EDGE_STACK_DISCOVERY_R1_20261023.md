<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# One metadata-only WineDbg discovery — no tools executed

**EXECUTED ONCE; cleanly stopped.** STACK_DISCOVERY_PLAN scope only. No browser,
Wineboot, debugger/attach, rebuild/download, staging/profile creation, screenshot,
payload transfer, cleanup or guest network change. Only fixed-path metadata/hash
queries via existing strict-key SSH; no alternative-path search. All tracked work
in the explicit owned C: worktree; D: master/other checkouts/submodules untouched,
pre-existing unused M wine unstaged. Source/production pins not changed.

## Actual guest metadata

BUILD=`/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64`.
Both available candidates resolve to exactly their requested paths, regular files,
not symlinks, PE machine0x8664. They were hashed/header-read, NEVER executed:

| Relative candidate | Bytes | SHA256 |
| --- | ---: | --- |
| programs/winedbg/x86_64-windows/winedbg.exe | 14198269 | 71b6beda29d24a525143c074956c125f67e8bd2b81f9e9730108280b8666b7f9 |
| dlls/dbghelp/x86_64-windows/dbghelp.dll | 4121451 | 5338dbd72c7b9d7fb6e4d3dcbb799542739308a53c409f46040c6b32fd6e3302 |

Optional dlls/dbghelp/dbghelp.so: unavailable/FileNotFoundError. All four requested
files under guest `/var/tmp/wex-g3-repair-20261018/clean-source/programs/winedbg/`
(winedbg.c,tgt_active.c,stack.c,dbg.y) also unavailable. No .git at that exact source
root: HEAD/status unavailable, no fallback directory search. Proposed Git command
records show4s/8192-byte limits, but commands were NOT executed after this guard.
**No guest source attestation obtained.** Missing files were not replaced/built.

## Parent's separate read-only HOST bridge

Parent subsequently reported HOST build/wow64/Makefile srcdir=../../wine under
the exact owned `/var/tmp/wex-g3-repair-20261018/clean-source` tree. HOST WineDbg
and dbghelp PE hashes exactly match the two guest candidates above. Actual HOST
sources are under clean-source/wine/programs/winedbg, not the failed guest paths.
Parent independently matched all four raw source hashes to4315be83 Git blobs via
D: Wine Git objects: winedbg.c d2008815...,tgt_active.c94597c21...,stack.c a0ebe20f...,
dbg.y3d09117e... (prefixes as supplied by parent). Scoped Git status for
programs/winedbg and dlls/dbghelp clean. This is parent's bridge, not an extra guest
query or independent validation by this run. Overall HOST Wine HEAD af80a466...
differs from the production pin: do NOT claim whole HEAD4315 or a reproducible
binary-build attestation. Narrow same-host-build2-binary/source4 evidence is for
later calibration review, not debugger execution authority.

## Gates, commands and shutdown

Live preboot usage75% five-hour/42% weekly, credits0. Five current C: control/helper
hashes and two public seeds matched accepted owned-thread prep6097d951...; public
key/mode600/known-host/storage paths verified, fresh output and no QEMU/launcher.
650 rawUTC guards passed before SSH/discovery with818.866568/818.132504s remaining.
Guest free1,308,270,592 bytes checked BEFORE queries/staging/creation; all17 known
actual binary hashes matched. Discovery elapsed2.088629s <120s; SSH calls bounded
to<=30s, all exit0. Candidate helper fixed-path metadata deadline20s; no arguments,
filesystem writes, privilege changes or general inspector. Host D3D12 launcher
record retained; no guest graphics/compositor/browser acceptance test performed.

Actual commands: reviewed C: session controller -RunName edge-stack-discovery-r1-20261023;
existing key/strict-known-host SSH true, Python disk_usage, sha256sum of accepted17
paths, then python3 -B - with fixed metadata helper via stdin (not staged on disk).
Exact SSH argv/commands/timeouts/exits in discovery-execution.json. Separate C:
stopper --identity this run's launcher-identity.json --timeout60, no Ctrl-C.
900s launcher/90s cleanup reserve unchanged.

Discovery host exit0; identity stopper0; stop_reason=signal, QMP graceful=true,
QEMU0, qemu_stopped/xvfb_stopped=true. Independent /proc check: launcher29791,
QEMU29806,Xvfb29793 absent. Windows WSL PID32092 exited0/absent; foreground controller
session86736 returned0. UTC06:39:34.8807710..06:41:20.3393146. Gracefulness checked
from explicit final QMP/controller records, not inferred from disappearance.

## Private evidence / handoff

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-stack-discovery-r1-20261023/`
retains raw fixed-metadata stdout/stderr, commands, all17 hash output, disk/guard/
identity/launcher/discovery/stopper/result logs, and independent-evidence-audit.json
with26 file hashes. Adjacent run-named host-preflight/helper-final/source copy and
Windows session/stdout/stderr retained. Preliminary helper hash a14d5272... is
history; before boot its bounded-Gitdir-read/root guard refinement was recorded
in helper-final.json. Executed stdin bytes match both final ignored C: source and
the D: source copy, SHA256 below. No tracked diagnostic changes/tests/rebuilds.

- Final private helper SHA256: `67e5a90d1d62632b1cb5e88f337a3ea2d82f2ebd6ae8bdbe8edb60dfefc794e7`
- fixed-metadata.stdout.log SHA256: `50e1312252104a2efdc3fb08a98362b0de61479283d86de26382ab9d855472ce`
- discovery-execution.json SHA256: `0041384d0bf70437a5d58c81aa0168b4cf60035dacc2271797b4d7e8f6815317`

Stop for parent tool-provenance/calibration review. No attach, selected-thread
stack collection, bt all/global debugger command or additional run authorized now.
