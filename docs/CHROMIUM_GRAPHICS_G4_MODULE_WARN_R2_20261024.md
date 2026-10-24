<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R2 module-warning diagnostic — executed once

Executed the one control authorized by `MODULE_WARN_RUN_AUTHORITY_20261024`
from unchanged preflight0e7ba49a…/TARcb27aa60… and exact controller/run/80s-stop
commands. **EXECUTED_ONCE_DIAGNOSTIC_NOT_ACCEPTANCE**. No code/matching changes,
retry, extra guest SSH, new image, calibration, rebuild, download or extraction.
R1 evidence/profile remains preserved; R2 stage/output/profile now exist too.

New correlated evidence in archived `helper.stderr.log`:

- Native journal binds Chromium WinPID228 (`00e4`), creation time
  134373519606819690, exact image/normal argv/nonce/R2 profile, flags0,
  no inheritance/environmentNULL; native binding tick42735ms.
- Line652, Wine timestamp44.106, PID`00e4`/TID`0234` (decimal564):
  `LdrGetProcedureAddress` reports named
  `DeriveAppContainerSidFromAppContainerName` absent in
  `C:\windows\system32\USERENV.dll`.
- Immediately next line653, SAME PID/TID/time: SEH dispatches`c06d007f`.
  Exception info/register/handler records remain raw; no pointer was dereferenced
  by a debugger/probe and no full argument claim is made.
- Held-handle signaled exit at tick45678ms returns3228369023/`0xc06d007f`,
  2943ms after binding. Independent Linux helper Popen932/start4174 exits
  naturally0, reaped before prefix cleanup; observer not killed.

This identifies a concrete failed resolution in the owned browser immediately
preceding the matching exception and is stronger than the earlier static list.
It supports this delayed-import failure as the observed startup blocker; it
does not establish a safe API implementation or full browser/security root cause.
Earlier FlsGetValue2/DnsIsZtEnabled and other-process warnings are preserved, not
automatically assigned causality. No speculative API/security patch follows.

Observation/cleanup complete=true, runner8.449s; diagnostic consumer1 deliberately
nonaccepting, archive0, adapter0/orchestration complete. HTTP requests0/errors0.
Viewed actual terminal front: console/window with WSALookupServiceBegin warning,
black desktop and pointer; no fixture/page or graphics acceptance. Helper stderr
182,182 bytes, below1MiB quota; logs were not truncated or assertions suppressed.

Fresh host292 file observations completed without errors/historical differences;
durable credential/public-seed match and intended idle R4 chain observed. Actual
readiness passed, guest free15,329,005,568 bytes before staging, COMPLETE-empty
prefix, canonical fresh paths and16 current deployed hashes passed. Existing
fixed258-file runtime reused read-only;19 environment files/internal loader
observed. PrivateLO before/after match, unique sockets and actual inner/outer
VirGL/GLES2 checks passed. Raw remaining seconds:674 gate880.358,
650 gate855.794, ownership435 gate855.654, consumer435 gate850.397.

Exact owned SIGINT stopper0; attributed guest SHUTDOWN/guest-shutdown and both
matched QMP ACKs/protocol complete, QEMU0, graceful=true/no fallback, both children
stopped. Persistent controller session0/Windows WSL0. Launcher379/QEMU394/Xvfb381
absent afterward. Post-stop normal four-layer/idle observations retained; R4
SHA9feb1369… changed as expected, full parent and both preserved failed siblings
unchanged. Older large layers received metadata observations, not65GB rescans.

Private evidence root:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-native-module-r2-20261024/`.
Contains execution/readiness/budget/transfer/backend/namespace records, native
journal, Chromium/helper/compositor logs, actual PNG and launcher/QMP/serial logs.
`chromium-native-evidence.tar.gz` has16 verified regular files,
SHA`5625781762bd5a6ce04861517da66d8a9001cbe6a9dd9af98db7951d08d68805`.
Extracted `chromium-native-evidence/terminal-front.png`:
SHA`67c598688a865d804fdd23cd28ee02b33b3154dc6da419a157d08e4ef7e22d3d`.
Sibling `chromium-native-module-r2-20261024.hostaudit/` holds before/after,
adapter/stop stdout/stderr/exit records, module/SEH line references, summary and
46-file seal. Profile/vendor bytes remain private in guest, excluded from archive
and Git. Report-only; no unchanged unit reruns. `git diff --check` passed.
Only C: graphics report staged; M wine/mainDmaster/other workers untouched.
Stop for parent actual-evidence review before any next implementation or run.
