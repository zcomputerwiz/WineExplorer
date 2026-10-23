<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Selected Edge stacks — OFFLINE / PREPARED_NOT_RUN

Implements EDGE_SELECTED_STACK_PLAN only. Code `6b1d2c03`; no VM/SSH/Wine/debugger/
Edge/native-PE execution this milestone. No loaded-build, production pin, D: master,
other checkout or original waiter/schema changes; existing unused `M wine` remains
unstaged. Parent review is required before any live diagnostic.

## Integration and boundaries

`check-g4-edge-headful.py` adds opt-in `--owned-stack-diagnostic`, requiring all
four startup/private-lo/HTTP/owned-thread flags before output. Calibrated tool
hashes are checked before output/listener/consumer; default FILE/HTTP/owner modes
do not import new optional stack code. Only the existing45s readiness timeout,
after its failure-front and fresh owner inventory, can trigger stack collection.
Original timeout/diagnostic exit1 remain even if useful stacks are collected.

New `g4_edge_selected_stacks.py` selects fresh GUI, unique IO/Broker and all three
scoped thread-pool role groups in deterministic order, GUI deduplication, <=8,
no truncation or historical IDs. Positive SUCCEEDED HRESULT0x10000000 is valid.
Successful unnamed nonselected threads are explicitly excluded; failed initial
name/query evidence fails closed because a required role could be hidden. Selected
threads require active/query/owner/name/creation evidence. Before/after requery
compares exact native window/title/full image/GUI/geometry plus selected creation,
name and owner; nonselected churn is permitted. Linux exact original Popen PID/
start/prefix/app/profile identity is independently checked, never mapped to WinIDs.

Small shared source-format parser extraction in `g4_stack_trace.py` keeps original
two-thread calibration/module requirements and tests unchanged, while validating
variable Edge headers and useful msedge/Windows frames. Nonzero PC/valid zero RBP,
contiguous frames, exact full image/TID/process bindings; initial unbound output
retained separately. No fabricated headers/PDB/argument claims. Numeric command
whitelist only: attach, one bt per selected TID, detach, quit. Calibrated bounded
transport reused, no framework. `debugger_attempted` means dispatch attempted,
NOT successful attachment; full selected trace binding is separately recorded.

Additional observation20s; debugger10s includes monitored quota/timeout kill/reap;
post-native-query/Linux checks reserved before dispatch. Cheap browser quota,
role/owner and compositor/server checks run throughout helper waits. Raw helper
logs retain the existing1MiB soft quota, browser128MiB, no truncation/retry. Forced
debugger termination may kill the owned browser and remains a failed observation.
Post evidence is attempted even after parser/debugger failure; errors do not mask
the primary browser timeout. Builtin dbghelp=b, exact BUILD Wine/server and empty
symbol paths: on-disk hashes + loader policy, NOT loaded-file attestation. Status
can credit stacks/post-ownership ONLY; UI resume/detach API return NOT_PROVEN.

## Actual offline verification

All commands exit0, explicit C: worktree (WSL `/mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer`):

```text
wsl -d Debian --exec unshare --net sh -lc 'ip link set dev lo up && cd /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts && python3 -B -m unittest test_graphics_private_loopback test_g4_edge_headful test_g4_edge_http_fixture test_g4_edge_owned_threads test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness test_g4_stack_commands test_g4_stack_calibration test_g4_edge_selected_stacks'
node scripts/test_g4_edge_scene.js
pwsh -NoProfile -File scripts/test_g4_virgl_session.ps1
```

226 Linux tests PASS/no skips (all205 baseline +21 new), actual Node fixture PASS,
real intercepted PowerShell controller PASS. Negatives cover mode/pin guards,
default optional dependencies, selection/schema/reuse/errors, before/after changes,
allowed background churn, exact command/argv, missing binding/module/frame errors,
budget/no attach, timeout/quota/cleanup and retained primary timeout. Mocks are
not Wine proof. Parent independently executed pure selection against the ACTUAL
archived R1 inventory56/seven unnamed: exact8 roles,48 excluded, correct numeric
commands. Historical archive compatibility ONLY, not a new native observation.

Unchanged query-only observer host-cross-compiled `-O2 -static -Wall -Wextra
-luser32`, exit0/no warnings; PE not executed. Real Linux-native X-socket control
also PASS (distinct inodes, SO_PEERCRED equals each owned Xvfb, first stable after
second starts; both ended). Private logs/results/artifact under
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-selected-stacks-offline-20261023-r1/`.
Native socket transport used fresh `/var/tmp` because DrvFS cannot host sockets;
the retained result/stdout/stderr are D:. No guest or existing display touched.

## Fresh immutable preparation

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-selected-stacks-r1-20261023.preflight/`
contains PREPARED_NOT_RUN, preflight.json, source-stage.tar, offline-inventory-audit.
Existing ignored C: private procedure gets only explicit `edge-selected-stacks`
mode/inventory. All15 tar/current sources,5 C: helpers,2 seeds, installed-reference
hash and19 expected core/tool hashes audited; future run root absent.

- Preflight SHA256 `437f88ed1f908081ba893d436cc7d9770650c5a37ee32d88cf20e136ebfc80db`.
- Archive SHA256 `73bba4ef49be51497731505c3ee00333ee989e36d7e34b2e09af5bac5935e444`.
- Private procedure SHA256 `28f75c2bda4b80c7701a0b786883f4821dbdd2a67735caeea3754a63e7401592`.

Exact C: launch/run/identity-stop commands and all five flags are recorded there;
900s launcher/90s cleanup reserve,650/435 rawUTC guards,285s runner/55s reserve,
300s SSH unchanged. Retained installed prefix + NEW run profile, original fixture/
URL/security/backend unchanged. Last live free1,308,176,384 bytes is historical:
future >=1GiB check BEFORE staging/creation remains mandatory, no evidence deletion
or reinstall. Guest hash/backend/ownership checks NOT performed offline. No live
authority from this preparation; return for parent source/tests/inventory review.

## Independent parent review / one diagnostic authorized

Parent read all changed source/tests and private mode additions. Independently
passed226 Linux tests/no skips, Node/controller, actual Linux-native socket control
and observer cross-compile without warnings. Pure selector also accepted actual
historical56/seven-empty-name inventory as8 selected/48 excluded; current shared
parser accepted actual calibrated waiter stdout with8/6 frames unchanged. Neither
offline replay is a new Wine/Edge run or proof of Edge UI resume.

Independently verified exact preflight/archive hashes above, all15 tar/current
sources,5 helpers,2 public seeds, installed-reference hash,19 exact pins matching
accepted calibration, correct commands/flags/C:source/D:storage and absent run.
ONE timeout-only Edge selected-stack diagnostic is authorized with this immutable
preparation and all live gates. No retry, extra browser, calibration repetition,
fault or production/security change. Preserve primary timeout and raw partial
evidence; debugger failure may kill its attached owned browser. Parent reviews
actual stacks and post-ownership before choosing any next action. Never a G4 pass.
