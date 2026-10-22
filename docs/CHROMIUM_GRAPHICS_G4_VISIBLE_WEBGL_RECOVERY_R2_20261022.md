<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Visible recovery r2 — independent bounded repeat

Exactly one fresh-prefix/profile/output trial in a separate owned boot passed.
All recovery/capture gates and all eight scene/canvas pixel verdicts true; both
canvases **1024/1024** in every image, frame rim valid. Inspected actual pre-fault
front and settled_4s front/page: magenta/cyan before, yellow/blue after, full frame
and scene intact. All eight PNG hashes equal their r1 counterparts (see
`CHROMIUM_GRAPHICS_G4_VISIBLE_WEBGL_RECOVERY_R1_20261022.md`), including identical
three recovered fronts/pages. This is a second scoped pass, NOT a historical
blanking fix, root-cause conclusion, post-fault matrix or full G4 acceptance.

Evidence root:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-recovery-r2-20261022/`.
`recovery-evidence/` retains eight PNGs, full decoded CSS/canvas/rim results,
host/Chromium/Wine/compositor/capture/native-window/process logs and all ACKs.
Rechecked all **39** archived file hashes: zero mismatches. Profiles/prefixes/
runtime directories excluded from archive; retained guest state was not cleaned.

Sibling `.preflight/` preserves fresh preboot archive/manifests/helper copies and
historical preparation marker. Separate live status now records completed trial
and verified shutdown; root `execution.json` retains exact argv and actual exits.
No old records modified. Launch HEAD `a09a2ee9`, diagnostic unchanged `eecf8bb3`;
all 12 source, four host-helper and archive hashes matched before boot; all 12
deployed source and 17 guest binary hashes matched before consumer launch.
Archive SHA remains `dda447bee4bfd670e7e2e0163262811e810b15a2f7d8022e720b70bdb11501e9`.
Native EXE SHA: `4e97f163e6d6a4fb8e4deb66108ebe79834a59e006947e251b53b048adf87860`.

Same prepared procedure/flags as r1, unique env run/boot names both
`visible-webgl-recovery-r2-20261022`, `prepare` before boot and `run` once.
Exact commands remain in preflight/execution manifests. Consumer flags unchanged:
`--gpu-recovery --visible-webgl --seconds 45 --renderer gl --gl-driver virgl
--compositor-renderer gles2`. No extra control, trace, retries, diagnostic edits
or unchanged test reruns. Shared disk/cache/build/runtime context remains;
separate boot is not a claim of cold-cache independence.

Nonce `8e459ea5085da048eb43cc973517b33e`; compositor PID/start ticks `892/4728`.
Healthy pre-fault page ACK 6746 ms preceded sole owned GPU fault 6747 ms.
GPU `528/1792697265378.176` → `916/1792697272410.2`; browser
`420/1792697264630.026` and renderer `640/1792697265658.299` unchanged.
Both APIs recorded loss events and valid fresh contexts/readbacks; frames and
visible transform resumed. One expected killed GPU exit, no renderer exit;
analyzer GPU-crash count 1, every other classified error 0. Inner EGL and outer
GLES2 verified VirGL/D3D12 Intel UHD 750; native Wayland true. SSH/host/diagnostic/
archive exits all 0, no procedure error. Full identity/backend gates passed.

Preboot usage 10% five-hour/2% weekly; guest free **15,393,673,216 bytes** was
sufficient, no cleanup. Procedure UTC `19:26:59–19:28:33`. Windows WSL PID 40428
ran `19:26:07.281–19:28:51.699Z`, exit 0 (~164 seconds inside 900, >90 reserved).
Validated launcher `357/406`, QEMU `372/452`, Xvfb `359/417`. Separate exact-identity
SIGINT stopper exited 0: signal/graceful QMP true, QEMU exit 0, children stopped.
No Wine servers remained; post-stop `/proc` checks absent and SSH2235 closed.
Windows final record/logs copied under `host-procedure/`. No Ctrl-C, no unrelated
VM changes. Only this report tracked. Return for parent review before separately
authorized post-fault controls; none attempted here.
