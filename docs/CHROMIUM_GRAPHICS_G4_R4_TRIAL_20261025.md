<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R4 startup diagnostic: preserved pre-probe deadline failure

One authorized R4 trial ran using the parent-reviewed immutable preparation.
Host raw-UTC readiness674/preflight650/preconsumer435 guards passed; guest
readiness passed. Current21 transfer files were deployed with integrity checks;
guest free space was checked before staging. Consumer1/diagnostic1 and archive0
were returned and the diagnostic record was preserved.

Parent independently verified12 archive files against the extracted evidence,
all21 deployed source hashes, outside module contents unchanged, and strict QMP
capabilities/powerdown ACKs and post-request guest shutdown evidence. Guest
free space before staging was15,035,727,872 bytes. Archive SHA256:
`c36c2ed06ed4d4796651d0d06b20696d2483cf99bb3e590a090d9160da7579e0`.
Private audit summary is the lab's
`chromium-userenv-r4-20261025.parent-review.json`.

Actual inner Wayland GL and outer compositor both reported
`virgl (D3D12 (Intel(R) UHD Graphics 750))` and passed explicit backend checks.
The native compositor started. The run then stopped before creating an API
probe/browser child or HTTP fixture with
`TimeoutError('Need full45s API probe/reap/intermediate cleanup allowance')`.
Consumer elapsed2.179s, observation_complete=false, cleanup_complete=true.
There is no browser rendering or new API outcome from this trial.

Parent deterministically reproduced that exception in the actual phase using
clock31.1/deadline200: the computed local end-start is44.99999999999999 while
the caller has168.9s available. See CHROMIUM_API_PROBE_DEADLINE_REWORK_20261025
for the bounded delegated fix. No blind retry or other API patch is justified.

The exact-identity stopper observed graceful QMP shutdown, QEMU0, both owned
QEMU/Xvfb stopped and fallback_used=false. Windows launcher exited0. The failed
run, stage, output, original source archive and front capture remain preserved
under D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-userenv-r4-20261025.
These paths are consumed; subsequent execution requires a fresh fixed-output
preparation after the clock fix is reviewed. Main master, Wine/gitlinks and
other workers' VM state were unchanged. No G4/master merge claim.
