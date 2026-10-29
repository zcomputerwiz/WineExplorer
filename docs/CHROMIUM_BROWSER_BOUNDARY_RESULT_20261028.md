# Chromium Browser Boundary — Result (WO-20261028-CHROMIUM-BROWSER-BOUNDARY)

- **Date:** 2026-10-28
- **Author:** Claude Code (executed on behalf of the Antigravity/Gemini work order)
- **Companion work order:** [`docs/CHROMIUM_BROWSER_BOUNDARY_WORK_ORDER_20261028.md`](CHROMIUM_BROWSER_BOUNDARY_WORK_ORDER_20261028.md)
- **Root branch / superproject HEAD:** `codex/chromium-navigation-handoff` @ `4aa3e662` (== `origin/codex/chromium-navigation-handoff`)

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
