# Navigation handoff — focused native-vs-Wine PE repros (2026-10-28)

Minimal Win32 PEs used for Task 2 of `WO-20261028-CHROMIUM-NAVIGATION-HANDOFF`. Each is built once
with mingw and the **identical binary** is run on native Windows and under Wine to compare the exact
OS primitive implicated by the Task 1 trace finding (network-service child Mojo channel bootstrap).

No payload/trace/profile is included — only source. Full results are in
`docs/CHROMIUM_NAVIGATION_HANDOFF_RESULT_20261028.md`.

## mojo_pipe.c — Repro A: inheritable pipe handle + sync & overlapped I/O completion

Parent creates two anonymous pipes, launches a child with the read end as its stdin and the write-back
end as its stdout (inheritable handles), writes `HANDSHAK` then (after 50 ms) `READY\n`, and reads the
child's `ACK`. The child does a synchronous read, an **overlapped** read (measuring submit→completion
latency), and writes `ACK` back. Child diagnostics go to `mojo_child.log`.

```
x86_64-w64-mingw32-gcc -O2 -static -o mojo_pipe.exe mojo_pipe.c
./mojo_pipe.exe            # parent auto-launches "./mojo_pipe.exe --child"
```

Result: **no divergence** — works identically on native Windows and stock Wine-10.0 (correct bytes,
status codes, completion). Rules out a generic pipe/overlapped/handle-inheritance break.

## mojo_pipe_ac.c — Repro B: restricted (AppContainer) child (discriminating variant)

Same handshake, but the child is launched via `CreateProcessAsUser` under a token carrying an
AppContainer SID (`S-1-15-2-…`) + Medium integrity (`S-1-16-8192`). Models Chromium's sandboxed
network-service context. Child diagnostics go to `mojo_child_ac.log`.

```
x86_64-w64-mingw32-gcc -O2 -static -o mojo_pipe_ac.exe mojo_pipe_ac.c -ladvapi32
./mojo_pipe_ac.exe
```

Result: stock Wine-10.0 implements `TokenIntegrityLevel` but **not** the AppContainer SID class —
`SetTokenInformation(TokenAppContainerSid)` returns `ERROR_INVALID_FUNCTION`, so a restricted child
cannot be created under stock Wine. (Native baseline is privilege-gated on `SeIncreaseLabelPrivilege`;
see the result doc.)
