<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: registry and service manager access checks under enforcement

Date: 2026-10-22. Status: scoped. Part of `ELEVATION_BOUNDARY_PLAN.md`; prerequisite of the system role (step 2b
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
