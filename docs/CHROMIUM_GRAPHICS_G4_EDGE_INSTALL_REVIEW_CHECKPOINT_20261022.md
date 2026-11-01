<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent checkpoint: reviewed Edge installation prerequisite

Branch: `codex/g4-r1-completion`. Reviewed implementation `9348b4be`, preparation
report `f15ed581`. This checkpoint supersedes older continuation instructions,
not historical failed evidence. No master merge or production patch this slice.
Wine pin `4315be83d75644f13500381dcec94dcdc5fa904e`; compositor pin
`94faa651f9e267fd03157348b26ffb3c66082484` unchanged.

## Verified progress

Two independent accelerated visible-WebGL single-GPU-child recovery trials passed
(50012793, ed7f7ccc). Parent inspected actual settled fronts, all 78 archived file
hashes, recovery/pixel/backend/identity gates and clean QMP/QEMU/controller exits.
Post-recovery fresh-process x64/i686 own/foreign-full DComp controls each passed
4096 exact pixels; strict real WebView2 WebGL1/2 controls passed (9cb47a12,
9d8e6935). These reboot/fresh-prefix controls do not prove faulted-process resource
survival. Historical stale fronts and whole-window blanking remain unexplained.

Preserve the x64 controller budget failure: re-parsing a local DateTime inflated
the remaining window. Raw-UTC budget enforcement was repaired and tested in
75bba089/748482c6; subsequent live controls enforced both 650/435-second guards.

The user separately accepted Edge terms and authorized private download/install:
"Yes, accept and test privately". Microsoft Edge Stable 156.0.4314.42 Windows x64
MSI is private on D:, 215257088 bytes, SHA256
`324d226c5ade08c2776645cfc2a7b12af9c08214abf18c6499410cd8ed92d1b3`.
Parent independently rehashed it and verified Valid Microsoft Authenticode.
See FULL_EDGE_ACQUISITION report; no payload is in Git. Consent is not graphics
acceptance. **Edge has not been installed or launched in this preparation.**

## Installer-only implementation review

Parent read the final runner/tests and private dispatch procedure. Independent
Linux execution passed all **53 focused tests, no skips**:

```powershell
wsl -d Debian --exec sh -lc 'cd /mnt/d/DeepThought/Projects/WineExplorer/scripts && python3 -B -m unittest test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness'
```

Twenty tests exercise the installer harness with mock vendor/process operations;
they are not actual MSI compatibility evidence. Guards pin approved MSI bytes,
consent/signature provenance, owned fresh paths and >=5 GiB free. Only ordinary
Wine msiexec installation is requested; no extraction substitution or browser
invocation. Exit zero alone cannot pass: require installed x64 PE bytes/hash and
matching product registration. Version evidence is registration-only, explicitly
not independently read from the PE version resource. Installer-created processes
are sampled and cleaned only in the private prefix. Timeout/reboot-required,
cleanup, payload and evidence failures fail closed. Preserve logs and prefix.

Current preparation:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-install-r1-v2-20261022.preflight/`.
Parent independently verified archive SHA256
`35150687282dd716801a7ab4c55ae9c70169c966d136d43a9d098696f2072f63`,
all seven tar members/size/hash, five current source and five host-helper hashes,
and absent run directory. Status **PREPARED_NOT_RUN**. Older non-v2 preparation
is superseded. The manifest contains exact launch/run/stop commands, 16 expected
guest binary hashes and the retained guest prefix path. Do not regenerate or
mutate reviewed files silently; recheck hashes before execution.

## Next bounded dispatch after reset

Resume GPT-6 Luna Dirac `01a163b6-80db-7cb1-9ab3-7687bbd04067` (closed after
completed implementation to release the slot). Parent plans/reviews, Luna executes.
Use EDGE_INSTALL_PREFLIGHT_PLAN and EDGE_INSTALL_PREP reports. One normal MSI
attempt only in the owned D:-backed VirGL guest, using the v2 manifest commands.
Recheck >=5 GiB guest space BEFORE transfer/creation; if insufficient stop and
report, do not delete evidence. Check 650/435-second raw-UTC guards, exact live
launcher/QEMU/Xvfb identities and deployed source/binary hashes. Offline isolated
per-run sockets, verified inner/outer VirGL, no sandbox bypass/capability spoofing.
Never rebuild loaded binaries. Launcher 900s; independent identity stopper and
90s cleanup reserve; require QMP/child/controller clean-exit evidence.

On install failure, timeout, reboot requirement, missing registration or payload,
retain exact failure/prefix/logs, return for diagnosis: no retry, extra Windows
components, administrative extraction or browser launch. Installation success is
only the prerequisite. Parent reviews before a separate full-Edge headful smoke.
Real shader/draw/texture-sampling coverage follows; existing clear/FBO probes do
not certify general WebGL workloads. Lower-priority shell cosmetics, efficiency
and unrelated API gaps remain TODO, not full-G4/merge passes.

No VM/SSH/installer/browser ran for the current installer preparation. All prior
owned tests/guests were stopped with clean shutdown evidence. Leave WineExplorer-VM,
Qwen/OOBE/security/SVGA and other workers' checkouts alone. All new VM storage and
private payload/evidence remain on D:. No host installation or payload publication.

## Usage and continuation

Last observed five-hour usage 92%, weekly 14%; reset 2026-10-23 00:18:39 UTC
(19:18:39 America/Chicago Oct 22). No new live attempt started near the checkpoint
threshold. Keep the existing heartbeat for shortly after reset; inspect live limits
and HEAD before resuming. At >=95% five-hour used, safely checkpoint and push;
at >=99% weekly used stop until reset. Never consume a reset credit. Continue
high-value Chromium/Edge work under the user's existing authorization.
