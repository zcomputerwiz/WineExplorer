<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Rework result: Chromium navigation handoff — pinned primitive, project Wine

Work order WO-20261028-CHROMIUM-HANDOFF-REWORK, continuing root commit c4f62c73 on branch
`codex/chromium-navigation-handoff`. This report responds to the parent review
`docs/CHROMIUM_HANDOFF_REVIEW_REWORK_20261028.md` (commit b215660c) and its four findings /
four assigned tasks. No VM/browser run was performed for this rework round; all evidence below
is from the retained native/Wine launch log plus two freshly built PEs run on native Windows 10
and the actual project candidate Wine.

## Verdict (up front)

- **Correction applied:** the repro now exercises Chromium's *actual* pinned platform-channel
  primitive — a duplex overlapped named pipe connected in-process, passed to the child through
  `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, with real IOCP overlapped I/O, bounded
  waits, owned cleanup, and all failures nonzero. No AppContainer / security-capabilities path is
  forced, because the retained launch log shows the network service starts with
  `--service-sandbox-type=none` (not a restricted token).
- **Definitive comparison:** run the *same* PE on native Windows 10 (10.0.26300) and the actual
  project Wine (g4 fork, wine-11.19). For both the single-pipe-handle primitive and a 3-handle
  mixed-type list (pipe + event + semaphore), behavior is **identical** on bytes, status codes,
  overlapped completion ordering, and IOCP key association. The only platform difference is
  latency (Wine ~15–30× slower per op; child startup adds ~9–12 ms to the parent's ACK read).
- **Task 3 not triggered:** no semantic divergence was proven in handle inheritance, named-pipe
  overlapped completion, or IOCP port association for the tested surface. The earlier "multi-handle
  NOT-USABLE on Wine / native err=87" result was a *repro bug* (event/sem never marked inheritable;
  `bInheritHandles` FALSE), not a Wine or Windows limitation. No Wine child branch is cut.
- **Remaining gap (browser level):** the page still goes blank at 8 GiB on Wine while the native
  comparator commits the target document with net_error 0. That failure is *not* in the pinned
  primitive tested here; its exact failing operation remains open (see "Remaining uncertainty").

## Runtime identity

| Role | Identity |
|---|---|
| Native comparator | Windows 10 Pro, build 10.0.26300.0 (reported by `[Environment]::OSVersion`) |
| Project candidate Wine | wine-11.19 (g4 fork), loader at `/var/tmp/wex-g4-token-parent-20261027/build/loader/wine` + `build/server/wineserver`; source marker `server/main.c` present; reused **read-only** in a fresh private prefix |
| Private Wine prefix | `WINEPREFIX=/home/debian/navhandoff/wineprefix-project` (fresh, isolated; `DISPLAY=""`, `WINEDLLOVERRIDES="mscoree,mshtml="`) |
| PE build | `x86_64-w64-mingw32-gcc -O2 -static` (GCC 14-win32) → PE32+ x86-64 console; one identical binary per variant run on both platforms |

## Task 1 — Trace and launch evidence (addresses review Finding 1)

Source: retained `helper.stderr.log` (`WINEDEBUG=trace:process`, chrome.exe under the project
Wine), phase dir `chromium-navigation-phase-20261028/chromium-native-evidence/`. Decoded IDs/times:

- Browser launch (line 227): `chrome.exe --app=http://127.0.0.1:36631/<nonce>/index.html#nonce=<nonce>`,
  started pid 0x0d8 (216).
- **network.mojom.NetworkService** launch (lines 845–862):
  - line 847 `UpdateProcThreadAttribute(... 00020002 ... 56 ...)` — attribute **0x00020002**
    (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), payload **56 bytes = 7 handles × sizeof(HANDLE)**.
  - line 848 cmdline: `--type=utility --utility-sub-type=network.mojom.NetworkService ... --service-sandbox-type=none`.
  - line 852 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST handle count 7.`
  - lines 861–862 child **pid 0x29c (668)**, tid 0x2a0 (672).
- Other children, per-type HANDLE_LIST counts: crashpad-handler 6 (pid 0x0e0), conhost 1 (pid 0x130),
  **gpu-process 7** (pid 0x1fc), **storage.mojom.StorageService 8** with `--service-sandbox-type=service`
  (pid 0x21c).

Conclusions forced by this evidence:
1. The observed network service is launched with **`--service-sandbox-type=none`**, not an
   AppContainer token. A missing tracing slice for a PID is *not* evidence of process death, pipe
   disconnect, or its cause — so no such claim is made here.
2. The attribute value Wine logs as `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` is **0x00020002**; the repro
   uses that exact constant and the child demonstrably receives the inherited handle on native
   Windows 10, so the constant is validated for this platform pair.
3. Observation (not attributed as a cause): gpu-process and storage-service launches log
   `fixme:process:CreateProcessInternalW Unsupported attribute 0x2000e`; the network service launch
   does not set that attribute. Recorded as an observation only, per the review's caution against
   inferring causation from a single signal.

## Task 2 — Chromium-matching PE repro on native + project Wine (addresses Findings 2, 3)

Two PEs, both built once and run unchanged on both platforms:

- `navhandoff_mojo_np.c` → `mojo_np.exe` — the **pinned primitive**: parent creates
  `\\.\pipe\mojo.<pid>.<rand>` via `CreateNamedPipeW(PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
  PIPE_TYPE_BYTE|PIPE_READMODE_BYTE, 1, ...)`, connects the client end in-process via
  `CreateFileW(GENERIC_READ|GENERIC_WRITE, ..., FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|
  SECURITY_ANONYMOUS)`, then launches the child with `STARTUPINFOEXW` +
  `UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_HANDLE_LIST)` carrying the **client** pipe handle
  (not std handles). The launch sequence matches Chromium `base/process/launch_win.cc`:
  `bInheritHandles=TRUE` and `SetHandleInformation(HANDLE_FLAG_INHERIT)` on each listed handle.
  Child parses `--mojo-platform-channel-handle=<int>` from `GetCommandLineW`, adopts the handle at
  its same index, and both ends do overlapped duplex I/O bound to an IOCP
  (`CreateIoCompletionPort` / bounded `GetQueuedCompletionStatus`). Authoritative byte counts come
  from the completion status, not `*lpNumberOfBytesRead`.
- `navhandoff_mojo_np_multi.c` → `mojo_np_multi.exe` — **3-handle mixed-type** list (client pipe +
  auto-reset event + semaphore) to mirror the shape of Chromium's multi-handle lists; child verifies
  each inherited handle is usable, then does the same pipe handshake.

Both use bounded per-completion waits, owned cleanup, and exit nonzero on any failure. Per the
review, no security-capabilities/AppContainer form is added because the actual path is not restricted.

### Definitive results (same PE, both platforms)

Single-handle (`mojo_np.exe`):

| Field | Native Win10 26300 | Project Wine 11.19 |
|---|---|---|
| exit | 0 | 0 |
| parent HANDLE_LIST | `{client=0xbc}` | `{client=0x40}` |
| parent comp#0 | WRITE(HANDSHAK) bytes=8 lat=8 µs | WRITE(HANDSHAK) bytes=8 lat=166 µs |
| parent comp#1 | READ(ACK) bytes=3 lat=3207 µs | READ(ACK) bytes=3 lat=8937 µs |
| parent ack-bytes | 3 `'ACK'` | 3 `'ACK'` |
| child adopted | 0xbc | 0x40 |
| child comp#0 | READ(HANDSHAK) bytes=8 lat=15 µs | READ(HANDSHAK) bytes=8 lat=268 µs |
| child comp#1 | WRITE(ACK) bytes=3 lat=9 µs | WRITE(ACK) bytes=3 lat=180 µs |
| child read-bytes | 8 `'HANDSHAK'` | 8 `'HANDSHAK'` |

Multi-handle (`mojo_np_multi.exe`):

| Field | Native Win10 26300 | Project Wine 11.19 |
|---|---|---|
| exit | 0 | 0 |
| parent HANDLE_LIST | `{client=0xd4 event=0xd8 sem=0xdc}` | `{client=0x40 event=0x44 sem=0x48}` |
| child adopted | pipe=0xd4 event=0xd8 sem=0xdc | pipe=0x40 event=0x44 sem=0x48 |
| inherited event/sem usable | **USABLE** (both `WaitForSingleObject`==WAIT_OBJECT_0) | **USABLE** (both `WaitForSingleObject`==WAIT_OBJECT_0) |
| parent comp#0 | WRITE(HANDSHAK) bytes=8 lat=6 µs | WRITE(HANDSHAK) bytes=8 lat=107 µs |
| parent comp#1 | READ(ACK) bytes=3 lat=2340 µs | READ(ACK) bytes=3 lat=11767 µs |
| child comp#0 | READ(HANDSHAK) bytes=8 lat=8 µs | READ(HANDSHAK) bytes=8 lat=259 µs |
| child comp#1 | WRITE(ACK) bytes=3 lat=5 µs | WRITE(ACK) bytes=3 lat=153 µs |

Reading:
- **Bytes / status / ordering identical.** Both platforms transfer the same 8-byte `HANDSHAK` and
  3-byte `ACK`, all operations succeed (exit 0), completion order is parent WRITE→READ and child
  READ→WRITE, and IOCP keys associate correctly. Inherited pipe, event, and semaphore are all usable
  in the child on both platforms.
- **Only latency differs.** Wine is ~15–30× slower per overlapped op, and the parent's ACK read is
  delayed by ~9–12 ms (vs native ~2.3–3.2 ms), consistent with the child needing to come up under
  `wineserver` before it can adopt the handle and submit I/O. This is a performance characteristic,
  not a semantic divergence.

This directly falsifies the specific hypothesis raised by the earlier confounded run (a Wine
multi-handle inheritance defect): with the launch sequence matched to Chromium, the multi-handle case
works identically on both platforms.

## Task 3 — Wine defect isolation (addresses Finding 4 / assigned task 3)

Not triggered. The project build was used **read-only** (`/var/tmp/wex-g4-token-parent-20261027/build`)
in a fresh private prefix, confirmed runnable (`wine --version` → wine-11.19). Because Task 2 found no
reproducible semantic difference in the tested surface (handle inheritance, overlapped named-pipe
completion, IOCP port association), there is no defect to fix and **no Wine child branch from
02d4ef2/fffa7de is cut**. Per the review: "If no defect is proven, report exact concrete failing
operation / remaining evidence, rather than attributing it to stock Wine or changing security
features." That is done below.

## Task 4 — Normal 8 GiB browser verification (assigned task 4)

Deferred this round. Its precondition in the work order is a proven defect plus a fix to validate;
with no defect proven here, an 8 GiB VirGL run would only re-confirm the already-known baseline
(Wine blank / empty HTTP at 8 GiB; native comparator commits the target document with net_error 0 but
its fixture still reports rAF timeout) without validating any change. It requires the full WEX-G4-VirGL
launcher contract (QMP, private loopback netns, VirGL, `-m 8192`, preserved R4 disk image) and is left
as the next step *after* a discriminating browser-level test isolates the actual failing operation.

## Remaining uncertainty / next discriminating test

- What was ruled out: the pinned Mojo platform-channel primitive (duplex overlapped named pipe +
  explicit handle-list inheritance of mixed handle types + IOCP overlapped completion ordering) is
  **not** where native and project Wine diverge, at least for 1-handle and 3-handle lists.
- What remains open (the actual browser-level failure): the page goes blank on Wine at 8 GiB while
  the native comparator commits the document with net_error 0. Candidate surfaces not yet isolated:
  (a) Chromium's full **7-handle** network-service bootstrap and the complete Mojo protocol (this
  repro used 1 and 3 handles, not 7, and a HANDSHAK/ACK exchange, not the real channel handshake);
  (b) higher-level network stack / URLLoader path; (c) renderer + GPU/VirGL compositing. The retained
  log's `Unsupported attribute 0x2000e` fixme on gpu/storage launches is one concrete signal to
  examine, but it is not established as the cause.
- Next discriminating test (recommended): a native-vs-Wine run that reproduces the *full* network
  service launch contract — all 7 inherited handles plus the real Mojo bootstrap handshake — and
  captures target-correlated send/receive/status with actual process identity, so the first divergent
  operation can be pinned before any Wine change is attempted.

## Evidence paths (private; not committed)

- Retained launch log: `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-navigation-phase-20261028\chromium-native-evidence\helper.stderr.log`
  (network-service launch at lines 845–862; gpu/storage `Unsupported attribute 0x2000e` at ~534/615).
- Repro sources + build/run scripts: committed under `docs/navhandoff-repro-20261028/` (this branch).
- Native run stdout + child logs and Wine run stdout + child logs were captured to the private lab
  dirs (`ac_build/` on D: for native; `/home/debian/navhandoff/` in Debian WSL for Wine) and are not
  published.

## Committed in this rework (explicit paths)

- `docs/CHROMIUM_HANDOFF_REWORK_RESULT_20261028.md` (this file)
- `docs/navhandoff-repro-20261028/navhandoff_mojo_np.c`
- `docs/navhandoff-repro-20261028/navhandoff_mojo_np_multi.c`
- `docs/navhandoff-repro-20261028/navhandoff_build_np.sh`
- `docs/navhandoff-repro-20261028/navhandoff_run_np_win_both.ps1`
- `docs/navhandoff-repro-20261028/navhandoff_run_np_wine_both.sh`
- `docs/navhandoff-repro-20261028/README.md` (updated to point at the authoritative repros)

The prior `mojo_pipe.c` / `mojo_pipe_ac.c` remain in place as superseded history; the README marks
the `navhandoff_mojo_np*.c` pair as the corrected, authoritative sources. The `wine` submodule pointer
is intentionally **not** staged or committed.
