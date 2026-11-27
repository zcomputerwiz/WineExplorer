<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# A session's system role and session 0's machine registry (design note)

Status: draft 2, 2026-11-27, for the owner's decision. Docs only. Draft 1 (dc657119) with the reviewer's comments
worked in: the steering vectors checked one by one, P1 shipped fail-closed before any further measuring, one choke
point for every token description, P2 as exact entries.

## The decision

| | Option 1: narrow brokered allow-list | Option 2: no machine writes from a session's system role |
|---|---|---|
| A session's system role writes session 0's HKLM | as the user (P1), except exact allow-listed values that session 0 writes for it (P2) | as the user (P1), no exceptions |
| Measured writes that need session 0 (below) | none found | none found |
| What breaks | nothing measured; P2 exists for what the refusal log finds later | the same today; a later need becomes a code change (session-local key, or a session 0 service) instead of a list entry |
| Effort | option 2 plus the P2 mechanism (a root-owned list, exact key/value/type matching, a caller check), about 2 more days, plus a review line per entry | P1 with the refusal log and USER_HIVE (about 1 day with tests), S1 session-local keys (about 1 day) |
| Risk | every entry is a SYSTEM write a user's session can trigger; the list is where the next hole would be | none added |

Recommendation: **option 2 now**. M1 found nothing a session's system role must write in session 0's machine
registry; build P2 only if the refusal log later shows a real need, and decide each entry then.

## The problem

Session 0 holds the machine registry (T1). Every program of a user's session gets a registry channel
(`get_registry_channel`, server/session0.c); session 0 serves it as a registry-only client with the program's token as
the user's server describes it (`start_registry_client`, `create_bound_token`). That binds only the user SID; groups,
privileges and integrity are the server's word. The same holds for `WEX_SESSION0_USER_HIVE`, which takes its SID from
`create_bound_token` too.

So a program of a session's **system role** (SYSTEM inside the user's own server, under the user's uid) writes
session 0's HKLM as SYSTEM, and HKU\S-1-5-18/19/20 as their owners. Measured: the suite's `svc_table` "system" case
creates `Services\WexSvcMine` (LocalSystem, an image of the test's choosing) in session 0's HKLM from a user's
session; session 0's service manager would run it as SYSTEM. Writes to session 0's `HKU\S-1-5-18` (SYSTEM's own
profile, `Environment` included) are a second path to code running as SYSTEM in session 0. B132 closed the same hole
for `link_bound_token`; the registry channel and USER_HIVE do not go through it.

## Can a standard user reach a session's system role?

Checked on the installed test image (msib108, standard user `wineuser`, uid 1001, Yama ptrace_scope 0), as root from
the outside, the user's desktop up:

| Vector | Result | Evidence |
|---|---|---|
| ptrace / `/proc/<pid>/mem` of the user's wineserver | checked, closed | saved gid 986 (the system group): not dumpable, read refused |
| ptrace / mem of services.exe, winedevice.exe (both), plugplay.exe | checked, closed | real, effective and saved gid 986: read refused (explorer.exe, gid 1001: readable, as expected) |
| Environment (`WINE*`, `LD_*`) passed to the system role | checked, closed | services.exe has only `WINEPREFIX=/var/lib/wineexplorer/machine`, `WINESERVERSOCKET`, `WINELOADERNOEXEC`; the user's `WINEDEBUG`, `WINEDLLOVERRIDES` do not reach it |
| The system role's prefix, `drive_c`, `system32` | checked, closed | `/var/lib/wineexplorer/machine`, root:wexsys, not writable by the user |
| dosdevices and drive links | checked, closed | root:wexsys directory, root-owned links |
| DLL directories (`system32`, `windows`, `/usr/local/lib/wine/*`) | checked, closed | root-owned, not writable by the user |
| The machine hives the session's server loads | checked, closed | with the system group the server loads `system.reg`/`userdef.reg` from the machine root only (`machine_hive_path`), `O_NOFOLLOW`, and refuses to start unless root (or session 0's account) owns the file, the system group is its group and others cannot write it; the user's own `~/.wine/system.reg` (mode 777, 40 bytes) is never read |
| Session-local machine keys (`Control\Class`, `Enum`, `Services`) written by the user's limited process | checked, closed | `reg add` as the user: "Unable to access or create" for all three |
| The user's own hive (`user.reg`), read by the system role | not a vector | HKCU of the system role is SYSTEM's, not the user's |

**The protection is the gid change** (B41's credentials: the launcher gives the session's server and its system role
the system group as saved group, which makes them non-dumpable and lets the server open the machine root). Any future
change that drops or shares that gid, or runs a system-role process without it, reopens the direct path.

So a standard user cannot steer the system role today. What remains is an amplifier: any code-execution bug in a
system-role process that takes the user's input (the session's service manager, plugplay, a driver in winedevice, a
service's RPC) becomes SYSTEM in session 0, i.e. the machine, instead of staying inside the user's own session. That
is reason enough to close it now; it is not an emergency.

## M1: what a session's system role writes in session 0 today

A temporary session 0 log of every create, set, delete through a channel whose described user is a service account
(a session's system role), over `svc_table`, `svc_table_elev`, `s0_service`, `s0_programs` (the run stopped in
`s0_programs` after `svc_table_elev` timed out; `c7_installers` and the install chain were not reached). The image was
not logged; the writer is attributed from the code. The patch was reverted, never committed.

| Key (session 0) | Values | Writer | Under P1 | Fix |
|---|---|---|---|---|
| `HKLM\System\ControlSet001\Control\TimeZoneInformation` | `StandardName`, `TimeZoneKeyName` | kernelbase locale init, every process that can open the key for write (kernelbase/locale.c) | refused, ignored (best effort; standard users already fail it) | S1: session-local, or leave refused |
| `HKLM\System\MountedDevices` | `\DosDevices\C:`, `Z:`, two volume GUIDs | mountmgr.sys in the session's winedevice | refused | S1: session-local (the session's drives are already session-local) |
| `HKLM\Software\Microsoft\Windows NT\CurrentVersion\NetworkCards\1`, `\2` | `ServiceName`, `Description` | ndis.sys in the session's winedevice | refused | S1: session-local |
| `HKLM\System\ControlSet001\Services\WexSvcMine`, `WexSvcTbl` | a full service entry; start type, description | the suite's probe as the session's system role through the service manager | refused: **this is the hole**, test (a) | none: service changes are an administrator's, through consent |
| `HKU\S-1-5-18\Console`, `Keyboard Layout\Preload`, `...\Explorer\Shell Folders`, `Software\Wine\Fonts\Cache`, `Software\Wine\Temporary System Parameters` | many | console, user32, shell32, win32u in the session's SYSTEM processes | refused | S1: the service-account hives (S-1-5-18/19/20) become session-local, so a session's system role has its own copies, as `Enum` already is |
| `HKU\S-1-5-19`, `HKU\S-1-5-20` `Control Panel\International` (+ `Geo`) | the locale cache | kernelbase locale init in the session's LocalService and NetworkService processes | refused | S1 as above |

Nothing in the list must reach session 0's own machine state: it is all state that describes the session (its drives,
adapters, time zone, its SYSTEM's console and locale) or the hole itself.

## USER_HIVE

`WEX_SESSION0_USER_HIVE` loads the user's `user.reg` under `HKU\<sid>`, with the SID from `create_bound_token`; a
session's system role may describe S-1-5-18/19/20. `session0_load_user_hive` takes the existing key if there is one:
if session 0 holds it as a saved branch the load fails with `STATUS_OBJECT_NAME_COLLISION` (expected for S-1-5-18);
otherwise it **merges** the file into it and registers it as a hive the user's server saves. M1 shows S-1-5-19 and
S-1-5-20 are created on demand, not loaded branches, so a user's server could fill session 0's LocalService and
NetworkService profiles from a file of its choosing. Not tried live. A system role has no user hive: P1 refuses
service-account SIDs outright.

## The work

- **P1, one choke point, fail closed.** `start_registry_client` and USER_HIVE build their tokens through
  `link_bound_token` (binding, session, `link_policy`); `create_bound_token` goes away. A session's system role then
  writes session 0 with the user's limited token (the account SID, SYSTEM deny-only, Medium), so HKLM\System,
  HKLM\Software and HKU\S-1-5-18/19/20 are refused as for any standard user. An administrator's own description is
  believed as since T1 (`link_policy` returns at once for an administrator's unmapped token). USER_HIVE refuses any
  service-account SID before the policy.
- **The refusal log (M1's successor).** Session 0 logs, once per key and value, each write refused for a mapped token:
  key, value name and the caller's image (the channel request does not carry the image today; the user's server adds
  it, for the log only, never for a decision). That is the measurement from now on,
  with the hole closed.
- **S1, session-local keys.** Add to `wex_session_key_match`: `System\MountedDevices`,
  `Software\Microsoft\Windows NT\CurrentVersion\NetworkCards`, `Control\TimeZoneInformation` (or leave it refused), and
  the service-account user hives `User\S-1-5-18`, `S-1-5-19`, `S-1-5-20` for a session's server. Each one checked in the
  suite after the change (drives, network adapters, the console of a session's service).
- **P2 (option 1 only).** A root-owned list in session 0; each entry an exact key path, value name and type, no
  prefixes or wildcards; session 0 writes the value as SYSTEM after checking the caller is that session's system role.
  Each entry gets its own review line here. Never `Services`, `Run*`, `Winlogon`, `Image File Execution Options`,
  policies, `Environment` or anything else that leads to code running as SYSTEM.
- **Reads stay as they are.** HKLM is readable by users.

## Tests

- (a) wexadm's session's system role writing `Services\WexHoleTest\ImagePath` (and the suite's `svc_table` "system"
  create) through its registry channel: succeeds before, refused after; session 0's `Services` unchanged.
- (b) USER_HIVE with a service-account description (S-1-5-18, S-1-5-19): refused; `HKU\S-1-5-18` and
  `HKU\S-1-5-19` in session 0 unchanged.
- (c) plugplay, winedevice (mountmgr: drives; ndis: adapters) and printing still work in a user's session; the full
  suite and the install chain pass; the refusal log reviewed after both.

## Risks

- **MSI under option A.** The B108 retest plans DoInstall in the user's session (option A). If MSI's NoImpersonate
  custom actions or its service installs then run as the session's system role, P1 refuses their machine writes, under
  both options. Session 0's MSI host (option B) is not affected. The B108 retest must run with P1 to show which; the
  answer may decide between A and B rather than add P2 entries.
- A write the measurement did not reach (installers, the install chain, Office, Edge) fails after P1. The refusal log
  finds it; S1 or a code change fixes it, P2 only under option 1.
- Programs that check "am I SYSTEM" see the user's limited token in session 0's registry only; their own process token
  in the session is unchanged.

## Order

1. P1 with the refusal log and USER_HIVE, tests (a) and (b), one batch.
2. S1 with test (c), the full suite and the install chain; the refusal log reviewed.
3. The owner's decision on option 1 versus 2; P2 only if the log shows a need, each entry decided separately.
