# Chromium Browser Boundary — Result (WO-20261028-CHROMIUM-BROWSER-BOUNDARY)

- **Date:** 2026-10-28
- **Author:** Claude Code (executed on behalf of the Antigravity/Gemini work order)
- **Companion work order:** [`docs/CHROMIUM_BROWSER_BOUNDARY_WORK_ORDER_20261028.md`](CHROMIUM_BROWSER_BOUNDARY_WORK_ORDER_20261028.md)
- **Root branch / superproject HEAD:** `codex/chromium-navigation-handoff` @ `4aa3e662` (== `origin/codex/chromium-navigation-handoff`)

> **This document records two trials.** The body below (§1–§9) is **Trial 1**
> (`userenv!CreateAppContainerProfile`). A follow-up work order,
> [`WO-20261028-CHROMIUM-APPCONTAINER-TOKEN`](CHROMIUM_APPCONTAINER_TOKEN_WORK_ORDER_20261028.md),
> corrected the root cause to `kernelbase!CreateAppContainerToken`; **Trial 2** is appended at the end of
> this document and is also **NEGATIVE**. Together they show both AppContainer APIs are *necessary* but
> neither is *sufficient* — the residual gap is downstream of token creation.

## Verdict (TL;DR)

**NEGATIVE — the fix is necessary but not sufficient.** The `userenv!CreateAppContainerProfile` /
`DeleteAppContainerProfile` implementation (Task 2, commit `d4e4075…`) was built, deployed to the
guest Wine build's verified load path, and hash-verified in place. Under one authorized normal
8 GiB browser verification trial, Chromium **still did not initialize the Network Service on its
first (sandboxed `kNetwork`) attempt**: it logged the same
`"Network service crashed or was terminated, restarting service."` line as before the fix and
spawned its **only** `network.mojom.NetworkService` process with the emergency fallback
`--service-sandbox-type=none`. The loopback HTTP fixture was never consumed (`http_fixture.requests=[]`,
classification `"browser consumption NOT_PROVEN"`), so there were no `index.html`/`scene.js` 200s and
no page commit. All orchestration gates passed and the VM shut down gracefully
(`qemu_exit_status: 0`).

The evidence is consistent with a **downstream** LPAC low-box gap remaining after
`CreateAppContainerProfile`: creating the AppContainer SID succeeds, but applying that SID to
constrain the spawned utility process (the mandatory-label / integrity step) still fails in Wine, so
the sandboxed network service is never created and Chromium falls back to `none`.

---

## 1. Root-cause hypothesis under test

From Antigravity's Perfetto decode of the pre-fix run: at browser start the browser process launches
the Network Service under `SandboxType::kNetwork` (Low Privilege AppContainer). The policy path is

```
sandbox/policy/win/sandbox_win.cc  GeneratePolicyForSandboxedProcess -> SetLowBox("cr.sb.net")
  -> userenv!CreateAppContainerProfile(L"cr.sb.net", ..., &container_sid)
```

In the project Wine candidate, `dlls/userenv/userenv_main.c` implemented this as an unfulfilled stub
returning `E_NOTIMPL`. Policy generation therefore failed, **no process was ever spawned**, the Mojo
remote disconnected, and Chromium logged (at `network_service_instance_impl.cc:652`)
`"Network service crashed or was terminated, restarting service."` before falling back to
`--service-sandbox-type=none`. Native Windows succeeds because `CreateAppContainerProfile` works.

The hypothesis to verify: implementing `CreateAppContainerProfile` (return `S_OK`, populate a valid
`S-1-15-2-…` AppContainer SID) makes the **first** network-service launch succeed under the real
sandbox, so the crash/restart line disappears and loopback navigation commits.

## 2. Task 1 — focused repro (completed pre-session)

Repro source: [`docs/navhandoff-repro-20261028/appcontainer_profile_repro.c`](navhandoff-repro-20261028/appcontainer_profile_repro.c).
It resolves `CreateAppContainerProfile` / `DeleteAppContainerProfile` via `GetProcAddress` and calls
`CreateAppContainerProfile(L"cr.sb.net.test", …)`, requiring `S_OK` (or `ERROR_ALREADY_EXISTS`) plus a
non-NULL SID whose string form begins `S-1-15-2-`.

| Platform | Result |
|---|---|
| Native Windows 10 (NT 10.0.26300) | **PASS, exit 0** |
| Project Wine candidate, BEFORE fix | `CreateAppContainerProfile` → `E_NOTIMPL`, exit 2 |

## 3. Task 2 — the fix (completed prior session; committed + pushed)

- **Commit:** `d4e4075520fb923d550e896519c5bf38082ee457` (short `d4e4075`)
  - Subject: `userenv: implement CreateAppContainerProfile and DeleteAppContainerProfile`
- **Branch:** `codex/g4-appcontainer-profile` — **pushed** to origin (`origin/codex/g4-appcontainer-profile` present).
- **Source checkout (build tree):** `/var/tmp/wex-g4-token-parent-20261027/wine` @ `d4e4075`.
- **Behavior:** `CreateAppContainerProfile` derives the AppContainer SID via
  `DeriveAppContainerSidFromAppContainerName` and registers it with `AppContainerRegisterSid`, returning
  `S_OK` / `HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)` and populating `*container_sid`.
  `DeleteAppContainerProfile` calls `AppContainerUnregisterSid`.

### Deployed candidate DLLs (built from `d4e4075`, per-DLL rebuild of `dlls/userenv`)

Deployed into the guest Wine build's load path
`GUEST_WINE_BUILD = /home/debian/wex-g4/wine-control-channel-20261027/build/wow64/dlls/userenv/<arch>/userenv.dll`:

| Arch | sha256 | size (bytes) |
|---|---|---|
| `x86_64-windows` | `8a2eec86008001349fcc8dcb24b0918c5fb30457c5e5b8a5e85a79c3e2b6081a` | 212774 |
| `i386-windows` | `c9e63e9e0ef91c211966f0d470b77dacadbdf7cb6f46bef0ec3c3a34c59f20c8` | 178360 |

**Load-path verification:** the guest `sha256sum` of both deployed DLLs was compared to the host
build and matched (fail-closed check — the run aborts on mismatch). The trial's own preflight also
verifies `userenv.dll` at exactly this path (`GUEST_WINE_BUILD/dlls/userenv/<arch>-windows/userenv.dll`)
and validates the reused build tree (`dlls/ntdll/ntdll.so`, `server/wineserver`). The consumer sets
`WINEPREFIX` to a separate prefix while Wine loads builtin DLLs from `GUEST_WINE_BUILD`.

## 4. Task 3 — one authorized normal 8 GiB browser verification trial (NEGATIVE)

### Trial contract (all satisfied)

- **Trial name:** `chromium-browser-boundary-20261028`
- **VM:** g4-virgl launcher, QEMU/KVM/q35/virtio-vga-gl (VirGL), `-m 8192` (8 GiB) per the standard
  launcher contract (`launch.py`).
- **Disk:** R4 qcow2 overlay on the preserved base layer; prior run evidence and disk images preserved.
- **Wine build:** REUSED `GUEST_WINE_BUILD` (control-channel candidate, no rebuild); only the two
  `userenv.dll` files were deployed (Task 2 artifact).
- **Sandbox / security:** normal sandbox enabled (no bypasses, no proxy switches, no fake stubs).
- **Fixture:** private loopback HTTP fixture serving `index.html` + `scene.js`.
- **Shutdown:** graceful QMP powerdown — **`qemu_exit_status: 0`** (`results.json:152`).

### Target criteria vs. actual

| # | Criterion (WO Task 3) | Target | Actual | Met? |
|---|---|---|---|---|
| 1 | Network Service initializes on the FIRST attempt (`utility!kNetwork` succeeds) | yes | **No** — only `--service-sandbox-type=none` spawn observed; no sandboxed `kNetwork` process created | ✗ |
| 2 | No crash/restart log | absent | **Present** — `chromium.log:132` (see below) | ✗ |
| 3 | Loopback navigation reaches fixture: `index.html` 200 | 200 | **Never requested** (`http_fixture.requests=[]`) | ✗ |
| 4 | Loopback navigation reaches fixture: `scene.js` 200 | 200 | **Never requested** | ✗ |
| 5 | Page commits: `has_committed=true` | true | **Absent** (never recorded; page never committed) | ✗ |
| 6 | `net_error=0` | 0 | Absent (no fixture request / no commit to measure) | n/a |
| 7 | Graceful QMP shutdown `qemu_exit_status: 0` | 0 | **0** (`results.json:152`) | ✓ |

### Evidence

Evidence directory:
`D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-browser-boundary-20261028\chromium-native-evidence\`

**(a) The crash/restart line is still present** — `chromium.log:132`:

```
[224:228:1029/021620.023:ERROR:content\browser\network_service_instance_impl.cc:652] Network service crashed or was terminated, restarting service.
```

Immediately after (t=021620.7+), a working network service (pid 672) comes up and issues real requests
to `update.googleapis.com` / `accounts.google.com`. That is the **`--service-sandbox-type=none`
fallback** functioning — not a sandboxed `kNetwork` service.

**(b) NetworkService was spawned exactly once, directly with `none`** — `helper.stderr.log`
(`WINEDEBUG=trace:process`). Exactly one `NtCreateUserProcess` for `network.mojom.NetworkService`, at
relative t≈44.380 s, with `--service-sandbox-type=none`:

```
44.376:…:CreateProcessInternalW … --type=utility --utility-sub-type=network.mojom.NetworkService … --service-sandbox-type=none …
44.380:…:NtCreateUserProcess … network.mojom.NetworkService … --service-sandbox-type=none …
```

`StorageService` and `TracingService` launched with `--service-sandbox-type=service`. **No**
sandboxed (`kNetwork`/AppContainer) network-service process was ever created — consistent with the
first (appcontainer) attempt failing *before* a process is spawned, then Chromium logging the
crash/restart line and falling back to `none`.

**(c) No AppContainer/token signals in the trace channel used.** A grep of `helper.stderr.log` for
`appcontainer | CreateAppContainerProfile | SetTokenInformation | MandatoryLabel | lowbox | S-1-15`
returns **no matches**. This is a **channel limitation, not proof of non-invocation**: the run used
`trace:process`, which does not log `userenv`/token/SID calls.

**(d) Fixture never consumed** — `results.json`:

```
passed = False
diagnostic_exit = 1
url = http://127.0.0.1:39761/86ef986511110527be004c371866a613/index.html#nonce=86ef986511110527be004c371866a613
http_fixture.requests = []
http_fixture.classification = "HTTP writes only; browser consumption NOT_PROVEN"
```

`netlog.json` (570 767 bytes): the fixture URL appears **only** as an echo of the `--app=` command-line
argument in netlog metadata — there are no actual HTTP request/response entries for `index.html` or
`scene.js`.

**(e) Orchestration gates all passed** — `execution.json`:
`orchestration_complete=True`, `candidate_reuse_valid=True`, `outside_modules_unchanged=True`,
`diagnostic_nonacceptance_expected=True`, `guest_wine_verify` all-true. `consumer_ssh_exit=1` is the
expected diagnostic exit (success is judged from evidence, not the exit code).

## 5. Analysis — why the result is negative

The fix resolves the **first** gap (`CreateAppContainerProfile`), which was confirmed as a hard blocker
by Task 1. It is therefore **necessary**. But it is **not sufficient** for the sandboxed network service
to launch:

- Deployment to the load path is verified (guest sha256 == host; trial preflight verifies `userenv.dll`
  at the deployed path; the reused wow64 build loads every other builtin DLL from the same tree). The
  negative result is therefore best characterized as **"fix loaded but insufficient,"** not "fix not
  deployed." (The one residual uncertainty — whether `CreateAppContainerProfile` was *invoked* — cannot
  be settled from this run because `trace:process` does not log `userenv` calls; see §6.)

- **Leading hypothesis for the residual gap:** after `CreateAppContainerProfile` returns a valid
  AppContainer SID, Chromium's LPAC low-box setup must *apply* that SID to constrain the spawned utility
  process. On Windows this is done through the mandatory-label / integrity mechanism (e.g.
  `SetTokenInformation(TokenMandatoryLabel)` carrying the AppContainer SID, and/or an SACL-based
  restriction applied at `CreateProcess`). If Wine still lacks one of those **downstream** primitives,
  policy generation or the constrained process creation fails *after* this fix — producing exactly the
  observed behavior: no `kNetwork` process created → crash/restart log → `none` fallback.

## 6. Caveats / limitations

1. **Trace channel:** the run used `WINEDEBUG=trace:process`. It cannot observe `userenv`/token/SID
   calls, so this evidence cannot pinpoint which specific downstream API fails after
   `CreateAppContainerProfile`, nor directly confirm that `CreateAppContainerProfile` was invoked in-band.
2. **Single trial:** WO Task 3 authorized *one* normal verification trial; no additional diagnostic runs
   were performed (a targeted re-run with a broader trace channel would be needed to localize the next
   gap and is outside this task's scope).

## 7. Recommendations for Antigravity / Gemini

1. **Localize the next gap:** add `trace:userenv` + `trace:sandbox` (and/or the Chromium
   `content/sandbox` / `base::subprocess` VLOG modules) to the consumer's debug channel and run one more
   *diagnostic* trial to capture the exact failing call after `CreateAppContainerProfile`. This needs
   explicit authorization (it is not a "normal verification" trial).
2. **Likely next Wine gap(s):** implement/verify the low-box mandatory-label application —
   `SetTokenInformation` with `TokenMandatoryLabel` carrying an AppContainer SID, and/or the SACL /
   integrity restriction applied at `CreateProcess` for a low-box process — checking each against native
   behavior.
3. **In-band signal:** confirm whether Chromium logs the specific sandbox-policy failure at a VLOG module
   not yet enabled, so the crash/restart line is corroborated by a direct reason rather than inferred.

## 8. Reproducibility

- **Trial script** (generated artifact; `build/` is gitignored per `.gitignore:17`, kept out of git by
  repo convention — path recorded here for reference):
  `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer\build\g4-virgl-20261022\chromium-browser-boundary-trial.py`
  (`mode='browser-boundary'`, `NAME=chromium-browser-boundary-20261028`).
- **Consumer:** `scripts/check-g4-chromium-native-exit.py --browser-boundary` (this change is committed).
- **Deploy + run harness** (scratch, not committed): `ac_build/deploy_and_run.sh`.
- **Evidence:** see the evidence directory in §4.

## 9. Security-constraint compliance

No VM mutation of any kind (no password/SSH/NAT/LightDM/reboot changes); preserved R4 base image and
prior run evidence; reused the authorized Wine build with only the two `userenv.dll` files deployed;
graceful QMP shutdown (`qemu_exit_status: 0`); staged explicit filenames only for this commit (result
doc, repro `.c`, consumer script edit) and left the `wine` submodule pointer **unstaged**; no vendor
packages / legal text / captures / secrets committed.

## Appendix — file inventory (this work order)

| Path | Status | Role |
|---|---|---|
| `docs/CHROMIUM_BROWSER_BOUNDARY_RESULT_20261028.md` | new (this commit) | Result document |
| `docs/navhandoff-repro-20261028/appcontainer_profile_repro.c` | new (this commit) | Task 1 repro PE source |
| `scripts/check-g4-chromium-native-exit.py` | modified (this commit) | Guest consumer, `browser-boundary` mode |
| wine `d4e4075…` (`codex/g4-appcontainer-profile`, pushed) | separate branch | Task 2 fix (`userenv_main.c`, `userenv.spec`) |
| `build/g4-virgl-20261022/chromium-browser-boundary-trial.py` | generated (gitignored) | Trial harness for the 8 GiB run |

---

# TRIAL 2 — WO-20261028-CHROMIUM-APPCONTAINER-TOKEN (`kernelbase!CreateAppContainerToken`)

- **Date:** 2026-10-28 (follow-up to Trial 1 above)
- **Author:** Claude Code (executed on behalf of the Antigravity/Gemini work order)
- **Companion work order:** [`docs/CHROMIUM_APPCONTAINER_TOKEN_WORK_ORDER_20261028.md`](CHROMIUM_APPCONTAINER_TOKEN_WORK_ORDER_20261028.md)
- **Superproject branch / base HEAD:** `codex/chromium-navigation-handoff` @ `8f878c53` (the WO-assignment commit, == origin)

## Trial 2 verdict (TL;DR)

**NEGATIVE — the fix is necessary but not sufficient.** Antigravity's corrected diagnosis was that
Chromium never calls `userenv!CreateAppContainerProfile`; instead it resolves
**`kernelbase!CreateAppContainerToken`** at runtime (`base/win/access_token.cc:753`,
`GetProcAddress(GetModuleHandle(L"kernelbase.dll"), "CreateAppContainerToken")`). In the project Wine
candidate that export was commented out as a stub, so `GetProcAddress` returned NULL and set
`ERROR_PROC_NOT_FOUND`.

Task 2 implemented and exported `CreateAppContainerToken` on top of the existing `NtCreateLowBoxToken`
low-box path — commit `002153a`, pushed to `codex/g4-appcontainer-profile`. The focused repro **confirms
the fix works in isolation**: pre-fix it fails to resolve the export (exit 2); post-fix it resolves the
export, derives the AppContainer SID, and successfully creates a low-box token handle (exit 0). Both
rebuilt `kernelbase.dll` files were deployed to the guest load path and hash-verified.

Yet under one authorized normal 8 GiB browser verification trial, Chromium **still did not initialize the
Network Service on its sandboxed `kNetwork` attempt**: it logged the same
`"Network service crashed or was terminated, restarting service."` line and spawned its **only**
`network.mojom.NetworkService` process with the emergency fallback `--service-sandbox-type=none`. The
loopback fixture was never consumed (`http_fixture.requests=[]`, classification
`"browser consumption NOT_PROVEN"`), so there were no `index.html`/`scene.js` 200s and no page commit.
All orchestration gates passed and the VM shut down gracefully (`qemu_exit_status: 0`).

**Combined with Trial 1, this is now a two-data-point result:** with *both*
`userenv!CreateAppContainerProfile` (Trial 1) **and** `kernelbase!CreateAppContainerToken` (Trial 2)
implemented — and the latter proven to create a real low-box token in isolation — the sandboxed network
service still does not launch. The residual gap is therefore **downstream of token creation**, most
likely the mandatory-label / integrity / SACL step that actually *applies* the AppContainer SID to
constrain the spawned utility process at `CreateProcess` time.

## T2.1 Root-cause hypothesis under test

From Antigravity's decode: at browser start the Network Service is launched under
`SandboxType::kNetwork` (Low Privilege AppContainer). The policy path is

```
sandbox/policy/win/sandbox_win.cc  GeneratePolicyForSandboxedProcess -> SetLowBox("cr.sb.net")
  -> base/win/access_token.cc:753  CreateAppContainerToken(...)   [resolved via GetProcAddress(kernelbase)]
```

In the project Wine candidate, `dlls/kernelbase/kernelbase.spec` had the export commented out
(`# @ stub CreateAppContainerToken`), so `GetProcAddress` returned NULL → `ERROR_PROC_NOT_FOUND` → the
sandboxed network-service policy setup failed before any process spawn → Chromium fell back to
`--service-sandbox-type=none`. The hypothesis: exporting and implementing `CreateAppContainerToken` makes
the **first** (sandboxed `kNetwork`) launch succeed, so the crash/restart line disappears and loopback
navigation commits.

## T2.2 Task 1 — focused repro

Repro source: [`docs/navhandoff-repro-20261028/appcontainer_token_repro.c`](navhandoff-repro-20261028/appcontainer_token_repro.c).
It resolves `kernelbase!CreateAppContainerToken` via
`GetProcAddress(GetModuleHandle(L"kernelbase.dll"), …)`, derives an AppContainer SID
(`DeriveAppContainerSidFromAppContainerName(L"cr.sb.net.test", …)`), and calls
`CreateAppContainerToken(NULL, &sec_cap, &token)`, requiring a non-NULL token handle.

| Platform | Result |
|---|---|
| Project Wine candidate, BEFORE fix (stub) | `GetProcAddress(kernelbase, CreateAppContainerToken) FAILED` → **exit 2** |
| Project Wine candidate, AFTER fix (rebuild) | `Resolved kernelbase!CreateAppContainerToken at 00006fffff353fc0` / `Derived SID successfully` / `SUCCESS: CreateAppContainerToken created token handle 0000000000000034` → **exit 0** |

(Repro compiled with `x86_64-w64-mingw32-gcc -O2 … -luserenv`; exit codes captured in the guest against
the rebuilt Wine during the deploy step.)

## T2.3 Task 2 — the fix (committed + pushed)

- **Commit:** `002153a4bd0c3174540afc2e01955373acc3d6f0` (short `002153a`)
  - Subject: `kernelbase: implement CreateAppContainerToken for Chromium LPAC sandbox`
- **Branch:** `codex/g4-appcontainer-profile` — **pushed** to origin (`d4e4075..002153a`).
- **Source checkout (build tree):** `/var/tmp/wex-g4-token-parent-20261027/wine` @ `002153a`.
- **Changes:**
  - `dlls/kernelbase/kernelbase.spec` line 180: `# @ stub CreateAppContainerToken` → `@ stdcall CreateAppContainerToken(ptr ptr ptr)`.
  - `dlls/kernelbase/security.c`: implemented `CreateAppContainerToken(HANDLE, PSECURITY_CAPABILITIES, PHANDLE)` — validates the SID/handle arguments, defaults a NULL token to `GetCurrentProcessToken()`, and wraps `NtCreateLowBoxToken` (the existing low-box path in `dlls/kernelbase/process.c`) with `TOKEN_ALL_ACCESS`, returning `set_ntstatus(status)`.

### Deployed candidate DLLs (built from `002153a`, per-DLL rebuild of `dlls/kernelbase`)

Deployed into the guest Wine build's load path
`GUEST_WINE_BUILD = /home/debian/wex-g4/wine-control-channel-20261027/build/wow64/dlls/kernelbase/<arch>/kernelbase.dll`:

| Arch | sha256 | size (bytes) | export (mingw `objdump -p`) |
|---|---|---|---|
| `x86_64-windows` | `2b103c0c9aa84bc321d5cb35f3684f765ed5f45088c9f0b48c0e0eb6a3915c83` | 7559265 | `[128] … CreateAppContainerToken` |
| `i386-windows` | `32cd8bfcd37cf610b8fbc3b2048a3718d113199735eef26a2c699bce2a5a1957` | 6463089 | `[127] … CreateAppContainerToken` |

**Retained from Trial 1:** the two `userenv.dll` files were left in place — x86_64
`8a2eec86008001349fcc8dcb24b0918c5fb30457c5e5b8a5e85a79c3e2b6081a`, i386
`c9e63e9e0ef91c211966f0d470b77dacadbdf7cb6f46bef0ec3c3a34c59f20c8`. Both AppContainer fixes are now
active in the guest build.

**Load-path verification:** the guest `sha256sum` of both deployed `kernelbase.dll` files was compared to
the host build and matched (fail-closed). The PE export table was confirmed present via mingw
`objdump -p` — Wine builtin DLLs are PE, not ELF, so `nm`/`readelf --dyn-syms` return nothing on them.

## T2.4 Task 3 — one authorized normal 8 GiB browser verification trial (NEGATIVE)

### Trial contract (all satisfied)

- **Trial name:** `chromium-browser-boundary-20261028` (reused, so the compiled helper PE's baked-in
  output path stays valid). Trial 1 host artifacts were preserved by renaming to the `.t1` suffix.
- **VM:** g4-virgl launcher, QEMU/KVM/q35/virtio-vga-gl (VirGL), `-m 8192` (8 GiB).
- **Disk:** R4 qcow2 overlay on the preserved base layer; prior run evidence and disk images preserved.
- **Wine build:** REUSED `GUEST_WINE_BUILD`; only the two `kernelbase.dll` files were deployed this trial
  (Trial 1's `userenv.dll` retained).
- **Sandbox / security:** normal sandbox enabled (no bypasses, no proxy switches, no fake stubs).
- **Fixture:** private loopback HTTP fixture serving `index.html` + `scene.js`.
- **Shutdown:** graceful QMP powerdown — **`qemu_exit_status: 0`** (`graceful_powerdown: true`,
  `stop_reason: signal`).

### Target criteria vs. actual

| # | Criterion (WO Task 3) | Target | Actual | Met? |
|---|---|---|---|---|
| 1 | Network Service initializes on the FIRST attempt (`utility!kNetwork` succeeds) | yes | **No** — only `--service-sandbox-type=none` spawn observed; no sandboxed `kNetwork` process created | ✗ |
| 2 | No crash/restart log | absent | **Present** — `chromium.log:132` (see below) | ✗ |
| 3 | Loopback navigation reaches fixture: `index.html` 200 | 200 | **Never requested** (`http_fixture.requests=[]`) | ✗ |
| 4 | Loopback navigation reaches fixture: `scene.js` 200 | 200 | **Never requested** | ✗ |
| 5 | Page commits: `has_committed=true` | true | **Absent** (page never committed) | ✗ |
| 6 | `net_error=0` | 0 | Absent (no fixture request / no commit to measure) | n/a |
| 7 | Graceful QMP shutdown `qemu_exit_status: 0` | 0 | **0** (`graceful_powerdown: true`, `stop_reason: signal`) | ✓ |

### Evidence

Evidence directory:
`D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-browser-boundary-20261028\chromium-native-evidence\`

**(a) The crash/restart line is still present** — `chromium.log:132`:

```
[220:224:1029/035323.769:ERROR:content\browser\network_service_instance_impl.cc:652] Network service crashed or was terminated, restarting service.
```

**(b) NetworkService was spawned exactly once, directly with `none`** — `helper.stderr.log`
(`WINEDEBUG=trace:process`). One `CreateProcessInternalW` → `NtCreateUserProcess` pair for
`network.mojom.NetworkService`, at relative t≈68.1 s, both carrying `--service-sandbox-type=none`:

```
851: 68.147:…:CreateProcessInternalW … chrome.exe --type=utility --utility-sub-type=network.mojom.NetworkService --lang=en-US --service-sandbox-type=none …
854: 68.170:…:NtCreateUserProcess … chrome.exe … --type=utility --utility-sub-type=network.mojom.NetworkService … --service-sandbox-type=none … parent (nil) machine 0
```

`StorageService` and `TracingService` launched with `--service-sandbox-type=service`. **No** sandboxed
(`kNetwork`/AppContainer) network-service process was ever created — consistent with the first
(appcontainer) attempt failing *before* a process is spawned, then Chromium logging the crash/restart
line and falling back to `none`. (Identical shape to Trial 1.)

**(c) No AppContainer/token signals in the trace channel used.** A grep of `helper.stderr.log` for
`appcontainer | lowbox | CreateAppContainerToken | SetTokenInformation | TokenMandatoryLabel | NtCreateLowBoxToken | S-1-15 | integrity`
returns **no matches**. This is a **channel limitation, not proof of non-invocation**: the run used
`trace:process`, which does not log token/SID calls. (The repro in T2.2 separately proves the export
resolves and creates a token when called directly.)

**(d) Fixture never consumed** — `results.json`:

```
passed = False
diagnostic_exit = 1
observation_complete = False
url = http://127.0.0.1:37941/c702869f4b3a742004d773a3a400f017/index.html#nonce=c702869f4b3a742004d773a3a400f017
http_fixture.requests = []
http_fixture.classification = "HTTP writes only; browser consumption NOT_PROVEN"
native.outcome = None
```

`netlog.json` (575 815 bytes): the fixture host/port and `index.html` appear **only** as echoes of the
navigation URL in netlog metadata (`37941`×2, `index.html`×1, `scene.js`×0) — there are no actual HTTP
request/response entries for the fixture.

**(e) Orchestration gates all passed** — `execution.json`:
`orchestration_complete=True`, `candidate_reuse_valid=True`, `outside_modules_unchanged=True`,
`diagnostic_nonacceptance_expected=True`, `archive_exit=0`. `consumer_ssh_exit=1` is the expected
diagnostic exit (success is judged from evidence, not the exit code).

## T2.5 Analysis — why the result is negative

The fix resolves the **`kernelbase!CreateAppContainerToken`** gap and is proven to work in isolation
(repro: export resolves, SID derives, low-box token handle created). It is therefore **necessary**. But
it is **not sufficient** for the sandboxed network service to launch:

- Deployment to the load path is verified (guest sha256 == host; PE export table confirmed present). The
  negative result is best characterized as **"fix loaded but insufficient,"** not "fix not deployed."
- **Combined with Trial 1**, both AppContainer APIs Chromium's `SetLowBox("cr.sb.net")` path depends on
  are now implemented, and the token-creation step is proven to succeed in isolation — yet the sandboxed
  network service still does not launch. This localizes the residual gap to **after** token creation: the
  mandatory-label / integrity mechanism that *applies* the AppContainer SID to constrain the spawned
  utility process (e.g. `SetTokenInformation(TokenMandatoryLabel)` carrying the AppContainer SID, and/or
  an SACL-based restriction applied at `CreateProcess`). If Wine still lacks one of those **downstream**
  primitives, policy generation / constrained process creation fails *after* this fix — producing exactly
  the observed behavior: no `kNetwork` process created → crash/restart log → `none` fallback.

## T2.6 Caveats / limitations

1. **Trace channel:** the run used `WINEDEBUG=trace:process`. It cannot observe token/SID calls, so this
   evidence cannot pinpoint which specific downstream API fails after `CreateAppContainerToken`, nor
   directly confirm in-band invocation (the repro confirms it works when called directly).
2. **Single trial:** WO Task 3 authorized *one* normal verification trial; a targeted diagnostic re-run
   with a broader trace channel (`+sandbox,+userenv,+token` and/or Chromium `content/sandbox` VLOG) would
   be needed to localize the next gap and is outside this task's scope.

## T2.7 Recommendations for Antigravity / Gemini

1. **Localize the next gap:** add a broader debug channel (Wine `+sandbox,+userenv,+token`; Chromium
   `content/sandbox` / `base::subprocess` VLOG) and run one more *diagnostic* trial to capture the exact
   failing call after `CreateAppContainerToken`. This needs explicit authorization (it is not a "normal
   verification" trial).
2. **Likely next Wine gap(s):** implement/verify the low-box mandatory-label application —
   `SetTokenInformation` with `TokenMandatoryLabel` carrying an AppContainer SID, and/or the SACL /
   integrity restriction applied at `CreateProcess` for a low-box process — checking each against native
   behavior.
3. **Keep both fixes:** retain the committed `userenv!CreateAppContainerProfile` (Trial 1) *and*
   `kernelbase!CreateAppContainerToken` (Trial 2) on `codex/g4-appcontainer-profile`; they are both
   necessary and neither is sufficient alone.

## T2.8 Reproducibility

- **Trial script** (generated; gitignored): `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer\build\g4-virgl-20261022\chromium-browser-boundary-trial.py` (`mode='browser-boundary'`, `NAME=chromium-browser-boundary-20261028`).
- **Deploy + run harness** (scratch, not committed): `ac_build/t2_deploy_trial.sh`.
- **Repro build** (scratch): `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\appcontainer-token-repro.build\appcontainer_token_repro.exe`.
- **Evidence:** see the evidence directory in T2.4.

## T2.9 Security-constraint compliance

No VM mutation of any kind (no password/SSH/NAT/LightDM/reboot changes); preserved R4 base image and
prior run evidence (Trial 1 host artifacts renamed to `.t1`, not deleted); reused the authorized Wine
build with only the two `kernelbase.dll` files deployed this trial; graceful QMP shutdown
(`qemu_exit_status: 0`); staged explicit filenames only for this commit (this result doc + the wine
gitlink) and no vendor packages / legal text / captures / secrets committed.

## Appendix T2 — file inventory (this work order)

| Path | Status | Role |
|---|---|---|
| `docs/CHROMIUM_BROWSER_BOUNDARY_RESULT_20261028.md` | modified (this commit) | Appended Trial 2 result + top banner |
| `docs/navhandoff-repro-20261028/appcontainer_token_repro.c` | committed earlier on this branch | Task 1 repro PE source |
| wine `002153a…` (`codex/g4-appcontainer-profile`, pushed) | gitlink updated (this commit) | Task 2 fix (`kernelbase.spec`, `security.c`) |
| `build/g4-virgl-20261022/chromium-browser-boundary-trial.py` | generated (gitignored) | Trial harness for the 8 GiB run |
