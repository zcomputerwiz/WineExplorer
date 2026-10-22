<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Post-recovery controls — x64 pass, matrix stopped for procedure review

Ran only `post-recovery-x86_64-20261022`, once. Both own-dcomp and
foreign-full-dcomp passed: producer exit 0, FRAME_SENT/PRODUCER_COMPLETE ok=1,
exactly **4096** matching front pixels each. Actual PNGs inspected. Inner EGL
and outer GLES2 verified VirGL/D3D12 Intel UHD 750; compositor `889/4321` remained
alive; no owned Wine servers remained. SSH/archive exits 0; no procedure stderr.
Only expected Wine OpenGL renderer diagnostics in case stderr; no fatal output
in case/native/compositor logs. FRAME_PATH reports CPU readbacks/compositions;
this is a presentation regression cell, not GPU-only-copy/efficiency proof.

## Procedure issue and unrun cells

Initial host budget expression parsed PowerShell's already-converted JSON date
as a string, losing its UTC meaning and inflating remaining time by five hours:
logged **18847.922 seconds**, not a valid budget check. Preserved original log.
Raw JSON timestamp parsing with explicit DateTimeOffset then verified **805.435
seconds** remaining at 19:40:25.234Z; at readiness/start it was ~847.922 seconds,
so actual available time exceeded the required 650. No timeout was extended and
the one consumer was not retried. Corrected log retained separately. Do not
claim the original incorrect host expression enforced the gate correctly.

Although graphics passed and actual budget was adequate, stopped the matrix for
this procedure issue per dispatch; i686 and strict WebView2 were **NOT RUN**.
Their preparation/archive/markers remain untouched and destination run dirs
absent. No further boot, cleanup, source changes, extra control or fault.
Parent review can decide continuation using raw UTC timestamp arithmetic before
any later launch; do not silently credit the two prepared cells as executed.

## Evidence, provenance and shutdown

Root `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/post-recovery-x86_64-20261022/`:
`foreign-evidence/` contains both PNGs, producer/Wine/native/compositor logs,
results and compiled EXE. All **14** archived hashes rechecked, zero mismatches.
Exact command and source/deployed/binary hashes in preflight/execution manifests;
all eight source, four host-helper, archive and 16 guest binary hashes matched.
Source archive unchanged from POST_RECOVERY_CONTROLS_PREP; launch HEAD `7d892a55`.
Compiled EXE SHA `0a64619703841d74d3cb12de0285100890d392e95e6a669bb09e7a5351c2ab2e`.
PNG SHA own `bc624fcf2f7b8a3e7b346fae295912936fc9d0cc97f53e66baea05ab0c19d0af`,
foreign-full `7eca39d5526a16ee7f0a7d2a2929eb8e2d3d0d237fcffec799c0ec81cfb97263`.
Profiles/runtime/prefix excluded from archive; guest state retained, no cleanup.

Usage before boot 27% five-hour/4% weekly; guest free 13,456,986,112 bytes.
Procedure UTC `19:39:42–19:40:45`. Separate persistent launcher Windows WSL
ran `19:38:50.669–19:41:16.969Z`, exit 0 (~146 seconds inside 900, >90 reserved).
Validated launcher `365/255`, QEMU `380/345`, Xvfb `367/270`. Exact-identity SIGINT
stopper exit 0, final QMP graceful true, QEMU exit 0 and both children stopped.
Explicit post-stop owned PIDs absent and SSH2235 closed; Windows final record/
logs copied to `host-procedure/`. Separate current status records matrix stop;
preparation marker remains history. No unchanged unit tests rerun.

Fresh reboot/prefix/process scope does not prove survival of faulted-process
resources or absence of pre-reboot global GPU corruption. Historical stale/
blanking uncertainty remains; no full G4/Edge/installed-shell/merge claim.
Only this report tracked. Return for parent review, with two cells unrun.
