<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# One owned-thread diagnostic: inventories obtained, readiness still fails

**EXECUTED ONCE, no retry.** Used the unchanged approved
edge-headful-owned-threads-r1-20261023.preflight (SHA2566097d951...; archive9592a287...;
sourcec00f8950). PREPARED_NOT_RUN is retained as preparation history, not current
execution state: execution.json/procedure-exit.json/audit record the actual run.
Report-only tracked change. No debugger/attach/suspend, extra browser, FILE arm,
production/security patch, reinstall/download or changes to other checkouts.
Explicit owned C: workdir; D: master/submodules untouched, unused M wine unstaged.

## Findings and limits

Primary `TimeoutError('No nonce-bound ready browser window')` after unchanged45s
readiness bound. Consumer/diagnostic SSH exit1; procedure0/archive0. Total runner
80.793358263s. Actual1280x720 failure front viewed: full framed Edge URL window,
white client, no original marker/canvases. No ready protocol/fixed pixel samples;
passed=false/rendering NOT_EVALUATED, not a graphics/G4 result.

Observation completed in **0.139243048s**, inside5s, before prefix cleanup, no
native/Linux observation errors. Owner checks held unchanged before/after:

- Native: unique HWND655478, WinPID408, process creation134372105672917660,
  pinned C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe,
  nonce title `127.0.0.1_/115a34fb221a55ce353573932f98aed6/index.html`.
  GUI WinTID412 `CrBrowserMain`;56 owned queried threads. Other observed native
  names include BrokerEvent424, Chrome_IOThread576, COMSTA492/496/596/600/780.
 49 nonempty names/7 empty names. All query_error0. Raw description HRESULT
  (field name_error)268435456/0x10000000 is **SUCCEEDED**, not56 errors.
- Linux, independently: exact PopenPID1012/start6948, retained prefix, full Linux
  image/app URL/fresh profile argv matched before/after;56 verified tasks, no
  unavailable query fields. Observed waits49 pipe_read,1 do_sys_poll,6
  futex_wait_queue. Linux main1012 commCrBrowserMain, wchanpipe_read, syscall0
  read with fd0xa/length0x10. These are wait-file values, NOT a native stack,
  WinTID mapping or identification of the pipe's peer/cause. Equal inventory
  counts/names do not establish cross-domain correspondence.

Raw native records/logs retained. First post-run audit's native_query_errors list
incorrectly grouped all nonzero raw HRESULTs; the separate
independent-evidence-audit-HRESULT-note.json corrects that label. Original evidence
bytes unchanged; native parser/runner never classified positive HRESULTs as errors.

HTTP again recorded EOF/idle preconnects then one complete GET200,637 approved
HTML bytes, no scene.js request/internal errors; listener/worker stopped. Complete
write is not proof of parsing/renderer consumption. One GPU role sampled, no
renderer role or pre-cleanup role exits sampled. Classified error counters0,
trace14,597,197 bytes <128MiB; absence of classified errors is not readiness.
No cause inferred from optional log messages, wait states or unsampled renderer.

## Gates, execution and shutdown

Host inventory11 sources/5 C: helpers/2 seeds/installed reference matched; fresh
outputs and no QEMU/launcher before boot. Live usage67% five-hour/40% weekly,
credits0. Guest free1,913,167,872 bytes BEFORE transfer/create,17 actual binaries
and11 deployed sources matched.650/435 rawUTC guards passed with853.575218/
851.541587s remaining. Inner/outer VirGL passed, host D3D12 passed; private-lo
start/end proof unchanged. Retained payload/registration survived Wineboot -u
(forced update), not a plain OS reboot. On-disk pending-hive observations do not
prove live pending-operation processing.

Exact immutable preflight launch/run/stop commands executed: C: session controller
-RunName edge-headful-owned-threads-r1-20261023; C: private procedure run with
WEX_G4_CONTROL=edge-owned-threads and matching run/boot env names; separate C:
stopper --identity this run's launcher-identity.json --timeout60. Full commands/
argv are in preflight/execution/readiness/adjacent .wsl-session.json. Initial SSH
readiness banner timed out255, next readiness check0; only ONE browser consumer.
No changed time bounds: launcher900s/reserve90s, SSH300s/runner285s.

Identity SIGINT stopper0; stop_reason=signal, QMP graceful_powerdown=true, QEMU0,
qemu_stopped/xvfb_stopped=true. Independent /proc confirms launcher29533,
QEMU29548, Xvfb29535 absent. Windows WSL PID7284 exited0/absent, controller session
16532 returned0. UTC06:28:06.2114807..06:30:46.1408661, well within bound/reserve.
Prefix cleanup wait0, no remaining prefix processes/servers or cleanup errors.

## Evidence and next review

Root `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-owned-threads-r1-20261023/`.
edge-owned-threads-evidence contains owned-threads.json, owned-window-selection and
owned-native-threads stdout/stderr, failure-front.png, all per-native-window,
browser/Wine/process/Chromium/compositor/capture/persistence/probe/cleanup logs.
Host guard/identity/launcher/procedure/archive/stopper records and adjacent Windows
session logs retained. Prefix/profile private in guest, excluded from archive/Git.
All114 extracted execution hashes AND tar members independently verified, zero
mismatches; parent independently viewed front/rehashed114 and checked shutdown.

- Evidence tar SHA256: `38d4f40a19b1f042faa4f707301223fe2cbd769517e63f3bafff87f45f1cb05a`
- Results SHA256: `fd97d3cb61e52ad39bac1c7bfb1826e484830f565ac49ac8bcaa4194d9e5ed91`
- Owned threads SHA256: `89c0d3c1c84a0829dfbcd9074bb3cc5e8f83af41118f7dea0daad03d00ccec70`
- Failure front SHA256: `656e2d9af1f098bce5a4969bb3612d343cbfd25407b2b8bed007717efc7ac112`

No unchanged unit reruns. Stop for parent review; any later selected-browser/
numeric-thread stack observation requires its own bounded ownership/failure plan.
No bt all, global debugger inventory or additional run authorized here.
