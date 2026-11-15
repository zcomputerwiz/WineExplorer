# Domain users logging on to WineExplorer (design note)

Written 2026-11-15 by the Group Policy session. Status: design; nothing implemented. The repository facts were
read from the code (`wine/server/accounts.c`, `token.c`, `docs/E1_S4_ACCOUNTS_MAP.md`); the Windows and Winbind
facts in the next two sections are from memory and still to be checked against the research that is running
(Antigravity), with sources added then. Why: Group Policy reaches the
machine ([GROUP_POLICY.md](GROUP_POLICY.md)), but user policy from the domain cannot reach a user, because only
local users can be WineExplorer accounts. This note says what has to change for a user of the domain the host is
joined to (Samba, Winbind) to log on, with the Windows identity XP gives a domain user. It touches the accounts
authority and the logon path of the system tier (E1, session 0), so the work orders below are to be agreed with
that session before any of it starts.

## What XP does for a domain user

- The token's user is the domain account's SID: the domain SID plus the account's RID. The token also holds
  Domain Users (domain SID plus RID 513), the domain groups of the account, and the local groups the domain
  groups are members of (Domain Users is a member of the local Users group of every member computer).
- `GetUserName` gives `user`; `GetUserNameEx(NameSamCompatible)` and `LookupAccountSid` give `DOMAIN\user`.
- The profile is `C:\Documents and Settings\user`, or `user.DOMAIN` when a local account of that name already
  has `user`; its hive is loaded under `HKEY_USERS\<domain SID>-<RID>`.
- Logon without the controller works with cached credentials (`CachedLogonsCount` under
  `HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon`, ten by default).

## What the host gives

On a host joined with Winbind (`security = ads`, `nsswitch.conf` with `winbind` for passwd and group, PAM with
`pam_winbind`), a domain user is a Linux user like any other:
- `getpwnam`/`getpwuid` answer for the user, with the UID the idmap backend gives (with `idmap config DOMAIN :
  backend = rid`, the RID plus the range's base), so it is stable across hosts with the same configuration;
- `wbinfo --name-to-sid`, `--uid-to-sid`, `--sid-to-uid` map between the user, the UID and the domain SID;
- the shell and home directory come from `template shell` and `template homedir`; the default shell is
  `/bin/false`;
- PAM authenticates against the controller (Kerberos), with `cached_login` for logon while the controller is
  away (`winbind offline logon = yes`), and `pam_mkhomedir` makes the home directory at the first logon.

## What stands in the way today

1. **The accounts map holds local accounts only.** `wine/server/accounts.c` (`parse_map`) takes a record's SID
   only as `21 <machine_a> <machine_b> <machine_c> <rid>` with the header's three numbers, or the one legacy
   SID; a RID must lie between 1001 and the header's counter. A domain SID cannot be written in it.
2. **Accounts enter the map only from `/etc/passwd`.** `wex-accounts sync` enumerates the local passwd file;
   accounts that only the directory knows are left out on purpose (`docs/E1_S4_ACCOUNTS_MAP.md`). A domain user
   has no record, so the server finds no SID for the UID (`accounts_own_sid`, used by `token.c` and
   `session0.c`) and session 0 has no account to load a hive for.
3. **The default shell.** `lookup_account` refuses an account whose shell is empty, `nologin` or `false`;
   Winbind's default template shell is `/bin/false`. `template shell` has to be set on a joined host (the lab's
   `machine-guest.sh` does not set it yet).
4. **The names.** `advapi32` resolves the caller's own mapped SID to `WINEUSERNAME` on this computer
   (`LookupAccountSidW`, `docs/STOCK_CHANGES.md`); a domain account needs the domain's flat name as its domain,
   and `GetUserNameEx(NameSamCompatible)` needs `DOMAIN\user`.
5. **The groups.** The token gets the account's SID and the groups the server computes for a local account;
   Domain Users and the account's domain groups are not there, nor the local Users membership through Domain
   Users.
6. **Administrators.** `administrator_member` asks NSS whether the user is in `admin_group`; with Winbind, a
   domain group such as `domain admins` can be that group, so this part works once the account has a record.
7. **The profile folder** is named after the account; a domain user with the name of a local account needs the
   `user.DOMAIN` rule.
8. **Group Policy for users.** `wex-gpupdate.sh` already looks at the active accounts of the map and asks Winbind
   whether each is a domain user, using the map's SID; with a domain record that SID is the domain SID, which is
   the SID user policy is recorded under (`wexpolicy apply /user:SID`). It needs no change beyond the record.

## Design

**A. Domain records in the accounts map (format version 2).** The header gains the joined domain's SID
(`21 d1 d2 d3`) and flat name; a record says whether its SID is the machine's (`local`) or the domain's
(`domain`), and a domain record carries the domain RID as it is (any RID, no counter). Rules: a domain record's
name is the Winbind name of the user (with `winbind use default domain = yes`, the plain name); a local and a
domain record cannot share a name or a UID while both are active; a change of the joined domain (leave, join
another) turns the old domain's records into tombstones. Version 1 maps are read as before, and `sync` writes
version 2 only once a domain record exists. The server, `wex-accounts`, `wex-is-admin`, `pam-worker` and the
broker read the map through `accounts.c`, so the parser change reaches all of them; the E1 work on the map
(generation, incarnation keys, tombstones) stays as it is.

**B. Admission at logon.** A domain user gets a record at the first logon, not by enumeration (a domain can have
many thousands of users). Root's part of the logon (a `pam_exec` line in the session stack of the greeter's PAM
service, as root) runs `wex-accounts admit USER`, which:
- asks Winbind for the user's SID (`wbinfo --name-to-sid`) and checks it is a user of the joined domain (the SID
  under the domain SID that `wex-domain-state.sh` recorded);
- checks the NSS round trip (name, UID, primary group), as `sync` does for local users;
- writes an active `domain` record with a new incarnation key, under the map's lock, raising the generation;
- does nothing when an active record for that SID and UID exists.
`sync` keeps the domain records it does not see in `/etc/passwd` and checks them against Winbind instead; a user
that Winbind no longer knows becomes a tombstone only when the controller says so, never because it is
unreachable (offline logon).

**C. The token.** For a domain record the server builds the user SID from the header's domain SID and the RID,
and adds Domain Users and the domain groups Winbind gives for the user (`wbinfo --user-domgroups` or the
`getgrouplist` GIDs mapped back with `--gid-to-sid`), and the local Users group. Asking Winbind from the server is
a new dependency; the alternative is for `admit` to record the groups in the map at logon (they are then as old as
the logon, as on Windows, where a token's groups are fixed at logon too). This note prefers recording them at
logon.

**D. The names.** `LookupAccountSidW` resolves a domain record's SID to `DOMAIN\user` (the flat name from the
header), `GetUserNameExW(NameSamCompatible)` gives the same, and `NetGetJoinInformation` already gives the
domain. `USERDOMAIN` in the environment is the flat name.

**E. The profile.** The managed profile is created as for a local account; its folder is `user` or, when that is
taken by a local account, `user.DOMAIN`. Its hive is `HKEY_USERS\<domain SID>-<RID>`, so the policy writer's
service restores the user's policy into it at logon with no change (it matches every `S-1-5-21-` hive).

**F. The host configuration.** Joining the host (the installer's or an administrator's `net ads join`) sets
`template shell = /bin/bash` and `template homedir = /home/%U`, `winbind offline logon = yes`, and `pam_winbind`
with `cached_login` and `krb5_auth`, and `pam_mkhomedir`. The lab's `machine-guest.sh` does the same.

## Work orders (proposed; to agree with the system tier session)

1. Format version 2 of the map and the parser (`accounts.c`), with the tool's `admit` and the `sync` rules for
   domain records; suite cases with a fake Winbind (`WEX_ACCOUNTS_TEST` already fakes passwd and group).
2. The token for a domain record (user SID, Domain Users, recorded domain groups, Users) and the names
   (`LookupAccountSidW`, `GetUserNameExW`); 32-bit and 64-bit probes.
3. The logon: the `pam_exec` line, the profile folder rule, `USERDOMAIN`.
4. The lab: `machine-guest.sh` logs `wexuser1` on through the greeter (the host side of a logon, as the E1 tests
   do), checks the token's SID, `DOMAIN\user`, the profile, and the user policy of "WexLab Domain"
   (`HKCU\Software\Policies\WexLab UserLimit = 3`), and `gpresult /scope user`.

## Open questions

- Groups: recorded at logon (preferred here) or asked of Winbind by the server.
- Names: `winbind use default domain = yes` (plain names, which can collide with local ones) or `DOMAIN\user`
  names in the map (no collisions; `accounts_valid_name` would have to allow the separator).
- SSSD-joined hosts: the same design with `sss` in place of Winbind for the SID lookups; Group Policy needs
  Winbind today ([GROUP_POLICY.md](GROUP_POLICY.md)), so Winbind first.
- Several domains (trusts): one joined domain only in this design.
