<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Owned C: source portability — offline review checkpoint

PREPARED_NOT_RUN. Implements only the source relocation prerequisite in
SOURCE_WORKTREE_20261023, not HTTP or another same-file dispatch. No VM, SSH,
Wine/browser, installer, network-namespace setup or guest operation ran.
Parent review must precede HTTP implementation and any eventual live authority.

All tracked edits/tests/commits used explicit working directory
`C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer`.
Parent's docs-only `3a29549c` arrived above the initial `4f85cf82`; implementation
commit is `2e306fac`. D: master files, old preparations and submodule contents
were not modified. The pre-existing unused ` M wine` gitlink mismatch remains
unstaged; no submodule update/build. Guest binaries/pins are unchanged.

## Small adaptation

Tracked controller derives its Windows launcher from its own PSScriptRoot,
checks that file, converts with Debian wslpath and fails before Start-Process
on conversion failure. Existing D: logs/storage, hidden persistent session,
900s launcher and identity/SIGINT/QMP stop procedure remain unchanged. Session
records now include both launcher paths. No general orchestration framework.

Copied ONLY launch.py, presentation-timing-trial.py, public user-data and
meta-data into ignored C: `build/g4-virgl-20261022/`. No key, disk, vendor file,
prefix/profile or runtime copied. Private REPO constants now name the C: WSL
path; launcher identity uses Path(__file__).resolve(). Private manifests record
actual C: source/helper/controller/stop paths and D: VM storage, plus public
seed hashes. The public key matches the existing seed; no credential accessed.
Tests convert the actual scene-test path for Windows Node rather than reading D:.
Diagnostic runner, fixture, isolation wrapper and production sources unchanged.

One actual offline preparation failed: Linux Git cannot resolve the managed
worktree's Windows drive-letter .git pointer (exit128). Preserved archive and
PREPARATION_FAILED.json in
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-file-loopback-portability-prep-20261023.preflight/`.
Private helper now queries Windows git.exe with the wslpath-mapped C: checkout;
no shared Git metadata was rewritten. Intermediate v2 preparation is preserved,
not run; final v3 below adds seed inventory and references the code commit.

## Final inventory, not execution approval

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-file-loopback-portability-prep-v3-20261023.preflight/`
contains PREPARED_NOT_RUN, preflight.json and source-stage.tar. Exact launch,
run and stop commands are in that JSON. This is a relocation audit using the
existing same-file diagnostic inventory, **not authorization to repeat it**.
HTTP remains unimplemented and undeployed. Future profile/stage names are
recorded, not checked on or created in the guest.

Offline re-verification passed: all9 tar members' sizes/hashes match current C:
source,5 helper hashes/absolute C: paths match,2 public seed files match, accepted
installed-evidence reference matches,17 expected guest binary hashes retained,
and prospective host run directory absent. No deployed/guest hash claim.

- Archive SHA256: `4a306b982108616eff5514dd4014fefa0d0136b0e8dde81057ccafa5c56d9416`
- preflight.json SHA256: `4f1a7ef69f76d4c169005d8eac8568bf6d7af56c99f626b8515fb923274d76a3`
- C: private launch.py SHA256: `944e3eac6ba80e3d6c56b6fb7eae9235e21200936f2349f9296a8e4cf678d221`
- C: private procedure SHA256: `f22e3403b37c25bd0e84bb8910a312efc63bb108d85d2634e5bb59637a01d6b5`

## Actual validation

Commands from the explicit C: workdir, all final exit0:

```powershell
wsl -d Debian --exec sh -lc 'cd /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts && python3 -B -m unittest test_g4_edge_headful test_g4_virgl_budget test_g4_virgl_launcher'
pwsh -NoProfile -File scripts/test_g4_virgl_session.ps1
node scripts/test_g4_edge_scene.js
git diff --check
```

73 Linux tests PASS, no skips. Includes actual fixture execution via Windows
Node (Linux Node absent), mocked path conversion failure/actual-checkout mapping,
Windows-Git argument/error tests, existing negative budget/no-SSH controls and
execution of only the real private launcher's identity-update AST with a wrong
repo alias. Launcher boot script was never imported/executed.
PowerShell test parses and executes the actual controller through real wslpath,
intercepts Start-Process before launch/log writes, checks C: launcher/D: logs,
unchanged isolation arguments and absence of run artifacts. Original-fixture
Node GL/FBO/frame/timeout/error/context-loss controls PASS. No live coverage or
graphics acceptance implied. Next: parent relocation review, then separately
bounded offline HTTP milestone per the navigation/loopback plan.

## Independent parent acceptance

Parent independently passed the73 Linux tests without skips, actual Node fixture,
and the real PowerShell controller test intercepted before process creation.
Verified all9 current archive members/sizes/hashes,5 helpers at their actual C:
paths,2 seed hashes, installed reference and prospective run absence. Main D:
checkout remains master with its original submodule mismatches, unchanged.
Relocation prerequisite accepted. This v3 inventory is an OFFLINE AUDIT ONLY,
never an authorization to rerun the same-file arm. Next is HTTP implementation,
new complete preparation and independent review before ONE HTTP diagnostic.
