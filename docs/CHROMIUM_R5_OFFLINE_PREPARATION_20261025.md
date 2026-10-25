<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium R5 offline preparation — PREPARED_NOT_RUN

Work order `msg_1792949396725_8b2995be` (Priority 3). Offline only: no VM boot,
SSH, mounts, browser/Wine/probe execution, disk creation/changes, runtime
extraction/download or package install. Verdict: **PREPARED_NOT_RUN**.

## Source base and changes

- Worktree: `D:/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer` (branch `codex/chromium-r5-preparation`)
- Base commit: `684c4ce5540dde3ab9e0036b8369642bc729f48d` (descendant of accepted timer fix `832da342`)
- Preparation commit: `cd1b90aa4fd878fc11e6a07a8d11af98b360f886` — the exact source base recorded in `preflight.json.source_commit`

Changes (4 files, +111/−8; full diff sealed as `.change/tracked-source.diff`):

1. `scripts/check-g4-chromium-native-exit.py` — explicit `userenv-r5` fixed mode
   and mutually-exclusive `--userenv-r5` CLI following R4 exactly: fixed host
   name `chromium-userenv-r5-20261025`, stage `…-stage`, output = host name;
   compositor child-only `WINEPREFIX` stripping (`strip_wineprefix=True`) and
   API-probe/intermediate-cleanup phase opt-in. Every legacy mode and the
   original eight browser arguments preserved; no new compatibility workaround,
   feature/security flags or GL behavior.
2. `scripts/g4_userenv_view.py` — `userenv-r5` fixed module-view mode with the
   same fixed names and `--userenv-r5` passthrough flag.
3. `scripts/test_g4_userenv_view.py` — fixed-mode/path/CLI tests for r3/r4/r5,
   R5 CLI mutual exclusion, and R5 failed-API-before-browser plus successful
   sequencing with the `strip_wineprefix=True` compositor assertion.
4. `scripts/test_g4_chromium_userenv_r5_adapter.py` (new) — focused R5 adapter
   tests: fixed paths/mode/compiled contract, transitive source inventory, and
   fully mocked `run` orchestration (outside-module observation before and
   after the consumer). Standalone class on purpose: the base `AdapterTests`
   skip-guard would inherit and silently skip in this worktree.

## Compiled parent PE (OUTPUT macro only)

- Build dir: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/userenv-browser-r5-20261025.build`
- Recipe: original R4 `build.py` reused read-only; diff is the repo path and the
  output name only. Original source/header copied; six-file source inventory
  hashed into `build-results.json`.
- Exact override (`output-only.h`, sha256 `59321b4c…e80f1`):
  `#define WEX_EXIT_OUTPUT L"Z:\home\debian\wex-g4\chromium-userenv-r5-20261025"`
- Command: `x86_64-w64-mingw32-g++ (GCC) 14-win32` (sha256 `fb69bd14…d895b`)
  `-std=c++11 -O2 -static -Wall -Wextra -Werror -municode -include output-only.h source/g4_native_exit.cpp -o native-exit.exe`
- Result: exit 0, empty stderr (strict warnings pass), 306537 bytes, machine
  `0x8664` (AMD64), PE sha256 `0788d2f1c2a200b8d192cadc9ec9e217ecf57347f1c45d8d810e63ea860ad985`.
- Original R1 image contract untouched: `native-exit-helper-r1-20261023.build/native-exit.exe`
  sha256 `366c6928…b8d60` before == after. PE and Wine never executed; SID/Wine not rebuilt.

## Fixed paths (recorded in preflight)

| Role | Path |
| --- | --- |
| Host output/profile name | `chromium-userenv-r5-20261025` |
| Guest stage | `/home/debian/wex-g4/chromium-userenv-r5-20261025-stage` |
| Compiled output (guest) | `/home/debian/wex-g4/chromium-userenv-r5-20261025` |
| Reused runtime (R1) | `/home/debian/wex-g4/chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win` |
| Shared prefix | `/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix` |
| Disk (same existing) | `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/system-native-identity-r4-20261024.qcow2` |
| New parent PE | `…/userenv-browser-r5-20261025.build/native-exit.exe` |
| Reused API probe | `…/userenv-browser-r3-20261024.build/userenv-probe.exe` (sha256 `fb0dee8d…eda3`) |
| Accepted SID modules | `…/userenv-sid-r1-20261024.build/{x86_64,i386}-windows/userenv.dll` |

## Private preparation paths (own worktree / D: lab, ignored from Git)

- Adapter + helpers: `<worktree>/build/g4-virgl-20261022/` — `chromium-userenv-r5-trial.py`
  (sha256 `04160dd3…00372`), `launch.py`, `presentation-timing-trial.py`,
  `meta-data`, `user-data`. Helper REPO paths point at this D: worktree; no
  stale C:/D: main-checkout aliases. Disk selection stays the same existing
  r4-named qcow2 (name is not a reason to create another disk).
- Fresh `.preflight`: `…/chromium-userenv-r5-20261025.preflight/` — `preflight.json`
  (sha256 `57f47ef3b63e0423da6d90485ddd25fd9966f92c301804114f9e60ff0cb09347`),
  `source-stage.tar` (sha256 `b370ecd3…652bf`, 21 members, current-content
  verified twice: at packaging and by `validate_transfer`), `PREPARED_NOT_RUN`.
  First draft preflight (at base `684c4ce5`, sha256 `3766f730…b827`) was
  regenerated after the preparation commit so `source_commit` pins it; both
  were fresh R5 artifacts, nothing consumed them.
- Fresh `.change`: `…/chromium-userenv-r5-20261025.change/` — `audit.py`,
  `audit.json` (55 verified files), `seal.json` (32 sealed entries covering
  `.change`, preflight and build artifacts), `r4-reference.py`, `r4-to-r5.diff`,
  `tracked-source.diff` (210 lines), copied source/test files, and the
  R4/R3 input before/after snapshots.

## Launch / run / stop (for later live dispatch only — NOT executed)

```
pwsh -NoProfile -File D:\DeepThought\Worktrees\chromium-r5-preparation\WineExplorer\scripts\g4-virgl-launcher-session.ps1 -RunName chromium-userenv-r5-20261025
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/build/g4-virgl-20261022/chromium-userenv-r5-trial.py run
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-userenv-r5-20261025/launcher-identity.json --timeout 80
```

Guest consumer (fixed): `sudo -n /usr/bin/python3 -B <stage>/scripts/run-isolated-graphics.py --socket-root /home/debian/wex-g4/isolated-sockets --private-loopback -- python3 -B <stage>/scripts/g4_userenv_view.py --userenv-r5`

Bounds unchanged: 900/90/80 outer, 674/650/435 raw UTC, 285/55 consumer,
82 observer, 300 SSH / 45 archive, 45 probe / 25 run / 5 reap / 15 cleanup.
Guard ordering preserved: guest readiness → free space before staging/profile →
prefix idle → guest paths absent → transfer integrity → explicit
backend/private network/socket/readonly-view checks → API/intermediate cleanup
→ browser observer. Existing runtime reused, not reinstalled. Durable native
credential used read-only (public key matches seed; never printed/rotated).

## Tests (WSL Debian, python 3.13)

Command: `python3 -m unittest -v test_g4_chromium_userenv_r5_adapter test_g4_userenv_view test_g4_chromium_native_exit test_g4_native_exit test_g4_compositor_scope test_g4_chromium_native_adapter test_g4_chromium_userenv_adapter test_g4_chromium_userenv_r4_adapter` (from `scripts/`)

- Result: **Ran 43 tests — OK, 32 passed, 11 skipped, 0 failures** (~10 s).
- New R5 coverage: fixed mode/path/CLI/constructor, failed-API-before-browser,
  successful sequencing with `strip_wineprefix`, compiled-path contract (6
  files), mocked adapter/transfer orchestration.
- Unchanged fractional-clock regressions from `832da342` re-run and passing
  (`test_g4_userenv_view.py`: exact 45 s admission, 44.99 s rejection,
  full-phase fractional start/admit/complete, deadline-exhaustion kill/reap/cleanup).
- Compile tests compiled the original small C harness only; no Wine or Windows
  binary executed.

Actual skips, reported separately (private fixtures not present in this
worktree by design — not populated with VM/runtime copies):

- `test_g4_chromium_native_adapter`: 3 skipped — "private source/storage unavailable"
- `test_g4_chromium_userenv_adapter` (R3): 4 skipped — "private R3 adapter unavailable"
- `test_g4_chromium_userenv_r4_adapter`: 4 skipped — "private R4 adapter unavailable"

## Preservation evidence

- R4/R3 inputs unchanged by preparation: 22-entry before/after snapshot in
  `.change/r4-r3-inputs-{before,after}.json` — `changed_entries: 0` (disk
  recorded by size/mtime only; no whole-disk rehash).
- Host RUN dir `…/chromium-userenv-r5-20261025` absent; launch/stop never exercised.
- Parent C: checkout, shader/timer branches, existing private helpers and
  consumed runs untouched; R4 adapter/preflight/build read as reference only.

## Remaining live checks (future observations, not offline passes)

Guest path absence, guest free space, actual VirGL behavior, runtime reuse in
the guest, and the browser/API outcome itself. Parent reviews all of this
before any live run.
