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

## Draft 3: the helper `wex-accountsd` and its link (owner's decision on B1: option B, narrowed; 2026-11-24)

The owner decided: a native helper under its own system account makes the AccountsService and sync-unit D-Bus calls,
and the polkit rules name only that account. The helper accepts requests only from session 0's wineserver, checked the T1
way, together with the server's verdict on the caller. This section is the helper and the link; it replaces draft 2's
"Authorization at the host" bullet and its polkit rule. For the reviewer before any helper code.

### The chain

```
caller (a session's program: lusrmgr, net.exe)
  netapi32 -- \pipe\WexSession0\accounts (bridged; identification-level client) -->
    the accounts service (src/, a Windows program in session 0): framing, limits, protocol, serialization (U6),
    asynchronous wait for the map's generation (U4); it impersonates the caller at identification level on the thread
    that makes the next call
      wineserver of session 0: the request accounts_forward (new, system tier): the server reads the
      IMPERSONATION token of the calling thread, makes the verdict itself (below), and sends request plus verdict
      down a Unix socket to the helper
        wex-accountsd (User=wexaccounts): checks the peer, the verdict and the request again, then D-Bus:
        org.freedesktop.Accounts and, for the sync, org.freedesktop.systemd1 StartUnit
          polkit: grants only to the unix account wexaccounts, only the two actions below
```

The verdict is never data the service or the client supplies: the service makes a server request on a thread that
impersonates the caller, and the server computes the verdict from that thread's impersonation token with the shared
predicate (U1). A service thread that does not impersonate anybody gets the verdict "no caller": reads and changes
refused. This keeps the Windows-side service small and unprivileged in what it can claim.

### The link

- A Unix SEQPACKET socket `/run/wineexplorer/accounts.sock`, made by a systemd socket unit (`wex-accountsd.socket`):
  owned by root, in the root-owned directory `/run/wineexplorer` (0711), mode 0660, group = the primary group of session
  0's account (`SocketUser=root`, `SocketGroup=` templated by the installer from `session0_user`), so only that account
  can connect at all; the peer check is still the authority, since any process of session 0 can open the socket. The
  **server connects to the helper**, never the other way round. One connection is served at a time; a second waits in
  the backlog (`Backlog=4`); a connection that sends nothing within 5 s is closed.
- **The peer check (the T1 way), in the helper, on every connection:** `SO_PEERCRED` and `SO_PEERPIDFD`, then
  `wex_sysrun_peer_ok` of `include/wine/wex_sysrun_peer.h` with the rule of session 0's own wineserver: uid = the
  account of session 0 (`session0_user` of the protected `sysrun.conf`), the saved gid the system group, the real gid
  not, the name `wineserver`, the pid pinned. A process of the same uid that is not the server (a Windows service of
  session 0, any program the server runs) is dumpable or has no saved system gid: refused. The pidfd pin guards against
  pid reuse. The helper cannot read `sysrun.conf` (root:wexsys 0640, and the helper stays out of `wexsys`), so the two
  facts it needs, the account of session 0 and the system group, come from `/etc/wineexplorer/accountsd.conf`, written by
  the root tool at sync (0644, never edited by hand).
- **Framing:** a message is one datagram of at most 64 KiB, `{ magic "WEXA", version 1, op, request id, flags }`, a
  verdict block `{ kind: none | user | elevated-administrator, the caller's unix uid, the caller's SID }`, then the
  operation's fields (strings UTF-8, length-prefixed, at most 255 bytes each, nothing NUL inside). An unknown version, op
  or flag, a short or long datagram, or a field out of bounds closes the connection and is logged once.
- **The verdict from the server is trusted only on that link**, and the helper still checks everything it can on its own
  (below): a verdict never widens what the helper accepts, it only allows a class of operation.

**The check, in detail (the reviewer's list, 2026-11-24):**

- It is the **same code** as the server's: the helper includes `include/wine/wex_sysrun_peer.h`, which `server/session0.c`
  (`peer_trusted`), `dlls/ntdll/unix/sysrun.c` and the launcher already share; nothing is copied. The two additions below
  are made in that header, so every user gets them (a stock change, listed in STOCK_CHANGES, with the header's test).
- **Non-dumpable:** the header's argument is that a launcher-started process is not dumpable; the helper now checks it:
  the files in `/proc/<pid>` of a non-dumpable process (not the directory itself) are owned by root, so `stat` of `/proc/<pid>/stat` must give uid 0. A dumpable
  process of the session 0 account (a Windows service, a program the server runs) fails here even if the other fields
  happened to match.
- **Before kernel 6.5** (no `SO_PEERPIDFD`, so no pin; the header already skips it): the pid-reuse window is closed
  by reading the process start time (`/proc/<pid>/stat`, field 22) before and after the status read and requiring
  the same value, and by requiring the pid's `/proc/<pid>` to be the same inode (`st_ino` is not stable across reuse, but
  `st_ctime` and the start time are) before and after. A process that exited and whose pid was reused within the few
  microseconds of the check cannot give the same start time twice. The kernels this project supports (Debian 13, 6.12)
  have the pin; the fallback is for a development machine and is documented, not relied on.
- **The server's side:** the server (`session0.c`) holds the helper's socket path and checks nothing about the helper
  except that the connect succeeded: the helper is root-started by systemd under its own account; the server sends only
  validated fields (below), so a fake helper could learn only what the server would have sent it, and could not change any
  account.

**One policy layer.** The protocol syntax and limits (frame size, field lengths, UTF-16 shape, the connection caps) are
checked by the Windows-side service, which stays in the path because the accounts pipe is a Windows named pipe of session 0
that the bridge serves, and the wineserver does not speak pipes as a server. **Policy is decided only in the helper**: the
UID range, never root, the last administrator, RID 500, the caller's own account, logged-in users, `removeFiles` always
false, the hash form. The server contributes the identity verdict (elevated administrator or not, caller SID and session);
the service contributes nothing of policy. This gives exactly one copy of each rule, in the one process that has the Linux
facts the rules need (the admin group, `/etc/login.defs`, AccountsService's system-account flag). The server cannot hold
those rules without reading them too, which would be a second copy.

### Operations, and what the helper checks

| Op | Verdict needed | The helper's own checks | D-Bus |
|---|---|---|---|
| list users | user | UID_MIN to UID_MAX of `/etc/login.defs`, 65534 excluded, system accounts (AccountsService `SystemAccount`) left out | `ListCachedUsers` + properties |
| get user (name) | user | the name's grammar; UID in range; not a system account | `FindUserByName`, properties |
| admin group members | user | none (reads `getgrnam` of the admin group named in the accounts config) | none |
| create user (name, full name, type, hash) | elevated administrator | name grammar and not existing; the full name at most 255, no control characters; **hash form** `$6$` + bounded `rounds=` + salt + digest, length and alphabet (U2); type 0 or 1 | `CreateUser`, `SetPassword`, then `SetAccountType` |
| delete user | elevated administrator | UID in range; not the caller's own account (the verdict carries it); not the last administrator; not RID 500's account; not a logged-in user (`loginctl`-style check through `ListSessions` or utmp: refused with a distinct status) | `DeleteUser(id, false)` |
| rename user | elevated administrator | the new name's grammar; not existing; not a logged-in user; not RID 500's | `SetUserName` |
| set full name | elevated administrator | as above | `SetRealName` |
| lock / unlock | elevated administrator | not the last administrator when locking; not the caller | `SetLocked` |
| set account type | elevated administrator | not removing the last administrator or RID 500's | `SetAccountType` |
| set password (hash) | elevated administrator | hash form (U2); the account in range | `SetPassword` |
| start the map sync | elevated administrator | none beyond the verdict | systemd1 `StartUnit("wex-accounts-sync.service", "replace")` |

**Serialization (U6):** the helper handles one request at a time, start to end, so the last-administrator check and the
change cannot race (the service also serializes; the helper is the last line). **Statuses:** a small table the service maps
to Windows codes: ok, denied (verdict too low or a protective rule), not found, exists, bad argument, logged in, busy
(AccountsService or polkit refused), unavailable (the bus is not reachable).

### The helper's own hardening

A native program of about 600 lines under `tools/wex-accountsd/` (as `tools/wex-accounts/`), using sd-bus
(`libsystemd`), no other library. `User=wexaccounts`, no supplementary groups (it is **not** in `wexsys`: it must not
carry the system group), `NoNewPrivileges=yes`, `CapabilityBoundingSet=` (none), `ProtectSystem=strict`,
`ProtectHome=yes`, `PrivateTmp=yes`, `RestrictAddressFamilies=AF_UNIX`, `SystemCallFilter=@system-service`,
`MemoryDenyWriteExecute=yes`, `LimitCORE=0`, `prctl(PR_SET_DUMPABLE, 0)`; the password hash is never logged and is
wiped after use; a log line per request (op, verdict kind, result, never the hash). The account `wexaccounts` has no
login shell, no home, and is made by the installer.

### The polkit rules (the host side, step 3, `installer/polkit/`)

```js
polkit.addRule(function(action, subject) {
    if (subject.user != "wexaccounts") return;
    if (action.id == "org.freedesktop.accounts.user-administration") return polkit.Result.YES;
    if (action.id == "org.freedesktop.systemd1.manage-units" &&
        action.lookup("unit") == "wex-accounts-sync.service" && action.lookup("verb") == "start")
        return polkit.Result.YES;
});
```

Nothing else is granted to that account, and no rule names any other uid or a group a user can join.

### What this changes elsewhere, for the orchestrator

- **System tier:** the server request `accounts_forward` (reads the impersonation token of the calling thread, computes
  the verdict with the U1 predicate, forwards to the helper's socket); the server holds the helper's address and the
  account facts from the protected configuration. The server is the only client of the helper.
- **Installer:** the user `wexaccounts`, the socket and service units, the polkit rule, `wex-accounts-sync.service` (a
  oneshot that runs the existing sync), `accountsd.conf` written at sync.
- **netapi32 (stock):** unchanged from U2 and U5.

### Tests of the helper and its link (the reviewer's list)

- a session 0 Windows process that connects to the helper's socket directly is refused (not the server: the peer check);
- the helper restarted with a request in flight gives 1722 to the caller, not a hang (the server's read of the reply has a
  timeout of 15 s and a dead connection is an immediate error; the service maps both);
- the polkit rule refuses session 0's own account calling AccountsService directly (a test that runs `busctl`-style
  `CreateUser` as that account and expects the refusal);
- the installer test checks the unit (User, sandboxing lines, no supplementary groups), the socket (root, 0660, the
  group of session 0's account) and the rule file;
- the helper's own native test (`WEX_ACCOUNTSD_TEST`, like `wex-accounts`' lab options): a fake AccountsService on a
  private bus, every operation, every protective rule, a malformed frame and an unknown version, the hash form, and the
  peer check against a process of the same uid that is dumpable, one that has no saved system gid, one with the wrong
  name, one whose pidfd has gone;
- a hash is never in the journal: the test greps the helper's log for the hash it sent.

### Amendments H1 and H2 (the reviewer's verdict on draft 3, 2026-11-24)

The reviewer: go on the helper itself; H1 and H2 replace the parts of this section they touch, before the server request
is coded. **Why:** a session 0 process holds genuine elevated-administrator impersonation tokens whenever an
administrator's program connects to anything it serves (its pipes, the bridged LRPC endpoints, COM). Wine drops the
client's quality of service (B104), so those are impersonation-level tokens, and a compromised LocalService could
`SetThreadToken` one and call `accounts_forward`.

**H1: the verdict comes from the accounts pipe connection, not from the thread token** (this replaces "the server reads
the impersonation token of the calling thread" in the chain above). `accounts_forward` takes the server end of an
accounts pipe connection, and the server uses **the client token recorded for that connection**. It refuses unless
(a) the pipe has the reserved accounts name, (b) the connection came through the bridge (the link-bound token, X0's rule)
and (c) the instance was created under the reservation. The service therefore passes a handle of the connection, never a
thread's identity.

- **The reservation:** in session 0, only a LocalSystem primary token may create a pipe under the accounts name
  (`\pipe\WexSession0\accounts`); LocalService, NetworkService and other services get access denied. A pipe not created
  under the reservation is not an accounts connection to the server.
- **Session 0's own local processes** (connections that did not come through the bridge) are refused for changes; reads
  are fine.
- **Residual risk, stated:** a compromised LocalSystem process in session 0 could squat the name before the service and relay
  a real administrator's request with altered operations, within the helper's checks (the protective rules and the hash
  form still apply; it cannot create what the helper refuses). This is parity with Windows, where SYSTEM is the trust
  anchor.

**H2: the peer rule** (this replaces the use of `wex_sysrun_peer_ok` for this peer; the header stays the rule for the
user servers). The helper accepts only **the MainPID of session 0's server unit**, read from systemd over sd-bus
(`org.freedesktop.systemd1` `GetUnit`, `MainPID` of the unit named in `accountsd.conf`) **at each connect**, compared with
the pidfd-pinned peer pid (the pid from `SO_PEERCRED`, pinned with `SO_PEERPIDFD`; before kernel 6.5 the start-time
double read of the previous paragraph), **plus** uid = `session0_user` from `accountsd.conf` and non-dumpable
(`/proc/<pid>/stat` owned by root) as a sanity check. Comm, dumpable, uid and saved gid can all be reached by any session 0
process, and one could exec a real `wineserver`; the main process of the unit cannot be forged by a process the server
runs. If session 0's wineserver is not its unit's main process today, it is made so (`ExecStart=` of the session 0 unit
runs it, or the launcher `exec`s it), a change for the system tier and the installer. Systemd must answer: no answer,
no accept.

The reviewer's re-check (2026-11-24): session 0's own Windows services (LocalService and third-party ones included) run
as session 0's uid with the saved gid `wexsys` and a real gid that is not, non-dumpable through the setgid exec (so
`/proc/<pid>/stat` is root-owned for them too), and `prctl(PR_SET_NAME)` gives them any process name. They pass the
header's checks. So those checks, the root-owned `/proc/<pid>/stat` included, are **sanity checks only**; **the identity is
the pidfd-pinned MainPID of the unit, read over sd-bus at each connect.** The helper is coded exactly so (`peer_ok` in
`tools/wex-accountsd/wex-accountsd.c`: the unit's MainPID must equal the pinned peer pid first; the rest only adds). The
underlying invariant is recorded in [SYSTEM_TIER_DESIGN.md](SYSTEM_TIER_DESIGN.md) (master 4c9bbf5e).

**The smaller notes, adopted:** the peer check runs at accept, before anything is read, and a failed one drops the
connection at once; the socket is 0660 with a group whose only member is session 0's account (the installer makes
the group, not the account's primary group if that has other members); the helper refuses user names that collide with
groups or system names (`sudo`, `wexsys`, `wexaccounts`, `root`, and every existing group name); the caller's unix uid
comes from the link (the verdict), never from the message. **"Logged-in users"** are decided from logind
(`org.freedesktop.login1` `ListSessions` and `GetUser`), with `usermod`'s or `userdel`'s own refusal mapped as a backstop.
"Last administrator" and "RID 500's account" are decided by the helper from the Linux facts (accepted).

**Tests added:** a session 0 process with an administrator's impersonation token from its own endpoint calls
`accounts_forward` and is refused (H1); a LocalService creating the reserved pipe is refused (H1); a session 0 process
faking comm, non-dumpable and the uid is refused by the helper (H2); a helper restart in the middle of a request gives 1722.

**H3 (system tier):** the elevated-administrator predicate lives in the server (`token_caller_class`, request
`get_caller_class`), written by the L1 session together with `accounts_forward` and the reservation; the accounts service
calls `get_caller_class` through `wex_caller.h`.

### Open points for the reviewer (draft 3; points 1 to 5 settled by the amendments above)

1. Is the server request, evaluated on an impersonating thread, an acceptable carrier for the verdict, or should the
   service hand the server a handle of the caller's token (the same facts, an extra object)?
2. The rule of the peer check: it assumes session 0's wineserver is started by the launcher and holds the system group
   as its saved gid like the user servers. The reviewer knows the session 0 start better: if session 0's own server
   differs (it runs as the session 0 account without that saved gid), the rule needs a field the header has not got
   ("the peer is the server of the account X, not dumpable, its parent the unit's main process").
3. The helper cannot read the accounts map (it is `root:wexsys` and the helper stays out of the group), so the wait
   for the map's generation (U4) is the service's, and "RID 500's account" and "the last administrator" are decided by the
   helper from the Linux facts (the admin group's members, the built-in administrator's name from `accountsd.conf`),
   not from the map. Acceptable?
4. sd-bus (libsystemd) or libdbus? sd-bus keeps the helper short; libdbus is what `wexhost` already loads.
5. "Not a logged-in user": which source decides, the AccountsService session list or the server's own account records?

### The reviewer's findings on the helper (F1 to F6, 2026-11-24), and what was done

| | Finding | Done |
|---|---|---|
| F1 | the list could overflow one 64 KiB reply and fail whole at about 200 users | the two lists (users, administrators) come in **pages**: the request carries an optional start index, the reply is `u32 total, u32 first, u32 count`, then entries; a page stops before an entry that does not fit; the client asks again from `first + count` (`src/include/wexaccounts_proto.h`). Tested with 405 users with 250-character full names (2 pages) and 1024 administrators of 63 characters. |
| F2 | create was not atomic | after `CreateUser`, a failure of the uid check, `SetPassword` or `SetAccountType` deletes the user again (`DeleteUser(uid, false)`) and reports the original status; tested with an injected failure of each |
| F3 | the lab options bypassed the peer check, gated only by an environment variable | compiled out of every build but one made with `-DWEX_ACCOUNTSD_LAB_TEST` (the test's); `scripts/test_accountsd_units.py` builds the shipped binary and checks that it has none of them in it and refuses each with `WEX_ACCOUNTSD_TEST=1` |
| F4 | the hash and the receive buffer were not wiped | the hash buffer is the caller's in one wrapper that `explicit_bzero`s it on every way out; the receive and reply buffers are wiped after each datagram |
| F5 | the log took the name before validating it | a name is logged only if it is made of `[A-Za-z0-9_.@-]` and at most 63 bytes, else `<invalid>`; tested with a newline and an escape sequence in a name |
| F6 | the default D-Bus wait (about 25 s) is longer than the server's 15 s | every call waits at most 10 s (`sd_bus_set_method_call_timeout`); tested with a call that takes 12 s: answered after 10.0 s |
| setup | `accountsd.conf` is written by `setup-machine-root.sh` from `sysrun.conf` (static after setup) | a change of `sysrun.conf` (the system group, the administrator group, the built-in administrator, session 0's account) needs setup's update to rewrite `accountsd.conf`; the helper reads it at its start, and it leaves when idle, so the next request sees the new file |
| nits | the empty `if`; `gr_mem` only; nscd/sssd staleness | removed; the administrators are read from `/etc/group` and `/etc/passwd` themselves (the group's listed members and the users whose primary group it is), never through `getgrnam`, so a change AccountsService just made is seen at once; the admin group is a local one |
