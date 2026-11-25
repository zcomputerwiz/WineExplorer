<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Windows / Wine behaviour comparison

Black-box probes (`../*-probe.c`) print one line per observed condition. `run-compare.ps1` builds the probes listed in `probes.txt`
with the mingw cross compiler (WSL), runs each on the Windows host and in the owned lab guest under Wine, normalises both outputs
(`normalize.py`: addresses, handles, timings, per-probe extra patterns), compares them (`diff.py`) and writes `report.md`.

- `expected/NAME.windows.txt` is the golden output: the normalised Windows result. `-UseGolden` compares Wine with it without the host;
  `-UpdateGolden` rewrites it from the host run (only after looking at a change on Windows).
- Exit status 0: all agree; 1: at least one differs; 2: tooling failure.
- A probe belongs in `probes.txt` when its output is stable between runs on one system (no race on timing); the third column lists
  patterns to blank out (a timer id, a stale last-error value).
- `lab.sh` (scripts/lab/uwp) drives the lab guest; `wine-run.sh` runs a pushed probe under the candidate build in the existing prefix.
- To port a result into a Wine conformance test (`dlls/*/tests`, passes on both systems, `todo_wine` for the gap), start from the
  probe's printed lines.

Usage: `.\run-compare.ps1`, `.\run-compare.ps1 -Only order-probe,peek-probe`, `.\run-compare.ps1 -UseGolden`.
