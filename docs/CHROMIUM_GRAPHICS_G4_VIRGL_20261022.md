<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# VirGL alternative: actual accelerated guest

Parent e9c42347 records the unsuccessful VirtualBox consumer route. Rather
than lying about SVGA capabilities, a fresh authorized QEMU/KVM guest was
created. This does not change the production Wine/compositor pins4315be8/
94faa651 or certify complete G4. No master merge or other worker imports.

## Guest and containment

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/` holds the new sparse64GiB QCOW2
overlay, seed.iso, serial logs, shader caches/config and retained evidence.
Backing image is the intact checksum-verified Debian13 generic image in the
previous VirtualBox test directory; do not move/delete it while overlay exists.
No existing VM disk was cloned/reset. VirtualBox test guest was gracefully
powered off first; existing WineExplorer-VM/Qwen sessions untouched.

QEMU uses KVM/q35/hostCPU/4vCPUs/4096MiB, virtio-vga-gl and GTK GL on an
owned Xvfb. Explicit host GALLIUM_DRIVER=d3d12, LIBGL_ALWAYS_SOFTWARE=0,
LD_LIBRARY_PATH=/usr/lib/wsl/lib. Host GL query proves D3D12/Intel UHD750,
Accelerated:yes. No user/WSLg display is used. Private mount namespace and
unique filesystem X sockets; host network is intentionally retained only
for QEMU user-mode NAT provisioning/loopback-only SSH127.0.0.1:2235.
Individual consumer controls still use offline mount+network isolation.

IPC sockets require a Linux filesystem: small transient X/QMP sockets live
under the owned `/var/tmp/wex-g4-tree-20261020/isolated-sockets`; all guest
disks/payloads/caches/logs remain D:. Launcher is ignored/private
`build/g4-virgl-20261022/launch.py`, bounded900seconds and requests QMP ACPI
powerdown in finally, with bounded owned-process cleanup if shutdown fails.
Run with `wsl -d Debian --exec unshare --mount --propagation private python3 -B
/mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/launch.py`.
Use a fresh bootstrap output directory each run; never double-open the disk.

Key-only debian account uses the existing lab public key, video/render groups;
root/password SSH disabled. Known-host file is private to the new directory.
Cloud-init done/errors[]/recoverable_errors{}. Standard Debian6.12.113 kernel,
virtio_gpu DRM card0/renderD128, +virgl/+context_init cap sets1/2. No custom
kernel, driver capability overrides, host DRM fakery or sandbox bypass.

## Capability gate and harness

Actual guest surfaceless EGL1.5, GL4.1 Mesa25.0.7 renderer:

```text
virgl (D3D12 (Intel(R) UHD Graphics 750))
```

The new original `scripts/probe-egl-capabilities.py` creates a real OpenGL4.1
core surfaceless context, enumerates extension strings and records vendor,
renderer and version. All six relevant inputs present: ARB/EXT polygon offset
clamp, ARB sampler objects, tessellation shader, draw indirect, texture
compression BPTC. This is an inventory, not sufficient D3D/pixel acceptance.
Independent host positive control D3D12 passes. Forced llvmpipe negative
control exits1/passedfalse even though it advertises all six capabilities.
Complete extension list retained on D: `virgl-capabilities-pbuffer/results.json`.

Explicit `--gl-driver virgl --compositor-renderer gles2` support checks BOTH
the outer compositor's own context log and native client EGL context. It
rejects nested virgl(llvmpipe/softpipe), not just a top-level software string.
36 unit tests PASS across theme, compositor, consumer and resource suites,
including missing frozen-frame analyzer rejection before any process launch.

Owned WoW64 bundle and pinned compositor transferred with the same verified
hashes as the preceding report. Both MinGW toolchains/runtime dependencies
installed from official Debian repositories, no system Wine replaced.
Lawful private runtime bundle copied only locally; hash matches preceding
report, never tracked/published. All tests use fresh prefixes/output dirs.

## Completed original pixel controls

`/home/debian/wex-g4/virgl-boundary-x64-full-deps`: actual64-bit own-dcomp
and foreign-full-dcomp both PASS4096/4096 expected front pixels, producer
exit0, first-frame proof. Own GLES2 compositor and native EGL renderer verified
as virgl(D3D12(Intel UHD750)). Not an API/RAF-only observation.
Foreign producer159frames:159 CPU readbacks/compositions, zero GPU captures/
compositions; total PresentCPU816ms/max11ms. **Not zero-copy or efficiency proof.**
No serial consumer performance conclusion from the provisioning session.

`virgl-boundary-i686`: actual32-bit own-dcomp and foreign-full-dcomp also
PASS4096/4096, producer exits0. These are separate fresh-prefix controls.
Wine trace reports maximum0xa100 (10.1), selected feature10 for this probe;
do not claim guest D3D11 from the host's device. No caps were overridden.

`virgl-webview2-strict`: real WebView2155.0.4283.45, host0 and diagnosticPASS.
Both WebGL1 and WebGL2 available/not lost, default framebuffer and texture
readback exactly[255,0,0,255], complete FBO36053, all GL errors0. Native client
and outer GLES2 compositor independently report virgl(D3D12(Intel UHD750)).
Zero GPU crashes, fatal/Skia/shared-context/DirectComposition-output/WARP
errors in the analyzer. Actual screen-18.png inspected: page text, input,
scrollbar, blue content square and caption visible, not an empty frame.
ANGLE's G45/G43 identity is Wine's exposed virtual identity, NOT a physical
Intel G45 hardware claim. No no-sandbox/disable-GPU flags. This is scoped
functional/pixel evidence, not sandbox enforcement or installed-shell proof.

Electron lifecycle/frozen-resize result and final power state are recorded in
the final addendum below when its independent serial run completes.

## Failed attempts and repairs (preserve negatives)

- Initial private launcher used sockets on DrvFS/D:. Xvfb repeatedly failed
  UNIX listener bind and timed out before any QEMU process existed. Cleanup
  true/true in bootstrap-first/results.json. Retain logs, don't call this a
  GPU failure. Second launch uses unique Linux-filesystem IPC directories.
- Early Get-Content of nonexistent qemu/serial logs: not yet created because
  Xvfb never started. Subsequent unbounded reading of the repetitive Xvfb log
  yielded truncated output; use tail for it. All owned processes stopped.
- Unescalated WSL ps: Wsl/E_ACCESSDENIED; same-scope approved read works.
- Lower-case cidata xorriso volume-label warning is expected NoCloud naming.
- eglinfo before cloud-init finished: command not found; wait for provisioning,
  don't start competing apt. Provisioned query succeeds.
- First EGL probe ChooseConfig failed with EGL_SUCCESS/zero configs: default
  EGL_SURFACE_TYPE requests WINDOW on a surfaceless display. Explicit PBUFFER
  config fixes it without fallback or changed GL version.
- First x64 control compile lacked included test_g4_composition_probe.cpp;
  transfer original include, fresh output dir.
- Next x64 control creates real accelerated GLES2 context but pinned compositor
  exits because /usr/bin/Xwayland is absent. Install Debian xwayland and rerun
  fresh; do not disable required compositor functionality.
- setpriv preserves root HOME: Mesa warns /root/.cache Permission denied.
  Subsequent commands explicitly set HOME=/home/debian after dropping UID.
- rg scripts/g4_wexcomp_harness.py: absent; actual helper graphics_wexcomp.py.
- Early tail of Electron host.log while wineboot still initializing: No such
  file; inspect only after the host starts, not a demonstrated consumer failure.
- First full Electron run was INVALID: graphics_frame_pixels.py was omitted
  from staging. The observer raised ModuleNotFoundError at resize_back, so
  frozen-frame acknowledgement failed and lifecycle diagnostic correctly failed.
  All seven action booleans true are NOT a completed lifecycle PASS. Cleanup
  records no remaining servers. Preserve virgl-electron-lifecycle as negative.
  Transfer original helper, and preflight its import before Wine starts in
  subsequent runs. Guest explicitly shut down cleanly before900second deadline;
  bootstrap-second launcher exited0, owned QEMU/Xvfb stopped.

No production patch was needed to obtain this accelerated pixel proof.
Recovery, installed shell/theme cells and efficiency remain separate gates;
the final addendum records the successful real consumer/lifecycle repeat.

## Final lifecycle repeat / continuation

`virgl-electron-lifecycle-complete`: host0, diagnostic_exit0, all seven lifecycle
checks PASS, all requested front captures acknowledged. Frozen-resize settled
samples at2/4seconds both have all three rim fractions1.0 and blue1600 pixels.
Actual resize-border-4.png independently inspected: caption/menu, text/input,
scrollbar, green background and blue square retained with complete frame.
No GPU crash/fatal/Skia/shared-context/WARP/output failures. Native Wine Wayland
driver and accelerated compositor/client contexts verified. This supersedes the
INVALID first Electron attempt, not the separate VirtualBox backend negatives.

Serial resource sample: active stable15.09s/18.27CPU-seconds (~121.08% one
core); phase labelled frozen-settled39.34s/4.31CPU-seconds (~10.96%), summed
peak RSS~2.50GB in that phase. Shared pages double-counted. Fixture resumes
animation after frozen-capture acknowledgement: this phase label is NOT an
idle workload guarantee or zero-copy/power/GPU-load measurement.

Evidence archive `virgl-consumer-evidence.tgz` on D: contains top-level results,
logs/PNGs only, not prefixes, executables or private runtime payloads. Actual
front images also retained separately at the VM root. Prefixes/private caches
remain in guest for diagnosis. Both completed consumer results record no
remaining owned Wine servers; final process inventory has no owned labwc,
wineserver or check-g4 probes. Guest explicitly gracefully shut down after
the serial repeat, not left consuming CPU. See consumer-repeat/results.json
for launcher confirmation: exit_before_shutdown0, qemu_stoppedtrue and
xvfb_stoppedtrue. No existing workers' machine was shut down.

Next scoped acceptance work: GPU-child fault/recovery on this accelerated
outer route (prior WSL client-only proof is not equivalent); actual installed
Classic/open Luna shell/taskbar locked/unlocked/toolbars/tray cells; relevant
sharing/copy-efficiency measurements and remaining release regressions. Do not
expand into irrelevant API completeness or claim full G4/merge readiness.
Use this guest instead of SVGA for accelerated consumer acceptance. To resume,
change private launcher's output to a fresh directory, keep D: backing image
intact, boot only this overlay and use new per-run isolated prefixes. Preserve
all negative evidence. No one-time reset credit consumed; usage last checked
89% consumed (11% remaining), above the user's5% checkpoint threshold.
