<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Disposable accelerated graphics VM — provisioning record

User approved the conventional accelerated Linux VM approach. No WSL kernel/
Mesa replacement, changes to existing VMs or imported OOBE/token work.

## Identity and containment

- New machine: `WEX-G4-Acceleration-20261022`.
- UUID: `d15c7143-37d3-49d2-92f0-9c06690ee691`.
- All machine files/disks/seed/serial logs/snapshots:
  `D:/VirtualBox_VMs/WEX-G4-Acceleration-20261022/`.
- VirtualBox7.2.20,4vCPUs,6144MiB RAM,VMSVGA,128MiB VRAM,3D enabled.
- New sparse system.vdi,64GiB virtual capacity (initial physical size~1.12GiB).
- No cloning of existing workers' guest states/build disks. No shared folders,
  clipboard, drag/drop or audio. NAT SSH bound only127.0.0.1:2234.
- SSH user `debian`, existing lab SSH key `D:/VirtualBox_VMs/vm_key`, no password
  authentication/root SSH. Public key only was copied into the private seed.
  Known-host file is local to the new VM directory, not the user's global file.

## OS provenance and provisioning

Official HTTPS source:
`https://cloud.debian.org/images/cloud/trixie/latest/debian-13-generic-amd64.qcow2`.
Downloaded checksum list and image remain in `image-source/` on D:.
Generic uses the standard Debian kernel; cloud-optimized genericcloud omits
many device drivers. See [Debian Cloud documentation](https://wiki.debian.org/Cloud/).
Original QCOW2 is kept intact; qemu-img converts it to a new dynamic VDI before
VirtualBox expands that new disk. Cloud-init grows the guest root on first boot.

Verified SHA512 against the checksum list fetched over official HTTPS:

```text
70ef6adc1c16155f8d575731b37d0f976d1c60b6144ac3f57914a3f4b30b202b69584d5d143aa39da0f0fe81df9e5b698909c0d94c7d91c6f6c4bb6581ead5f0
```

This daily image directory provides SHA512SUMS but not SHA512SUMS.sign. There
is no independent OpenPGP-signature claim: TLS origin plus published checksum
was verified, not a Debian release-image detached signature. No image was booted
with a mismatching checksum. Official source redirects to Debian's umu.se mirror.

NoCloud seed sources are private ignored `build/g4-vm-20261022/seed/{user-data,
meta-data}`; seed.iso stays D:. It provisions SSH, Mesa/libdrm/GBM/EGL diagnostics,
Wayland tools/Weston, basic Xorg/vmware driver/xinit and dbus-user-session, plus
video/render group membership. No Wine build or private Chromium runtime has
been transferred at this stage. Baseline packages use the guest's Debian apt
sources, not third-party driver scripts. Provisioning must finish before tests.

Initial host VBox.log identifies the real host adapter as Intel UHD Graphics750
(8086:4c8a), enables VMSVGA3D caps, and boots successfully headless. These host
observations do not by themselves certify the guest renderer or actual pixels.

## Readiness gates and verified evidence

1. PASS: key-only SSH, cloud-init `done`, errors[] and recoverable_errors{}.
2. PASS: Debian kernel6.12.113+deb13-amd64 binds vmwgfx to15ad:0405;
   /dev/dri/card0 (226:0) and renderD128 (226:128), video/render groups work.
3. PASS: Mesa25.0.7 EGL1.5, GL4.1, renderer `SVGA3D; build: RELEASE; LLVM;`.
   LLVM here is the driver's shader compiler, not a llvmpipe renderer.
   Host VBox.log reports feature level0xb100 and Intel UHD Graphics750 adapter.
4. PASS: exact pinned wexcomp creates GLES2, OpenGL ES3.0, renderer SVGA3D.
   See below for independent actual compositor-pixel controls.
5. NEXT: stage only owned combined-WoW64 test binaries and lawful private
   runtimes; repeat exact pixels/lifecycle/recovery with hardware and software
   labels kept separate. Existing WSL client proofs are not outer-GPU proof.

## Command errors / diagnostic limits retained

- Web fetch of cloud.debian.org was robots-blocked; Debian Cloud wiki open
  returned403. Official HTTPS curl access succeeds. Search results explain
  generic versus genericcloud; no third-party image substituted.
- `curl .../SHA512SUMS.sign` returned404. The following gpgv failed because that
  file did not exist. A check whether SHA512SUMS was clearsigned also failed
  `no valid OpenPGP data found`. The actual directory index confirms only an
  unsigned checksum list; provenance limitation explicitly recorded above.
- `curl .../latest/ | head -35` returned curl23 when head closed its output
  pipe; the index itself downloaded successfully. Not a disk/download failure.
- Unescalated `VBoxManage modifyvm --help` failed E_ACCESSDENIED during COM
  initialization. Approved same-host access reached help, but that command
  itself printed `Not enough parameters`; correct option names were visible.
  The actual new-VM configuration command succeeds.
- xorriso warned lower-case `cidata` does not comply with ISO9660 naming rules;
  this is the intended NoCloud volume label, and seed creation succeeds.
- First SSH probe times out at banner exchange while the kernel is still at
  early boot (~3seconds). No existing VM's SSH/forwarding/service was changed.

## Pinned outer-compositor controls

Only the public pinned compositor and original Linux protocol test were
transferred; no Wine executable, Chromium runtime or other workers' code.
Compositor remains94faa651 with unchanged binary SHA256:

```text
4a24dd13fed3d733d4edd6f213e4db284ff0dee9229e5df898d694d4446e0ec8
```

Guest lab `/home/debian/wex-g4`. Open configuration copied from scripts/wayland.
Existing generated protocol C/headers came from the owned combined-WoW64 build;
the `--wine-build .../generated` argument here supplies ONLY protocol metadata,
not an installed Wine runtime. Minimal Debian compiler/Pillow/Wayland-dev and
wlroots0.18.2/grim dependencies were installed into this disposable guest.

| Guest run directory | Result |
| --- | --- |
| egl-gles2-first | Native private-endpoint EGL SVGA3D PASS; outer log independently shows GLES2/SVGA3D |
| gpu-render-contract-first | All24 exact GPU compositor pixel/geometry/lifetime controls PASS |
| software-render-contract-first | Same24 exact controls PASS with explicit software pixman |
| gpu-render-contract-final | Repeat24 PASS with explicit cleanup records, probe exit0 |

New strict outer-driver verification reads the compositor's OWN context log,
requiring GLES2 initialization and exactly one matching non-software renderer.
This is distinct from a client's EGL query. Startup-log hash is a snapshot,
not a claim that the compositor's later append-only log remains unchanged.
SVGA verifier cannot be substituted for the WSL D3D12 verifier; wrong drivers,
software strings, missing/duplicate outer contexts and exited compositors are
unit-tested failures. All24 local compositor/consumer/resource unit tests PASS.

No pixel/color/bounds assertion was relaxed. GPU captures independently show
4096 red pixels attached and0 after parent shrink. Both actual PNGs inspected.
The24 cases cover placement/negative/empty clipping, buffer scale, parent shrink/
regrowth, wrong/stale/revoked parents and producer/parent/surface destruction.
GPU rendering is verified here, but the test output is headless: physical/VM
display scanout, Wine DMA-buffer interop, real Chromium, input and installed-shell
acceptance are NOT thereby certified. No zero-copy, containment or G4-ready claim.

Final raw JSON/logs/PNGs retained in guest directories and host
`D:/VirtualBox_VMs/WEX-G4-Acceleration-20261022/evidence/` (including final run).
No test process was left running: pgrep finds neither labwc nor render-role;
final results record probe_stopped=true, owned_compositor_stopped=true.
Guest root uses2.2GiB with59GiB available. Sparse disk remains on D:.

## Continuation

VM baseline is gracefully powered off after tests; existing WineExplorer-VM
remains running untouched. Powered-off baseline snapshot
`drm-svga-ready-20261022` has UUID `3cef3350-4d51-4ce1-9298-1baea21b1d73`;
snapshot storage path verified under this new VM's D: directory. Original sparse
system.vdi physical size is2,864,722,432 bytes (~2.67GiB), not64GiB allocated.
Start only this new guest when continuing:

```powershell
& 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe' startvm WEX-G4-Acceleration-20261022 --type headless
ssh -F NUL -o BatchMode=yes -o IdentitiesOnly=yes -o UserKnownHostsFile=D:/VirtualBox_VMs/WEX-G4-Acceleration-20261022/known_hosts -i D:/VirtualBox_VMs/vm_key -p 2234 debian@127.0.0.1
```

Exact final GPU command (inside guest):

```text
python3 -B /home/debian/wex-g4/check-wex-render-surface.py --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --wine-build /home/debian/wex-g4/generated --compositor-renderer gles2 --gl-driver svga --output-dir /home/debian/wex-g4/NEW-UNIQUE-OUTPUT
```

Next: add explicit SVGA/GLES2 selection to the real-consumer harness (currently
its software/d3d12 selection and pixman outer default are separate), stage an
owned installable combined-WoW64 Wine bundle and private lawful cached runtimes,
and repeat actual Chromium pixels/lifecycle/recovery. Do not copy WSL's D3D12
Mesa libraries into the VM or infer end-to-end proof from the present controls.
Continue remaining real-consumer copy/completion measurements afterward.
No source production changes, master merge, OOBE/token imports or reset credit.
