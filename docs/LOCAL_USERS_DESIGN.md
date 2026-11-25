<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Local Users and Groups (design note)

Status: draft 1, 2026-11-24, GPO and Domain agent. For review by the Session 0 reviewer and the orchestrator before any
code (step 3 of the Computer Management plan, [XP_SETTINGS_GAPS.md](XP_SETTINGS_GAPS.md)). It is the back end of the
`lusrmgr.msc` node of Computer Management, and of `net user`, `net localgroup` and, later, the User Accounts panel.
Constraints from the orchestrator (2026-11-24): the authority stays the Linux accounts plus
`/etc/wineexplorer/accounts.map`, kept in sync by the root tool `wex-accounts` and its watcher
([E1_S4_ACCOUNTS_MAP.md](E1_S4_ACCOUNTS_MAP.md)); **Wine never writes the map**; account changes go through D-Bus
(AccountsService, polkit-authorized for an administrator), as OOBE does (`src/dlls/wexhost/unixlib.c`, `user_create`);
`netapi32`'s `NetUser*` and `NetLocalGroup*` become clients of that path, so every program gets it, not only the snap-in.

## What exists

- `netapi32` (`dlls/netapi32/netapi32.c`) keeps a per-process in-memory `sam_user` list: `NetUserAdd` adds to it,
  `NetUserDel` removes from it (and returns `ERROR_INVALID_LEVEL`, a stock bug), nothing reaches the host, nothing
  survives the process. `NetUserSetInfo`, `NetUserSetGroups`, `NetGroupAdd` and others are stubs;
  `NetLocalGroupAdd` and `NetLocalGroupAddMember(s)` are stubs that succeed.
- `wexhost` already talks to `org.freedesktop.Accounts` from the session: `FindUserById`, `ListCachedUsers`, and (OOBE)
  `CreateUser` with account type 1 (administrator = the sudo group), `SetPassword` (a crypt(3) SHA-512 hash built in
  the module, libcrypt loaded at run time), `SetPasswordMode`, `SetXSession`, `SetAutomaticLogin`.
- The accounts map gives every account a stable SID (`S-1-5-21-<machine>-<RID>`, RID 500 for the built-in
  administrator, counter RIDs from 1001; domain accounts from version 2). `wex-accounts sync` runs from a path unit and a
  timer, so a new Linux account reaches the map by itself, as root, without Wine writing it.
- The server decides "administrator" with one rule: membership of `admin_group=` of `sysrun.conf` (default `sudo`),
  plus Domain Admins (RID 512) of the joined domain (`wex-is-admin`, `user_is_administrator`).

## Goals

1. `lusrmgr.msc` (a node of Computer Management and a console of its own): list, create, rename, set the full name and
   description of, disable, enable, set the password of and delete local users; list the local groups and their members;
   put a user in the Administrators group or take the user out.
2. `net user` and `net localgroup`, and any program using `NetUser*`/`NetLocalGroup*`, see and change the same accounts.
3. Nothing here writes the accounts map, the token, or session 0's registry: the map follows the Linux accounts by its own
   watcher; tokens are made by the server from the map and the group rule, as now.

Non-goals (stated as grayed items with a reason, never silently ignored): password policy and age, "password never
expires", logon hours, profile and home paths, terminal services profile (the host's PAM and the domain own them);
creating groups of our own (see Groups); remote computers (`ERROR_NOT_SUPPORTED`).

## The model

| Windows | Here |
|---|---|
| a local user | a Linux account of UID 1000 or more (AccountsService's non-system users), SID from the accounts map |
| full name | the account's real name (GECOS, AccountsService `RealName`) |
| description (comment) | AccountsService has no field for it and D-Bus cannot reach the other GECOS parts. Proposed: the description is empty and read-only, shown grayed with the reason. Phase 2 may keep it in a root-owned file the root tool maintains. |
| account disabled | AccountsService `Locked` (the host's `passwd -l` state); the logon screen already hides locked accounts |
| administrator / standard | AccountsService `AccountType` 1 / 0 = membership of the admin group (sudo) |
| Administrators (S-1-5-32-544) | the admin group, members = users in it (+ Domain Admins implicitly on a joined machine, read-only, as on Windows) |
| Users (S-1-5-32-545) | every local account and Authenticated Users: computed, membership not editable |
| Guests, Power Users, Backup Operators, Replicator, others | shown only if they have a Linux group behind them: none in phase 1, so the node lists Administrators and Users; the rest are absent, not grayed |
| user rename | `wex-accounts rename` (root tool) exists; through D-Bus only `SetUserName`, which AccountsService implements; the map keeps the SID across a rename (`wex-accounts` rename rule) |

SIDs are never made by `netapi32`: a user's SID in `NetUserGetInfo` level 23 and the snap-in comes from the accounts map
(read through the existing lookup the server already offers to `LookupAccountName`), so the answer matches the token's.

## Layers

```
lusrmgr snap-in / net.exe / any program
    netapi32: NetUserAdd/Del/Enum/GetInfo/SetInfo/ChangePassword, NetLocalGroup*   (Wine's names and structures)
        wexhost.dll (PE side) -> wexhost unixlib: libdbus to the SYSTEM bus
            org.freedesktop.Accounts (AccountsService), polkit decides
```

- `netapi32` calls `wexhost` only on a WineExplorer system; on any other Wine the in-memory list stays (it is stock
  behaviour, and Wine's own tests of it must keep passing): the switch is the same one the other wexhost clients use
  (`wexhost` loadable and D-Bus reachable), decided once per process.
- Reads (enumerate, get info, group members) need no authorization: AccountsService `ListCachedUsers` and
  `FindUserByName`, `GetUid`, `Locked`, `AccountType`, and `getgrnam` for the admin group's members (read directly from
  the group database: it is world readable). A user with UID 1000 or more that AccountsService has not cached yet is
  found with `CacheUser` (also unauthorized for reading).
- Writes call `CreateUser`, `DeleteUser(id, removeFiles=false)`, `SetRealName`, `SetAccountType`, `SetLocked`,
  `SetUserName`, `SetPassword`, each on a user object path, from the process of the caller.
- Errors map to Windows' codes (`NERR_UserExists`, `NERR_UserNotFound`, `NERR_InvalidComputer`, `ERROR_ACCESS_DENIED` for
  a polkit refusal, `NERR_PasswordTooShort` is never returned: the host decides, and says `ERROR_INVALID_PASSWORD` with
  PAM's text in the debug log).
- Safety limits in the client: names the account grammar of the accounts map accepts (63 bytes ASCII, no `/`, no
  leading `-`, nothing AccountsService refuses); a request for UID below 1000, `root` or the built-in administrator is
  refused before D-Bus; `DeleteUser` of the caller's own account, of the last administrator, and of RID 500 is refused
  with `NERR_LastAdmin` / `ERROR_ACCESS_DENIED`.

## Authorization (the decision that needs the reviewer)

AccountsService asks polkit for `org.freedesktop.accounts.user-administration` and the caller is **the user's own
process**, so polkit's default is "authenticate as an administrator". Windows' model is different: an administrator's
elevated process is allowed, and the consent prompt (WineExplorer's `consent.exe`, through the broker) is the question.
Two prompts would be wrong, no authority at all would be worse. Proposed:

1. The snap-in and `net.exe` run elevated through the existing path (the consent prompt, the broker): the elevated
   process has the full token, Administrators enabled and the **system group** as its supplementary group (what the
   elevation tests already assert).
2. A polkit rule in `installer/polkit/` grants `org.freedesktop.accounts.user-administration` to a caller for which
   `polkit.spawn(["/usr/local/libexec/wineexplorer/wex-polkit-elevated", pid])` exits 0. That helper reads
   `/proc/<pid>/status` (its supplementary groups) and requires: the system group among them, the process uid an
   administrator by `wex-is-admin`, uid the same as the D-Bus caller's, and the process started by the launcher (its
   exe is the Wine loader of the machine root: the same fact the server checks for the system role). Anything else:
   the rule returns nothing and polkit's default applies. The rule never grants to a process without the group.
3. Everything the polkit rule allows is the user-administration action only (not `login-session`, not
   `change-own-password`).

Open points for the reviewer: (a) whether the system group on a process is a good enough proof of "passed the consent
prompt" (the server grants it only for the fixed system programs and the broker's elevated children; a user process
cannot gain it); (b) whether polkit's JS `spawn` helper reading `/proc/<pid>` is acceptable against pid reuse (the
D-Bus caller is a connection; polkit gives the subject's pid and start time: the helper takes the start time too and
compares); (c) if (a) is not acceptable, the fallback is a root request spool as for `gpupdate` (the session writes a
request, a root unit validates and applies it, all through the same elevated-process proof): that leaves D-Bus, which
the orchestrator asked for, so it is only the fallback.

Without elevation (limited administrator or standard user) writes fail with `ERROR_ACCESS_DENIED`; the snap-in itself is
started through the consent path (`runas` style), as Computer Management is on Windows for a limited administrator.

## Groups

AccountsService has **no group API**. Phase 1 therefore has exactly what it can do:

- **Administrators**: members are the users in the admin group; add or remove is `SetAccountType(1|0)` on that user.
  The built-in administrator (RID 500) cannot be removed; the caller cannot remove the last administrator.
  `NetLocalGroupAddMembers`, `NetLocalGroupDelMembers`, `NetLocalGroupSetMembers`, `NetUserSetGroups` for
  Administrators reduce to those calls; for any other group they return `NERR_GroupNotFound`.
- **Users**: `NetLocalGroupGetMembers` lists all local accounts (computed); changing it returns
  `ERROR_ACCESS_DENIED`.
- `NetLocalGroupAdd` of a new name returns `ERROR_ACCESS_DENIED` with the debug text "custom local groups need the group
  service (phase 2)": the snap-in's New Group item is grayed with that reason.
- **Phase 2 (to decide with the orchestrator, not part of this review):** custom local groups and Power Users and the
  like as Linux groups `wexgrp-<name>` made by a root helper (the request spool above), and their SIDs by the token
  side (E1), since a group SID in a token needs the server to know it. This is where Domain accounts as members of a
  local group would be stored too.

### A domain account in a local group (the orchestrator's question)

- Domain Admins is in Administrators by default on a joined machine, as on Windows: the server already treats it as the
  administrator group, and the snap-in shows it as a member of Administrators, read-only.
- An individual domain user or other domain group as a member of Administrators or Users cannot be stored by AccountsService
  (it manages local users only). Phase 1: the Add Member dialog resolves `DOMAIN\name` through Winbind and answers
  "not supported yet" for it; no silent success. Phase 2 stores them in a root-owned file next to the map, written by
  the same root helper, read by the server when it builds the token (the same shape as `domain-groups`, WO 2 of the
  domain work), so it needs the token side and the orchestrator.
- Domain users are listed in the snap-in under the same Users node (grayed, with the domain name) only when they have been
  admitted (they have a record in the map); an account never seen on this machine has no record and no SID here.

## The snap-in

`src/dlls/lusrmgr` (a module like the others; Computer Management hosts it as `Local Users and Groups` with `Users` and
`Groups` below it, the console `lusrmgr.msc` shows only that node). Users: columns Name, Full Name, Description (always
empty, phase 1); New User (name, full name, password twice, "User must change password at next logon" grayed, "Account is
disabled"), Set Password, Rename, Delete, Properties (General: full name, disabled; Member Of: the groups, Add and Remove
as the Groups section allows). Groups: Administrators and Users, Properties with Members and Add/Remove. Every grayed item
has its reason in the status line. Changes go through `netapi32` only (no D-Bus code in the snap-in).

## Tests

- **Wine tests** (netapi32 `tests`, both architectures): the stock in-memory behaviour unchanged when `wexhost` cannot be
  loaded; the new paths against a fake `org.freedesktop.Accounts` service on a private bus: create, enumerate,
  set real name, lock, set type, set password, delete, each refusal in the client's limits, each polkit error mapped.
- **The suite** (a section `localusers`, after `svc_table`): on the fixture, with a private `dbus-daemon` and a small
  AccountsService stand-in the section starts, drive `net user` and the snap-in's netapi32 calls as the elevated
  administrator (the c4 broker stub) and as a limited and a standard user; check that the accounts map is not touched
  by Wine (its bytes unchanged until `wex-accounts sync` runs), that a user created appears after the sync with a SID, and
  that the polkit helper refuses a process without the system group, with the wrong start time, and for another uid.
- **The lab** (wexgpo, an installed machine with the real AccountsService and polkit): the whole path once, end to end,
  including a Domain Admins user shown in Administrators.

## Order of work

1. This note, reviewed. 2. The polkit helper and rule, with their tests (they are the security part; the reviewer first).
3. `wexhost` account calls (a new small export set, not the OOBE ones changed). 4. `netapi32` clients and Wine tests.
5. The snap-in (and `net.exe` wiring). 6. Suite section and lab run. Phase 2 (custom groups, domain members) only after
the orchestrator and E1 decide the token side.

## Questions to the orchestrator

1. Authorization: is the system-group proof (above) the right fact, or does the server offer a better one for "this
   process passed consent"? A polkit helper that asks the server is possible (`wex-is-admin` style) but needs a
   server-side query that does not exist.
2. May `netapi32` (stock Wine) depend on `wexhost` at run time (as `wexhost`-using stock modules already do, listed in
   STOCK_CHANGES), loaded with `LoadLibrary` and absent on plain Wine?
3. Phase 2: do you want custom local groups at all in the first release, given the token side?
