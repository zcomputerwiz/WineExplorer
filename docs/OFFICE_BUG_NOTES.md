<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Bugs found while running Microsoft Office on WineExplorer

Written 2026-11-15 for an agent who takes one of them without the rest of the history. Each entry says what is
wrong, how to see it, what is known and what is not. The history, the lab and the other findings are in
[OFFICE.md](OFFICE.md); the lab scripts are in `scripts/lab/office/`.

**Checked first:** the known-bugs document is `docs/KNOWN_BUGS.md` on the branch `claude/wine-reuse` (not on
master or on this branch; the Wine fork has none). Its rule: the system tier session owns the list, other sessions
report there, it fixes or routes each entry. At its state of 2026-11-14 (`c1b5a8f9`) it lists B1 to B13; none of
the entries below is recorded in it. Overlaps: **B4** (per-machine MSI, the way through session 0 or the
elevation broker: the lab here runs setup in session 0 as the workaround, OB-1 and OB-5 are what is found there),
**B5** (Windows version: Office 2016's MSI needs Windows 7, so OB-3 and B5's open question "how an `.msi` gets a
version" are the same need), **B10** ("a session 0 image would add the cross-server question": that is OB-1),
**B11** (machine root maintained without a notion of session 0). The T5 design note
`SYSTEM_TIER_T5_SERVICES.md` leaves open what OB-1 is (its relay carries `svcctl` only).

**To put in `KNOWN_BUGS.md`** (rows for its table, numbered after B13; the entries below are the detail):

| # | Bug | Reported | State |
|---|---|---|---|
| B14 | A service in session 0 cannot be reached through its own RPC endpoint (`\pipe\lrpc\...`, `ncacn_np`) by a program in a user's session: pipe namespaces are per server, the T5 relay carries `svcctl` only (`SLOpen` of Office's licensing client fails with `0x80070078`, `c0000034` on the pipe open) | Office session, 2026-11-15 | to do; detail OB-1 in `OFFICE_BUG_NOTES.md` |
| B15 | `MsiSourceListEnumSourcesW` returns 1605 for a package that is installed; `setup.exe /uninstall` of Office 2016 stops at once | Office session, 2026-11-15 | to reproduce and fix in `dlls/msi`; OB-2 |
| B16 | Session 0's account cannot save its own `HKCU` (`machine/user.reg` is `root:wexsys` 0644): a setting such as the Windows version is lost at every restart of the unit | Office session, 2026-11-15 | to decide (permissions or another hive); OB-3. Related: B5 |
| B17 | An installer cannot overwrite a font that the machine root links to an open font (`EACCES` through the link, setup 1603) | Office session, 2026-11-15 | fixed on the fork branch `claude/office-wine` (`53e7017`, `server/fd.c`), tested directly; design question in OB-4 |
| B18 | Office 2016's licensing service refuses to register its plugin during setup in session 0 (`0xc004f025`, rolls back with 1603) | Office session, 2026-11-15 | cause unknown; OB-5 |
| B19 | Word 2016's "First things first" dialog: Accept does nothing and starts `DW20.EXE` | Office session, 2026-11-15 | cause unknown; OB-6 |

Environment of all entries: the lab VM `WineExplorer-Office` (a clone of `WineExplorer-VM`), Debian 13, this
branch's Wine (`wine-11.21`, WoW64) with the system tier on (`WEX_SESSION0=1 scripts/setup-machine-root.sh`:
session 0 runs as the Unix account `wexsys` on the machine root `/var/lib/wineexplorer/machine`). Office 2016
Professional Plus (MSI, 32-bit) in Windows 7 mode unless stated.

---

## OB-1 A service in session 0 cannot be reached by a program in a user's session through its own RPC endpoint

**Where:** named pipes and `ncalrpc` endpoints between sessions; `server` (pipe namespace per server),
`dlls/rpcrt4/rpc_transport.c`, the T5 relay (`programs/services/forward.c`).

**What.** Each wineserver has its own pipe namespace. Session 0's service processes create their pipes there; a
user's session has another server and does not see them. The T5 relay forwards the service control manager
(`\pipe\svcctl`) only. Any service that offers its own endpoint is unreachable from a user's program, although on
Windows one namespace serves every session.

**See it.**

1. Start Office's licensing service in session 0: `scripts/lab/office/start-ospp-session0.sh` (needs the copy
   made by `scripts/lab/office/grab-ospp.sh` during a Windows 7 mode setup; see OFFICE.md blocker 4). The service
   runs: `ps -eo user,args | grep OSPPSVC` shows it as `wexsys`.
2. As a standard user run the probe with Office's own client:
   `scripts/lab/office/run-user.sh USER probe 98 150 sppc-probe64.exe "C:/Program Files/Common Files/Microsoft Shared/OfficeSoftwareProtectionPlatform/OSPPC.DLL"`
   (`sppc-probe.c` takes the DLL as its first argument).
3. `SLOpen: 0x80070078`. With `WINEDEBUG=+rpc,+file` the client binds `ncalrpc:[OSPPCTransportEndpoint-00001]`,
   opens `\\.\pipe\lrpc\OSPPCTransportEndpoint-00001`, gets status `c0000034` (object name not found, in the
   user's own server) and `I_RpcMapWin32Status (1722)` (server unavailable).
4. The same probe as the system account (`run-session0.sh`) succeeds: the endpoint is in session 0's server.

**Known.** The service itself is fine and answers the calls it is given. The user's server never asks session 0
for a pipe it does not have.

**Not known / direction.** Extend the T5 channel (the mechanism of `get_registry_channel`, a client kind that
may only open named things in session 0) to pipe names under `\pipe\lrpc\` and to `ncacn_np` pipes that are not
in the user's namespace: open in the user's server first, on "not found" ask session 0 (impersonating the
caller, as T5.4 does for `svcctl`), return a handle that reads and writes through the channel. Access checks stay
with session 0. A shared namespace for services' pipes is the other way; it changes more.

**Why it matters.** Office in Windows 7 mode cannot be licensed for a user without it (Word in a user's session
cannot reach `osppsvc`). The same holds for any product with a machine-wide service that offers an endpoint (the
browser updaters' services, for example).

---

## OB-2 `MsiSourceListEnumSourcesW` fails for an installed package; Office cannot be uninstalled

**Where:** `dlls/msi` (source list functions).

**What.** `setup.exe /uninstall ProPlus /config scripts/lab/office/config-2016-uninstall.xml` ends at once with
return code 1605 (`ERROR_UNKNOWN_PRODUCT`): the setup log says `Error: Failed MsiSourceListEnumSourcesW
ErrorCode: 1605(0x645)` for package `Office64WW` ({90160000-002A-0000-1000-0000000FF1CE}). The package was
installed by the same setup earlier (per machine, as the system account) and is listed as installed.

**See it.** Install Office 2016 (OFFICE.md, Windows 10 mode install works to the end), then run the uninstall
as the same account. The log is the `SetupExe(...).log` in that account's `AppData\Local\Temp`.

**Known.** The product is registered; the failing call is the first thing uninstall does. **Not known:** which
key or value of the source list Wine's `msi` expects and the package's registration lacks (it is written by
Wine's own `msi` during the install, so possibly the install writes it for another context than the one the
query uses: per machine against per user, or the `SourceList\Net`/`URL`/`Media` subkeys).

---

## OB-3 The system account's own registry hive is not persisted; its Windows version setting is lost at every session 0 restart

**Where:** `scripts/setup-machine-root.sh` (modes and owners of the machine root's files), `server` (session 0's
`HKCU`).

**What.** Session 0's account has `machine/user.reg` as its `HKCU`. The file is `root:wexsys` mode 0644, so the
account (group `wexsys`) cannot write it. `reg add "HKCU\Software\Wine" /v Version /d win7` works in the running
session 0 and is gone after `systemctl restart wex-session0.service`. (`system.reg` is owned by `wexsys` and is
saved.) A deployment that needs the system account to run in Windows 7 mode, as Office 2016 does, has to set it
after every restart; and it is easy to run a setup in the wrong mode without noticing (this happened twice in
the lab; check with `cmd /c ver` right before a run).

**See it.** `sudo ls -l /var/lib/wineexplorer/machine/user.reg`; set the value, restart the unit, run
`cmd /c ver` as the account.

**Not known.** Whether `user.reg` is root-owned on purpose (it is the template for users; `/etc/skel/.wine`
copies of it exist) and the system account's `HKCU` should live in another file, or the mode is an oversight.
Also: `HKLM\Software\Wine\...` keys are saved; the default Windows version could be set machine-wide there.

---

## OB-4 An installer cannot overwrite a font that the machine root links to an open font

**Where:** `server/fd.c` (`open_fd`), `scripts/setup-machine-root.sh` (font links).

**What.** `setup-machine-root.sh` links open fonts into `windows\Fonts` under Windows names (25 links:
`segoeui*.ttf`, `calibri*`, `cambria*`, ...; `src/resources/fonts/font-links.txt`), pointing at root-owned
files in `/usr/local/share/fonts/`. Office ships the real fonts and overwrites those names. The open goes through
the link; the target is not writable; `CreateFileW` returns access denied (`c0000022`); `InstallFiles` fails
("Failed to extract cabinet: ProPsWW2.CAB"), setup returns 1603.

**Fix (done, not proven by a full setup run).** Wine fork branch **`claude/office-wine`** (based on the pin
`1052fab3`, commit `53e7017`): in `open_fd` an overwrite (`O_CREAT|O_TRUNC`, write access) that fails with
`EACCES` on a symbolic link removes the link and creates the file. Test: `scripts/lab/office/font-overwrite-test.bat`
as the system account in session 0 (`run-session0.sh LABEL 98 60 Z:/.../font-overwrite-test.bat`): `1 file(s)
copied`, the link is a regular file. Before the change the same copy is refused. The full setup run that
reaches the fonts is blocked by OB-5.

**Open design question.** Replacing a link is what Windows does with an existing file, but a server change is
heavy for it. Alternatives: make the links in `setup-machine-root.sh` writable targets (copies owned by
`wexsys`), or make the font links only for fonts that Office and other installers do not ship. The fix is
harmless for a user's own prefix (the user could unlink it natively).

---

## OB-5 Office's licensing service refuses to register its plugin during setup (`0xc004f025`), setup rolls back

**Where:** unknown. The service is `OSPPSVC.EXE` (account NetworkService), called by the installer action
`CAInstallPlugin.x64` of `Office64WW.msi`.

**What.** Windows 7 mode, setup as the system account in session 0 on a fresh machine root:

```
CAInstallPlugin.x64:  Error: Failed to register plugin. HResult: 0xc004f025.
Error: Failed to install product: ...\Office64WW.msi ErrorCode: 1603
```

In Wine's log: `err:rpc:I_RpcReceive we got fault packet with status 0xc004f025`: the service returned the
status in an RPC fault. `0xc004f025` is the licensing platform's "the action requires administrator privilege"
code, as far as I know; I did not confirm this against the SDK header.

**See it.** `scripts/lab/office/run-session0.sh setup 98 7200 .../media/2016/setup.exe /config config-2016.xml`
after `reg add HKCU\Software\Wine /v Version /d win7` for the system account (check `cmd /c ver`). OB-3 explains
the need for the check.

**Known.**
- The service starts, answers setup's earlier calls and answers the probe (`sppc-probe.c` with `OSPPC.DLL`) with
  the codes Windows 11 gives; a write request with junk is refused for its content (`0xc004f01f`), not for the
  caller.
- The caller's token (`scripts/lab/office/token-probe.c`, run as the system account in session 0): user
  `S-1-5-18`, elevated, member of Administrators, integrity System. So the process token is right.
- `ActivePlugins` of the service is not found (no plugin registered), consistent with the failure.

**Found 2026-11-15 (service-side trace, see below): hypothesis 1 holds in this form.** In the service's thread that
handles the plugin call, `RpcImpersonateClient` succeeds (`rpcrt4_conn_np_impersonate_client`), then
`CheckTokenMembership(NULL, S-1-5-32-544)` runs on the thread's token, and that token has **7 groups and no
Administrators** (Everyone, Users, `S-1-5-6`, `S-1-2-1`, Authenticated Users, `S-1-5-15`, `S-1-2-0`), so the
service raises `0xc004f025` ("requires administrator"). The system account's own token has the 8th group
`S-1-5-32-544` (attributes 0xe). A plain pipe test in one process as the system account
(`scripts/lab/office/pipe-impersonation-probe.c`) sees all 8 groups through `ImpersonateNamedPipeClient` and
`CheckTokenMembership` returns 1, so pipe impersonation as such works. The caller in the real setup is therefore
a process whose token has no Administrators group: the custom action process that the installer service starts
(`msiexec -Embedding`). What token the MSI service gives it, and why it is not the elevated system token, is the
open question. Test started and not finished: `service-token-test.bat` runs `token-probe.exe` as a service as
LocalSystem and as NetworkService to see the token a service process gets (its first run hung and printed
nothing).

**How to see the service's side.** The service runs under session 0's `services.exe`, so its Wine output is not in
the log of the setup run (a trace of `+token,+security,+service` on the run shows only the installer's side: the
service is started, then the fault comes back). To trace the service itself put `WINEDEBUG` into the machine-wide
Environment (`GUEST_SYSROLE.md` step 9, `scripts/lab/guest-sysrole/guest-env-debug.py`) and restart session 0; the
output goes to the journal of `wex-session0-services.service`. Keep the channels few (`+relay` of a whole run
wrote 6 GB).

**Not known (hypotheses, none tested).**
1. The service checks the RPC caller's token through impersonation over `ncalrpc` (`RpcImpersonateClient`, which
   for a pipe is `ImpersonateNamedPipeClient`) and what it sees is not an administrator: the impersonation
   level of the client's open of the `\pipe\lrpc\...` pipe, or a token that the server hands out filtered. Check
   with a probe that serves an `ncalrpc` endpoint, is called by a program of the same account, and prints the
   impersonated token.
2. The service runs as NetworkService and cannot write something it needs (its registry key
   `HKLM\Software\Microsoft\OfficeSoftwareProtectionPlatform` or its data directory), because Wine's `msi` does
   not apply the installer's permission tables (`LockPermissions`); setup's own log shows one such step failing:
   `DDSet_Error: allowDeleteAccessRegistryKey: GetNamedSecurityInfo for MACHINE\Software\Microsoft\Schema
   Library\ActionsPane3 failed with 2`. This would give an access error, not the administrator code.
3. The service wants a group or privilege the system account's token lacks in the form it compares.

---

## OB-6 First-run dialog of Word: Accept does nothing and starts the error reporter

**Where:** unknown (Word 2016, `WINWORD.EXE`, as a standard user).

**What.** "First things first" (Install updates only / Ask me later, Accept): clicking Accept leaves the dialog
open and starts `DW20.EXE` (Microsoft's error reporting, `Application Error Reporting` window) each time. The
Wine log has 44 `err:msvcrt:_invalid_parameter`. Setting `HKCU\Software\Microsoft\Office\16.0\Common\General`
`ShownFirstRunOptin` = 1 skips the dialog. Seen in Windows 10 mode with the licence store of `sppc`; not retried in
Windows 7 mode, so it may depend on the licensing state.

**Not known.** Which call fails; `DW20.EXE -x -s <pid>` is Word crashing a thread (an exception in the
dialog's code). A `WINEDEBUG=+seh,+msvcrt` run during the click would show it.

---

## Where the evidence is

| What | Where |
|---|---|
| Lab, scripts, every run | [OFFICE.md](OFFICE.md), `scripts/lab/office/` |
| Probes (read-only unless stated) | `sppc-probe.c` (client DLL as argument, `junk` writes a refused request), `token-probe.c`, `pidgenx2-probe.c`, `stdcall-args-probe.c` |
| Fixed in this branch | `src/dlls/pidgenx` (key check), `src/dlls/sppc` (licence store) |
| Wine fork change | `zcomputerwiz/wine`, branch `claude/office-wine`, commit `53e7017` |
| Windows 11 answers the probes were compared with | recorded in OFFICE.md ("Error codes ... follow Windows 11") |
