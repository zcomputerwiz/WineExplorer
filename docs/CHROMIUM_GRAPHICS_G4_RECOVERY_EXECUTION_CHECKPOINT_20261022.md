<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Accelerated recovery execution checkpoint

Plan execution was authorized by the user, but no recovery run or fault
injection has started. Branch `codex/g4-r1-completion`, current parent
`9dcd781db54175aab260fb7fa84c26028a481164`; root and wexcomp submodule
worktrees were clean on explicit read-only Git status. Recovery plan:
`CHROMIUM_GRAPHICS_G4_ACCELERATED_RECOVERY_PLAN.md`.

At this checkpoint the account's five-hour window was94% used (6% remaining);
one-time reset credit was left untouched. No long-running command was started.
Resume after the window resets. Preserve this exact checkpoint and plan.

## Machine and evidence state

- QEMU/VirGL test overlay `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/system.qcow2`
  and seed remain intact on D:. Prior latest run directory is
  `consumer-repeat`; its results report QEMU exit0 and both QEMU/Xvfb stopped.
- The new VirGL VM is not running. Existing VirtualBox G4 SVGA guest is powered
  off. Existing WineExplorer-VM and Qwen's machine/work are out of scope.
- D: has about1.9TB free. No new files were added to either guest this turn.
- Last completed tests (from prior turn): both-PE own/foreign DComp exact4096
  pixels, real WebView2 strict WebGL1/2, Electron lifecycle/frozen resize. They
  do not cover accelerated GPU-process recovery.
- The previous full Electron lifecycle attempt failed because its observer
  lacked `graphics_frame_pixels.py`; this cause is documented in the VirGL
  report. The helper was staged for the successful repeat and fail-before-launch
  preflight added/tested, but the staged VM helper was not revalidated against
  the exact current branch after the plan-only commit.

## Resume order

1. Read the recovery plan and this checkpoint. Recheck usage, root/submodule
   status, exact parent/branch, D: storage and that only the owned guest is off.
2. Review existing recovery fixture and parser tests. Implement the plan's
   scoped stage-specific front capture and stable main/renderer/compositor
   identity checks, plus fail-closed observer/capture handling and negative
   tests. Run unit tests, stage every dependency, then commit/push this bounded
   harness step before guest execution.
3. Update the private ignored launcher to use a fresh output directory for the
   healthy baseline. Boot once; confirm explicit VirGL renderer and ordinary
   no-fault actual front pixels/zero exits; preserve logs and power state.
4. Run two serial fresh-prefix one-fault GPU recovery trials with event-aligned
   pre/post front captures. Require all criteria in the plan. No retry or
   fallback waiver. If a failure needs code changes, save it first.
5. Run the listed post-fault 32/64-bit pixel controls and strict WebView2,
   archive only logs/results/PNGs to D:, inspect images, update status and
   continuation docs, commit/push. Leave guest cleanly powered off. No master
   merge or full-G4 claim from this task.

No automation has been scheduled by this checkpoint. If work reaches the
user's5% usage threshold, finish only the bounded active step, commit/push a
continuation note and schedule the reset continuation as previously requested.

## Live continuation update — 2026-10-21

The user authorized continued execution until usage limits out and asked to
leave the existing heartbeat enabled. Automation
`resume-accelerated-chromium-gpu-recovery` was verified in the app and remains
enabled for the next window. Last usage check: 95% of the five-hour window and
90% of the weekly window consumed; the one-time reset credit remains untouched.

At branch commit `4cb294cf`, root files were clean. A bounded harness fix is now
in progress: unexpected exceptions from the background observer are preserved
as `observer_error` and fail the GPU-recovery verdict; an observer that cannot
join within five seconds also fails the verdict. A Wine-free regression test
was added. AST parsing and `git diff --check` pass. The complete unit suite was
attempted on Windows: 12/13 pass; the remaining symlink test requires a
privilege Windows lacks. WSL execution was denied by the current environment.
No VM boot, recovery fault injection, guest modification, or reset-credit use
occurred. Re-run the full Linux suite when WSL is available, then continue with
the healthy accelerated control and the two serial single-fault VirGL trials
from the plan. Preserve the D: storage boundary and private runtime policy.
