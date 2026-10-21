<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 additional WSL validation — 2026-10-21

Branch: `codex/g4-r1-completion`. Parent before this test-only step: `1464ecf8`.
Wine remains `1dcd479`; no production Wine changes, rebuild, VM/session change,
OOBE import, proprietary payload publication or master merge in this step.

**28 correctness groups PASS for both PEs on software GL and explicit lavapipe.**
This closes the scoped normal fence-creator-exit/re-export proof, not all R2/R3
or G4. The preceding event ownership/allocation fixes and real-consumer results
remain in `CHROMIUM_GRAPHICS_G4_R2_LIFETIME_20261021.md`.

## New independent lifetime test

`fence_creator_exit_and_reexport` uses a real separate creator process/device:

1. Child creates shared fence at value7, duplicates its export handle into the
   parent and publishes it through an inherited IPC mapping/event.
2. Parent imports, verifies7, registers an unsatisfied value9 event, and verifies
   it has not already fired. Child then releases its resources and exits0.
   Parent waits for process exit; it holds no reference to the creator's device.
3. After exit, parent duplicates/reopens the export, closes both original export
   handles, signals9 on the imported fence and requires the pending event plus
   exact completed value9.
4. Parent re-exports, releases all imported fence COM objects, reopens only the
   surviving export, closes it, registers11, checks no premature wake, signals11
   and requires wake/exact value11.

The handshake determines exit-before-reopen/signal; sleeps are not used to guess
ordering. Child waits are bounded; failure cleanup terminates only the owned
child, waits and closes IPC/duplicated handles. This is not a deterministic proof
of the internal observer-publication/rearm interleaving or multi-context GPU use.

Native Windows x64 and x86 controls both pass this exact normal-exit case,
without replacing DLLs, changing registry/security flags or installing runtimes:

```text
FENCE_CREATOR_EXIT abrupt=0 exit=0 initial=7 imported=9 reexported=11
fence_creator_exit_and_reexport=PASS
```

## Important negative control: forced creator termination is different

The initial hypothesis required continued signaling after both normal exit and
`TerminateProcess`. Wine passed it on both PEs/GL and both full-suite backends.
**Native Windows rejected forced-survival on BOTH PEs**, after normal exit passed:

```text
FENCE_CREATOR_FAILURE abrupt=1 stage=signal_after_exit exit=124 completed=18446744073709551615 removed=00000000 error=0
```

The shared fence reports UINT64_MAX, while the importing device's
GetDeviceRemovedReason remains S_OK. This does NOT demonstrate a whole-machine
GPU reset or that the importing device itself was removed. Microsoft's
[D3D11 fence contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11fence-getcompletedvalue)
points to its D3D12 equivalent; the
[D3D12 return-value contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue)
identifies UINT64_MAX as a device-removal sentinel. The observed forced-process
behavior is a native control on this host, not a universal driver guarantee.

Therefore the required normal-exit test does not demand the unsubstantiated
forced-survival contract. The original strict forced-survival assertions remain
available separately as `--fence-creator-abort-hypothesis`, outside the required
case list. They are a diagnostic hypothesis, NOT a portability/compatibility
certificate. The diagnostic returns0 for survival and1 for rejected survival;
do not convert a native1 to a pass or suppress the discrepancy.

**Crash/device-loss compatibility remains OPEN.** No production change was made
to force software Wine to poison fences. Real Chromium GPU-process crash/restart
and device recreation need a discriminating consumer test before deciding what
implementation is required. Prior animation/lifecycle tests do not prove that.

## Final evidence

Owned Debian source/build are unchanged from the previous report:
`/var/tmp/wex-g3-repair-20261018/clean-source`, combined WoW64 `build/wow64`.
Evidence root: `/var/tmp/wex-g4-tree-20261020/`.

| Evidence directory | x64 | i686 | Scope |
| --- | --- | --- | --- |
| r2-creator-final-28-gl | PASS/exit0 | PASS/exit0 | All28 groups, software GL, memory profile |
| r2-creator-final-28-vk | PASS/exit0 | PASS/exit0 | All28 groups, explicit lavapipe, memory profile |

These final runs use the normal-exit required case and the retained separate
forced-survival diagnostic. Earlier `r2-creator-exit-gl` and `r2-final-28-gl/vk`
used the initial combined hypothesis; their Wine PASS is historical evidence,
not native compatibility acceptance. No assertion in the existing27 groups was
weakened. Four Wine-free `test_graphics_harness.py` unit checks also PASS.

Final i686 committed address-space ends at108,195,840 bytes(GL) and111,693,824
bytes(lavapipe); the device-memory-reclaim bound remains enforced. No recurrence
of the old64MiB/device WoW64 Vulkan leak in these runs. Actual opaque-route
counters still show the accelerated-path operations on GL and CPU fallback on
Vulkan: software Vulkan correctness is NOT a GPU-efficient-sharing certificate.

Both final results.json files report successful server waits and empty remaining
server lists for both PEs. Independent WSL/Windows process checks find no owned
graphics test processes left running. Owned Xvfb displays were stopped; no
existing VM GUI, shell session, NAT, SSH, account or password was changed.

Native controls are free-source compiler outputs in ignored
`build/graphics-validation-20261021/final-x64.exe` and `final-x86.exe`, SHA256:

```text
x64 4768a3115676b91245e6576e5e92f1dcda1454c0df6bebe3ba943d5bc6222ba0
x86 db06f7a15c4d5467879a62d1681b4c97e0e45c982b39b64b4ac0a820225201a3
```

Reproduce with the preceding report's isolated command, fresh output directory,
`--g4-correctness --memory-profile`; optional
`--g4-case fence_creator_exit_and_reexport` isolates the new group. Vulkan adds
`--renderer vulkan --vulkan-icd /usr/share/vulkan/icd.d/lvp_icd.json`. Always use
`scripts/run-isolated-graphics.py` with fresh per-run filesystem X sockets, not
shared bind directories. No running build/probe was rebuilt underneath a test.

## WSL limits and next gates

`/dev/dri` is absent. `/mnt/wslg/runtime-dir/wayland-0` exists, but this is NOT
proof of the project's installed Wayland/wexcomp shell. Neither `weston` nor
`wayland-info` is on this Debian PATH. No owned nested Wayland compositor was
started and no WSLg user session was taken over. This turn's display evidence is
isolated Xvfb/software, not Wayland or physical older Intel/AMD/Nvidia evidence.

Still open: deterministic observer registration/rearm, allocation/callback/cancel
fault injection, broader COM/mapping/FD lifetime, multi-context GPU ordering,
efficient backend sharing/private DXVK interop, real consumer crash/restart,
installed Classic/Open Luna, X11 outer-compositor OFF/ON, Wayland/wexcomp and
physical older GPUs/resolutions/taskbar/toolbars/tray checks. The two previously
approved generic HWND deferrals remain DEFERRED, not PASS. Inherited upstream
DXGI67 failures and D3D11 legacy-sharing exit5 remain open, not rerun here.

No G4-complete, merge-ready or G5-handoff claim follows from these controls.

## Failed checks/commands retained

- `git status --short` without escalation failed because the sandbox could not
  enter `.git/modules/wexcomp` (Permission denied). Escalated
  `git status --short --ignore-submodules=all` and scoped Wine status worked;
  no Git state was repaired, reset or discarded.
- Native initial commands
  `./build/graphics-validation-20261021/g4-correctness-x86_64.exe --case fence_creator_exit_and_reexport`
  and the corresponding `g4-correctness-i686.exe` returned1. They included the
  unverified forced-survival hypothesis. Instrumented `diagnostic-x64.exe` and
  `diagnostic-x86.exe` repeated the failure and localized UINT64_MAX above.
  Final `final-x64.exe`/`final-x86.exe --case fence_creator_exit_and_reexport`
  both return0 for the now explicitly scoped normal-exit contract. The rejected
  forced-survival hypothesis is retained separately, not fixed/claimed PASS.
- `grep -n "wayland\|WAYLAND" /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64/config.h`
  addressed a nonexistent generated-header path. This diagnostic did not build
  or change anything; listing `dlls/winewayland.drv` confirmed compiled Wayland
  objects but does not certify runnable Wayland acceptance.
- `ls -l /dev/dri /mnt/wslg/runtime-dir/wayland-0` (and later `ls -ld`) returns1
  because `/dev/dri` is missing, while listing the actual Wayland socket.
  `command -v weston; command -v wayland-info` also ends1 because neither command
  is on PATH. These are explicit unavailable cells, not renderer failures.
