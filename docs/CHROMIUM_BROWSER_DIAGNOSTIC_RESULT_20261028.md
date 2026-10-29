# Chromium Browser Targeted Diagnostic — Result (Run 1)

- **Work order:** WO-20261028-CHROMIUM-TARGETED-DIAGNOSTIC (Diagnostic Run 1)
- **Identity:** `chromium-browser-diagnostic-20261028`
- **Date:** 2026-10-28
- **Objective:** stop guessing — run ONE authorized normal 8 GiB diagnostic trial with enriched
  debug channels and pinpoint which gate in the Chromium kNetwork sandbox launch pipeline is the
  **first failing operation**, with process / timing / error detail.

---

## Verdict (read this first)

**The failure is NOT in the Chromium kNetwork app-container / lowbox sandbox launch pipeline.**
The enriched channels let us say this definitively rather than guess:

1. The network service was launched with **`--service-sandbox-type=none`** — it runs **unsandboxed**.
   No LPAC / AppContainer policy is generated or applied to it, and **no lowbox token was ever
   created for any child process** in the whole run (no `NtCreateLowBoxToken`, no
   `CreateAppContainerToken`, no lowbox `SetSecurityInfo`/`GetSecurityInfo` in the `+security` trace).
2. All of the browser's core child processes were **spawned successfully** — crashpad, GPU process,
   the storage utility (the one *sandboxed* service), two renderers, and the network service. So
   `CreateProcess`/token-attachment (Gates 3–4) is not the blocker.
3. The **first observable failure is a network-service disruption at navigation start**:
   `content/browser/network_service_instance_impl.cc:652 — Network service crashed or was
   terminated, restarting service` at `05:37:37.021`, ~98 ms after the target URL began loading.
4. After that disruption the **target document request was never sent**. The browser only performed a
   *speculative* host-resolution/preconnect for the fixture host; every real outbound request was
   unrelated Google account sync (`accounts.google.com/ListAccounts`) that fails with no internet
   (`connect failed: 10051` / `ERR_ADDRESS_UNREACHABLE`). The local fixture received **zero** requests,
   the page never loaded, and the helper timed out at 45 s.

In short: this is a **network-service runtime disruption that drops the in-flight navigation**, not a
sandbox-policy/token gate failure. (See "Recommended next step".)

---

## Answers to the three localization questions (Task 4)

**Q1 — Did `GeneratePolicyForSandboxedProcess` run for utility!kNetwork?**
No, not in the app-container sense. The network service was created with
`--service-sandbox-type=none` (`helper.stderr.log:1054`), so no AppContainer/lowbox policy is generated
or applied to it. Only the storage utility is sandboxed (`--service-sandbox-type=service`,
`helper.stderr.log:717`). Note the `NetworkServiceSandboxWindows:Enabled_20231025` field trial *is*
active in this build (`netlog.json:1` `activeFieldTrialGroups`), yet the effective per-launch type was
`none` — so the sandbox path was not taken for the running network process.

**Q2 — Did `AddAppContainerProfileToConfig` run, and which step failed (profile creation / capability
setup / executable AccessCheck)?**
Not on the running network process's path. The *only* AppContainer activity in the trace is a single pair
on the browser thread (PID 0x00dc, tid 0x01f8):

```
helper.stderr.log:665  126.220  security:AppContainerRegisterSid (…, L"cr.sb.netED86A9060BF9087DCF4C7462DCD014893457BF94", L"Chrome Sandbox")
helper.stderr.log:669  126.223  security:AppContainerUnregisterSid (…)
```

i.e. the network SID (`cr.sb.net…`) is **registered and unregistered 3 ms apart**, with **no capability
setup and no AccessCheck between them, and no lowbox token afterward**. This register→rollback is *not*
attributable to the running (unsandboxed) network process. From this trace alone its exact role — a rolled-back
initial-attempt profile vs. a transient pre-initialization probe — **cannot be distinguished**, because the
first network instance's own creation/exit are not distinctly logged. So no Step A/B/C failure is confirmed
for kNetwork; the gate simply is not on its launch path in this run.

**Q3 — Did `CreateAppContainerToken` / `ReplacePackageSidInDacl` run, and what were the return codes?**
Neither was called. There is **no** `NtCreateLowBoxToken`, no `CreateAppContainerToken`, and no lowbox
`SetSecurityInfo`/`GetSecurityInfo` anywhere in the `+security` trace for any PID. **No lowbox token was
created for any child.** (Consequently there are no such return codes to record.)

---

## Process / timing timeline (evidence)

Time bases: `helper.stderr.log` uses Wine seconds since start (`126.xxx`); `chromium.log` uses wall clock
(`1029/053737.xxx`). Browser = PID 0x00dc (220), created by the helper (PID 0x0020 / 32).

| When | Event | Evidence |
|------|-------|----------|
| ~125.44 | Helper launches browser `chrome.exe` (child_pid 220) | `helper.stderr.log:330-333`; `native-exit.jsonl:1` |
| ~125.5  | crashpad-handler spawned (pid 0x00e4) | `helper.stderr.log:365-369` |
| ~126.17 | GPU process spawned (pid 0x0200/512) | `helper.stderr.log:600-611` |
| 126.220→126.223 | `AppContainerRegisterSid("cr.sb.net…")` → `AppContainerUnregisterSid` (3 ms), no token after | `helper.stderr.log:665,669` |
| ~126.26 | **storage** utility spawned, `--service-sandbox-type=service` (pid 0x0220/544) | `helper.stderr.log:717,727-728` |
| ~126.59–126.62 | Two renderers spawned (e.g. pid 0x0284/644) | `helper.stderr.log:849-903` |
| 05:37:36.923 | Browser begins navigation to target nonce URL ("URL to scan") | `chromium.log:120` |
| **05:37:37.021** | **ERROR: "Network service crashed or was terminated, restarting service."** | `chromium.log:130` |
| ~126.79 (~37.3) | Network service (re)launched with `--service-sandbox-type=none` (pid 0x02a0/672), starts OK | `helper.stderr.log:1054,1093-1094` |
| t=127427 | Speculative host-resolution/preconnect for `http://127.0.0.1:44175` (NOT a document request) | `netlog.json:3-4` |
| 05:37:42 → 05:38:12 | Network service only requests `accounts.google.com/ListAccounts`; `connect failed: 10051` / `ERR_ADDRESS_UNREACHABLE` (repeated ~every 12 s) | `chromium.log:447-482`; `netlog.json` |
| 05:37:42→… | Helper waits for browser exit signal; **times out at 45 s** (`wait_result 258`) | `native-exit.jsonl:2`; `results.json` |
| — | Startup Perfetto trace flushed to `trace.json` | `chromium.log:484` |

Key negative evidence (searched the full traces):
- `--utility-sub-type=network…` appears on **exactly one** `CreateProcessInternalW` in the whole trace
  (`helper.stderr.log:1054`) — there is no separate sandboxed first network launch to attribute the crash to.
- The target nonce path (`…/ce034dae…/index.html#nonce=…`) appears in `netlog.json` **only** as metadata
  (`clientInfo.command_line`, line 1) and a speculative host-resolution (line 3). There is **no**
  `URL_REQUEST` / `HTTP_STREAM_REQUEST` for it. Every real request is `accounts.google.com/ListAccounts`.

---

## Environment, ownership & integrity (Tasks 2–3)

- **Fresh isolated paths** for identity `chromium-browser-diagnostic-20261028`; the prior
  `chromium-browser-boundary-20261028`, `.t1`, and `.t2` directories were not touched or reused.
- **Ownership reconciled** before launch: `execution.json` → `ownership.busy = false`, readiness
  `ready = true`, `orchestration_complete = true`, `consumer_started = true`, `candidate_reuse_valid = true`.
- **Coherent matched candidate reused, no rebuild:** the already-deployed
  `wine-control-channel-20261027/build/wow64` candidate. `outside_modules_unchanged = true` across the run —
  deployed `userenv.dll` (x86_64 `8a2eec86…`, i386 `c9e63e9e…`) and `kernelbase.dll`
  (x86_64 `2b103c0c…`, i386 `32cd8bfc…`) SHA-256 identical before/after. No Wine/compositor pin changed.
- **8 GiB confirmed:** QEMU argv `-m 8192`; guest `MemTotal` ≈ 8,138,028 kB (~7.76 GiB usable).
- **Graceful shutdown enforced:** stop → QMP powerdown → `qemu_exit_status: 0`, `graceful_powerdown: true`,
  `qemu_stopped: true`, `xvfb_stopped: true`.

## Instrumentation applied (Task 1)

- `WINEDEBUG = -all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security` — confirmed in
  `results.json` environment; the `+security`/`+userenv` channels produced the AppContainer/token trace above.
- Chromium scoped VLOG (release-safe) with the sandbox/base-process categories added for this run:
  `--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2,*/sandbox/*=2,*/base/process/*=2`,
  plus bounded NetLog (integer 40 s, Default capture, 8 MiB) and best-effort startup trace (35 s). All present in
  the bound command line (`native-exit.jsonl:1`) and `netlog.json:1`.
- **Fresh path-locked helper PE** for this identity, compiled with the `WEX_DIAGNOSTIC` variant so it embeds the
  enriched vmodule (the existing boundary/navigation PEs are untouched). The C-side command line exactly matched
  the consumer's `child_args()` — the binding check passed and the `bound` event was written.
- Note: the `*/sandbox/*=2` vmodule produced no sandbox-policy lines in `chromium.log` (the Windows LPAC
  app-container code paths did not emit at that module scope), which is itself consistent with the network
  service running unsandboxed. The decisive signal came from the `+security`/`+process` Wine channels.

## Outcome (Task 3)

- Consumer result: `journal_outcome.outcome = native_timeout`; helper `bound` then single `timeout`
  (`wait_result 258`, `wait_ms 45000`). Expected non-acceptance for a diagnostic trial.
- `http_fixture.requests = []` — **zero** requests reached the local fixture server; browser consumption
  of the nonce page is NOT proven (it never loaded).
- Browser process stayed alive through the 45 s window (no clean exit signal), hence the timeout rather than
  a crash-exit.

## Evidence preserved (Task 3)

All under `chromium-browser-diagnostic-20261028/chromium-native-evidence/`:
`results.json`, `chromium.log`, `helper.stderr.log`, `native-exit.jsonl`, `netlog.json`, `trace.json`
(Perfetto, flushed per `chromium.log:484`), plus support logs (`wexcomp.log`, `wayland-info.log`,
`native-egl.*.log`, `terminal-front.*`) and a `terminal-front.png` screenshot. Orchestrator state in
`…/chromium-browser-diagnostic-20261028/execution.json` and `results.json`.

## Interpretation & recommended next step (out of scope for this WO)

This run's purpose was to stop guessing about the sandbox pipeline, and it did: **the app-container/lowbox
gates are not engaged for the network service in this configuration** (`--service-sandbox-type=none`, no lowbox
token), so they cannot be "the first failing gate." The actionable root cause to pursue next is *why the
network service is disrupted at navigation start and why the target document request is dropped and never
retried*. A focused follow-up (separate work order) should:

1. Capture the **first network instance's exit reason** — e.g. a Crashpad dump under
   `<profile>/Crashpad`, or a child-scoped `+netlog`/`err:all` to see its fatal — to distinguish a crash from a
   clean early exit.
2. Confirm whether the navigation drop is a consequence of the service restart losing in-flight main-frame
   navigations (vs. an independent cause), and whether forcing the network sandbox on/off changes the outcome.

The lone `AppContainerRegisterSid("cr.sb.net…")→UnregisterSid` (no token) should be re-examined in that
follow-up with child-scoped tracing to determine whether it was a rolled-back initial attempt or a pre-init probe.

## Constraints honored

- Worked only in the designated worktree; reused the existing candidate build (no Wine/compositor rebuild or
  pin change); did not touch other workers' builds/prefixes.
- Preserved prior run evidence and disk images; used fresh private paths for this identity.
- Graceful QMP shutdown (`qemu_exit_status: 0`); no VM password/SSH/LightDM changes; no secrets/captures in Git.
