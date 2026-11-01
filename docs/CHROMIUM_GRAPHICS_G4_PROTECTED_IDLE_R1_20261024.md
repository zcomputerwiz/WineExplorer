<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# ONE protected boot — host credential unavailable to SSH; stopped gracefully

Continuation of the preserved [prelaunch stop](CHROMIUM_GRAPHICS_G4_PROTECTED_IDLE_PREFLIGHT_STOP_20261024.md),
under [PROTECTED_IDLE_RUN_PLAN](CHROMIUM_GRAPHICS_G4_PROTECTED_IDLE_RUN_PLAN_20261024.md).
No source/test/manifest changes, image recreation, guest repair, staging,
calibration/browser execution or retry. M wine/D:master/other workers untouched.

Explicitly authorized existing411-byte D: lab key was copied exclusively to
`/tmp/wex-visible-recovery-r1/vm_key` (directory0700/key0600, no symlink/existing
targets). Internally derived public key matched accepted seed; fingerprint
`SHA256:JBfsSgezXod5+mnaZgYAbCMZwC65X0PDgPfejqFDPHQ`. No private material
printed/committed. Fresh r2 host audit passed27 input hashes/expected19 pins,
seed/ISO/probe, unused new-top identity/hash, idle snapshots, original3 identities,
normal-lock4chain and host reserve. Original failed audit remains immutable.

## Actual single boot / diagnostic

Exact immutable prep ce85b091... launch/run/stop commands used, no TCP/ad hoc SSH
wait. Controller started02:05:17.9893923UTC; owned launcher/QEMU/display identities
were verified before diagnostic. Raw674 headroom gate passed:882.497648s remain,
readiness cap180s. Four SSH255 attempts over21.109304926s; diagnostic exit1.
EVERY attempt explicitly records:

```text
Warning: Identity file /tmp/wex-visible-recovery-r1/vm_key not accessible: No such file or directory.
```

First3 additionally reported banner-exchange timeouts; fourth reported
`debian@127.0.0.1: Permission denied (publickey).` All stdout empty. These bytes,
durations, actual argv and exits are in `readiness.json`, preserved by the gate.
The copy existed/passed host gates earlier but was unavailable in the actual SSH
context. Post-stop host path check also found it absent. Its lifetime/disappearance
cause is NOT established; no guest authorized-key mismatch or auth repair inferred.

No authenticated JSON reply, boot UUID or cloud-init/systemd/power-handler proof.
Readiness failed closed;650/free/19 ACTUAL hashes/435/original idle probe were
NOT reached. No later SSH, staging, transfer, archive, service fix or consumer.
Original earlier idle exit1 remains unexplained. Serial reached a login prompt,
which is NOT authenticated readiness. Host GL check passed; no inner/outer guest
VirGL, pixel, browser, calibration or G4 acceptance claimed.

## Verified shutdown / backing preservation

Immediate exact-identity stopper80s exited0. Persistent controller held through
exit0 at02:06:08.7322270UTC. QMP transcript retains distinct matching capability/
powerdown ACKs, POWERDOWN and SHUTDOWN guest=true/reason=guest-shutdown, observed
after own request. Protocol4.712364207s; whole shutdown4.843836898s. Actual owned
QEMU0, fallback=false, both children stopped; launcher graceful=true.
Linux342/357/344 and Windows29292 independently confirmed absent. This proves
this shutdown's strict gates, not filesystem cleanliness or the old timeout cause.

Post-stop normal-lock4chain passed; original3 device/inode/size/mtime/ctime equal.
Frozen former-top full SHA c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936
matched before/after in10.678732840/11.162172499s (<120s), no65GB rescan.
New top alone changed:27590656physical bytes, final SHA256
`393308d2dcf343df1dac1d6a2f47a4f4f27d70e26a88596d0d3f97eacf14999c`.
Idle/open-file snapshots and27 post-run inventory hashes pass. Preserve all layers.

## Evidence / boundary

Private D: lab root `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `native-idle-ready-r1-20261024/`: execution/readiness/budget/QMP-results/identity,
  serial and QEMU/Xvfb/host-GL logs, copied immutable prep.
- Adjacent `.wsl-session.json`, `.wsl.stdout.log`, `.wsl.stderr.log`.
- `native-idle-ready-r1-20261024.host-audit-r2/`: credential match/fingerprint,
  before/after metadata/hash inventories, diagnostic/stopper stdout/stderr.
  `final-evidence.json` seals28 final files; SHA256
  `8a71bec6719ebcf5902f21ff0c8a2f4766165fb8f6b4df673b838f1af8408ccf`.

No unit reruns or implementation changes during this execution-only handoff.
STOP for parent review; no further key recreation, guest boot/SSH, credential/
service repair or calibration. A possible volatile-/tmp/session-lifetime issue
is a future host-only question, NOT an established cause or automatic next task.
