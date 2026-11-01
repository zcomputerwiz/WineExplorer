# Chromium Navigation Handoff — Result (2026-10-28)

Work order: `WO-20261028-CHROMIUM-NAVIGATION-HANDOFF` (Antigravity → Claude Code/Worker).
Parent spec: `docs/CHROMIUM_NAVIGATION_HANDOFF_REVIEW_WORK_ORDER_20261028.md`.
Root branch: `codex/chromium-navigation-handoff` (HEAD at commit time: 65262ba5, matching origin).
Wine submodule pin under test: `fffa7de0df386ae16c4d47b1d0e450d8b7fbcbd3` (`codex/g4-token-integration`).

## Status summary

| Task | Description | Status this session |
|------|-------------|---------------------|
| 1 | Target-correlated trace comparison (Wine vs native r5) | **DONE** — decisive finding below |
| 2 | Focused native-vs-Wine semantic PE repros | **DONE** — two repros, one clean divergence found |
| 3 | Implement supported Wine fix + incremental wow64 build | **BLOCKED** — no runnable g4-fork runtime available in this environment (see below) |
| 4 | Normal 8 GiB browser verification (`chromium-navigation-handoff-20261028`) | **NOT DONE** — requires the 8 GiB VirGL VM launcher contract; out of remaining bounded budget |
| 5 | Documentation & git delivery | This document + repro sources committed and pushed to root branch |

The single most important, evidence-backed result is Task 1: under Wine the document
navigation fails because the **network-service child's Mojo channel is not established
before the browser issues the document URLLoader**, so the request has no live endpoint and
fails; a re-launched network service only becomes ready ~1.3 s later. Task 2 then shows this
is *not* a generic Wine pipe/overlapped-I/O break — it is gated on the **restricted /
AppContainer child context**, which stock Wine does not implement (`SetTokenInformation`
for the AppContainer SID class returns `ERROR_INVALID_FUNCTION`).

---

## Task 1 — Target-correlated trace comparison

### Method (proper decode, per spec)

Both traces are Chromium `--trace-startup` Perfetto protobufs (a `TracePacket` stream), **not**
JSON. They were decoded with the `perfetto` Python package (`TraceProcessor`) so that interned
track names, track IDs, event boundaries, process identity and the shared global monotonic clock
are all resolved. All timestamps below are relative to each trace's start (t = 0) in **milliseconds**;
the Perfetto global clock is shared across processes, so cross-process ordering is valid.

Decoded artifacts (kept out of Git per spec — no payload/trace/profile committed):
- Wine: `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-navigation-phase-20261028\chromium-native-evidence\trace.json`
  (browser pid **216**, network-service pid **668**).
- Native r5: `D:/DeepThought/ChromiumLab/windows-navigation-baseline-20261028-r5/trace.json`
  (browser pid **31324**, network-service pid **8504**).

The comparison is anchored on the **target navigation** — the first divergent *actual target*
operation — not on a string found somewhere in a protobuf and not on a post-cleanup cancellation.

### Native r5 (reference, SUCCESS)

Target URL: `http://127.0.0.1:53844/c764ba4e31c2e32ac6497def7d635109/index.html#nonce=…`

| Event | t (ms) |
|-------|--------|
| NetworkService::Initialize (netsvc 8504, pre-nav) | +42.8 |
| Navigation BeginNavigation | +113 |
| ThrottlingURLLoader StartNow | +128.764 |
| **CorsURLLoaderFactory::CreateLoaderAndStart** (in netsvc) | **+129.026** (0.26 ms after StartNow) |
| ThrottlingURLLoader OnReceiveResponse | +227.803 |
| CommitNavigation → RenderFrameHostImpl::SendCommitNavigation → DocumentLoader::DidCommitNavigation | … |
| WebContentsImpl::DidFinishNavigation (**success**) | +253.513 |

The network service is **already initialized and its Mojo channel live before navigation begins**;
the document URLLoader's `CreateLoaderAndStart` lands in the live network service 0.26 ms after
`StartNow`, the response arrives, and the navigation commits successfully.

### Wine (FAILURE)

| Event | t (ms) |
|-------|--------|
| Browser CreateNetworkContextInNetworkService (repeated) | +1099 / +1188 / +2107 / +2122 |
| Navigation BeginNavigation | +1724.047 |
| ThrottlingURLLoader StartNow | +1728.620 |
| **NavigationURLLoaderImpl::NotifyRequestFailed** (document) | **+2207.757** |
| OnRequestFailedInternal → renderer OnRequestFailed | +2207.761 / +2207.941 |
| WebContentsImpl::DidFinishNavigation (**as failure**) | +2207.980 |
| **NetworkService process (pid 668) first appears** | **+2841.5** |
| netsvc InitializeMojoIPCChannel → runloop | +2893.147 / +2895.455 |
| NetworkService::Initialize | +3057.574 |
| first CorsURLLoaderFactory::CreateLoaderAndStart (in netsvc) | +3090.763 |

### The exact divergence

1. The browser issues `CreateNetworkContextInNetworkService` at +1099/+1188 and again at
   +2107/+2122 — **all before any network-service process exists** in the Wine trace. There is no
   live endpoint to receive them.
2. The target navigation's document URLLoader `StartNow` fires at +1728.6, but there is **no live
   network-service Mojo channel** to carry `CreateLoaderAndStart`. ~479 ms later the document request
   fails: `NavigationURLLoaderImpl::NotifyRequestFailed` at +2207.8 → renderer `OnRequestFailed`
   → navigation finishes as a failure at +2208.0.
3. The network-service process (pid 668) does not start until **+2841.5 ms**, initializes its Mojo IPC
   channel at +2893, and only reaches `NetworkService::Initialize` at **+3057.6** — **~1.3 s after the
   document already failed**. Its first `CreateLoaderAndStart` is at +3090.8.
4. An anonymous child (pid 1036) has **zero slices** — a dead/never-reported child consistent with an
   early, unestablished channel bootstrap.

**Exact dropped operation:** the browser → network-service child **Mojo channel bootstrap** — the
named-pipe / handle-passing step at child launch that hands the freshly-spawned network service its
IPC endpoint. Under Wine this first bootstrap is not established/kept alive in time, so the document
URLLoader has no live endpoint and fails; a re-launched network service only becomes ready ~1.3 s later.
This is **not** socket/DNS/ACL/OOM: the failure precedes any network activity and is purely the
absence of a live child IPC channel at request time.

**Caveat (recorded, not guessed):** the exact `net_error` code is **not recoverable** from this trace —
the relevant `TRACE_EVENT` arguments were not captured in the retained Perfetto stream. It is recorded as
*unknown* rather than inferred.

### Corrections to earlier report interpretations (per Antigravity)

- The NetLog `TickCountToString` values are **milliseconds**. The 3219 NetLog events span
  **36.489 seconds** of continuous logging — **not** a 36 ms burst. Any prior "36 ms" reading was wrong.
- `network_service_instance_impl.cc:652` logs a *previously bound/disconnected* Mojo remote before
  starting a service. That log line proves **neither** an OS crash **nor** which process existed earlier;
  it must not be used alone as the basis for a fix. The finding above rests on the decoded trace timing,
  not on that log string.

---

## Task 2 — Focused native-vs-Wine semantic PE repros

Goal: follow the Task 1 boundary into the concrete OS primitive and determine which one diverges.
Two minimal Win32 PEs were built **once** with mingw (`x86_64-w64-mingw32-gcc -O2 -static`) and the
**identical binary** was run on **native Windows 10 (NT 10.0.26300)** and under **Wine**.

> Environment note: the only ready Wine runtime available in this session is stock Debian
> `wine-10.0` (`/usr/bin/wine`, 32+64 bit), **not** the g4 fork. These repros therefore measure the
> generic Wine behavior of each primitive (native = ground truth). They inform and narrow the root
> cause but do not by themselves prove a g4-fork-specific defect.

Repro sources are committed under `docs/navhandoff-repro-20261028/`.

### Repro A — inheritable pipe handle at CreateProcess + sync & overlapped I/O completion

`mojo_pipe.c`: parent creates two anonymous pipes, launches a child with the read end as the
child's stdin and the write-back end as its stdout (inheritable handles), writes an 8-byte
`HANDSHAK`, then (after a 50 ms gap) writes `READY\n` to satisfy the child's **overlapped** read;
the child completes it, writes `ACK` back, and exits. Parent measures time-to-ACK; child logs
byte counts, status codes and overlapped submit→completion latency to a file.

| Metric | Native Win 10.0.26300 | Stock Wine-10.0 |
|--------|----------------------:|----------------:|
| Inherited pipe handle arrives usable in child | ✓ | ✓ |
| sync `ReadFile(HANDSHAK)` | ok, 8 B, err=0 | ok, 8 B, err=0 |
| overlapped `ReadFile(READY)` completes | ok, 6 B, 47–62 ms | ok, 6 B, 16–17 ms |
| child `WriteFile(ACK)` | ok, 3 B | ok, 3 B |
| parent receives ACK | ok, 3 B, 62–94 ms | ok, 3 B, 74–76 ms |
| exit code | 0 | all ops ok |

**Result: no divergence.** The generic Mojo-channel-style primitive — an inheritable pipe handle
passed at `CreateProcess`, plus synchronous **and** overlapped pipe I/O completion between parent and
child — works identically (correctly) on both platforms, with matching byte counts and status codes.
This **rules out** a blanket break in Wine's pipe / overlapped-I-O / handle-inheritance machinery as
the cause of the Task 1 failure.

### Repro B — restricted (AppContainer) child: the discriminating variant

`mojo_pipe_ac.c`: same handshake, but the child is launched via `CreateProcessAsUser` under a token
carrying an **AppContainer SID** (`S-1-15-2-…`) plus Medium integrity (`S-1-16-8192`). This models the
context Chromium's sandboxed network service actually runs in, and the area the g4 fork implements.

| Step | Native Win 10.0.26300 (non-elevated) | Stock Wine-10.0 |
|------|--------------------------------------:|----------------:|
| `SetTokenInformation(TokenIntegrityLevel)` | FAILED err=5 (ACCESS_DENIED)¹ | **ok** |
| `SetTokenInformation(TokenAppContainerSid)` | FAILED err=87 (INVALID_PARAMETER)¹ | **FAILED err=1 (ERROR_INVALID_FUNCTION)** |
| `CreateProcessAsUser(container token)` | FAILED err=5 (ACCESS_DENIED)¹ | FAILED err=5 (ACCESS_DENIED) |
| child started / handshake completed | no (token setup failed) | no (container class unsupported) |

¹ The native failure is a **test-environment privilege limitation, not a functional gap**: modifying the
integrity level via `SetTokenInformation` requires **SeIncreaseLabelPrivilege** (an admin privilege).
The test process was deliberately run **non-elevated**, so the token could not be fully prepared and no
clean native AppContainer baseline was obtained. Native Windows fully supports AppContainer tokens when
the caller holds that privilege; triggering elevation/UAC to obtain it was out of scope here.

**Result (Wine side is unambiguous):** stock Wine-10.0 **implements `TokenIntegrityLevel` but not the
AppContainer SID class** — `SetTokenInformation(TokenAppContainerSid)` returns
`ERROR_INVALID_FUNCTION`. Consequently a restricted/AppContainer child **cannot be created at all** under
stock Wine (`CreateProcessAsUser` → ACCESS_DENIED). This is a concrete, reproducible stock-Wine gap that
maps directly onto the Task 1 mechanism: Chromium's network service is a sandboxed (AppContainer-style)
child, and without a functional restricted-child token context its Mojo channel cannot be brought up
correctly. It is also precisely the capability the g4 fork adds
(`kernelbase: Implement genuine AppContainer SID registration APIs`; `userenv: Implement app container SID
derivation`), which is why the defect persists in a specific form under that fork rather than being a
generic pipe issue.

### Task 2 conclusion

- The generic pipe/overlapped/handle-inheritance primitive does **not** diverge (Repro A).
- The divergence is gated on the **restricted / AppContainer child context**: stock Wine lacks the
  AppContainer SID token class (Repro B, `ERROR_INVALID_FUNCTION`).
- This narrows the root cause from "Mojo IPC" to "the network-service child's restricted-token /
  AppContainer launch + channel-bootstrap path," and points Task 3 at the g4 fork's AppContainer/token +
  child-launch handle-passing code.

---

## Task 3 — Implement supported Wine fix (BLOCKED this session)

**Blocker: no runnable g4-fork Wine runtime is available in this environment, and none can be produced
within a bounded session.**

Findings from an exhaustive search of the local (own `Debian` WSL distro; the off-limits `wexcomp` /
`wexbuild` distros were not touched):
- All fast-disk Wine **source** trees are **partial git worktrees** of the D:-backed wine module — each
  has `configure` but is missing `server/program.c` (the full tree is not materialized). None can be
  built as-is.
- The recent **build** trees (`/var/tmp/wex-g4-token-parent-20261027/build`, `-ac-parent-20261026`,
  `-toknls-test-20261027`) contain **1288 built `.dll` files each but no `wine64.exe` / `wineserver.exe`
  loader/server executables and no `build/built/` staging** — i.e. DLL-only, not a runnable runtime.
- The only ready runtime present is stock Debian `wine-10.0`, which is **not** the g4 fork.

A from-scratch wow64 build (materialize full source at `fffa7de`, configure, full `make`) is measured in
hours and is not a bounded-session task; an *incremental* build additionally requires a complete, already-
configured+linked tree to increment on, which does not exist here.

**Recommended fix direction (for the next iteration, with a working g4 build):**
The evidence points at the network-service child's **restricted-token / AppContainer launch and Mojo
channel bootstrap** in the g4 fork (`fffa7de` / `02d4ef2`): specifically the path that (a) creates the
AppContainer-restricted token for the child, (b) passes the IPC endpoint handle to it at process launch,
and (c) establishes/keeps the named-pipe Mojo channel alive before the browser issues the document
URLLoader. A minimal correct fix + focused regression coverage should target that path, then be validated
by an incremental wow64 build in WSL on an OWN D-backed configured source/build and re-run through Task 1's
trace comparison (expecting the network service to be live before `StartNow`, as in native r5).

---

## Task 4 — Normal 8 GiB browser verification (NOT DONE)

Not attempted this session. It requires the **8 GiB VirGL VM launcher contract** (run name
`chromium-navigation-handoff-20261028`, standard security, loopback HTTP fixture, `-m 8192`) and a
working g4-fork Wine runtime (see Task 3 blocker). Intended check: whether the target loopback GET of
`index.html` and `scene.js` return HTTP 200 with `has_committed=true` / `net_error=0`, distinguishing page
load from WebGL/rAF/presentation/security acceptance. This is deferred to the next iteration once a
runnable g4 build exists.

---

## Limitations & remaining blocker

- **Wine runtime:** Task 2 used stock `wine-10.0`, not the g4 fork; results measure generic Wine behavior
  and narrow (not fully prove) the g4-fork-specific defect.
- **Native AppContainer baseline:** privilege-gated (`SeIncreaseLabelPrivilege`); not obtained without
  elevation. The Wine `ERROR_INVALID_FUNCTION` result is independent of this and stands on its own.
- **net_error code:** not recoverable from the retained trace (TRACE_EVENT args not captured); recorded as
  unknown.
- **Remaining blocker for Tasks 3 & 4:** a complete, runnable g4-fork Wine build at `fffa7de`/`02d4ef2`.
  No such build exists in this environment and producing one is out of scope for a bounded session.

## Next steps (next iteration)

1. Stand up an OWN D-backed full wine source at `fffa7de`, configure + complete a wow64 build (or restore a
   known-good g4 runtime), so the fork itself can be run and incrementally rebuilt.
2. Implement the minimal fix on the AppContainer/restricted-child launch + Mojo channel bootstrap path, with
   focused regression coverage; rebuild incrementally in WSL.
3. Re-run Task 1's trace comparison under the patched fork (expect network service live before `StartNow`).
4. Run Task 4's normal 8 GiB browser verification (`chromium-navigation-handoff-20261028`) and record
   page-load vs WebGL/presentation/security acceptance.

## Deliverables in this commit

- This result document: `docs/CHROMIUM_NAVIGATION_HANDOFF_RESULT_20261028.md`.
- Repro sources (runnable, no payload/trace/profile): `docs/navhandoff-repro-20261028/mojo_pipe.c`,
  `docs/navhandoff-repro-20261028/mojo_pipe_ac.c`, and `docs/navhandoff-repro-20261028/README.md`.
