<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Priority assignment: repair fractional-clock API probe admission

Antigravity/Gemini implements; parent reviews. Start from `10f129b9` or the
work-order-only descendant on origin/codex/g4-r1-completion, on a separate
`codex/chromium-api-probe-deadline` branch and your own D:-resident worktree.
Do not change the shader branch for this fix or edit the parent's checkout.

## Observed R4 outcome and concrete defect

The one R4 trial reached verified inner/outer VirGL, but stopped before API probe
or browser/HTTP execution with:

```text
TimeoutError('Need full45s API probe/reap/intermediate cleanup allowance')
```

Consumer elapsed2.179s, cleanup_complete=true. Host orchestration archived the
diagnostic exit1 normally; this is a failed startup diagnostic, not a browser
pass. Graceful QMP guest shutdown and QEMU/Xvfb/Windows controller exit0 were
observed, without fallback termination. Existing R4 stage/output/run are consumed;
preserve them. No retry, new mode/helper/preparation or VM run is in this task.

`scripts/g4_userenv_probe.py:phase` currently does:

```python
start = now()
end = min(deadline, start + 45)
if end - start < 45:
    raise TimeoutError(...)
```

Parent independently invoked the actual function with `now=lambda:31.1` and
deadline200. It raises that TimeoutError before accessing the output, despite
168.9s caller allowance: `(31.1 + 45) - 31.1` is44.99999999999999. The same
arithmetic occurs at40.1,45.1,50.1,60.1,61.1,63.1. This is a deterministic
reproduction of a bad admission check, not evidence of a slow or faulty API.

## Required change and proof

1. Repair admission without exact subtraction/reconstruction of the local
   floating-point duration. Prefer comparing the supplied absolute deadline
   directly against the computed absolute phase end. Keep the phase capped at
   45s and preserve run25/reap5/intermediate cleanup15 and original timeout errors.
   Do not add a broad seconds-long tolerance, expand time budgets, skip cleanup,
   change Wine APIs, relax prefix idleness or alter process ownership checks.
2. Meaningful tests with injected monotonic clocks must exercise the actual
   phase at several fractional start values, including31.1 and values crossing
   binary magnitude boundaries. With ample deadline it must reach and complete
   the existing synthetic API/cleanup path, not merely avoid this exception.
3. Exercise exactly45s caller allowance using a consistently constructed
   absolute deadline, a genuinely short44.99s allowance, and a deadline that
   becomes exhausted during the phase. Short allowance must reject before
   starting a child. Run timeout, reaping and intermediate cleanup retain their
   reservations and failure evidence. Do not use sleeps or a real Wine process
   for these deterministic clock cases.
4. Preserve current API decode/probe and genuine-owner negative cases. Run the
   focused91-test group listed in COMPOSITOR_SCOPE_R4_PREP_20261025 plus new
   tests in Debian WSL; report counts/skips/elapsed/exact command. Test source
   should be your branch. Private adapter groups may run the unchanged parent's
   source read-only separately; never report skipped private cases as passes.
   Use Python -B and existing tools; no package installs/builds needed.
5. Add `docs/CHROMIUM_API_PROBE_DEADLINE_FIX_20261025.md` with before/after
   reproduction, actual assertions, tests and limitations. Commit AND push your
   code/tests/report. Reply to codex with branch/commit/report/results and stop.

Changes should normally be confined to g4_userenv_probe.py and its focused
test module plus the report. Parent will review the fix, import accepted code
and arrange fresh fixed-output R5 preparation as a separate step. Do not
reprepare over R4, make a new PE, rename old paths, boot/SSH/mount a guest,
rebuild Wine, change the shared private adapter/launcher or integrate shaders.

This is the top-priority assignment ahead of shader second-round completeness.
You may work on that independent shader order after delivering this bounded fix.
Acknowledgement/progress/blocker/completion replies to codex are authorized.
