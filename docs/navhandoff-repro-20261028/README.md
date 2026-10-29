# Navigation handoff — focused native-vs-Wine PE repros (2026-10-28)

Minimal Win32 PEs for Task 2 of the Chromium navigation-handoff rework. Each is built once with
mingw and the **identical binary** is run on native Windows and under the **actual project candidate
Wine** to compare the exact OS primitive implicated by the Task 1 trace finding (network-service
child Mojo channel bootstrap).

No payload/trace/profile is included — only source. Authoritative results are in
`docs/CHROMIUM_HANDOFF_REWORK_RESULT_20261028.md`.

## Authoritative rework repros (use these)

These two supersede the earlier `mojo_pipe*.c` pair, which was tested against **stock** Debian
Wine 10 — a confound the parent review rejected. The rework pair runs against the actual project
candidate Wine and exercises Chromium's real launch contract.

### navhandoff_mojo_np.c — pinned single-pipe-handle primitive (`mojo_np.exe`)

Parent creates a duplex overlapped named pipe, connects the client end in-process, and launches the
child with `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` carrying the **client** pipe handle
(not std handles). The launch sequence matches Chromium `base/process/launch_win.cc`:
`bInheritHandles=TRUE` and `SetHandleInformation(HANDLE_FLAG_INHERIT)` on each listed handle. Child
adopts the inherited handle at its same index; both ends do overlapped duplex I/O over an IOCP with
bounded `GetQueuedCompletionStatus` waits, owned cleanup, and nonzero exit on any failure.

### navhandoff_mojo_np_multi.c — 3-handle mixed-type list (`mojo_np_multi.exe`)

Same pipe handshake, but the HANDLE_LIST carries a client pipe **plus** an auto-reset event and a
semaphore (mirroring the shape of Chromium's multi-handle lists). Child verifies each inherited
handle is usable before doing the pipe handshake.

### Build + run (same PE on both platforms)

```
# build both exes (Debian WSL, x86_64-w64-mingw32-gcc -O2 -static)
bash navhandoff_build_np.sh

# native Windows 10: runs mojo_np.exe then mojo_np_multi.exe natively
pwsh -NoProfile -ExecutionPolicy Bypass -File navhandoff_run_np_win_both.ps1

# actual project candidate Wine (g4 fork, wine-11.19), read-only build + fresh private prefix
bash navhandoff_run_np_wine_both.sh
```

Result: **no semantic divergence** — identical bytes (8-byte `HANDSHAK` / 3-byte `ACK`), status codes,
overlapped completion ordering, and IOCP key association on both platforms for the single-handle and
3-handle cases; inherited pipe/event/semaphore all usable in the child. Only latency differs (Wine
~15–30× slower per op). See the result doc for the full tables.

## Superseded history (kept for reference, do not use for conclusions)

### mojo_pipe.c — Repro A: inheritable pipe handle + sync & overlapped I/O completion

Anonymous pipes via std handles; parent does a blocking read and returned 0 after a failed ACK.
Tested against **stock** Wine 10 only. Superseded by `navhandoff_mojo_np.c` (real named pipe,
explicit handle list, IOCP, bounded waits).

### mojo_pipe_ac.c — Repro B: restricted (AppContainer) child

Attempted an AppContainer child via `SetTokenInformation(TokenAppContainerSid)` +
`CreateProcessAsUser`; the source logged SID-setup failure and continued, and did not implement
Chromium's `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` + HANDLE_LIST launch. It fails on Windows
too (err 87/5). Superseded — and not applicable, because the retained launch log shows the network
service starts with `--service-sandbox-type=none`, not a restricted token.
