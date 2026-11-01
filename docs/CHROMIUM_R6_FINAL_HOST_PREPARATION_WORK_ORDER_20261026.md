<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Luna: finish the concrete R6 host/guest preparation, offline only

Use existing Luna Dirac and YOUR stopped clean D:/DeepThought/Worktrees/
chromium-r5-preparation/WineExplorer checkout. Create codex/g4-r6-final-preparation
from current parent code-review commit, preserving all prior committed independent
validation history/branches. Do not modify Antigravity's stopped checkout or old
R5/R6 artifacts. Read CHROMIUM_R6_PARENT_CODE_REVIEW_20261026.md first.

R6 code is accepted and imported; independent Winecb1d117 clean build and actual
x64/i68649security/260userenv units accepted. No DLL rebuild/test rerun needed.
Native metadata PE21/21 separately passed native Windows parent check. No new
API/function/security work. Final acceptance needs the actual private adapter,
not another document pretending host /home/debian and D: are the same directory.

Reuse your exact already-accepted R5 host/guest adapter/preflight pattern from
build/g4-virgl-20261022/chromium-userenv-r5-trial.py and its immutable R5 prep.
Read/copy as template only, never overwrite or rerun R5. Implement a NEW owned
private R6 adapter plus minimal original tracked tests/report as appropriate:

- Existing D:-backed system-native-identity-r4-20261024.qcow2 only. Correct owned
  Windows launcher script, explicit D: worker-root source mapping, same guest
  debian@localhost2235 credentials and shared cached runtime/prefix as R5.
- Fresh chromium-registration-r6-20261026 host run, guest stage/output/profile;
  profile OUTPUT/profile, native helper/output contract already aligned. Abort
  on actual path collision, do not delete old evidence to make space.
- Include ALL current R6 runner/probe/view/helper dependencies in source-stage TAR,
  exact four accepted DLLs from /var/tmp/wex-g4-ac-parent-20261026/build/dlls/
  {userenv,kernelbase}/{x86_64,i386}-windows/*.dll, and actual two R6 helper PEs
  from D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/userenv-browser-r6-20261026.build/.
  Correct guest filenames native-exit.exe and userenv-r6-probe.exe. No new
  runtime download/install/extraction or proprietary payload publication.
- Dispatch THROUGH readonly four-module view and proper isolated sockets/private
  net+mount namespaces, lo-only, real inner/outer VirGL. Direct guest-runner
  invocation alone does not set up the views/isolation. Preserve R5 space/budget/
  idleness/HTTP/native observer/archive guards and exact owned stop semantics.
  Include kernelbase AND userenv outside before/after comparisons. Historical
  IDs/hashes/version differences log warnings; actual wrong disk/owner/overwrite
  or failed transfer/function assertions remain failures.
- Correct raw UTC674/650/435 headroom,900s launcher/90s reserve/80s stop,
  existing consumer/API/reap/archive bounds. No hidden timeout enlargement.
- Complete actual immutable PREPARED_NOT_RUN directory with reviewed source/
  helper/binary inputs, source-stage TAR, command inventory and current source
  root. Exact launch (Windows), adapter run (WSL) and exact-identity stop commands.
  Unit tests for adapter preparation/source membership/R6 dispatch/cleanup scope,
  genuine checked bytes/path mapping, no mock evidence as real guest proof.

OFFLINE ONLY: no VM boot, SSH, staging, Wine/browser/probe execution, layer
creation, production build or rerun of accepted unit matrices. Final source/artifact
inventory must reflect current files but don't revive historical provenance
blockers. If unavailable prerequisite blocks preparation, report it specifically
rather than substitute a generic host command. Readonly input inspection/copy to
own D: preparation is allowed. Preserve older failures and PEs, never modify
another worker's loaded binaries/builds/prefixes.

Commit AND push original code/tests/report on your new branch; return exact
commit/prep paths and commands, stop for parent final review before one real trial.
Check usage >=95%five-hour safe bounded cleanup/checkpoint/push/defer; weekly>=99%
stop. Never consume reset credits. No master/token merge or extra worker.
