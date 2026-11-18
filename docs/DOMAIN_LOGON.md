# Domain users logging on to WineExplorer (design note)

Written 2026-11-15 by the Group Policy session. Status: design; nothing implemented. The repository facts were
read from the code (`wine/server/accounts.c`, `token.c`, `docs/E1_S4_ACCOUNTS_MAP.md`) and cross-checked with
a survey by Antigravity, whose code claims used below were checked by hand; the Windows and Winbind facts carry
their sources (end of the note). Why: Group Policy reaches the
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
5. **The groups.** The token gets the account's SID and the groups the server computes for a local account; its
   owner and primary group is a fixed `domain_users_sid`, `S-1-5-21-0-0-0-513` (`token.c`), not the domain's
   Domain Users; the account's domain groups are not there, nor Domain Admins in the local Administrators.
5b. **Names in advapi32 beyond the caller.** `LookupAccountNameW` resolves only names on this computer and
   `ADVAPI_GetComputerSid` is the fixed `S-1-5-21-0-0-0` (`dlls/advapi32/security.c`).
5c. **The greeter.** `logonui` shows a tile for each user AccountsService has cached (`ListCachedUsers`,
   `src/dlls/wexhost/unixlib.c`); a domain user who never logged on has no tile, so the first logon needs the
   classic dialog (`msgina`, a typed name) and it has to accept a domain user.
5d. **Name checks.** `accounts_valid_name`, `valid_account_name` (`wex_profile_storage.c`) and
   `scripts/personalize-prefix.sh` take plain names only (no `\` or `@`); with `winbind use default domain =
   yes` a domain user's name is plain, so this matters only for `DOMAIN\user` names.
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
makes the domain's Domain Users (RID 513) the primary group and owner in place of the fixed one, puts the local
Administrators group in the token for Domain Admins (RID 512) as joining does on Windows, and adds the domain
groups Winbind gives for the user (`wbinfo --user-domgroups` or the
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
`template shell = /bin/bash` and `template homedir = /home/%D/%U` (not `/home/%U`: a domain user must not share a
local account's home), `winbind use default domain = no`, `winbind offline logon = yes`, and `pam_winbind`
with `cached_login` and `krb5_auth`, and `pam_mkhomedir`. The lab's `machine-guest.sh` does the same.

## Agreed with the system tier session (2026-11-15)

The design is agreed, with these conditions, which take precedence over the text above where they differ:

1. **Names are `DOMAIN\user` in the map** (`winbind use default domain = no`), never plain: a plain domain name
   could collide with a local account and an ordering mistake would hand one identity to the other. The plain
   name is only for display and the profile folder.
2. **Admission takes the user only from `PAM_USER`** in the root `pam_exec` step, never from an argument or
   variable the user's session can influence. It refuses a name that maps to a local record, a UID below 1000 or
   in the local range, and a SID whose domain part is not the header's domain SID; all under the map lock.
   Failure is fail-closed: no logon, never a logon with a local or fallback identity.
3. **Domain Admins goes through the same elevation model as local administrators**: the logon token is limited
   (Administrators deny-only) and elevation goes through the broker and consent. WO 2's tests check that a domain
   administrator never gets a full token at logon.
4. **Groups are recorded only by the root step at authentication** (Winbind or the PAC, as root), in a root-owned
   place the server reads, never from anything the user's processes write. Offline, the last recorded set is
   used; groups are never added then.
5. **Version 2 parsing fails closed** as version 1 does: an unknown record kind, a domain record without a header
   domain SID, or a domain SID in a local record make the map invalid. Version 1 maps keep working unchanged.
   Fuzz-style suite cases cover these (the parser runs in the server). Whole-map rejection, as in version 1, was
   confirmed by the system tier session: skipping records could silently drop or re-pick identities.
6. **Tombstones**: never while the controller is unreachable; joining a different domain tombstones the old
   domain's records.
7. **The local computer SID scheme is unchanged** (`ADVAPI_GetComputerSid`, `S-1-5-21-0-0-0`); domain SIDs come
   only from the map's header.

Ownership: the Group Policy session writes WO 1 to 4; the system tier session reviews WO 1 and WO 2 (the map
parser and the token are the system tier's security core) before they merge.

## State

- WO 1 (map version 2, admit, sync, restore): on master since 2026-11-15 (93baa899, Wine f1347be7).
- WO 2 (token and names): on master since 2026-11-15 (cec68383, Wine a8a504bb). A limited token keeps the UAC filtered groups (Administrators, Domain Admins and the other
  privileged domain groups and builtin aliases) for deny only; the elevated token has them enabled. The groups file and its rules are in
  [E1_S4_ACCOUNTS_MAP.md](E1_S4_ACCOUNTS_MAP.md); the token, `user_is_administrator`, `LookupAccountSidW` and
  `GetUserNameExW` in [STOCK_CHANGES.md](STOCK_CHANGES.md). `wex-domain-state.sh` records the domain SID (`Sid`)
  beside the flat and DNS names. Tests: `scripts/test_accounts_map.sh` (groups recorded and filtered, Domain Admins
  makes an administrator and removal undoes it at the next logon, Winbind not answering keeps the groups, a damaged
  groups file gives no groups, the domain's Administrator RID 500 is an ordinary domain account) and
  `scripts/test_sysrun_server.sh` (`st_domain_accounts_token_names_and_domain_admins_limited`: the token's user,
  primary group and groups, DOMAIN\user from both APIs in 64-bit and 32-bit programs, a member of Domain Admins with
  Administrators deny-only and a Limited token at logon, a local account unchanged).

- After WO 4 (2026-11-16, in review): the admission step also asks AccountsService to cache the user (`CacheUser`), so
  the Welcome screen has the tile after the first logon (it had none: AccountsService lists only users with a local
  passwd entry; `busctl` runs with a clean environment and a 3 second timeout). A user's policy arrives after the
  shell is up: the session (`wine-explorer-session-common.sh`, `wex_refresh_policy_after_logon`) asks for a refresh
  through `gpupdate` once an `explorer.exe` of the user runs, as Windows applies user policy after the profile has
  loaded. The first version started the refresh from the admission step, which is where an intermittent
  failure of the session to register with session 0 (B26) showed. (Since 2026-11-17 the PAM step also applies a user's
  policy on the first logon, before the session exists: `wex-gpupdate.sh --logon`, bounded to 60 seconds, so it does
  not compete with the session's registration; GROUP_POLICY.md, "Policy before the desktop".) The lab tests the tile, the policy and the
  offline logon with the controller unreachable ([GPO_LAB.md](GPO_LAB.md), 34 checks).
- WO 4 (the lab, end to end): `machine` logs `WEXLAB\wexuser1` on through the greeter and passes all 24 checks
  (2026-11-16; [GPO_LAB.md](GPO_LAB.md)). It found three gaps, fixed in the same change: `wex-sysrun` accepts one
  backslash in a passwd name (`safe_account_name`: a safe flat name of 1 to 15 and a safe user name of 1 to 63
  characters; four malformed shapes are refused, `scripts/test_wex_sysrun.sh`); `wex-gpupdate.sh` reads a version 2
  map (the SID columns, domain records only) and asks Samba for the plain name
  (`scripts/test_gpupdate_users.sh`); `record_domain_groups` leaves out the user's own SID. In review.
- WO 3 (logon): on master since 2026-11-16 (b91c3631, Wine 9bbba491).
  - Admission: the session package adds
    `account requisite pam_exec.so quiet /usr/local/sbin/wex-domain-admit-pam.sh` to `/etc/pam.d/lightdm` and
    `lightdm-autologin`, after `common-account` (pam_winbind). The script passes a local account (a plain name) and
    runs `wex-accounts admit` for `DOMAIN\user`; it refuses a domain account when the system tier's `wex-accounts`
    is not installed (fail closed: no identity without a record). The script sets its own `PATH` and runs the tool
    with only `PAM_USER` and `PAM_TYPE` (`env -i`): pam_exec passes the whole PAM environment, which can carry a
    user's `pam_env` settings.
  - Only LightDM runs the step. A domain user who logs on another way (a text console, ssh, su) gets no record from
    it, so Wine has no domain identity for that user (no SID under the domain, no recorded groups): this fails safe,
    with fewer rights, never more.
  - The classic dialog (`logonui`, `resolve_domain_account`): `DOMAIN\user` with the joined domain's flat name
    (`HKLM\Software\WineExplorer\Domain`, `FlatName`) goes to PAM as `FLAT\user` (user in lower case, 1 to 20
    characters of `A-Z a-z 0-9 . - _`) without a tile; any other domain is refused as before. A domain user who has
    logged on once may get a tile from AccountsService; that is not verified (Open questions). It assumes Winbind's
    default separator `\`; a host with `winbind separator = +` is not supported.
  - The profile folder is the plain name (`wine-explorer-session-common.sh` personalizes the template prefix with
    `${USER##*\}`, as Wine's `WINEUSERNAME`). The `user.DOMAIN` rule of E is not needed: every account has its own
    prefix under its own home (`/home/DOMAIN/user`), so a domain account and a local one of the same name never share
    `C:\users`; the hive is keyed by SID (`HKEY_USERS\<domain SID>-<RID>`). The managed profile layout (lab builds)
    keys folders on SID and incarnation already.
  - `USERDOMAIN` (wineboot's Volatile Environment) is the flat name for a domain account, the computer name for a
    local one and for SYSTEM, LocalService and NetworkService (only an S-1-5-21 SID takes the looked-up domain, so
    never "NT AUTHORITY"). `LOGONSERVER` stays `\\COMPUTER`; Windows gives `\\DC` for a domain account (WO 4 or
    later).
  - The lab (`machine-guest.sh`, `client-provision.sh`) joins with `winbind use default domain = no`,
    `template homedir = /home/%D/%U`, `template shell = /bin/bash` and `winbind offline logon = yes`.
  - Tests: `scripts/test_accounts_map.sh` (the PAM step: a local account passes without the tool, a domain account
    is refused without it, admitted through it with `PAM_USER` and `PAM_TYPE` and nothing else of the environment,
    refused when it refuses),
    `scripts/test_sysrun_server.sh` (`USERDOMAIN`, `USERNAME` and `USERPROFILE` of a domain account, the
    `USERDOMAIN` of a local account and of SYSTEM in session 0), `scripts/test_safemode.py` (the postinst lines, once each). The greeter's typed domain
    logon is checked end to end by WO 4.

## Work orders

1. Format version 2 of the map and the parser (`accounts.c`), with the tool's `admit` and the `sync` rules for
   domain records; suite cases with a fake Winbind (`WEX_ACCOUNTS_TEST` already fakes passwd and group).
2. The token for a domain record (user SID, Domain Users, recorded domain groups, Users) and the names
   (`LookupAccountSidW`, `GetUserNameExW`); 32-bit and 64-bit probes.
3. The logon: the `pam_exec` line, the classic dialog taking a domain user who has no tile yet, the profile
   folder rule, `USERDOMAIN`.
4. The lab: `machine-guest.sh` logs `wexuser1` on through the greeter (the host side of a logon, as the E1 tests
   do), checks the token's SID, `DOMAIN\user`, the profile, and the user policy of "WexLab Domain"
   (`HKCU\Software\Policies\WexLab UserLimit = 3`), and `gpresult /scope user`.

## Open questions

- Groups: recorded at logon (preferred here) or asked of Winbind by the server.
- Names: `winbind use default domain = yes` (plain names, which can collide with local ones) or `DOMAIN\user`
  names in the map (no collisions; `accounts_valid_name` would have to allow the separator).
- SSSD-joined hosts: the same design with `sss` in place of Winbind for the SID lookups; Group Policy needs
  Winbind today ([GROUP_POLICY.md](GROUP_POLICY.md)), so Winbind first.
- Several domains: decided 2026-11-16 (owner): one joined domain at a time, as on Windows, where a computer is a
  member of exactly one domain. Users of other, trusted domains (which Windows lets log on to a member computer)
  are not covered: a name of another domain is refused (the greeter, `admit`, the map's domain SID). Offline logon
  and the tile after a first logon are tested in the lab ([GPO_LAB.md](GPO_LAB.md)).
- Settled by the lab: AccountsService does not list a Winbind user on its own (the admission step caches them); unverified: where `pam_winbind`
  puts the Kerberos ticket cache for a LightDM session (`KRB5CCNAME`).

## Sources

- Samba: [Winbind](https://wiki.samba.org/index.php/Winbind),
  [pam_winbind(8)](https://www.samba.org/samba/docs/current/man-html/pam_winbind.8.html),
  [pam_winbind.conf(5)](https://www.samba.org/samba/docs/current/man-html/pam_winbind.conf.5.html),
  [idmap_rid(8)](https://www.samba.org/samba/docs/current/man-html/idmap_rid.8.html),
  [wbinfo(1)](https://www.samba.org/samba/docs/current/man-html/wbinfo.1.html),
  [PAM offline authentication](https://wiki.samba.org/index.php/PAM_Offline_Authentication).
- Microsoft: [Security identifiers](https://learn.microsoft.com/en-us/windows/win32/secauthz/security-identifiers),
  [Well-known SIDs](https://learn.microsoft.com/en-us/windows/win32/secauthz/well-known-sids),
  [HKEY_USERS](https://learn.microsoft.com/en-us/windows/win32/sysinfo/hkey-users),
  [Cached domain logon information](https://learn.microsoft.com/en-us/troubleshoot/windows-server/user-profiles-and-logon/cached-domain-logon-information),
  [GetUserNameExW](https://learn.microsoft.com/en-us/windows/win32/api/secext/nf-secext-getusernameexw),
  [NetGetJoinInformation](https://learn.microsoft.com/en-us/windows/win32/api/lmjoin/nf-lmjoin-netgetjoininformation).
  The `user.DOMAIN` profile folder rule is from Microsoft's troubleshooting articles on profile folder names (the
  exact page not re-checked).

## The supported join (2026-11-18)

Winbind is the supported way to join (owner's decision): `net ads join` with Winbind, `libnss-winbind` and `libpam-winbind`.
Everything here (the accounts map's domain records, the admission step, `wbinfo` and `net ads` in `wex-gpupdate.sh`)
is built on it. `installer/scripts/wex-domain-join.sh` joins and leaves that way (`DOMAIN_JOIN.md`); a join made with SSSD
(`realm join`) is reported by `wex-domain-state.sh --kind` as `sssd`, refused by the join tool, and gets local policy
only; its domain users are not admitted at logon.
