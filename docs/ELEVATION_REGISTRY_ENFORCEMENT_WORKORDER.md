<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: registry and service manager access checks under enforcement

Date: 2026-10-22. Status: **done**. Items 1 to 5 implemented; the services blocker was resolved by the
user's decision below ("Results, grant of ServiceCurrent and ActiveComputerName"). Part of `ELEVATION_BOUNDARY_PLAN.md`; prerequisite of the system role (step 2b
work order, item 5 and decision D2).

## Why

With a helper configured, a limited process now has a real filtered token (Administrators deny-only, Medium,
three privileges). The registry ignores it: the server skips the access check for any key without an explicit
descriptor (`check_object_access` returns success when `obj->sd` is NULL, `server/token.c`), and `create_key`
grants the new handle with `alloc_handle_no_access_check`. So a limited process can write `HKLM`, including
`HKLM\System\CurrentControlSet\Services`, and the service manager only checks handle access masks
(`programs/services/rpc.c`), which it grants as requested. A limited process can therefore register a
LocalSystem service. Until both are closed, no process may be given the system role.

## Native expectations (`TOKEN_SECURITY_REFERENCES.md`, D1 section)

Filtered token on Windows 10.0.26300:

| Operation | Result |
| :--- | :--- |
| Create a key under `HKCU` | succeeds |
| Create a key under `HKLM\SOFTWARE` | `ERROR_ACCESS_DENIED` |
| `reg add HKLM\SOFTWARE\Microsoft /v X ...` (set value) | `ERROR_ACCESS_DENIED`, `reg` exit 1 |
| Open `HKLM\SOFTWARE` for `KEY_READ` | succeeds |
| `OpenSCManager(SC_MANAGER_CREATE_SERVICE)` | `ERROR_ACCESS_DENIED` (documented SCM descriptor: Authenticated Users connect, enumerate, query lock status, read; LocalSystem the same plus modify boot config; Administrators all access) |
| `ChangeServiceConfig` on an existing service | `ERROR_ACCESS_DENIED` (documented default service descriptor: Authenticated Users only query, enumerate dependents, interrogate, user-defined control and read control) |

The SCM rows come from Microsoft's documented default descriptors ("Service Security and Access Rights"); a
worker run can confirm the codes later.

## Scope

Everything is gated on `elevation_enforced()`; with the helper unset, behavior must stay byte-for-byte what it
is today (the unset-helper suites prove it).

1. **Diagnose the earlier hang first.** The trial patch (`/tmp/wexout/registry-enforce-trial.patch` in the
   build container; reproduce it from the description in the step 2b work order if missing) used `key_get_sd`
   for keys without a descriptor in `check_object_access`. `wineboot -u` then never finished and `wineserver`
   spun at 40% CPU. Reapply it on a scratch branch, run `wineboot -u` with a helper configured and
   `WINEDEBUG=+reg,+server` (or `+reg` alone if too large), and record: which process loops, which key and
   access it is denied, and why it loops instead of failing. Expected cause: `wineboot -u`, `services.exe` and
   the `rundll32 setupapi` INF pass all run on the limited token and must write `HKLM`, as a Windows setup
   process running as SYSTEM or TrustedInstaller would. Write the finding into "Results" before item 2.
2. **Decide how the prefix update runs** (main session decides from item 1's data, not the agent). The prefix
   update writes `HKLM` by design. Options to evaluate and report, not implement yet:
   - (a) `wineboot -u` started from Unix gets the full token for its process tree. Weakness: a limited
     process can run Unix programs (`start /unix`), and a fresh Unix-launched `wine` is a new root process, so
     the server cannot tell the user's shell from a limited process; this would hand out the full token.
   - (b) Prefix updates only in the machine root, run by its root-owned setup (`setup-machine-root.sh
     --update`), which already writes the registry files as Unix root outside any Wine session; user-writable
     prefixes are updated with the helper unset.
   - (c) The prefix update asks for elevation through the broker once it exists (plan step 4).
   Recommendation to check against the data: (b) now, (c) later.

   **Decided (2026-10-22), from the item 1 data:**
   - Prefix update: (b) now, (c) once the broker exists. User-writable prefixes are updated with the helper
     unset.
   - Display device cache (deviation, accepted by the user): win32u's `prepare_devices`
     (`dlls/win32u/sysparams.c`) rewrites `HKLM\System\CurrentControlSet\Enum\DISPLAY`, `Enum\PCI` display
     entries, the display and monitor `Control\Class` keys, `HARDWARE\DEVICEMAP\VIDEO` and the DirectX key on
     every start of a GUI process, which on Windows SYSTEM components do. The server's default descriptor for
     exactly those keys and their subkeys grants Users full control (like `key_in_user_tree` selects the user
     descriptor). Effect: a limited process can change the cached display configuration. Listed in
     `STOCK_CHANGES.md` as a deviation.
   - `reg_empty_key` gets a failure exit (stops when a delete fails), so a denied delete is an error, not a
     hang. Upstreamable.
   - Callers that open `HKLM` keys with `KEY_ALL_ACCESS` (or `MAXIMUM_ALLOWED` and then write) only to read
     ask for `KEY_READ` instead (upstreamable; Windows would deny them too). Callers that really write `HKLM`
     at runtime as a limited process are listed, not widened.
3. **Key access checks.** With the agreed design from item 2:
   - `check_object_access` uses the key's effective descriptor (`key_get_sd`, explicit or default) for keys
     when enforced;
   - `create_key` checks `KEY_CREATE_SUB_KEY` on the parent before creating a new key, and checks the
     requested access on an existing key it opens (today both use `alloc_handle_no_access_check`);
   - value writes and deletes already go through handle access masks, so they follow once the handle access
     is checked; prove it with the probe.
   Native rows above become probe checks (new `--registry-enforcement` mode in
   `scripts/test_token_security_regressions.cpp`, run in the limited-shape prefix by `check-token-security.py`,
   marker `registry_enforcement=PASS|FAIL`), plus `HKCU` writes still succeed and `HKLM` reads still succeed.
4. **Service manager access checks** (`programs/services/rpc.c`): give the SCM and each service a security
   descriptor matching the native defaults, and check the requested access with `AccessCheck` against the
   caller's token (impersonate the RPC client) when opening the SCM, creating a service, and opening a service.
   Probe: a limited process gets `ERROR_ACCESS_DENIED` for `OpenSCManager(SC_MANAGER_CREATE_SERVICE)` and for
   `OpenService(SERVICE_CHANGE_CONFIG)`; `OpenSCManager(SC_MANAGER_CONNECT)` and enumeration still succeed.
   Wine-side unset-helper behavior unchanged. Every change goes into `STOCK_CHANGES.md`.
5. **Breakage sweep under enforcement:** the step 2b item 6 list (`wineboot -u` under the item 2 design,
   `regedit` reading `HKLM`, `notepad`, `taskmgr` where scriptable), the B4 smoke test with `WEX_B4_HELPER=1`,
   and a 32-bit program writing `HKCU`.

## Acceptance

- Unset helper: full harness (`--all`, run from a directory the test user can write) with the known counts:
  `advapi32 security` 0 failures, `ntdll info` 0, `shell32 shlexec` the known 2, consent PASS; runner sentinel
  PASS; B4 smoke test PASS.
- Helper configured, both architectures: `limited_token_shape=PASS`, `linked_token_routes_closed=PASS`,
  `registry_enforcement=PASS`, the service manager probe PASS.
- No hang: every run finishes within the harness timeouts.

## How the work is split

Planning and review: the main session. Implementation: a Sonnet agent, item 1 first, then stop and report;
items 3 to 5 after the main session records the item 2 decision here.

## Results

### Item 1: diagnosis of the hang (2026-10-22)

Setup: the trial patch applied cleanly to `server/token.c` (scratch branch `trial-registry-enforce` in
`wine-dev`, local only, never pushed), WoW64 build, fresh prefix, `wineboot -i`, helper set, then
`wineboot -u` under `timeout 300` and `WINEDEBUG=+reg,+server,+process` as the user `wextest`. Control: the same
sequence without the patch finishes in 13 s with exit 0.

**The looping process is `explorer.exe /desktop`** (the second instance, pid 00b4, thread 00b8), not
`wineboot`, `services.exe` or `rundll32 setupapi`; those sit at 0% CPU while it loops. `wineserver` runs at about
49% and the explorer at about 37%. In the first 600 MB of the log thread 00b8 issues 1.30 M `enum_key`, 0.98 M
`open_key`, 0.98 M `close_handle` and 326 k `delete_key` requests, every `delete_key` answered `ACCESS_DENIED`;
all other threads together issue 17 k requests. The loop never ends because nothing in it changes.

**Denied loop key:** `HKLM\System\CurrentControlSet\Enum\DISPLAY\Default_Monitor\0000&0000\Device Parameters`
(`delete_key` needs `DELETE`, 0x10000). The caller opened it with `access=02000000` (`MAXIMUM_ALLOWED`); the
default key descriptor (`key_get_sd`, `server/registry.c:921`) gives Users only `KEY_READ`, so the granted mask
has no `DELETE` for the filtered token.

**Root cause (caller source):** `reg_empty_key` in `dlls/win32u/sysparams.c:825-826`:

```
while (!NtEnumerateKey( hkey, 0, KeyNodeInformation, key, sizeof(buffer), &size ))
    reg_delete_tree( hkey, key->Name, key->NameLength );
```

It always enumerates index 0 and ignores the result of `reg_delete_tree`, which returns FALSE when
`NtDeleteKey` fails (`dlls/win32u/font.c:652-669`). With a key that cannot be deleted, index 0 is the same key
forever. It is called from `prepare_devices` (`sysparams.c:891`, `reg_empty_key( enum_key, "DISPLAY", FALSE )`;
further calls at 894, 900, 905, 911 for `Control\Class\{monitor}`, `DEVICEMAP\VIDEO`, `Class\{display}` and the
DirectX key) on every display-cache rebuild. So this is not part of the prefix update: every `explorer.exe`
start, in any prefix with the helper set, would hang once the check is enforced on keys without a descriptor.
The expected cause in item 1 (prefix update processes failing) is only half right: they are denied too (table
below) but fail and go on; only the unchecked-return delete loop spins.

**Denied requests** (all `STATUS_ACCESS_DENIED`; the same two or three rows appear in every process at startup,
and processes continue):

| Process | Request | Key | Access |
| :--- | :--- | :--- | :--- |
| every process (start, wineboot, services, winedevice, plugplay, svchost, rundll32, explorer) | `open_key` | `HKLM\Software\Microsoft\Windows NT\CurrentVersion` and `...\Windows\CurrentVersion` | `0xf003f` |
| every process | `create_key` | `HKLM\System\CurrentControlSet\Control\Nls`, `HKLM\Software\Microsoft\Windows NT\CurrentVersion\Time Zones` | `0xf003f` |
| `wineboot --init` | `set_key_value` x5 | `Session Manager\Environment` (`PROCESSOR_*`, `NUMBER_OF_PROCESSORS`) | handle |
| `wineboot --init` | `create_key` | `Services\Tcpip\Parameters`, `ProfileList` (x6), `SQMClient`, `RunOnce`, `RunServicesOnce` | `0xf003f` |
| `wineboot --init` | `open_key` | `Session Manager`, `Session Manager\KnownDLLs` | `0xf003f` |
| `wineboot -u` | `create_key` | `Hardware\Description\System`, `ActiveComputerName`, `Services\Tcpip\Parameters`, `RunServicesOnce` | `0xf003f` |
| `wineboot -u` | `create_key` | `PerfStats\StatData`, `Config Manager\Enum` | `0x20006` |
| `services.exe`, `winedevice.exe` | `open_key` | `CurrentControlSet\Enum\`, `HARDWARE`, `Software` | `0xf003f`, `0xf013f` |
| `plugplay.exe` | `set_key_value`, `delete_key_value` | value `Noto Color Emoji` | handle |
| `explorer.exe` (both) | `open_key` | `System` (under the machine root) | `0x102` |
| `explorer.exe` (second) | `delete_key` (looping) | `Enum\DISPLAY\Default_Monitor\0000&0000\Device Parameters` | `DELETE` |

The log was capped at 600 MB, so denials after the loop starts are unknown (the later `reg_empty_key` calls on
`Control\Class\...` and `DEVICEMAP\VIDEO` were not reached; `create_key` on `DEVICEMAP\VIDEO` succeeded because
`create_key` does not check access).

### Item 4: limited `reg.exe` with the trial patch

Prefix finished `wineboot -u` without the patch, helper set, server built with the patch; `reg` is the 64-bit
`reg.exe`, the 32-bit run used `C:\windows\syswow64\reg.exe`. Results are identical for both. Exit codes from
`WINEDEBUG=-all` runs.

| Command | Exit | Message | Native |
| :--- | :--- | :--- | :--- |
| `reg add HKCU\Software\WexProbe /f` | 0 | The operation completed successfully | 0 |
| `reg add HKLM\Software\WexProbe /f` (key does not exist) | 0 | The operation completed successfully | 1 |
| `reg add HKLM\Software\WexProbe /f` (key exists) | 1 | Unable to access or create the specified registry key | 1 |
| `reg add HKLM\Software\Microsoft /v WexX /d 1 /f` | 1 | Unable to access or create the specified registry key | 1 |
| `reg query HKLM\Software\Microsoft` | 0 | lists subkeys | 0 |

So the patch denies opens of existing keys and value writes, but creating a new `HKLM` key still succeeds:
`create_key` still uses `alloc_handle_no_access_check` (item 3 must check `KEY_CREATE_SUB_KEY` on the parent).
Unexplained: in 2 of the first 4 batches of probe runs every `HKLM` write succeeded (also on existing keys and
for value writes); all later runs, including 6 spaced 5 to 30 s apart after the server exited, were denied as
above. The helper value was present throughout. Not reproduced, so not diagnosed; it should
be rechecked with the probe, because a fail-open window would defeat the enforcement.

### Evidence for item 2

- (a) `wineboot -u` with the full token: not tested here. The server filters every root process (no parent) when
  enforced (`server/process.c:674`), so a Unix-launched `wineboot` is indistinguishable from any other limited
  Unix-launched process; the weakness in the work order stands.
- (b) Machine-root-only updates: consistent with the data (the unpatched control finishes in 13 s with the helper
  set, and the prefix-update writes are a large set: Environment, ProfileList, Tcpip, RunOnce, Hardware, Config
  Manager, fonts, display devices). It does not cover explorer: `prepare_devices` writes `HKLM` (Enum, Control,
  DirectX) on every explorer start, not only during a prefix update, so under item 3 explorer needs either a fix
  to `reg_empty_key` (stop on a failed delete, advance the index) plus a writable location or exemption for the
  display-device keys, or the display-device tree must be created by the system role. This applies to every
  prefix with the helper set, the machine root included.
- (c) Broker elevation: the denied set above is the list of what an elevated update would need; nothing in the
  data argues against it. The same applies to the display-device writes.
- Whichever option is chosen, `reg_empty_key` (`sysparams.c:825`) needs a failure exit; without it any
  `HKLM`-denied delete turns into a hang at 40% CPU instead of an error.

### Results, fail-open recheck (2026-10-22)

The intermittent `HKLM` write successes in item 1 are not a token fail-open. A probe (`tp64.exe`, `tp32.exe`)
printed the process token and tried `RegSetValueEx` on the existing `HKLM\Software\Microsoft`, from a fresh
server each run (alternating `wineserver -w` and `-k`, 0 to 9 s apart), as `wextest` with the helper set:

| Build | Runs (x64 + x86) | Token | `HKLM` write |
| :--- | :--- | :--- | :--- |
| `ccr-work`, no trial patch | 24 | always Limited, Medium (`0x2000`), Administrators `0x19` (deny-only) | 0 (succeeds, no enforcement yet) |
| trial patch | 40 | same, every run | 5 (`ERROR_ACCESS_DENIED`) every run |

Cause of the earlier successes: `reg add` to a key that does not exist yet. `create_key` creates the key and
returns its handle through `alloc_handle_no_access_check`, so the creating process holds full access and its value
write succeeds; the next `reg add` to the now-existing key is denied (reproduced: first run exit 0, second exit 1).
The item 1 batches wrote to new key names, which explains the pattern. Item 3's `KEY_CREATE_SUB_KEY` check on the
parent, with the requested access checked on the new key's descriptor, closes it.

### Results, items 3 to 5 (2026-10-22)

Wine branch `ccr-work` (pushed as `ccr-08223ee4-p830bf`), on top of `b4a3861b`. WoW64 build, both architectures,
unprivileged `wextest`. Stock changes are in `STOCK_CHANGES.md` ("Elevation boundary: registry and service manager
access checks").

| Commit | Item | Change |
| :--- | :--- | :--- |
| `91b53ca8` | A | `reg_empty_key` stops at the first failed delete (unconditional) |
| `c28aafba` | B, C | default descriptor for the display cache keys; `check_object_access` uses `key_get_sd`; `create_key` checks `KEY_CREATE_SUB_KEY` on the parent |
| `437525d3` | D | read-only `HKLM` callers ask for `KEY_READ` (ntdll version, kernelbase locale), shell32 `ProfileList` fallback, explorer driver key |
| `2487739e` | E (needed) | `FSCTL_PIPE_IMPERSONATE` impersonates the pipe client's token while enforced |
| `9bb835d1` | E | SCM and service default descriptors, `AccessCheck` with the RPC client's token in `OpenSCManager`, `OpenService`, `CreateService` |

**Things the work order did not expect**

1. `create_key` on an existing key already used `alloc_handle` (checked); only a new key used
   `alloc_handle_no_access_check`. So only the parent check was added; the new key's handle is still granted
   without a check (the creator holds `KEY_CREATE_SUB_KEY` on the parent; checking the new key's default
   descriptor would leave the key behind when it fails).
2. Wine's `FSCTL_PIPE_IMPERSONATE` impersonates the *server's own token* ("FIXME: use the client token"), so
   `RpcImpersonateClient` could not return the client. Fixed in `server/named_pipe.c`, enforced only.
3. kernelbase's `create_key` fallback (missing parent chain) opens every existing parent with the *requested* access.
   `RegCreateKeyEx(HKLM, "System\...\Video\{guid}\0000", KEY_SET_VALUE)` therefore fails on `System`, even though
   the target is writable. explorer's driver key now asks for `MAXIMUM_ALLOWED`. The same applies to any limited
   caller creating a deep path under `HKLM` (it is denied anyway there, but `reg add HKLM\...` reports it from the
   parent open, not from the create).
4. A prefix that has only had `wineboot -i` is not updated (no `Svchost` key, no services registered). Under
   enforcement it cannot be updated afterwards (decision (b)). `check-token-security.py` now runs `wineboot -u`
   in the limited prefix before it sets the helper.

**Display cache keys** (`display_cache_paths` in `server/registry.c`, Users full control, with subkeys; relative to
`\Registry\Machine`): `System\ControlSet001\Enum\DISPLAY`, `...\Enum\PCI`, `...\Control\Class\{4D36E968-...}`
(display), `...\Control\Class\{4D36E96E-...}` (monitor), `...\Control\Video`, `...\Control\DeviceClasses`,
`System\ControlSet001\Hardware Profiles\Current`, `HARDWARE\DEVICEMAP`, `Software\Microsoft\DirectX`. A new key
above one of them (`HARDWARE`, `HARDWARE\DEVICEMAP` after a server restart, since they are volatile) may be created.
This is slightly wider than the list in the "Decided" block: `Control\Video` (the per-GPU symbolic links),
`Control\DeviceClasses` (`link_device`) and `Hardware Profiles\Current` (source and config links) are written by
`prepare_devices` and the functions it calls on every start as well.

**Read-only `HKLM` callers changed to `KEY_READ`:** `ntdll/version.c` (`CurrentVersion` x2, `Control\Windows`,
`Control\ProductOptions`: Windows version, service pack, product type), `kernelbase/locale.c` (`Control\Nls`, `Time
Zones`, and the time zone lookups under them). `shell32/shellpath.c` `_SHOpenProfilesKey` falls back to a
`KEY_READ` open when the `KEY_ALL_ACCESS` create is denied (it writes defaults when a value is missing, so it
cannot simply ask for `KEY_READ`).

**Callers that still write `HKLM` at runtime as a limited process (listed, not widened):**
`services.exe` (`Control\ServiceCurrent`, volatile, at start: **it exits with code 5**); the processes services.exe
starts (`OpenService(SERVICE_SET_STATUS)` and `GENERIC_READ|GENERIC_WRITE` on their own service, see below);
every process at start (`kernelbase/locale.c` writes `Control\TimeZoneInformation`, and `Control\Nls\Codepage`
when the locale changed); `wineboot --init` (`Session Manager\Environment` values, `ActiveComputerName` (volatile,
so rpcrt4 later logs "Failed to retrieve the computer name, error 2"), `Hardware\Description\System`, `Tcpip\Parameters`,
`SQMClient`, `RunOnce`, `RunServicesOnce`, `ProfileList`, `\Registry\DynData`, opens `Session Manager` and
`KnownDLLs` with `KEY_ALL_ACCESS`); win32u's font code (`Noto Color Emoji` value set and delete, in `winedevice`
and `plugplay`). `wineboot -u` additionally has the `setupapi` INF pass, which opens thousands of `HKLM\Software\Classes`
keys with `DELETE` (denied, it goes on).

**Design call needed (stopped here).** With the helper set, `services.exe` runs on the limited token and cannot create
`Control\ServiceCurrent`; it returns the error and exits (`wineboot` logs "Unexpected termination of services.exe -
exit code 5"). Every SCM call then fails with 1722. Adding `System\ControlSet001\Control\ServiceCurrent` to
`display_cache_paths` lets services.exe start, and the probe and the full harness then pass (table below), but that
widens access for a key outside the accepted deviation, so it is **not committed**. Even with it, item 4 makes the
service processes that services.exe starts (`winedevice`, `svchost`, `rpcss`, `plugplay`, `mountmgr`...) fail:
sechost opens the process's own service with `SERVICE_SET_STATUS` (a Wine-only right, `0x8000`) and
`GENERIC_READ|GENERIC_WRITE`, which the native default descriptor does not give a limited token or LocalSystem
(`SERVICE_CHANGE_CONFIG`). Result under enforcement: "failed to open service Winedevice1", MountMgr/NDIS "failed to
start: 1053" (30 s timeouts each), "Failed to open RpcSs service" in `ole32 start_rpcss` (needs `SERVICE_START`).
This is what the system role is for: services.exe and its service processes need the system token, and the SCM
descriptor needs a way to identify a service's own process. Suggested order: system role first, then these checks stay on.

**Test results** (focused harness lines are from `check-token-security.py --all --focused`; the registry and service
rows pass only with the local, uncommitted `ServiceCurrent` entry):

| Check | Result |
| :--- | :--- |
| Full harness, helper unset (`--all`), both architectures (local `ServiceCurrent` entry present; irrelevant with the helper unset) | `advapi32 security` 3728 / 0; `ntdll info` 2811 (x86_64) and 2744 (i386) / 0; `shell32 shlexec` 1805 / 2; consent PASS |
| Focused, helper set, both architectures, with the `ServiceCurrent` entry | `limited_token_shape=PASS`, `linked_token_routes_closed=PASS`, `registry_enforcement=PASS` |
| Focused, helper set, committed state (no `ServiceCurrent`) | `limited_token_shape=PASS`; `registry_enforcement=FAIL` on x86_64: the registry rows pass, `FAIL: services: OpenSCManager(SC_MANAGER_CONNECT) succeeds (error 1722)`, `OpenSCManager(SC_MANAGER_CREATE_SERVICE) is denied (error 1722)`, `OpenSCManager(SC_MANAGER_ENUMERATE_SERVICE) succeeds (error 1722)`, `EnumServicesStatusEx succeeds (error 1722)`; the run stops there |
| Runner sentinel | success, failure, timeout, signal PASS |
| B4 smoke test, helper unset | PASS, 36 s |
| B4 smoke test, `WEX_B4_HELPER=1` | PASS, no hang, but 2 min 18 s (MountMgr/NDIS 1053 timeouts); log lines: "Auto-start service NDIS failed to start: 1053", "Failed to open RpcSs service" |
| `wineboot -u` under enforcement (updated prefix, helper set) | finishes, exit 0, 63 to 69 s (about 13 s without the helper); denied writes as listed above |
| `regedit /e` of `HKLM\Software\Microsoft\Windows\CurrentVersion` under enforcement | exit 0, 799796 bytes |
| `notepad`, `taskmgr` under enforcement | start and keep running (12 s), `taskmgr` prints only the usual fixmes; `notepad` logs "Failed to open RpcSs service" |
| 32-bit program (`hkcu32.exe`) under enforcement | HKCU create and set value succeed; new key under `HKLM\Software` gives 5 |
| `cmd /c reg add HKCU\...` from `syswow64\cmd.exe` | exit 0 |

### Results, review fixes (2026-10-22)

Wine commits `a252e52b` (pipe impersonation level) and `d668329f` (service process access); parent commits for the
probe (`--pipe-impersonation`) and the B4 script.

1. **Pipe impersonation (blocking).** `token_impersonation_level()` in `server/token.c` follows SeTokenCanImpersonate:
   a server whose process token lacks an enabled `SeImpersonatePrivilege` gets identification level unless the client
   has the server's user, its integrity is not higher, and it is not elevated (Full) while the server is not. The
   requested SQOS level is **not** plumbed (the create_file request carries no SQOS; it would need a protocol change),
   so the cap is the client token's own level; marked with a `shortcut:` comment. Needed a second fix: `open_token`
   with `OpenAsSelf` checked access with the impersonated identification token and failed (1346), so it now checks
   against the process token. Probe `pipe_impersonation=PASS` (x86_64 and i386): a Medium client thread against a
   server lowered to Low gets level 1 (identification); equal integrity gets level 2 (impersonation). The harness
   requires it.
2. **Services under enforcement.** Findings:
   - `Control\ServiceCurrent` is **not** informational. services.exe writes the next pipe number into it for each
     service process it starts and sechost in the service process reads it to find `NtControlPipe<N>`. Without the key
     no service process can be reached, so tolerating the failure would break every service. A limited services.exe
     cannot create it without a grant, and I kept the key out of `display_cache_paths` as ordered. **This is the one
     remaining blocker**: committed state still fails `registry_enforcement` with 1722 on the SCM rows. Options:
     the one-line table entry, or run services.exe on the system token (the system role).
   - Own-process rights: done (`own_process_access` in `programs/services/rpc.c`, pid from
     `I_RpcBindingInqLocalClientPID`, which needed an `ncacn_np` implementation in rpcrt4). sechost's second handle now
     asks only `SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG`.
   - `RpcSs` (started on demand by `ole32`/`combase` with `SERVICE_START|SERVICE_QUERY_STATUS`) is openable for start
     and status by anyone; native has it running from boot.
   - With the local `ServiceCurrent` entry, winedevice, plugplay, mountmgr, eventlog, svchost start, and the NDIS 1053
     and "Failed to open RpcSs service" errors are gone. Left: "Failed to retrieve the computer name, error 2" from
     rpcrt4 (`ActiveComputerName` is written by `wineboot --init`, denied), harmless.
3. `test_b4_explorer_fresh.sh` runs `wineboot -u` before it sets the helper.
4. **Display table lookup cost.** `wineboot -u` with the helper set, same prefix, server restarted per run: with the
   lookup 13.6 to 15.8 s; with the lookup skipped (every key treated as display cache) 17.6 s; helper unset about 13 s.
   The lookup is not measurable against noise, so no cache is needed now. B4 with `WEX_B4_HELPER=1`: 56 s including
   the 14 s `wineboot -u`, down from 2 min 18 s; helper unset 36 s.

| Check (local `ServiceCurrent` entry, uncommitted) | Result |
| :--- | :--- |
| Full harness `--all`, both architectures | limited shape, registry, pipe, consent PASS; `advapi32 security` 3728 / 0; `ntdll info` 2811 and 2744 / 0; `shlexec` 1805 / 2 |
| B4 helper set | PASS, 56 s |
| Committed state, focused harness | `registry_enforcement=FAIL` on the four SCM rows (1722), as above |

### Results, grant of ServiceCurrent and ActiveComputerName (2026-10-22)

Decision (user): `Control\ServiceCurrent` and `Control\ComputerName\ActiveComputerName` join the Users-writable
table in `server/registry.c` for now, and are removed when services.exe and `wineboot --init` get the system role
(step 2b work order, item 5). Reason: in a user-writable prefix a limited process can already rewrite `system.reg`
through `Z:` (same Unix user), so registry protection there is not a boundary; in the machine root no service gets
a SYSTEM token before the system role exists, and the pipe impersonation rule gives a hijacked control pipe only an
identification-level token.

| Check | Result |
| :--- | :--- |
| Focused harness, helper set, x86_64 and i386 | all markers PASS, among them `limited_token_shape`, `linked_token_routes_closed`, `registry_enforcement`, `pipe_impersonation`, consent |
| B4 smoke test, `WEX_B4_HELPER=1` | PASS, 57 s |
| Errors after the helper is set (fresh prefix: `wineboot -i`, `-u`, helper, then `cmd` and a token probe) | only `winemenubuilder` (disabled by the override); no RpcSs, service start or computer-name errors; the probe reports a Limited, Medium token and `HKLM` write 5 |
