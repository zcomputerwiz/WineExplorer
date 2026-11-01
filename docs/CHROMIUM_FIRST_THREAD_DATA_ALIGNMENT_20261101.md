<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium stalls on the new Wine base: first thread data alignment

Date: 2026-11-01. Status: cause found and repaired. The repair is upstream Wine `3de138d3d3`, taken as `2ebf5f72` on
the Wine branch `copilot/first-thread-data-alignment` (on top of `ccebeff5`, the token broker reconciliation tip). This
document covers the stall only. Two more failures sat behind it and are covered separately:
[CHROMIUM_MISSING_WINE_FONTS_20261101.md](CHROMIUM_MISSING_WINE_FONTS_20261101.md) (the R6 runtime package lacks Wine's
TrueType fonts) and [CHROMIUM_APPCONTAINER_LABEL_20261101.md](CHROMIUM_APPCONTAINER_LABEL_20261101.md) (a stock Wine gap that
broke the network service's sandbox profile on a fresh prefix). None of the three is rendering, security or token
acceptance of Chromium.

## Result

- Chromium's browser process worked on the R5 runtime (Wine base `eba89375`, 2026-10-02) and stalled on the R6 runtime
  (base `66caeb46`, 2026-10-28): the window never appeared and the fixture server saw no request (runs b, cm and cm2 below).
- The token security work and the cleanup are not the cause. Clean upstream `66caeb46`, without a single WineExplorer
  commit, stalls at the same point, and the unchanged R5 runtime still works when replayed on the same guest.
- The cause is upstream `08889af3` "ntdll: Allocate the initial thread data with mmap." (2026-10-28, inside the new base).
  It made the first thread's data block only page aligned, but the signal handlers need it aligned to 64 KB. Whether the
  block lands aligned depends on what the C library mapped earlier (ASLR, locale files), so the stall is intermittent.
- Effect: the main thread's suspend signal handler reads a wrong `thread_data`, never delivers its context, and
  Chromium's `StackSamplingProfiler` thread waits for that context in `GetThreadContext` forever.
- Upstream fixed it the next day (`3de138d3d3`, 2026-10-29), after our base was cut. Taking that commit repairs it.

## Timeline

| Run (local lab evidence) | Wine | Result |
|---|---|---|
| `chromium-browser-verify-r5-20261029` and its replay `.replay-A-20261101` | `eba89375` base, token series | works: `index.html` and `scene.js` served (HTTP 200), `chromium.log`, netlog and trace written |
| `chromium-prep-b-20261101` | `c92bf4b4` (token series on the new base) | stall: no fixture request, no `chromium.log` |
| `chromium-prep-cm-20261101`, `chromium-prep-cm2-20261101` | `66caeb46` clean upstream | stall, same signature; `cm2` was captured live (below) |
| `chromium-prep-tfix1-20261101` | `2ebf5f72` on the R6 runtime | the sampler stall is gone, but the browser's main thread dies about 2 s after the start and the fixture server sees no request: the next failure ([fonts](CHROMIUM_MISSING_WINE_FONTS_20261101.md)) |
| `chromium-prep-tdiag1-20261101` | `2ebf5f72`, `1cc71ccd`, `67317644` plus a trace-only diagnostic patch in ntdll (not kept) | identified that failure: a stack overflow in the main thread |
| `chromium-prep-tfont1-20261101` | `2ebf5f72`, `1cc71ccd`, `67317644` and the six Wine TrueType fonts added to the R6 runtime | works: fixture page and script served, network service sandboxed (below) |

Searching the 416 commits between the two bases for changes to how the thread data block is allocated finds `08889af3`.

## Mechanism

`get_current_thread_data()` returns `rsp & ~signal_stack_mask` (`signal_stack_mask` is 0xffff, `unix_private.h`;
`signal_x86_64.c:541`, `signal_i386.c:567`) and every signal handler starts with `init_handler()`, so the block must start
on a 64 KB boundary. The first thread's block is `signal_stack_mask + 1 + kernel_stack_size` bytes
(`virtual_alloc_first_thread_data()`): `struct thread_data` and the signal stack fill the first 64 KB, the kernel stack follows
(`unix_private.h:464`).

With a block that starts at a page boundary only, the mask lands inside the block instead of on `struct thread_data`.
`data->teb` is then NULL, and `usr1_handler()` (`signal_x86_64.c:2560`, `signal_i386.c:2198`) takes the branch that waits in
`server_select( NULL, ... )` without a context: SIGUSR1 is the suspend request, so the server never gets the context that
`NtGetContextThread()` of the suspending thread waits for. Only the signal handlers are affected;
`get_thread_data()` uses `pthread_getspecific()`, and threads created later get blocks from `map_view()`, which aligns.

Why intermittent: the start address is whatever the kernel hands out after the mappings the C library made before
`start_main_thread()`. With `LANG=C.UTF-8` nine 4 KB `/usr/lib/locale/C.utf8/LC_*` files are mapped first and shift the
block by a page multiple, so 17 % of the starts were misaligned locally (below); `en_US.UTF-8` happened to be aligned in
all 300 starts. The VM guest differs again.

## Evidence from a stalled browser (`chromium-prep-cm2-20261101`)

A watcher inside the guest sampled the browser (pid 1221) at +6 s and +9 s of its life, found it stalled, and captured it:

- Main thread (tid 1221, `chrome.exe`) and the sampler thread (tid 1232, comm `StackSamplingPr`) both sleep in `pipe_read`,
  unchanged at +9 s and +30 s; the browser process exits at +45.9 s.
- The main thread's `thread_data` pointer on its Unix stack is `0x7f309dbe7000`, residue 0x7000: not 64 KB aligned. Its stack
  pointer is `0x7f309dbf5348`, 0xe348 into the block, so the mask gives `0x7f309dbf0000`, 0x9000 into the block: unused
  signal stack, `teb` NULL. The sampler's `thread_data` (a later thread, allocated through `map_view()`) is `0x100030000`,
  aligned.

Chromium side (checked in the source): `components/sampling_profiler/thread_profiler.cc`,
`ThreadProfiler::CreateAndStartOnMainThread()` starts a profiler for the main thread immediately; its
`StackSamplingProfiler` thread (named "StackSamplingProfiler") calls `::SuspendThread()` on it and then `::GetThreadContext()`
while it is suspended (`base/profiler/suspendable_thread_delegate_win.cc`, `ScopedSuspendThread`). That is exactly the
suspend/context hand-off that the misaligned block breaks, so the stall happens whenever the dice fall wrong.

## Fix

Upstream `3de138d3d3` replaces `unmap_extra_space()` with `map_anon_area( size, unix_prot, align_mask )` in
`dlls/ntdll/unix/virtual.c`, uses it in `map_view()` and makes `virtual_alloc_first_thread_data()` call
`map_anon_area( size, PROT_READ | PROT_WRITE, granularity_mask )`, so the first block starts on the 64 KB boundary like every
later one. Recorded in `STOCK_CHANGES.md` ("Backported from upstream Wine"). An audit of the other allocation sites found
none that can start unaligned: `init_thread_data()` has two callers, the first thread's block above and
`virtual_alloc_thread_data()`, which goes through `map_view()` with its default alignment.

## Validation

All Wine builds are WoW64 (`--enable-archs=i386,x86_64`); the 32-bit checks run the 32-bit program in the same build.

Alignment of the first thread's block (`scripts/check-first-thread-alignment.py`, `WINEDEBUG=+virtual`, `LANG=C.UTF-8`):

| Build | Starts | Aligned | Misaligned |
|---|---|---|---|
| `66caeb46` clean upstream | 1000 | 834 | 166 (all residue 0x6000) |
| `2ebf5f72` | 1000 | 1000 | 0 |

With the default `en_US.UTF-8`: 300 of 300 aligned on both builds.

Suspend stress (`scripts/suspend-context-probe.c` flavor, 1000 suspend/context rounds per run, 60 runs per cell, hung when
not finished in 25 s):

| Build | 64-bit probe: finished / hung | 32-bit probe: finished / hung |
|---|---|---|
| `66caeb46` clean upstream | 50 / 10 | 43 / 17 |
| `2ebf5f72` | 60 / 0 | 60 / 0 |

Sandboxed Chromium in the lab VM (R6 harness, sandbox on): this fix alone is not enough. `chromium-prep-tfix1-20261101`
(the fix on the R6 runtime) no longer stalls in the sampler, but the browser's main thread dies about two seconds after the
start and the fixture server sees no request. Two more failures were behind the stall (the two documents linked in the
header). With every fix and the six Wine fonts (`chromium-prep-tfont1-20261101`) the browser loads the fixture page:
`index.html` and `scene.js` are served with 200, the network service runs with `--service-sandbox-type=network`, and no
stack overflow, `c00000fd` or "crashed or was terminated" line is logged. That run is diagnostic evidence only (the harness
reports `passed=false` for it, as it also does for the good R5 replay), not rendering or security acceptance.

## Reproduce

```
x86_64-w64-mingw32-gcc -O2 -Wall -o suspend-context-probe64.exe scripts/suspend-context-probe.c
i686-w64-mingw32-gcc   -O2 -Wall -o suspend-context-probe32.exe scripts/suspend-context-probe.c
python3 scripts/check-first-thread-alignment.py --wine BUILD/wine --prefix PREFIX --runs 300 \
    --probe suspend-context-probe64.exe --probe suspend-context-probe32.exe
```

`PREFIX` must be a working WoW64 prefix. Run it after every Wine rebase (HANDOFF.md). On the unfixed build it prints the
misaligned runs and `probe ...: stuck after 0 iterations` for the runs where the suspend request is never answered, and
exits 1.

## Open items

- Rebase or merge the Wine branch into the line you ship; the fix is a clean cherry-pick of an upstream commit and drops out
  by itself once the base contains `3de138d3d3`.
- The earlier R6 attempts (token wait, exception and stack-overflow traces) were taken on builds that could hit this bug and,
  being R6 packages, without Wine's TrueType fonts. They were not re-read against these findings. Any failure that remains
  with all fixes needs a new look from a clean baseline.
- Before a new Chromium run, check the package with `scripts/check-runtime-fonts.py` (see the fonts document); the
  alignment check above guards this fix.
