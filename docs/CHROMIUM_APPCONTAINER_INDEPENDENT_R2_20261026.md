<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Independent r2 validation — normal builtin unit matrix passes

**PASS for the scoped unit matrix.** Reused the accepted clean r1 build from
Winecb1d117d02fef13af18772104b942c4aded50d00 plus committed root4aabab76;
no rebuild, DLL copy/replacement, production patch or test assertion change.
Read the r1 independent report for complete build/assembly provenance. All45
r1 seal entries remain unchanged. No VM/SSH/browser/R6/token work.

The parent source diagnosis identifies optional first-prefix installation:
mscoree DllRegisterServer invokes install_wine_mono, with appwiz addon UI.
The r1 actual inventory showed `control.exe appwiz.cpl install_mono`; it was
not a registration assertion failure. Under the explicit r2 authority, each
fresh prefix now has a separate bounded setup phase:

1. Copy the owned test environment, add ONLY
   `WINEDLLOVERRIDES='mscoree,mshtml='`, run the owned build frontend with
   `wineboot --init` (90s cap).
2. Reap that exact initializer; own prefix-scoped wineserver-k/-w (15s each),
   then require COMPLETE-empty ownership. Any failure stops before unit launch.
3. Run the unchanged unit with the original environment: **WINEDLLOVERRIDES
   entirely absent**, no WINEDLLPATH, explicit own WINESERVER and private display.
   Unit240s bound starts after setup, with separate final scoped cleanup/reap.

This is environment-only suppression of OPTIONAL Mono/Gecko installers during
unit-lab initialization. No registry DLL override, module/security feature change,
runtime install/download, loader override during assertions, or browser sandbox
workaround. Private netns has only loopback, no outbound route. No foreign runtime.
The helper now stops at the first init, test, or cleanup failure; no automatic
next-cell startup after failure as occurred in r1.

## Actual results

| Architecture/suite | Assertions | Todo | Failures | Skips | Unit exit | Init exit/time |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| x64 kernelbase security |49 |0 |0 |0 |0 |0 /13.620s |
| x64 userenv |260 |1 |0 |0 |0 |0 /9.709s |
| i686 kernelbase security |49 |0 |0 |0 |0 |0 /10.761s |
| i686 userenv |260 |1 |0 |0 |0 |0 /10.812s |

Zero flaky assertions in every summary. Both userenv todos are inherited
`userenv.c:405`, expected ERROR_INVALID_HANDLE versus observed87; the existing
todo annotation and all other checks remain intact. Full userenv suite ran,
including accepted SID cases; counts above are observed, not assumed. Matrix
exit0,53.440766140s. Each initializer/unit observer was reaped; all init and final
own wineserver-k/-w exits0. Four final ownership scans COMPLETE-empty. Xvfb
exit0 and absent; private loopback before/after equal. No global kill patterns.

Normal builtin evidence is per cell: loader traces, logical Windows paths,
actual Linux mapped-file samples, actual PE machine0x8664/0x014c, prefix and
build DLL paths/hashes, test executable hashes. All six current core DLL hashes
equal the accepted r1 build identities (differences would be logged, not treated
as historical refusal). In particular the actual DLL files remain under:
`/var/tmp/wex-g4-ac-parent-20261026/build/dlls/{kernelbase,userenv,ntdll}/{x86_64,i386}-windows/`.
No DLL override was present in any actual unit environment. These are normal
builtin unit results, not Chromium rendering, token isolation or full G4 proof.

## Commands and retained evidence

Exact matrix dispatch, once, stdout/stderr redirected to private files:

```text
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/scripts/run-isolated-graphics.py --socket-root /var/tmp/wex-g4-ac-parent-20261026/sockets-r2 --private-loopback -- python3 -B /mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/scripts/validate_g4_appcontainer_independent.py --run
```

Each cell JSON records full init/test argv and separate environment treatment.
Prefixes are NEW `/var/tmp/wex-g4-ac-parent-20261026/r2/{x86_64,i386}-{kernelbase,userenv}/prefix`;
all r1 source/build/evidence/prefixes preserved. Unique r2 filesystem X socket
directory/private mount+network namespace; no shared prefix or display.

Focused command from this worker's scripts directory:
`python3 -B -m unittest test_g4_appcontainer_independent_runner -v`:
**5 PASS,0.212s,no skips**. Actual helper flow with fake processes tests local
override copying/removal, failed init preventing test launch, timeout checked
reap, primary-error preservation, owner/cleanup failure and matrix fail-fast/order.
No production API is mirrored. No broad unchanged suite rerun. Diff check passes.

Private evidence:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/appcontainer-independent-r2-20261026/`:
47-file seal, runner test log, matrix logs/exit0, four cell JSONs, separate init/
unit/cleanup logs, builtin/maps/module identities, final ownership, summary/audit
and executed source snapshots. Source functional diff vs cb1 remains0; old r1
45-file seal independently rechecked unchanged. No payloads/raw private data
committed. Parent C/main D master/submodules/other worktrees and builds untouched.

Remaining defensive API fault gaps are TODO under the human high-value policy;
these passing required cases do not certify untested faults or sandbox semantics.
STOP parent review; no further cell, runtime change or browser trial authorized
by this report.
