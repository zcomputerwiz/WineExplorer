<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium resource-pressure check — result (WO-20261027-CHROMIUM-RESOURCE-PRESSURE)

Worker: claude. Branch: `codex/chromium-resource-pressure` (cut from token-integration
`7863e721`). Wine candidate under test: `codex/g4-token-integration` @ `fffa7de`
(self-reports `wine-11.19`), retained unchanged. Owned session used for measurement:
`chromium-resource-pressure-20261027` (fresh named identity; prior attempt folders left
untouched; guest shut down by owned graceful QMP powerdown, `qemu_exit_status=0`).

## Verdict (up front)

**Path C — memory is normal and ample; the memory hypothesis is ruled out.** Wine's
`GlobalMemoryStatusEx` reports ~3.4 GiB of available physical memory in a 4 GiB guest
(88% free, `dwMemoryLoad=12`). That is ~3.4× Chromium's moderate threshold, so its
`SystemMemoryPressureEvaluator` returns **NONE** — no pressure signal at all. Wine's
reported value is accurate and self-consistent with the guest's real `/proc/meminfo`.
No Wine memory-API bug was demonstrated, so **no Wine variable was changed**, no child
branch, and **no browser replay / RAM comparison was run**.

## 1. Source predicate actually being tested

Pinned Chromium rev `7d9824099030f5c28c939ba08654562faaad7c09`.

`components/memory_pressure/system_memory_pressure_evaluator_win.cc`:
- Polls every `kDefaultPeriod = base::Seconds(5)`.
- `GetSystemMemoryStatus()` calls `GlobalMemoryStatusEx(&mem_status)` and reads
  `ullAvailPhys` (`phys_free`) and `ullAvailPageFile` (`commit_available`).
- Default constructor sets **fixed** thresholds:
  `moderate_threshold_ = kPhysicalMemoryDefaultModerateThreshold = base::MiB(1000)`,
  `critical_threshold_ = kPhysicalMemoryDefaultCriticalThreshold = base::MiB(400)`.
- The commit-based path is gated by feature
  `kCommitAvailableMemoryPressureThresholds` = **`FEATURE_DISABLED_BY_DEFAULT`** (would
  use 200/500 MiB on `commit_available`; not taken).

`CalculateCurrentPressureLevel()` therefore reduces, on the default (shipped) path, to a
function of **`ullAvailPhys` only**:
1. Early exit: if `phys_free > 1000 MiB` **and** `commit_available > 2 GiB` → **NONE**.
   (`kEarlyExitCommitThreshold = base::GiB(2)`; this can only short-circuit to NONE.)
2. Otherwise (feature disabled): **CRITICAL iff `phys_free ≤ 400 MiB`**;
   **MODERATE iff `phys_free ≤ 1000 MiB`** (and > 400); else **NONE**.

So the decision under test is: *does Wine report `ullAvailPhys` low enough (≤1000 MiB,
ideally ≤400 MiB) to make Chromium believe the guest is under memory pressure?*

`chrome/browser/performance_manager/policies/page_discarding_helper.cc:158`:
```cpp
LOG(WARNING) << "Discarding multiple pages with target (kb): "
             << (reclaim_target ? reclaim_target->target.InKiB() : 0)
             << ", discard_protected_tabs: " << discard_protected_tabs;
```
This fires at the **start** of `DiscardMultiplePagesImpl`, **before** any candidate is
selected. `"target (kb): 0"` means `reclaim_target` was `std::nullopt` (no optional
reclaim target) — it does **not** mean zero available memory and does **not** prove a
tab was killed. Per the work order, that log was not used as evidence of a kill.

## 2. Wine `GlobalMemoryStatusEx` implementation reviewed (candidate `fffa7de`)

- `wine/dlls/kernelbase/memory.c:1400` — `GlobalMemoryStatusEx`, with a 1-second result
  cache; fields assembled from `SystemBasicInformation` (`MmNumberOfPhysicalPages`) and
  `SystemPerformanceInformation` (`AvailablePages`, `TotalCommitLimit`,
  `TotalCommittedPages`).
- `wine/dlls/ntdll/unix/system.c:2634` — on Linux the parser overwrites `freeram` with
  `/proc/meminfo` **MemAvailable**; lines ~2718–2720 set `AvailablePages`,
  `TotalCommittedPages`, `TotalCommitLimit`.
- `wine/dlls/ntdll/unix/virtual.c:3750–3785` — `MmNumberOfPhysicalPages =
  sysinfo().totalram × mem_unit / page_size − 1` (so `ullTotalPhys ≈ totalram`).

Net mapping: `ullAvailPhys ≈ MemAvailable`, `ullTotalPhys ≈ MemTotal`,
`ullTotalPageFile ≈ MemTotal + SwapTotal`, `ullAvailPageFile ≈ MemAvailable + SwapFree`.
Both totals and availables are guest-scoped and consistent — no truncation, scaling, or
unit bug evident from source. (This is exactly what the live measurement below confirms.)

## 3. Actual measured values (one owned session)

Read-only probe: original tiny PE `scripts/g4_memory_probe.cpp` (calls
`GlobalMemoryStatusEx` once, prints bounded fields + `GetLastError`; no allocation/IO
beyond one stdout write). Built x86_64 static (`memory-probe.exe`, a build artifact —
**not** committed). Run under the candidate build; `/proc/meminfo` captured immediately
before and after in the same session.

Probe output (`memory-probe.json`), candidate build `wine-11.19`:
```json
{"ok":true,"getlasterror":0,"dwMemoryLoad":12,
 "ullTotalPhys":4111765504,"ullAvailPhys":3594944512,
 "ullTotalPageFile":4111773696,"ullAvailPageFile":3594944512,
 "ullTotalVirtual":140737488224256,"ullAvailVirtual":140737481474048}
```

| Field | Bytes | ≈ MiB | Meaning |
|---|---:|---:|---|
| `ok` / `getlasterror` | — | — | success / 0 |
| `dwMemoryLoad` | — | 12 % | ~88 % free |
| `ullTotalPhys` | 4,111,765,504 | 3921.3 | guest RAM total |
| **`ullAvailPhys`** | **3,594,944,512** | **3428.4** | **available physical (the value Chromium reads)** |
| `ullTotalPageFile` | 4,111,773,696 | 3921.3 | ≈ total (SwapTotal = 0) |
| `ullAvailPageFile` | 3,594,944,512 | 3428.4 | ≈ avail phys (SwapFree = 0) |
| `ullTotalVirtual` | 140,737,488,224,256 | 128 TiB | x86_64 user VA |
| `ullAvailVirtual` | 140,737,481,474,048 | ≈128 TiB | ~all free |

Bracketing guest `/proc/meminfo` (same session):

| | before probe | after probe |
|---|---:|---:|
| `MemTotal` | 4,015,400 kB (3921.3 MiB) | 4,015,400 kB |
| `MemFree` | 3,738,884 kB | 232,376 kB |
| **`MemAvailable`** | **3,685,448 kB (3600.0 MiB)** | **3,536,396 kB (3453.5 MiB)** |
| `SwapTotal` / `SwapFree` | 0 / 0 | 0 / 0 |
| `CommitLimit` | 2,007,700 kB | 2,007,700 kB |
| `Committed_AS` | 63,036 kB | 577,464 kB |

Cross-checks (all consistent):
- `ullTotalPhys` == `MemTotal` exactly (4,015,400 kB).
- Wine `ullAvailPhys` (3428 MiB) sits between the before/after `MemAvailable`
  (3600 → 3453 MiB); the ~25 MiB gap is the probe's own wine process at call time.
- `dwMemoryLoad=12` matches `(ullTotalPhys − ullAvailPhys)/ullTotalPhys ≈ 12.6 %`.
- No swap in guest, so pagefile fields mirror physical fields (as Wine computes them).

## 4. Decision against the predicate

`ullAvailPhys = 3428 MiB` is far above `moderate_threshold_ = 1000 MiB`:
- Early-exit condition holds (`3428 > 1000` and `commit_available=3428 < 2 GiB`? — the
  early exit also requires `commit_available > 2 GiB`, which is **false** here, so it
  does not short-circuit; but that only matters as an optimization).
- Falling through to the default path: `3428 MiB > 1000 MiB` → **NONE**.

Result: **Chromium's evaluator reports no memory pressure.** There is neither a false
pressure signal (Wine is not reporting artificially low free RAM) nor real exhaustion
(the guest genuinely has ~3.4 GiB free). The measurement was taken at rest in the same
4 GiB configuration where Chromium previously ran — so the observed failure cannot be
attributed to system memory pressure.

## 5. Exact changed variable

**None.** No Wine source, flag, or build input was modified. Path C (memory normal) was
taken, so no OWN Wine child branch of `fffa7de` was created and no rebuild was performed.
The only new file committed is the read-only probe source (`scripts/g4_memory_probe.cpp`)
and this report.

## 6. Actual page outcome

**No browser replay was run.** Per the work order ("If memory values/pressure are
normal, report that finding and avoid an identical browser replay"), and because the
memory hypothesis is now measured-false, a fresh Chromium page control would be an
identical replay with no new information. The retained prior evidence stands: final
consumer result was `Aw, Snap` / `RESULT_CODE_KILLED`; native held the browser to a 220 s
timeout (45000 ms); HTTP empty; cleanup complete; graceful QMP powerdown recorded.

Timing note from that retained log: the **network service process died ~0.9 s after
launch** (chromium.log ~line 132) and the fixture tab was killed ~12 s later (~line 364,
`sad_tab.cc`). A crash at ~0.9 s is far too early for the guest to exhaust its ~3.4 GiB
of free memory — independently corroborating that resource pressure is not the cause.

## 7. Remaining blocker and next specific candidate

**Blocker:** the page-load failure (`Aw, Snap` / `RESULT_CODE_KILLED`, empty HTTP)
persists independent of system memory. The concrete retained signal is a **network
service process crash ~0.9 s after spawn**, before any tab-level activity.

**Next specific IPC/network candidate (from retained evidence, no memory fix implied):**
the network service's own startup/crash under the candidate Wine build. Concretely, next:
1. Capture the network service's crash signature — signal + faulting module/function —
   from the native journal and any Crashpad minidump it wrote, to classify it as a
   Wine process-spawn/sandbox defect vs an in-process fault.
2. If the fault is at spawn/IPC setup, inspect the browser→network-service **Mojo IPC
   channel establishment** (endpoint creation / pipe handoff) and the network service's
   sandboxed launch under this build — i.e., whether the second process fails to come up
   or to attach its Mojo pipes, rather than being starved of memory.

This keeps the investigation on the actual retained failure (early network-service
death) instead of a resource story that the measurement has now excluded.

## Evidence location (private, D:)

Owned run dir `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-resource-pressure-20261027\`:
`memory-probe.json`, `meminfo-before.txt`, `meminfo-after.txt`, `measure-summary.json`
(scp/deploy/warmup/probe all exit 0; deploy check confirmed the candidate build is
properly deployed), and `results.json` (`graceful_powerdown=true`, `qemu_exit_status=0`).
These are private lab artifacts and are intentionally **not** committed to this repo.
