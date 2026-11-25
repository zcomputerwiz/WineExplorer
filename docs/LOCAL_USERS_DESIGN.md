<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Local Users and Groups (design note)

Status: design draft 2, 2026-11-24, GPO and Domain agent. Draft 1 (the same day) put the authorization in a polkit rule
that inspected the calling process; the orchestrator answered that only the Wine server knows whether a caller is an
elevated administrator, and that machine-wide changes go through session 0 (as the registry and the event log do). This
draft is that design. For review by the Session 0 reviewer and the orchestrator before any code (step 3 of the Computer
Management plan, [XP_SETTINGS_GAPS.md](XP_SETTINGS_GAPS.md)). It is the back end of the `lusrmgr.msc` node of Computer
Management, and of `net user`, `net localgroup` and, later, the User Accounts panel.

Constraints (orchestrator, 2026-11-24): the authority stays the Linux accounts plus `/etc/wineexplorer/accounts.map`, kept
in sync by the root tool `wex-accounts` and its watcher ([E1_S4_ACCOUNTS_MAP.md](E1_S4_ACCOUNTS_MAP.md)); **Wine never
writes the map**; account changes go through D-Bus (AccountsService, polkit-authorized), as OOBE does
(`src/dlls/wexhost/unixlib.c`, `user_create`); `netapi32`'s `NetUser*` and `NetLocalGroup*` become clients of that path,
so every program gets it, not only the snap-in; the same patterns as the event log service
([EVENTLOG_DESIGN.md](EVENTLOG_DESIGN.md)): a bridged pipe, an identification-level client, our own versioned protocol, limits,
no path or name trusted from the client.

## What exists

- `netapi32` (`dlls/netapi32/netapi32.c`) keeps a per-process in-memory `sam_user` list: `NetUserAdd` adds to it,
  `NetUserDel` removes from it (and returns `ERROR_INVALID_LEVEL`, a stock bug), nothing reaches the host, nothing
  survives the process. `NetUserSetInfo`, `NetUserSetGroups` and the `NetGroup*` calls are stubs;
  `NetLocalGroupAdd` and `NetLocalGroupAddMember(s)` are stubs that succeed.
- `wexhost` already talks to `org.freedesktop.Accounts` from the OOBE: `CreateUser` with account type 1 (administrator =
  the sudo group), `SetPassword` (a crypt(3) SHA-512 hash built in the module, libcrypt loaded at run time),
  `SetPasswordMode`, and from the session `FindUserById`, `ListCachedUsers`. That code is the model of the calls, not
  the path: it runs where nothing else is asked.
- The accounts map gives every account a stable SID (`S-1-5-21-<machine>-<RID>`, RID 500 for the built-in
  administrator, counter RIDs from 1001; domain accounts in version 2). `wex-accounts sync` runs from a path unit and a
  timer, so a new Linux account reaches the map by itself, as root, without Wine writing it.
- The server decides "administrator" with one rule (the `admin_group=` of `sysrun.conf`, default `sudo`, plus Domain
  Admins), and knows the token of every caller; the pipe bridge (T5.2) hands session 0 the caller's token and real
  process id (R6 step 2).

## Goals and non-goals

Goals: (1) `lusrmgr.msc`: list, create, rename, set the full name of, disable, enable, set the password of and delete
local users; list the local groups and their members; put a user in Administrators or take the user out. (2) `net user`
and `net localgroup` and every program that uses `NetUser*`/`NetLocalGroup*` see and change the same accounts.
(3) Nothing here writes the accounts map, the token, or session 0's registry.

Non-goals, shown grayed with a reason, never silently ignored: password policy and age, "password never expires",
logon hours, profile and home paths (the host's PAM and the domain own them); custom local groups and domain members of
local groups (phase 2); remote computers (`ERROR_NOT_SUPPORTED`).

## The model

| Windows | Here |
|---|---|
| a local user | a Linux account of UID 1000 or more (AccountsService's non-system users), SID from the accounts map |
| full name | the account's real name (AccountsService `RealName`) |
| description (comment) | AccountsService has no field for it. The description is empty and read-only, grayed with the reason. Phase 2 may keep it in a root-owned file the root tool maintains. |
| account disabled | AccountsService `Locked`; the logon screen already hides locked accounts |
| administrator / standard | AccountsService `AccountType` 1 / 0 = membership of the admin group (sudo) |
| Administrators (S-1-5-32-544) | the admin group, members = the users in it (+ Domain Admins implicitly on a joined machine, read-only, as on Windows) |
| Users (S-1-5-32-545) | every local account and Authenticated Users: computed, membership not editable |
| Guests, Power Users, Backup Operators, Replicator, custom | none in phase 1: the node lists Administrators and Users, the rest are absent |
| user rename | AccountsService `SetUserName`; the map keeps the SID across a rename (`wex-accounts` rename rule) |

A user's SID in `NetUserGetInfo` level 23 and in the snap-in comes from the accounts map through the lookup the server
already offers (`LookupAccountName`), so it is the same SID the token has. `netapi32` never makes one.

## Layers

```
lusrmgr snap-in / net.exe / any program
    netapi32: NetUserAdd/Del/Enum/GetInfo/SetInfo/ChangePassword, NetLocalGroup*   (Wine's names and structures)
        \pipe\WexSession0\accounts   (bridged to session 0; our protocol, versioned)
            the accounts service in session 0
                org.freedesktop.Accounts on the system bus (AccountsService), polkit decides
```

- **The service** is an operation set of a session 0 service, built next to the event log service on the same pipe
  skeleton (the L1 session builds it: framing, limits, the identification-level client, the bridge's link-bound token).
  Which service hosts it, a new one (`WexAccounts`) or an existing one, is the orchestrator's call; the protocol does not
  care. It is the **only** process that talks D-Bus about accounts; no program in a session does.
- **Identity comes from the pipe and the bridge, never from the message.** The caller's token is the link-bound token
  at identification level (X0's rule: account and session are the link's), anonymous refused. Reads (list, get info,
  group members) are open to every authenticated caller. Every change requires a token that is **an elevated
  administrator's**: the server's own fact (Administrators enabled, High integrity, a full token), decided by the one
  shared predicate the other session 0 services use (the event log's E1), not by anything the client says. A limited
  administrator or a standard user gets `ERROR_ACCESS_DENIED`. The snap-in and `net.exe` are therefore started through
  the existing consent path (the "open again as an administrator" offer the other consoles make).
- **No names or paths trusted from the client.** A user name is checked against the accounts map's grammar (63 bytes
  ASCII, nothing AccountsService refuses, no `/`, no leading `-`), UTF-16 strings are even-length, terminated, at most
  255 characters (a full name 256), a password at most 127 characters and never logged; a frame is at most 64 KiB, one
  request in flight per connection, an unknown operation or version is refused, a malformed frame closes the connection;
  per account at most 64 connections. The service resolves every user by AccountsService itself (`FindUserByName`,
  `GetUid`) and checks the UID is 1000 or more and not the caller-supplied number.
- **Protective rules in the service** (not the client): never `root`, never a UID below 1000; `DeleteUser` of the
  caller's own account, of the last administrator, and of the built-in administrator (RID 500) is refused
  (`NERR_LastAdmin` or `ERROR_ACCESS_DENIED`); the last administrator cannot be removed from Administrators or locked;
  `removeFiles` is always false (a deleted user's profile stays, as Windows keeps it).
- **Authorization at the host:** the polkit rule (`installer/polkit/`) grants `org.freedesktop.accounts.user-administration`
  only to the unix account of session 0, and nothing else; a rule that names the account, never a group a user can
  join, and never `login-session` or `change-own-password`. A process in a session cannot use it: it is not that
  account, and the D-Bus caller identity is the connection's uid.
- **The calls** (AccountsService methods): `CreateUser(name, fullname, type)`, `DeleteUser(id, false)`,
  `SetRealName`, `SetAccountType`, `SetLocked`, `SetUserName`, `SetPassword(hash, hint)` (the hash made in the service:
  the password crosses the bridged pipe once, never a file, never an argument, and is wiped after use),
  `FindUserByName`, `ListCachedUsers`, `CacheUser` (so a Linux account AccountsService has not seen is found). Group
  membership of the admin group is read with `getgrnam`.
- **Errors map to Windows' codes:** `NERR_UserExists`, `NERR_UserNotFound`, `ERROR_ACCESS_DENIED` for any refusal,
  `ERROR_INVALID_PASSWORD` for a password the host rejects (PAM's text goes to the service's log, never to the client),
  `NERR_InvalidComputer` for a server name that is not this computer, `RPC_S_SERVER_UNAVAILABLE` (1722) when the service
  cannot be reached.
- **Plain Wine keeps its behaviour:** where the pipe does not exist (plain Wine, a prefix without session 0, the setup
  account), `netapi32` uses the stock in-memory list exactly as today, and Wine's own `netapi32` tests keep passing:
  the choice is made once per process by trying the pipe, with no WineExplorer DLL loaded. Where session 0 is there but
  the service is down, the calls return 1722 and do **not** fall back to the in-memory list (a user that appears to be
  created and is not would be worse than an error).
- **Changes take effect on the next logon:** a change to Administrators does not change a running token (as on
  Windows); the map picks up a new account at the next `wex-accounts sync`, the service asks the watcher to run
  (a `systemctl start` of the sync unit through the same polkit grant, if D-Bus to systemd is allowed to session 0; else
  the timer). Until the map has the new account, a request that needs its SID answers `ERROR_NONE_MAPPED`, never an
  invented SID. A new user is shown at once in the snap-in with its name and full name; the SID fills in once the map
  has it.

## Groups (phase 1)

AccountsService has **no group API**, so phase 1 does exactly what it can:

- **Administrators**: members are the users in the admin group; add or remove is `SetAccountType(1|0)` on that user.
  The built-in administrator (RID 500) cannot be removed; the last administrator cannot be removed.
  `NetLocalGroupAddMembers`, `NetLocalGroupDelMembers`, `NetLocalGroupSetMembers` and `NetUserSetGroups` for
  Administrators reduce to those calls; for any other group they return `NERR_GroupNotFound`.
- **Users**: `NetLocalGroupGetMembers` lists all local accounts (computed); changing it returns
  `ERROR_ACCESS_DENIED`.
- `NetLocalGroupAdd` of a new name returns `ERROR_ACCESS_DENIED`; the snap-in's New Group item is grayed with the reason
  "custom local groups are not available yet".
- **A domain account in a local group:** Domain Admins is in Administrators by default on a joined machine, as on
  Windows: the server already treats it as the administrator group, and the snap-in shows it as a member of
  Administrators, read-only. An individual domain user or another domain group cannot be a member of a local group in
  phase 1: the Add Member dialog resolves `DOMAIN\name` through Winbind and answers "not supported yet", no silent
  success. Domain users appear under Users (grayed, with the domain name) only once admitted (a record in the map).

**Phase 2 (after a token-side design with the orchestrator, not part of this review):** custom local groups and the
Windows built-in ones as groups the token carries, and individual domain members of local groups.

## The snap-in

`src/dlls/lusrmgr` (a module like the others; Computer Management hosts it as `Local Users and Groups` with `Users` and
`Groups` below it, the console `lusrmgr.msc` shows only that node). Users: columns Name, Full Name, Description (empty,
grayed in properties); New User (name, full name, password twice, "User must change password at next logon" grayed,
"Account is disabled"), Set Password, Rename, Delete, Properties (General: full name, disabled; Member Of: Administrators
and Users, Add and Remove as above). Groups: Administrators and Users, Properties with Members. Every grayed item has its
reason in the status line. The snap-in uses `netapi32` only and has no pipe or D-Bus code of its own.

## Tests

- **Wine tests** (`netapi32` tests, both architectures): the stock in-memory behaviour unchanged when no pipe exists; with
  a fake accounts service on the pipe name in a prefix without session 0: every call and every mapped error, the client's
  limits, 1722 when the service is down and no fallback to the list.
- **The service's own tests** (with the event log's pattern, in the suite as a section `accounts`): over the bridged pipe,
  with a private `dbus-daemon` and an AccountsService stand-in: create, enumerate, set real name, lock, set type, set
  password (the hash only on the bus), delete, each protective rule, each limit of the protocol, a malformed frame, a
  caller that is a limited administrator or a standard user (all changes refused, reads allowed), an elevated
  administrator (changes allowed; the c4 broker stub is the way to one), a process in a session that tries D-Bus itself
  (refused by polkit: not session 0's account), and the accounts map's bytes unchanged until `wex-accounts sync`.
- **The lab** (wexgpo, an installed machine with the real AccountsService and polkit): the whole path once, including a
  Domain Admins user shown in Administrators.

## Order of work

1. This note, reviewed. 2. The service's operation set on the L1 pipe skeleton, with its tests (the security part; the
reviewer first). 3. The polkit rule for session 0's account (and the host side of it in the installer). 4. `netapi32`
clients and Wine tests. 5. The snap-in and `net.exe` wiring. 6. Suite section and lab run. Phase 2 only after the token-side
design.

## Review record

| Point | Draft 1 | Draft 2 |
|---|---|---|
| Authorization | polkit rule inspecting the calling process (system group, `/proc`) | session 0 service decides from the server's token (elevated administrator); polkit grants the action to session 0's account only |
| Client | `netapi32` over `wexhost` (D-Bus in the session) | `netapi32` over the bridged pipe; no WineExplorer DLL, plain Wine unchanged |
| Spool fallback | a root request spool | not needed |
| Custom groups | question | phase 2, after the token-side design |

Open for the reviewer: (a) the unit that runs `wex-accounts sync` after a change (a direct start through the polkit grant,
or wait for the timer); (b) whether the password may cross the bridged pipe at all in phase 1, or the service should
accept only a client-made hash (the client then needs libcrypt; the draft keeps the hash in the service, so the
password is on the pipe once at identification level); (c) the host service that carries the operation set.

## Reviewer's verdict on draft 2 (2026-11-24): go on steps 2, 4, 5 and 6 under U1 to U6

Step 3 (the polkit grant and its installer) and the D-Bus executor wait for the owner's decision on B1 below.

**B1: polkit decides by uid.** A grant to session 0's account is a grant to every process of that uid (every Windows
service in session 0, the LocalService ones included), and creating an administrator is Linux root: any code execution in
session 0 would equal root. Options: (A) accept and document; **(B), recommended by the reviewer and the orchestrator:**
a small native helper under its own system account (a systemd unit, `User=wexaccounts`) makes the D-Bus calls, and polkit
names that account. The helper accepts requests only from session 0's wineserver, checked the T1 way (`SO_PEERCRED`,
pidfd pin, comm, non-dumpable, saved gid); the server forwards each request with its own verdict on whether the caller is
an elevated administrator. Under (B) the `src/` service of this note is the Windows-side half (pipe, protocol, limits).

| | Condition |
|---|---|
| U1 | The shared elevated-administrator predicate refuses a session's system and service tokens: a user SY, LS or NS, the SERVICE group and the service SIDs never count; only a real user's elevated token does. One predicate, with its test, plus an accounts test: a session's SYSTEM token cannot create a user. |
| U2 | The password is hashed in the client, not the service (session 0's processes share a uid and are dumpable): `netapi32`'s unix side makes the SHA-512 crypt hash (libcrypt loaded at run time as `wexhost` does, a `getrandom` salt; a stock change, listed in STOCK_CHANGES). The service checks the form: `$6$`, bounded `rounds=`, length, alphabet. `NetUserChangePassword` (old password) is `ERROR_NOT_SUPPORTED` in phase 1 and grayed in the UI. |
| U3 | UIDs: `UID_MIN` and `UID_MAX` from `/etc/login.defs`, 65534 excluded, anything AccountsService marks as a system account refused. |
| U4 | Sync: polkit allows only `org.freedesktop.systemd1.manage-units` with unit == the sync unit and verb == `start`, for the same single account. The wait for the map's generation (about 10 s) runs asynchronously, not on a service thread; success with a warning on timeout. |
| U5 | `netapi32` decides pipe versus stock from whether the machine has session 0 (the bridge or link answering), not from one connect attempt; only then is "not reachable" 1722. |
| U6 | Changes are serialized service-wide (closes the last-administrator check-then-act race). The refusal to rename or delete a logged-in user maps to a Windows error code. |

Open items settled: the host is a WineExplorer-owned service program under `src/` in session 0 (not stock Wine, not shared
with `wevtsvc`); the password crosses the pipe as a client-made hash only (U2); the wait after a change as U4. Draft 2
above is amended by this table wherever it says otherwise (the hash in the service, the polkit rule for session 0's
account, the one-connect pipe test).
