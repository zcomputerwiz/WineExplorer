# E1-S14 lab status

## S14b production commit update (2026-11-08; static only)

Added COMMIT_PROCESS=11 on request 325/protocol 979, independent of the crash
define. The original S10 context now carries a server-only lease binding and
must survive LOAD through commit or rollback. Exact issuance, broker ownership,
lease state, alive local process and never-resumed initial suspension gates
are in [the S14b report](E1_S14_LEASES.md#e1-s14b-broker-only-production-lease-commit-2026-11-08).
S18 and the unload lifecycle now use it; TEST_TRANSFER=10 is retired.

The existing 92 PASS S13/S14/S16 baseline is to be rerun by the lab owner.
Added one commit mode per broker ABI, with both fresh LOADs checking accept,
double, wrong-account process, same-SID other issuance, dead, non-broker,
after-unload, running, resuspended, invalid-context and foreign-handle-value
denial. Existing save-fail/post-subst modes add poison denial. thread.o is now
forced with process/token/registry; the running child signals an inherited
event before the denial check. All existing fixture ownership and crash
checkpoint/recovery evidence requirements remain.

No build, tests (including test_broker_e1s18.sh), harness, shell syntax check,
Wine, generator, WSL, VM or service was run for S14b. In addition to both ABIs
and normal-server Wine tests, validate commit in a lab server built without
WEX_E1S14_CRASH_TEST. No commit or production enablement.

The earlier lab history below is unchanged.

Run 2026-11-07 (scripts/test_hku_load.sh in the clean wexbuild; owned lab tree rebuilt from the synced clone):
builds pass (main and normal, protocol 979); S13 cases accept, absent, unprovisioned, wrong-incarnation,
wrong-reference, owner, already-loaded, replay and the S14 unload case (unload with live process denied, unload after the
last process ends, persistence and reload, stale root, double unload) all PASS for the 64-bit broker.

Fixes made while bringing S14 up (all in wine/server/registry.c): a persisted hive already contains Software\Classes
and Wow6432Node and its saved parent keys, so create_key_recursive and load_keys leave STATUS_OBJECT_NAME_EXISTS set
and the reload failed on `if (get_error()) goto failed` after set_value (and in e1s_read_hive). That warning is now
cleared at both places, and after a successful managed load.

Observed failure before the harness fix: the first crash case (crash-1, checkpoint 1 of the durable save, lab-only hook WEX_E1S14_CRASH_TEST in
wine/server/wex_profile_storage.c save_checkpoint, _exit(86)) fails in scripts/lib/e1s13-fixture.sh e1s14_crash_finish:
the crash did happen (server stderr has E1S14 save-crash=1) but the peer (32-bit, Bob) ended with rc=124 (the
`timeout -k 2 $PEER_SECONDS` kill, 90 s) instead of dying with the server. The peer log stops after
"E1S10 PEER MARKER SENT" (waiting for the broker's result byte). The run did not establish the relative timing or
why the blocked client survived server death.

## Static diagnosis and harness correction (2026-11-07, not run)

The crash driver in `scripts/e1s13_hku_checks.h:e1s14_lifecycle` writes Persisted, opens a managed root, arms
WEX_HKU_TEST_CRASH and prints E1S14 CRASH ARMED, then calls RegFlushKey. The arming request only stores
test_crash_step; it does not save or unload. `wine/server/registry.c:flush_key` calls e1s_save_hive directly,
without e1s_unload_hive's process/handle busy checks. The peer is ordinary Bob, whereas the managed hive belongs
to Alice's issued token; Bob's mere presence is not an exact-SID live-user reference to Alice's hive either.

The peer has sent its marker and is waiting in ReadFile. The broker's result WriteFile is after both
e1s13_load_checks calls and cannot run before the crashing flush. `wine/server/wex_profile_storage.c:save_checkpoint`
prints the checkpoint and calls _exit(86) during that synchronous save: checkpoint 1 follows marker/directory
fsync, 2 follows completed temp/directory fsync, and 3 follows rename/directory fsync. No successful unload,
broker result byte, or normal server shutdown follows that exit. The old parent waited for the peer first, so
its rc=124 was observed after the crash, not a prerequisite for starting the save.

The assumption that a peer must exit promptly and nonzero is invalid. A pending pipe read follows
`ntdll/unix/file.c:server_read_file` through wait_async and server_wait_for_object (which explicitly bypasses
fast synchronization) to `ntdll/unix/server.c:wait_select_reply`. That reads wait_fd[0], but init_thread_pipe
retains the client's own wait_fd[1] after sending a copy to the server; wait_select_reply uses that writer to
put stolen wakeups back. Server _exit closes only the server's copy, so this pending wait need not see EOF or
receive a completion. This is a source-level explanation consistent with the timeout; the run has no captured
client stack. Conversely, a client awaiting a synchronous server reply can hit read_reply_data's EOF path,
which calls abort_thread(0). Neither prompt exit nor a nonzero client status is a valid crash oracle. The
adapter may likewise never publish .done.

Corrections:

* Only crash cases prestart an owned, persistent foreground wineserver (`-f -p`), with its own timeout and stderr.
  A signal-0 lock-owner query checks startup before any Wine client can auto-start a competing server. The
  harness waits for this job and requires exit **86**, the matching server checkpoint and broker arm line, no
  watchdog intervention, the S6 positive control and no broker FAIL. No prefix kill precedes this proof. A
  returned RegFlushKey now always fails the probe; a generic error can no longer print SERVER LOST.
* Checkpoint 1 requires an unchanged original hive, an empty regular state marker and no temp. Checkpoint 2
  requires the same original hive plus the completed temp containing Persisted. Checkpoint 3 requires the
  completed published hive, the state marker and no temp. Links are refused.
* The peer's checks through PEER MARKER SENT remain mandatory. Its subsequent status is recorded only for
  client reaping; the existing peer/bootstrap timeout bounds still apply. A blocked peer's timeout is neither
  success evidence nor a reason to reject an independently proved server crash. Only owned fixture jobs and
  prefixes enter cleanup, and hashes must remain unchanged through that cleanup.
* Each checkpoint is still followed immediately by recovery with the same provisioned profile and generation,
  with no reprovisioning or repair. Crash hashes must match the pre-load snapshot; per-case before/after hashes
  are retained separately, so later checkpoints no longer overwrite earlier recovery evidence.

Recovery is a **denial test**, including after checkpoint 3's completed rename. Storage's SID-wide
find_existing/save_recovery check sees either transaction name before hive parsing, security registration or
HKU publication, returning WEX_PROFILE_RECOVERY; registry load maps it to STATUS_REGISTRY_CORRUPT. The failed
attempt still consumes its S10 context, so replay must deny ACCESS_DENIED and a fresh second context must reach
the recovery denial again. The existing two-attempt/replay assertions remain. Added assertions require zero
loaded generation for the still-managed token, absence of both HKU SID and SID_Classes, failed impersonated
RegOpenCurrentUser with no handle, two E1S14 RECOVERY DENIED lines, and unchanged surviving user.reg/state/temp
hashes after recovery shutdown. There is no automatic recovery or claim that the next load succeeds.

Unverified: all compilation/linking, both broker/peer ABI pairings, foreground startup/status propagation in
wexbuild, all three injected crashes and subsequent recovery runs, and a runtime stack of the blocked client.
Only static source/diff review and shell syntax checks are applicable to this fix; no build, harness, Wine,
WSL, VM or service was run, and no commit was made.

## Crash-case rerun after the harness change (2026-11-07 16:30)

All earlier cases pass (accept, absent, unprovisioned, wrong-incarnation, wrong-reference, owner, already-loaded, replay,
unload). The first crash case (crash-1, case 10) now fails earlier, before any crash: `FAIL: broker readiness E1S10 READY
(/var/lib/wexsys-s10/case-10/broker.log)`. Evidence in /var/tmp/e1s10-results.*/lab: fixture/case-10-command.txt exists
(broker command line fine), case-10-broker.log is empty (the broker never printed anything, not even START), the
foreground wineserver's stderr (case-10-wineserver.stderr.log) is empty, the peer log is absent, hive.before and the
storage log exist. The new e1s14_server_start in scripts/lib/e1s13-fixture.sh starts `wineserver -f -p` as Bob through
run_as before the bootstrap; the ordinary cases let the first Wine client auto-start the server. Suspects: the server
started by run_as does not get the same environment (WINEPREFIX, WINESERVERNAME or the socket directory under
/tmp/.wine-UID, WINEDEBUG, DISPLAY, the lab build's wineserver versus `$BUILD/wine` loader pairing), so the clients
start a second server or cannot connect; `-k0` as readiness probe may succeed against a different instance; or the
timeout wrapper's -k kills it; or the bootstrap `service_start` expects to own the server (stop_prefixes before it).

## Foreground startup correction (static only, not run)

Cause: the foreground helper bypassed the **setgid system-role launcher**. In an ordinary case,
`service_start` first stops the owned prefixes, restores/stages the machine hive and service, then starts
`roles64.exe hold` through `run_as`. That supplies Bob's home, UID/groups and prefix, PATH=/usr/bin:/bin,
LANG=C, DISPLAY, WINEDEBUG=-all and the fixture's WINEDLLOVERRIDES, with no inherited environment.
`ntdll/unix/loader.c:start_server` does not normally exec a plain Bob wineserver here: it calls
`sysrun_spawn("wineserver")`. The installed `wex-sysrun` starts the configured lab server with Bob's UID
and real group, but the fixture system group as its effective and saved groups. Its `build_env` then
reduces the server environment to HOME, USER, LOGNAME, PATH, WINEPREFIX and LANG=C; it removes the client
DISPLAY/debug/DLL override variables too. The configured loader, ntdll and server come from the same BUILD.

The direct `run_as ... wineserver -f -p` had Bob's group for all three group IDs. Therefore
`server/sysrun.c:sysrun_init` returned before setting `sysrun_active`, and the server could not provide the
system role. More decisively, the bootstrap's `wineboot-init` launcher calls `connect_server`/`server_ok`,
which requires Bob's UID and real group plus the system **saved** group. It refuses this plain server.
`ntdll/unix/server.c:server_connect` independently enforces the same trust condition for system-group
clients. The skeleton's system.reg is a link to the machine hive, so absence of service registry bytes is
not established by this failure; the missing trusted system-role server is the startup contract violation.

The bootstrap waits in `ntdll/unix/env.c:run_wineboot` for
`\KernelObjects\__wineboot_event`: first machine wineboot through the launcher (or its exit/refusal), then
ordinary user wineboot (event or process exit). These internal waits can exceed the fixture's outer bound.
The harness separately waits for an exact `E1S10 READY` broker log line before starting the peer.
`wineserver -k0` only queries/signals the lock holder with signal zero; it proves neither this identity nor
boot/service readiness. `service_start` stops prefixes **before** starting the foreground job, so it does
not itself kill the new server. The timeout remains a guard, not proof of an injected crash.

Changes in `scripts/lib/e1s13-fixture.sh`:

* Crash startup uses the already-root fixture's isolated Python child to initialize Bob's supplementary
  groups, set real/effective/saved groups to Bob/system/system, permanently drop all three UIDs to Bob,
  check the resulting IDs, and exec the same protected BUILD/server/wineserver with `-f -p`. It refuses
  no_new_privs just as the ordinary launcher does, and refuses a Bob identity that belongs to the system
  group. The child reproduces the launcher's server environment, cwd=/, umask=022 and closed extra FDs.
  This is confined to crash cases in the root-owned lab fixture; no installed launcher mode, production
  source or identity check changes. The ordinary bootstrap/peer environment and server path remain intact.
* The timeout job remains directly waitable: Python execs the foreground server, so the existing exact
  exit-86 assertion remains. Launch IDs/prefix/image are recorded in server-CASE.log, separately from
  server stderr and bootstrap stderr. The prefix lock query, all timeout bounds, arm/checkpoint lines,
  S6 control, no-FAIL requirement and ownership-scoped cleanup remain mandatory.
* Recovery hashing now checks `-L` as well as `-e` before requiring a regular non-link file, so a dangling
  transaction symlink cannot evade evidence validation.

Rechecked the rest of the driver against `e1s14_lifecycle`, `flush_key`, `wex_profile_save`,
`token_claim_e1s_profile` and the storage/load paths:

* Arming only stores the requested step. RegFlushKey synchronously saves the managed hive, without the
  unload busy gates; any returned flush still fails. The broker cannot send the peer's result byte until
  after these checks, and the peer's assertions through MARKER SENT remain required.
* Step 1 exits after the empty marker and directory fsync, before temp creation: original hive hash
  unchanged, marker present/empty, no temp. Step 2 exits after completed temp and directory fsync:
  original hash unchanged, marker present/empty, temp containing Persisted. Step 3 exits after rename and
  directory fsync, before marker removal: published hive containing Persisted, marker present/empty,
  no temp. All require the matching server line and actual foreground status 86 before cleanup.
* Each crash is immediately followed by recovery without provisioning, deletion or generation changes.
  The surviving transaction hashes must match before load, after client reaping, and after recovery
  shutdown. Storage's SID-wide transaction check denies all three checkpoints, including step 3, with
  REGISTRY_CORRUPT before hive parsing, file security registration or HKU publication. Each consumed
  context's replay must deny ACCESS_DENIED; a fresh second context must independently reach recovery
  denial. Both attempts require zero loaded generation, no SID or SID_Classes root, failed impersonated
  RegOpenCurrentUser with no handle, and two RECOVERY DENIED/HKU CHECK OK lines overall. Client timeout
  status and adapter .done publication are still not crash evidence.

Verification is static source review and `git diff --check` only. No build, shell/harness execution,
Wine, service, WSL or VM run, and no commit. The startup correction, exact foreground status propagation,
both broker/peer ABI pairings and all three crash/recovery sequences remain unverified in wexbuild.
