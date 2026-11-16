# Group Policy and Linux GPO sync (design note)

Surveyed on 2026-11-11. Status: steps 1 to 7 are implemented (see each step); "Next" lists what is left. The Linux facts below were checked online on the
same day (sources at the end); anything marked "unverified" was not. Reviewed on 2026-11-11 against the tree and the
elevation work (E1): the Wine and ReactOS statements were rechecked, the Samba statements were not; the machine
registry, the policy trees and the security-template parts were corrected. Roadmap entry: "Future: Group Policy and
domain join" in [ROADMAP.md](ROADMAP.md).

## Goal

When the Linux host is joined to an Active Directory domain, the domain's Group Policy should reach the Wine prefix
the shell runs in, so policy-aware Windows components see the same policy values they would on an XP machine: the
four policy trees, `Software\Policies` and `Software\Microsoft\Windows\CurrentVersion\Policies`, each under `HKLM`
and `HKCU`. What does not apply to Wine is shown grayed out, never silently ignored.

## Current state

### Wine (`wine/dlls/userenv/userenv_main.c`)

- `GetAppliedGPOListW` returns `ERROR_ACCESS_DENIED` after a FIXME. `EnterCriticalPolicySection` returns NULL with
  `ERROR_ACCESS_DENIED`. `LeaveCriticalPolicySection`, `RegisterGPNotification` and `UnregisterGPNotification` log a
  FIXME and return TRUE.
- Not exported: `RefreshPolicy`, `RefreshPolicyEx`, `GetGPOList`, `FreeGPOList`, `ProcessGroupPolicyCompleted`,
  `WaitForUserPolicyForegroundProcessing`, `WaitForMachinePolicyForegroundProcessing`.
- `NetGetJoinInformation` (`wkscli.c`) always answers "Workgroup". `DsRoleGetPrimaryDomainInformation` (`dsrole.c`)
  always reports a standalone workstation. `netapi32` has a libnetapi bridge, and `wldap32` and `ntdsapi` exist, but
  nothing reports a domain join.
- No `gpupdate`, `gpresult` or `gpedit`. `programs/secedit` is a 22-line stub.
- `shell32/shpolicy.c` reads the `...\CurrentVersion\Policies\...` shell restrictions, so values written to the
  registry are honoured by the components that already read them.

### This project (elevation track E1)

- The machine registry is decided: `HKLM` is the machine root's hive (`machine_root` in the root-owned
  `/etc/wineexplorer/sysrun.conf`), loaded and saved by the Wine server, and only clients with the system group may
  write it (`tools/wex-sysrun`). A user's own programs, `wexhost` included, cannot write machine policy.
- Policy already read from it: `HKLM\Software\Microsoft\Windows\CurrentVersion\Policies\System`
  (`ConsentPromptBehaviorAdmin`, `PromptOnSecureDesktop`, read by `wexelevate`) and
  `HKLM\Software\WineExplorer\Elevation` (`EnableStandardUserElevation`; `LockoutBadCount` and `LockoutDuration`,
  the account lockout policy, read by the password verifier from the saved hive; `E1_S18E_WORK_ORDERS.md`, WO-38).
- In a domain these arrive in the security template (`GptTmpl.inf` in the GPO's machine part), not in
  `Registry.pol`: the lockout policy in `[System Access]` (`LockoutBadCount`, `LockoutDuration`,
  `ResetLockoutCount`), the consent settings as security options in `[Registry Values]`.

### ReactOS (`references/reactos/dll/win32/userenv`)

- `gpolicy.c` implements the signalling: `RefreshPolicy`, `RefreshPolicyEx`, `EnterCriticalPolicySection`,
  `LeaveCriticalPolicySection`, the two `WaitFor*PolicyForegroundProcessing` calls and the named events and mutexes
  they use. Nothing in the tree signals or consumes those events.
- `GetAppliedGPOListA/W` are unimplemented (`ERROR_CALL_NOT_IMPLEMENTED`). `GetGPOList`, `FreeGPOList`,
  `ProcessGroupPolicyCompleted` and the RSoP calls are `stub` in the spec.
- No `gpedit` or `gptext`, and no Group Policy service. `misc.c` reads only
  `Software\Policies\Microsoft\Windows\System`.
- ReactOS code may be ported (GPL-compatible).

## Linux side

- Samba applies GPOs on Linux clients since 4.14. `samba-gpupdate` downloads the applicable GPOs from SYSVOL, runs the
  registered client-side extensions (CSEs) and records what it applied in a local database. `--force` reapplies
  everything, `--rsop` prints the resultant set. The packaged version at the time of writing is 4.24.5.
- Shipped CSEs: scripts, certificate auto-enrollment, Firefox and Chromium settings, password and Kerberos policy
  (listed only when the client is a Samba domain controller), `smb.conf`, cron, sudo and the login message. None writes
  Windows-style registry policy. There is no complete per-release list online; the source is the authority.
- SSSD-joined hosts need `oddjob-gpupdate` for `samba-gpupdate` to run automatically. Winbind-joined hosts do not.
- SSSD's `ad_gpo_access_control` (permissive, enforcing, disabled) only decides logon access and only handles GPOs
  linked at the site, domain or OU level. It does not apply settings.
- Ubuntu's adsys only ships a dconf manager. Script execution needs a Pro subscription and certificate auto-enrollment
  needs `python3-cepces`. Not a fit for registry policy.
- Registry policy lives in `Registry.pol` under SYSVOL (PReg format, specified in MS-GPREG). Samba ships Python code to
  read and write it and has `samba-tool gpo` commands for it.
- Prior art for Wine consuming `Registry.pol`: none found. A 2007 Samba slide deck lists "Wine as registry consumer?"
  as an open question.

## samba-gpupdate and Winbind in detail

Read from the Samba master sources (`python/samba/gp/gpclass.py`, `source4/scripting/bin/samba-gpupdate`,
`gp_gnome_settings_ext.py`) and the Samba docs on 2026-11-11. Master may differ from a packaged release.

**Fetching.** `samba-gpupdate` always builds its credentials from the machine account (secrets.tdb, via
`Credentials.guess` and `set_machine_account`), even for user policy. It finds a DC with `Net.finddc`, reads the
account and `gPLink` data over LDAP (`SamDB`), orders GPOs Local, Site, Domain, OU (forced links last, block-inheritance
honoured), checks read access, and mirrors each GPO directory from the `sysvol` share into a local cache over SMB with
signing required. Applying is skipped for a GPO whose `GPT.INI` version equals the stored one unless `--force`. The code
shows nothing specific to Winbind or SSSD; both just need a working machine account and Kerberos.

**Targets.** `--target Computer` (default) runs as root. `--target User -U name` builds the user as `DOMAIN\name` and
runs each extension through `drop_privileges`, which switches the effective uid and gid to that user and requires root.
User state is kept per user in the same TDB. The user CSE set is small: user scripts, user crontab (Centrify) and drive
maps. The machine set is large (security, scripts, sudoers, `smb.conf`, motd/issue, firewalld, certificates, Firefox,
Chromium, GNOME dconf and others).

**Refresh.** Two automatic paths, both about every 90 to 120 minutes:
- Winbind: `apply group policies = yes` (default no) makes `winbindd` run `gpo update command` (default
  `samba-gpupdate`). The `smb.conf` manual says this applies machine policy; the group policy book says it covers user
  policy too with `--force --target=User -U <user>`. The sources conflict; a 2021 talk by the same author says the work
  focused on machine policy. Treat user policy as manual or self-scheduled until the `winbindd` source says otherwise.
  The first run must be done by hand.
- SSSD: `oddjob-gpupdate` runs it on the interval. It needs a minimal `smb.conf` (`security = ADS`,
  `kerberos method = secrets and keytab`), `net ads join -k`, and `ad_update_samba_machine_account_password = true`.
  The book says neither the Samba nor the SSSD team supports this pairing and recommends Winbind when in doubt.

**Writing a CSE.** Subclass `gp_pol_ext` (parses `Registry.pol` through `ndr_unpack(preg.file, ...)`; entries have
`keyname`, `valuename`, `type`, `data`) and `gp_file_applier` or `gp_applier` (hash-checked apply, per-GPO attribute
cache, `unapply`, `clean`). Implement `__str__`, `process_group_policy(deleted_gpo_list, changed_gpo_list)` (unapply
deleted GPOs, apply changed ones, then `clean`) and optionally `rsop(gpo)`. Register with `register_gp_extension()` or,
since Samba 4.18, `samba-tool gpo cse register|list|unregister`, with `machine` and `user` flags. The GNOME CSE is the
model: it filters `**delvals.` entries itself and never reads `entry.type`, so a registry CSE must handle `**del.`,
`**delvals.`, `**DeleteValues` and `**DeleteKeys` and value types itself.

**Consequences for this project.**
- A Wine-registry CSE is feasible and small, but it needs the user CSE flag and a user-state path that works as the
  logged-on user.
- Writing `user.reg` or `system.reg` directly while a `wineserver` runs is not safe: the server holds the hive in
  memory and overwrites the file when it saves. User policy can go to `wexhost` (D-Bus), which writes through the live
  server of that user.
- Machine policy runs as root and belongs in the machine root's hive (see "This project" above). `wexhost` cannot
  write it. It needs a writer with the system group: a small helper started through `wex-sysrun` that writes through
  a server, or a write to the saved hive only while no server has the machine root loaded.

## What the system tier changes (2026-11-12)

This note was written before session 0 ([SYSTEM_TIER_DESIGN.md](SYSTEM_TIER_DESIGN.md),
[SYSTEM_TIER_WORK_ORDERS.md](SYSTEM_TIER_WORK_ORDERS.md)). Where the two differ, this section holds.

| Topic | Before | With session 0 |
|---|---|---|
| Who writes machine policy | a system-group helper through `wex-sysrun`, or the saved hive while no server has it loaded | a program of session 0: session 0 holds `HKLM` for every session and its programs run as LocalSystem (T5.3, T5.4). The Samba extension hands the values to a small session 0 writer; later that writer is a service there, as the Group Policy Client service is on Windows. Writing the saved hive directly is never safe now: session 0 always has it loaded |
| When sessions see a change | at their next start | at once: every session reads `HKLM` from session 0 (T1) |
| User policy (`HKCU`) | through `wexhost` in the user's session | the user's hive is held by session 0 while the user is logged on (T4a), so the same session 0 writer can write `\Registry\User\<SID>\Software\Policies`; a user who is not logged on gets it at logon |
| Making user policy binding (open question below) | not possible: a user's programs write their own `HKCU` | possible: session 0 serves the user's hive and can refuse writes below the two policy trees to tokens that are not SYSTEM or administrators. It needs a path rule in session 0, because Wine gives a new key no inherited descriptor (the reason T3 is postponed) |
| Refresh signalling | `wexhost` sets the refresh events | the events live in each session's server; session 0 tells the sessions over the session link (T6.2), with a new order "policy refreshed" |
| The elevation settings | read by `wexelevate` in the session | with E1 R9 ([E1_S18E_R9_SPEC.md](E1_S18E_R9_SPEC.md)) the broker in session 0 reads `ConsentPromptBehaviorAdmin`, `ConsentPromptBehaviorUser`, `PromptOnSecureDesktop`, `EnableStandardUserElevation` and the lockout policy from session 0's registry at each request, so a policy write there applies to the next request of every session |

**Order of work.** Nothing of Group Policy belongs inside R9: R9 moves the reader of the elevation policy to
session 0, and Group Policy later adds a writer beside it, with no change to R9. After R9:

1. the session 0 policy writer and the path rule for the user policy trees (system tier, no domain needed; also
   gives local policy a place: `secpol.msc`, `net accounts`);
2. join detection and the two stock-Wine changes ("First work order" below; needs the Samba AD controller in the
   lab);
3. the Samba extension, refresh signalling over the session link, the applied-GPO list.

## Step 1: the session 0 policy writer (2026-11-13, implemented)

**The writer, `wexpolicy`** (`src/programs/wexpolicy`). A console program of session 0, run as LocalSystem:

```
wexpolicy apply /machine FILE [FILE...]     HKEY_LOCAL_MACHINE
wexpolicy apply /user:SID FILE [FILE...]    HKEY_USERS\SID, or kept for the user's next logon
wexpolicy clear /machine | /user:SID        remove what the last apply wrote
wexpolicy list  /machine | /user:SID        print what the last apply wrote
```

- It reads `Registry.pol` (PReg) itself, with a small parser of its own (`preg.h`). This differs from step 3 of
  the design below ("through Samba, not our own parser"): fetching and choosing the GPOs stays Samba's work, but the
  values are written by a program of session 0, which cannot run Samba's Python. The Samba extension only copies
  the files and starts the writer. The same writer serves local policy, where there is no Samba at all.
- The files are applied in the order given; a later file wins. PReg deletions and the `**del.`, `**delvals.`,
  `**DeleteValues`, `**DeleteKeys` and `**soft.` markers are honoured; `**SecureKey` is ignored (the path rule
  below protects the keys).
- Only keys in the two policy trees are written (`Software\Policies`,
  `Software\Microsoft\Windows\CurrentVersion\Policies`); other entries are counted and skipped.
- Every file is read and checked completely before anything changes.
- What an apply wrote is kept in `%SystemRoot%\system32\GroupPolicy\Archive\Machine.pol` or `User-<SID>.pol`, in
  session 0's root. The next apply first removes exactly those values, so a value that left the policy leaves the
  registry, and values that were never policy (the local security options under `...\Policies\System`) stay.
- The caller must be LocalSystem; anyone else is refused before a file is read.

**Making user policy binding.** Session 0 serves each logged-on user's hive. Three rules in its registry code, all
effective only while elevation is enforced (without it every token is unrestricted):

1. *Path rule.* A key at or below one of the two policy trees of a user's hive has the policy descriptor (the user
   reads; SYSTEM and Administrators have full control) instead of the hive's default (the user has full control).
   A path rule, because Wine gives a new key no inherited descriptor.
2. *The two tree roots always exist.* Session 0 creates them when it loads a hive. A create below them then meets
   a parent with the policy descriptor, and the roots cannot be deleted by the user, so the keys above them cannot
   be deleted either (a key with subkeys is not deleted).
3. *No rename around the trees.* The keys above the roots (`Software`, `Software\Microsoft`, `...\Windows`,
   `...\CurrentVersion`) are not renamed by a token the policy descriptor refuses. Otherwise the user could move
   them away, build a tree of their own and move it into place. Renaming another key to one of those names needs
   no rule: the names are always taken.

**The service, `gpsvc`.** The hive file is the user's own file, so the user can change the policy trees in it
while logged off. Therefore:

- Session 0 empties the two policy trees when it loads a user's hive (rule 2 then creates the roots again). A
  key on the way to a tree that the file made a registry link is removed with all below it and made again as a
  plain key; otherwise the readers of the policy would be led to a place the user writes.
- `wexpolicy` started without arguments by the service manager is the service `gpsvc` ("Group Policy Client"), an
  automatic service that only works as LocalSystem. It looks at `HKEY_USERS` every two seconds, and at once on a
  change notification. An account's hive without the marker key gets the values of its archive
  (`User-<SID>.pol`, which holds the values with their data) and the marker.
- The marker is the volatile key `Software\Policies\WineExplorer\Policy Applied`. It is in the policy tree, so the
  user cannot make it, and it goes away with the hive.
- `wexpolicy apply /user:SID` for a user who is not logged on changes the archive only; the service writes it at
  the next logon.

Between the hive's arrival and the service's write (up to two seconds) the user's programs see no user policy.
Windows has the same kind of window when policy is applied asynchronously at logon.

Machine policy needs none of this: `HKLM` is session 0's own and is saved in its root.

**Status (2026-11-13).** Implemented and passing in `scripts/test_sysrun_server.sh` (normal suite 345, lab suite
419): a user's policy applied while logged off appears at logon for a 32-bit program, and the user can change it
neither in the registry nor in the hive file; machine policy, `clear`, `list`; the writer refused to a standard
user. Reviewed by Qwen (read-only), which found the link case above independently. Not tested: the link case
itself, the rename rule, the service on an installed machine. Next: join detection (step 2).

## Step 2: the domain join, told to Windows programs (2026-11-13)

This differs from design steps 1 and 2 below (detection in `wexhost`, published over D-Bus): with session 0 the
machine registry is one place that every session reads at once, so the join state is two registry values and no
program asks a host service at each call.

- `installer/scripts/wex-domain-state.sh` runs after session 0's Windows has started (an `ExecStartPost` of
  `wex-session0-services.service`), as session 0's account. It detects the join without network or root rights:
  `realm list` (realmd; SSSD or Winbind), else Samba's `security = ads` with a realm and a machine keytab. It
  writes `HKLM\Software\WineExplorer\Domain` (`FlatName`, `Name`), or removes the key on a host that is not joined.
  The flat name is Samba's `workgroup` when configured, else the first label of the DNS name.
- `NetGetJoinInformation` (`wkscli`) returns `NetSetupDomainName` and the flat name; `DsRoleGetPrimaryDomainInformation`
  (`dsrole`) returns a member workstation with the flat, DNS and forest names. Without the key both behave as
  stock Wine. Listed in [STOCK_CHANGES.md](STOCK_CHANGES.md).
- The state is read at session 0's start; a join or leave shows after `systemctl restart wex-session0-services`
  or a reboot.

Tested: the two functions from 64-bit and 32-bit programs of a standard user, with and without the key
(`scripts/test_sysrun_server.sh`); the detection script against a real join made with `net ads join` in the
lab domain (below): nothing before the join, `WEXLAB ad.wexlab.example` after it. Not tested: a join made with
`realm join` (SSSD), the script's write into session 0 on an installed machine, and the forest name of a child
domain (the DNS name is given).

**The lab domain** (`installer/tests/ad/`, in the Debian WSL distribution, `/var/tmp/wex-ad-lab`):
`dc-create.sh` makes and starts a Samba AD controller for `AD.WEXLAB.EXAMPLE` (flat name `WEXLAB`) from a Debian
12 cloud image; `client-create.sh` makes a member guest, joins it and checks the detection. The guests share a
QEMU multicast socket network, 10.71.0.0/24, controller 10.71.0.10; the scripts' headers have the details. One
trap, handled in `dc-provision.sh`: Samba must serve the lab card only, or it registers QEMU's user-mode address
(the same in every guest) for the controller's name and a member that looks the controller up reaches itself.

## Step 3: refresh signalling (2026-11-13, implemented)

Tested in `scripts/test_sysrun_server.sh`: a 32-bit program of a standard user registers for the machine and for
the user notification and is signalled when session 0 applies policy. Ported by Qwen, reviewed and extended by
Claude. Not tested: `RefreshPolicy` (nothing listens to its events yet: there is no periodic refresh), the
critical sections, the foreground waits (their events are never created here, so the waits return FALSE).

`userenv` gets the ReactOS implementation of `RegisterGPNotification`, `UnregisterGPNotification`,
`RefreshPolicy`, `RefreshPolicyEx`, the critical policy sections and the two foreground waits (`gpolicy.c`).

The named events it uses live in each session's own server, so session 0 cannot set them. This differs from the
table above ("a new order over the session link"): instead the writer leaves a serial number in the registry after
every apply, clear and logon restore, and `userenv`'s notification thread watches it with a registry change
notification, which already reaches every session through session 0. No server change is needed.

- Machine: `HKLM\Software\Microsoft\Windows\CurrentVersion\Group Policy\WineExplorer Applied`, value `Serial`.
- User: the marker key `HKCU\Software\Policies\WineExplorer\Policy Applied`, value `Serial` (inside the protected
  tree, so the user cannot fake a refresh).

## End to end on an installed machine (2026-11-13)

`installer/tests/ad/machine.sh SOURCE_VM` boots an overlay of an installed session 0 disk on the lab network; the
guest installs Samba, joins the lab domain, and checks. Passed on an ISO of 159d820e (10 checks): not joined
before; joined; after a restart of session 0's services a user's program reads the domain's two names; after
`wex-gpupdate.service` it reads the test object's value from `HKLM\Software\Policies\WexLab`; the timer is
enabled; the policy service runs once in session 0; no unit failed.

One finding, reported by the test as a note: the installed user could overwrite that machine policy value, and
the next refresh put it back. An installed system does not enforce elevation by default, so every user's token
is unrestricted and session 0 takes the write. Until installed systems enforce elevation, domain policy is
applied and re-applied, but not binding between refreshes. This is a decision about the elevation default, not
about Group Policy.

On an image with enforced elevation (`WEX_ELEVATION=1`; [SYSTEM_TIER_WORK_ORDERS.md](SYSTEM_TIER_WORK_ORDERS.md),
"Enforced elevation on an installed machine") the same test passes and its note reads "refused": the installed
administrator's own programs are limited there, and session 0 refuses the write. Policy is binding with
enforcement, with no change to Group Policy.

Not covered: user policy from the domain (the accounts map has local users only; logging on to WineExplorer as a
domain user does not exist yet), the lockout policy from the security template, SSSD joins.

## Step 4: domain policy, from Samba to the writer (2026-11-13)

`installer/scripts/wex-gpupdate.sh`, run as root by `wex-gpupdate.timer` (at start, then every 90 minutes plus
up to 30 at random) on a machine joined with Samba:

1. `samba-gpupdate --force` refreshes Samba's cache of the domain's policy objects.
2. `net ads gpo list` names the objects that apply to the computer, and to each user of the accounts map whom
   Winbind knows as a domain user, in Samba's order.
3. The objects' `Registry.pol` files go to `wexpolicy apply /machine` and `wexpolicy apply /user:SID` in session
   0; a target without any file gets `wexpolicy clear`.

This differs from design step 3 below (an extension inside `samba-gpupdate`): a shell script on Samba's command
line tools has no licence question (an extension is a Python file under Samba's GPL-3.0-or-later) and needs no
registration in Samba. The price: it depends on the text `net ads gpo list` prints, and it reads the security
template (`GptTmpl.inf`, the lockout policy) not at all yet.

Tested in the lab domain: steps 1 and 2 on the joined member (`--print`: the machine file of the test object for
the computer, the user file for a domain user, nothing for a local account); step 3 for the computer and the
timer on an installed machine ("End to end" above). Not tested: step 3 for a user; the order of several objects (applied in
the order Samba lists them, later wins; unverified against Windows); SSSD joins (the script needs Winbind).

## Step 5: the security template (2026-11-14)

`wexpolicy security /machine TEMPLATE...` (`src/programs/wexpolicy/sectmpl.c`) applies the GPOs' security
templates, `Machine\Microsoft\Windows NT\SecEdit\GptTmpl.inf` (UTF-16LE INF, `signature="$CHICAGO$"`, read with
setupapi), in the order given; a later one wins. `wex-gpupdate.sh` passes the templates of the objects that apply
to the computer.

- `[System Access]`: `LockoutBadCount` (0 to 999), `LockoutDuration` and `ResetLockoutCount` (minutes; -1, "until
  an administrator unlocks", and 0 become the longest, 99999) go to `HKLM\Software\WineExplorer\Elevation`, where
  `net accounts`, `NetUserModalsGet/Set` level 3 and the elevation verifier keep the lockout policy. Password and
  Kerberos policy and the rest of the section belong to the host and the domain: counted as not applicable.
- `[Registry Values]` (`MACHINE\path\name=type,data`; types 1, 2, 4 and 7): written when the key is in one of the
  two policy trees, which holds the consent settings (`...\CurrentVersion\Policies\System`). Other keys, and the
  binary type, are counted as not applicable.
- Every other section (privilege rights, audit, restricted groups, services, file and registry permissions) is
  not read.
- Security settings stay when no GPO sets them any more, as on Windows ("tattooing"); nothing is undone.
- A template with a fault (no `[Version]` signature, a value out of range) changes nothing.

Which objects count. On Windows the lockout policy of domain accounts comes only from GPOs linked at the domain;
an OU-linked GPO's lockout policy applies to the local accounts of the computers in the OU (MS-GPSB; Microsoft
Learn, "Account policies"). The policy here protects the local accounts of the accounts map, so every object that
applies to the computer counts, in order.

Tested in `scripts/test_sysrun_server.sh` (`st_security_template_lockout_policy_and_options`): the three values,
`net accounts` showing the threshold, an option in the tree written and one outside not, a template without a
signature refused, a standard user refused. Not tested: a template made by `samba-tool gpo manage security set`
(Samba 4.14 and later) in the lab domain.

## Step 6: the applied GPO list (2026-11-14)

`wexpolicy record /machine|/user:SID LIST` (`gpolist.c`) records which objects were applied, and Wine's `userenv`
answers `GetAppliedGPOListA/W` from it (`FreeGPOListA/W` added; stock change). As on Windows the list is kept per
client-side extension, in application order: `HKLM\Software\Microsoft\Windows\CurrentVersion\Group Policy\History\{extension}\<n>`
for the machine, with the values `GPOName`, `DisplayName`, `FileSysPath`, `DSPath`, `Link`, `Extensions`,
`Version`, `GPOLink`, `Options`, `lParam`. A user's list is in `...\Group Policy\<SID>\History\...` in the machine
registry, which only session 0 writes and which is there before the user logs on; Windows reads it from the user's
hive. An object is listed for every extension that starts a group of its extension list
(`[{extension}{tool}...]`). No list: success and an empty list, as on Windows.

`wex-gpupdate.sh` builds LIST from `net ads gpo list` (Samba's `dump_gpo`, printed on the error stream: `name:`,
`displayname:`, `version:`, `filesyspath:`, `dspath:`, `options:`, `link:`, `link_type:`, `machine_extensions:`,
`user_extensions:`; read from the Samba source). Samba's `GP_LINK_*` numbers are the `GPO_LINK` values; the two
directory paths get the `LDAP://` prefix Windows gives them.

Tested: the parser against a dump in that layout; the suite case `st_applied_gpo_list_per_extension_for_machine_and_user`
(64-bit and 32-bit programs, machine and user, per extension, a damaged list refused, the empty list). Not tested:
`net ads gpo list` of the lab domain through the whole script.

## Step 7: gpupdate and gpresult (2026-11-14)

**gpupdate** (`src/programs/gpupdate`; XP's switches `/target:{computer|user}`, `/force`, `/wait:N`, `/logoff`,
`/boot`, `/sync`). A user's program cannot start root's `wex-gpupdate.service`, so:

1. `gpupdate` sends session 0's Group Policy service (`gpsvc`, `wexpolicy`) the user-defined control 128. The
   service manager's default descriptor lets every authenticated user send user-defined controls (as on
   Windows), and the session's service manager forwards the call to session 0's.
2. `gpsvc` writes `C:\windows\system32\GroupPolicy\Refresh` in session 0's root.
3. Root's `wex-gpupdate.path` (enabled with session 0 by `setup-machine-root.sh`, which adds a drop-in when the
   machine root is not the default) starts `wex-gpupdate.service`. systemd's start limits bound how often users
   can make it run.
4. Every run of `wex-gpupdate.sh` ends with `wexpolicy refreshed`, also on a machine that is not joined, which
   raises `Refreshed` under `HKLM\...\Group Policy\WineExplorer Applied`. `gpupdate` waits for it (600 seconds by
   default) and prints "Computer Policy Refresh has completed." and "User Policy Refresh has completed." (XP's
   wording as remembered, unverified; the research found only the Vista and later wording).

One run refreshes the machine and every user, and always with `--force`: `/target` only chooses the lines
printed, `/force` changes nothing. Nothing here needs a restart or a logoff.

**gpresult** (`src/programs/gpresult`; `/SCOPE`, `/USER`, `/V`, `/Z`; `/S` is refused). XP's layout: the header
`RSOP data for DOMAIN\user on COMPUTER : Logging Mode`, the OS and domain lines (from
`DsRoleGetPrimaryDomainInformation` and `ProductName`), then `COMPUTER SETTINGS` and `USER SETTINGS` with
`Applied Group Policy Objects`: the objects of every known extension (registry, security, scripts) from
`GetAppliedGPOList`, merged in application order. `/V` adds each object's GUID and its extensions. Not shown:
the computer's and user's directory names, the controller (Samba does not record it; the domain is shown), the
filtered-out objects, security groups and the settings themselves.

Tested in the suite: `st_gpupdate_asks_session0_and_waits_for_the_refresh` (the request reaches session 0's root,
gpupdate waits for and reports the refresh, `/wait:0`, usage, a user cannot say a refresh is done); gpresult in
`st_applied_gpo_list_per_extension_for_machine_and_user`. Not tested: the path unit and the whole loop on an
installed machine of the lab domain (`installer/tests/ad/machine.sh` can take it next).

**End to end (2026-11-15).** On an installed machine of the GPO VM lab ([GPO_LAB.md](GPO_LAB.md)), joined into
the lab's OU: the OU's object applied after the domain's, the domain's security template (lockout values and a
security option), gpresult listing the objects in order, and the installed user's gpupdate running a refresh
through gpsvc and `wex-gpupdate.path`; 14 checks of `installer/tests/ad/machine.sh` passed. Normal suite 350 on
the lab's build machine.

## The order of several links on one container (2026-11-15)

Windows and Samba read a container's `gPLink` in opposite directions.

- Windows (MS-GPOL v20240423, 2.2.2 and 3.2.5.1, "GPO Search"; 3.2.5.2, "GPO Processing Order"): the position
  in `gPLink` is the link order, and a smaller link order has the higher precedence. The client walks the
  containers from the account's own up to the domain (and the site), puts each container's non-enforced links,
  first to last, in front of the list, and appends the enforced ones after all of them; the list is applied in
  order. So within one container the first `gPLink` entry (link order 1) is applied last and wins. A new link
  without a position is written at the end of `gPLink` (3.3.5, "GPO Link Creation"), with the lowest precedence.
- Samba (`libgpo/gpo_ldap.c`, `add_gplink_to_gpo_list`; `python/samba/gp/gpclass.py`, `get_gpo_list`): keeps
  each container's links in `gPLink` order, so the last entry is applied last and wins. `samba-tool gpo setlink`
  writes a new link at the front of `gPLink`, which a Windows client takes as link order 1.

`wex-gpupdate.sh` keeps Samba's choice of objects (security filtering, blocked inheritance, disabled links) and
puts them in Windows' order (`win_order`): it reads the account's DN and the `gPLink` of each OU above it and of
the domain (`net ads search` with the machine account) and orders as MS-GPOL does; objects the directory does not
explain keep Samba's place at the end, and Samba's order stays whole when the directory cannot be read. In the
lab domain the three objects linked at the domain were applied "WexLab Domain", "WexLab", "Default Domain
Policy" before and the other way round now; `gpo-lab.sh parse` checks it on the member.

## Next

- User policy from the domain end to end: a domain user logging on to WineExplorer (the accounts map has local
  users only).
- The Windows Server evaluation controller: Microsoft's Evaluation Center requires a registration form with
  personal details for the ISO, so per the owner's condition the lab stays on Samba AD.

## Local policy (without a domain)

Wanted by the owner (2026-11-16): a machine that is not joined has policy of its own, set by its administrators, and
a joined machine keeps it beneath the domain's (Windows applies Local, then Site, Domain, OU; the last wins).

**What exists** (survey of 2026-11-16, tree at Wine a8a504bb):

- The writer is not tied to Samba. `wexpolicy apply /machine|/user:SID FILE...` parses PReg itself, honours the
  delete markers, undoes the previous archive (registry policy un-tattoos, a security template tattoos) and, for a
  user who is not logged on, only changes the archive; `gpsvc` restores it at the logon (`src/programs/wexpolicy`).
  `wexpolicy security /machine` applies a `GptTmpl.inf` (the lockout values and the consent options).
- `net accounts` (`/lockoutthreshold`, `/lockoutduration`, `/lockoutwindow`) and `NetUserModalsGet/Set` level 3
  work, with no RPC: they read and write `HKLM\Software\WineExplorer\Elevation` (`LockoutBadCount`,
  `LockoutDuration`, `ResetLockoutCount`). (The "stub" in the open question of an earlier note was out of date.)
- `GetAppliedGPOListW` reads whatever `wexpolicy record` stored, so a local entry needs no stock-Wine change.

**What is missing**, in the order a user would need it:

1. **A local `Registry.pol` that applies.** Nothing reads `%SystemRoot%\System32\GroupPolicy\Machine\Registry.pol`
   or `\User\Registry.pol` (in the machine root's `drive_c`), and `wex-gpupdate.sh` exits at "not joined" before it
   applies anything, so a machine without a domain never gets a refresh that does work. The work: the store; the
   script applies local files when not joined (and clears the archive when there are none), and puts them in front
   of the domain's files when joined (one `apply` per target holds the whole ordered set); `wexpolicy record`
   accepts a non-GUID entry "Local Group Policy" so that `gpresult` and `GetAppliedGPOList` show it; a
   `gpupdate` of an administrator triggers it through the existing `GPSVC_CONTROL_REFRESH`. WineExplorer code only.
   Later, as Windows has them: the Administrators and Non-Administrators local objects and the user-specific ones
   under `GroupPolicyUsers`.
2. **An editor.** A `gpedit.msc` that edits the local `Registry.pol` (`mmc` knows one snap-in today, Device
   Manager): the policies that Wine components honour (the shell restrictions, `Policies\System`, the elevation
   and lockout settings), the rest grayed out as "not applicable" with a reason. Writing machine policy needs an
   administrator, so it runs elevated through the broker.
3. **`secpol.msc` and `secedit`.** Done 2026-11-16 (the command line; `secpol.msc` itself needs the editor of 2):
   the local object's security template is `GroupPolicy\Machine\Microsoft\Windows NT\SecEdit\GptTmpl.inf` in
   session 0's root, which `wex-gpupdate.sh` applies first and the domain's templates after it (the last one wins
   where they set the same value). `secedit` is WineExplorer's (`src/programs/secedit`, replacing Wine's stub:
   `src/stock-replacements.txt`): `/export /cfg FILE` writes the lockout policy and the consent options as they are
   now; `/configure /cfg FILE` stores a template as the local one and runs `gpupdate /target:computer`, returning
   when the refresh has applied it. `/db`, `/overwrite`, `/log`, `/quiet` are accepted and change nothing, and
   `/areas` other than `SECURITYPOLICY` are reported not applicable. Only the system group may write the store
   (an elevated administrator, as for machine policy); a standard user gets "Task is not completed". Limits: the
   template replaces the previous one (earlier values stay applied, as security settings stay on Windows, but are
   no longer in the file); and `net accounts` and a template still write the same lockout values, the last writer
   winning. (Checked on the installed image, 2026-11-16: the machine root has the default ACL `g:wexsys:rwX`, so a
   directory or file that one elevated administrator creates is group-writable for every other administrator; no
   one is locked out.) The system tier session wants machine policy written by session 0's own account, as the
   owner's rule for machine-registry writes has it: the editor of 2 hands its files to `gpsvc` through a spool
   directory instead (see there).
   The root script reads the local files as session 0's account (`stage_file`), so a link put in place of one
   after its check reaches only what that account could read.
3b. **`net accounts` and `NetUserModals`.** The password-age lines, `/forcelogoff` and `/domain` of `net accounts`
   and `NetUserModalsGet` levels 0 and 1, `NetUserModalsSet` levels 0 to 2: stock Wine (`dlls/netapi32`,
   `programs/net`), each listed in [STOCK_CHANGES.md](STOCK_CHANGES.md). Password length and age have no store or
   enforcement here (the host's PAM owns them): shown as "not applicable".
4. **Local users and groups** (`net user`, `net localgroup`, `lusrmgr.msc`, the User Accounts panel): not needed for
   local policy to work; `NetUserAdd` and `NetLocalGroupAdd` are stubs today.
5. **Binding.** An installed system does not enforce elevation by default, so any user can overwrite machine policy
   between refreshes; the next refresh puts it back. Whether the default changes is a decision for the system tier.

### Writing the local files: the spool (implemented 2026-11-16, in review by the system tier session)

Where it is: `scripts/setup-machine-root.sh` (section 2b: the store tree is session 0's account's, only the spool is
group-writable), `src/dlls/wexgpstore` (`native.c`: the file work relative to directory descriptors; `unixlib.c`,
`wexgpstore.c`: the Wine glue, three exports), `src/programs/wexpolicy/store.c` (control code 129: read, check, install,
`NAME.error`), `src/include/wexgpclient.h` (the client: spool file, control code, wait for the refresh) and
`src/programs/secedit/main.c` (the first client). Tests: `scripts/test_b3_machine_root.sh` (setup with a hostile
tree), `scripts/test_gpstore_native.sh` (the native code against hostile trees: links, FIFOs, hard links, sizes, a
planted `STORE.new` or `NAME.error`, a tree that is not as setup makes it; mutation-checked), and the session 0 suite
(`st_gpsvc_spool_installs_good_requests_and_refuses_hostile_ones`, `st_secedit_...`). `ControlService` does not
pass a handler's error code on, so a client learns of a refused tree from the request that stays (`ERROR_TIMEOUT`
in the helper) and of a refused file from `NAME.error`.

Machine policy is written by session 0's own account. The editor (2) and `secedit /configure` (3) therefore do not
write the store themselves; they hand a file to `gpsvc`, which checks it and installs it.

- **Store** (as now): `%SystemRoot%\System32\GroupPolicy\Machine\Registry.pol`, `...\User\Registry.pol`,
  `...\Machine\Microsoft\Windows NT\SecEdit\GptTmpl.inf`, in session 0's root, read by everyone and written only by
  `gpsvc` (so always owned by session 0's account). **Spool:** `%SystemRoot%\System32\GroupPolicy\Spool`, which
  `gpsvc` creates at its start; the machine root's default ACL (`g:wexsys:rwX`) makes it writable by the system
  group (an elevated administrator's process) and by nobody else.
- **A request** is a file in the spool with one of the names `Machine.Registry.pol`, `User.Registry.pol`,
  `Machine.GptTmpl.inf`, written as `NAME.tmp` and renamed to `NAME` (atomic). For a short time (2026-11-16) the
  client wrote to `NAME` itself, because a rename in the spool by an elevated process failed with
  `ERROR_INVALID_HANDLE` (B28: the server's name lookups for a system-group client ran without the group; fixed in the
  server, `c4_elevated_process_renames_in_a_group_only_directory`). Zero bytes mean "remove the store
  file" (every setting back to Not Configured). Later, per user: `User-SID.Registry.pol`.
- **The trigger** is a new user-defined control code of `gpsvc`, `GPSVC_CONTROL_STORE` (129), without data, sendable
  by any authenticated user like 128 (`GPSVC_CONTROL_REFRESH`): it only makes `gpsvc` look at what the system group
  put in the spool.
- **`gpsvc` handling**, per known name present: open the file without following a link, regular file, at most
  4 MiB (1 MiB for a template); size 0 removes the store file; else check it completely with the parsers `wexpolicy
  apply` and `wexpolicy security` use (PReg: signature, version, every record; template: the signature and the
  sections), write `STORE.new` and replace the store file with it, delete the request. A request that fails the
  check is deleted too and leaves `NAME.error` (the Win32 error and a one-line reason, at most 200 bytes); a good
  one removes an old `NAME.error`. When anything changed, `gpsvc` leaves the refresh request (`Refresh`, as for
  128), so root's `wex-gpupdate.path` applies it. Nothing in the spool is trusted for its content or its name.
- **Who owns what** (system tier review, 2026-11-16): the store tree (`GroupPolicy`, `Machine`, `User`, the SecEdit
  path) is session 0's account's own, mode 2755 (the setgid bit passes its group on), files 0644, no ACL entries;
  only `Spool` is group-writable (2770, session 0's account and the system group, default ACL for the group).
  `setup-machine-root.sh` sets this at every run, which also puts an existing tree right (links and special files
  in it are removed, ACLs dropped, stale requests removed); a link in place of `GroupPolicy` stops it. Otherwise an
  elevated administrator could rename `Spool` or plant links at `Registry.pol.new`, in the SecEdit path and so on,
  and `gpsvc`, which runs as session 0's account, would read, delete or write where they point. The consequence
  is wanted: Win32 code run by an administrator (secedit's `MoveFileEx` into the store) can no longer write the
  store; everything goes through `GpStoreWrite`.
- **How `gpsvc` does the file work**: `gpsvc` is a Windows program and has no `openat` or `O_NOFOLLOW`, so the file
  operations are native functions of `wexhost`'s unix side, which runs inside session 0's process as its account
  (`WexGpSpoolRead`, `WexGpSpoolDone`, `WexGpStoreWrite`). They open `GroupPolicy` once with
  `O_DIRECTORY|O_NOFOLLOW` (owner session 0, group the system group, or refuse) and work from that descriptor
  with `openat`, `O_NOFOLLOW` and `unlinkat`/`renameat`, never building a path string under `GroupPolicy`. A request
  is opened `O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC`, then `fstat`: a regular file with one link and a size
  within the limit, and exactly `st_size` bytes are read into the buffer that is validated (never reopened). The
  store file is written as `STORE.new` (`O_CREAT|O_EXCL|O_NOFOLLOW`, after an `unlinkat` of an old one) and
  `renameat`d; a stale `STORE.new` is removed at start; `NAME.error` is written the same way. The content checks
  (PReg, template) stay in `gpsvc`, on the bytes that the native side returned.
- **Client side**: a small shared helper (`GpStoreWrite(kind, data, size)`): write the temporary file, rename it,
  `ControlService(129)`, wait for the `Refreshed` serial as `gpupdate` does, read and delete `NAME.error`. `secedit
  /configure` moves to it; the editor uses it. Reading the store (to show current settings) is a plain file read.
- **Tests** (session 0 suite): a request of the system group becomes a store file owned by session 0's account and
  is applied by the next refresh; a standard user cannot write the spool; a damaged PReg, a template without
  signature, a name that is not on the list, a link, an oversized file and a path trick leave the store as it was
  (the first two with an `.error`); zero bytes remove the store file; 129 with an empty spool does nothing.

Order of work: 1, then 3 (the lockout and consent settings are what already works end to end), then 2, then 3b. Each
step ends with a lab check on an unjoined machine and, where it touches the joined case, on the lab domain.

## Design

1. **Join detection (`wexhost`).** Detect the join from `net ads info` or SSSD/realmd state and publish domain name,
   flat name and role over D-Bus (prefer D-Bus over spawning tools). Unjoined hosts keep today's behaviour.
2. **Report the join to Windows programs.** Make `NetGetJoinInformation` return `NetSetupDomainName` and
   `DsRoleGetPrimaryDomainInformation` return a domain member role with the real names, using the `wexhost` data.
   Both are small stock-Wine changes; list them in [STOCK_CHANGES.md](STOCK_CHANGES.md).
3. **Fetch and apply through Samba, not our own parser.** Write a small Wine-registry CSE using Samba's documented
   extension API. It runs inside `samba-gpupdate`, reads the GPO's `Registry.pol` and writes the values (the policy
   trees named under "Goal") to the machine root's `HKLM` through the system-group writer and to the user's `HKCU`
   through `wexhost`, honouring PReg delete and `**del.` value markers. `wexhost` only schedules `samba-gpupdate`
   (and hands the SSSD case to `oddjob-gpupdate`).
   The same CSE reads the machine security template (`GptTmpl.inf`) for the two parts that apply here: the account
   lockout policy, written to the `LockoutBadCount` and `LockoutDuration` values the verifier reads, and the
   security options in `[Registry Values]` that are consent settings.
4. **Refresh signalling.** Port the ReactOS event and mutex code into Wine's `userenv` and export `RefreshPolicy`,
   `RefreshPolicyEx` and the wait calls. After the CSE finishes, `wexhost` sets the refresh-done events.
5. **Applied GPO list.** `GetAppliedGPOListW` returns the list recorded by the CSE (name, GUID, link, version) instead
   of failing, so `gpresult`-style tools have data.
6. **Not applicable to Wine.** Software installation, folder redirection, disk quotas, IPsec and the rest of the
   security settings (password and Kerberos policy belong to the host and the domain, user rights, audit, restricted
   groups, services, file and registry permissions) are shown grayed out in any policy UI and reported "not
   applicable" in RSoP output. Account lockout and the consent security options apply (step 3).
7. **Later.** A `gpupdate` command that calls the refresh path, `gpresult` over the recorded list, and a local
   `gpedit.msc` that edits a local `Registry.pol`.

## Open questions

- Per-user policy: `samba-gpupdate --target=User` works as root and drops to the user, but no automatic per-logon
  trigger is documented. Decide whether the wex logon flow (or a PAM hook) runs it, and check `winbindd` source for what
  `apply group policies` covers. The profile hive loading (`e1s_load_profile`) decides where the user hive is writable.
- Who may write the policy trees: decided 2026-11-16 (owner): a user may write their own `HKCU\Software\Policies`
  keys, as on Windows, where user policy is not tamper-proof either (the policy engine rewrites them at each
  refresh, and only `HKLM` policy needs administrators). Nothing to protect.
- Local policy without a domain: wanted (owner, 2026-11-16): `gpedit.msc`-style local Group Policy, `secpol.msc`,
  `net accounts` and `NetUserModalsGet/Set` level 3 (a stub in Wine) on top of the lockout values, working on a
  machine that is not joined, through the same policy writer as domain policy (local objects first, overridden by
  the domain's). Survey and plan: the "Local policy" section.
- Winbind or SSSD as the supported join: the docs steer to Winbind for GPO. Decide before the installer offers a join.
- SYSVOL access with the machine account's Kerberos ticket works in both; SSSD additionally needs the `net ads join -k`
  Samba machine account to stay in sync.
- Whether the CSE belongs in this repository or upstream Samba. The Samba API is Python and GPL-3.0-or-later; this
  project's rule is that GPL-3.0 code makes the module containing it GPL-3.0, so keep the CSE a separate file and make
  that licence decision explicitly.
- Test setup: a Samba AD domain controller in the QEMU lab (see [TOOLING.md](TOOLING.md)) with one GPO that sets a
  registry value, a 32-bit program reading it, and a join/unjoin round trip. Any distribution with current Samba
  packages serves as the controller; a Debian guest fits the lab's tooling. Needed: a network the controller and
  the WineExplorer guest share (a QEMU socket or bridge network; user-mode networking with forwarded ports is not
  enough), the client's DNS pointing at the controller, clocks within Kerberos' 5 minutes, and a realm name that
  is not under `.local`. Test policies are made on the controller with `samba-tool gpo`; whether it writes the
  security template (`GptTmpl.inf`) is unverified, else the file is put into SYSVOL by hand. A Windows Server
  controller (lab only) is the later check against policies authored with Windows' own tools.

## First work order

Join detection in `wexhost` plus the two stock-Wine changes (step 1 and 2), tested with a Samba AD controller in the
lab and a 32-bit program that calls `NetGetJoinInformation`. No policy application yet.

## Sources

- Samba sources: https://github.com/samba-team/samba/blob/master/python/samba/gp/gpclass.py and
  https://github.com/samba-team/samba/blob/master/source4/scripting/bin/samba-gpupdate
- GNOME CSE (model for a `Registry.pol` CSE):
  https://github.com/samba-team/samba/blob/master/python/samba/gp/gp_gnome_settings_ext.py
- Automatic policy refresh (Winbind, oddjob-gpupdate): https://dmulder.github.io/group-policy-book/policy-refresh.html
- smb.conf manual (`apply group policies`): https://www.samba.org/samba/docs/current/man-html/smb.conf.5.html
- Samba wiki, Group Policy: https://wiki.samba.org/index.php/Group_Policy
- samba-gpupdate man page: https://manpages.ubuntu.com/manpages/jammy/man8/samba-gpupdate.8.html
- Samba GPO management overview: https://deepwiki.com/samba-team/samba/5.3-group-policy-object-management
- Mulder, Group Policy book (CSE API, `Registry.pol`): https://dmulder.github.io/group-policy-book/intro.html
- adsys how-to: https://documentation.ubuntu.com/adsys/stable/how-to/use-gpo
- Red Hat, GPO access control (SSSD): https://docs.redhat.com/zh-cn/documentation/red_hat_enterprise_linux/10/html/integrating_rhel_systems_directly_with_windows_active_directory/applying-group-policy-object-access-control-in-rhel
- MS-GPREG: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gpreg/5c092c22-bf6b-4e7f-b180-b20743d368f5

### Shell restrictions (2026-11-16)

The Start menus carry out the common restrictions of `Policies\Explorer` (`src/dlls/wexshell/restrictions.c`: the
value counts when it is not 0 in the user's hive or in the machine's). `NoRun`, `NoClose`, `NoLogoff`,
`NoControlPanel`, `NoSetFolders` (Control Panel, Network Connections and Printers), `NoSetTaskbar`, `NoFind`
(including F3 on the desktop), `NoSMHelp`, `NoSMMyDocs` and `NoRecentDocsMenu` take the command out of both the Classic
and the XP-style menu (an empty cascade goes too, and the Log Off and Turn Off buttons close up), and
`run_start_command` and the Run dialog refuse the command when something else asks for it. The editor lists these ten
as applying; the rest of the "stored" ones (`NoChangeStartMenu`, `NoFolderOptions`, `NoViewContextMenu`,
`NoFileAssociate`, `NoNetHood`, `ClearRecentDocsOnExit`, `NoWinKeys`) are still only read by programs that call
`SHRestricted`. Checked by hand under Xvfb with the build tree (menu with and without `NoRun`, `NoClose`, `NoLogoff`,
`NoFind`); there is no automated check yet, since the shell needs a desktop. The procedure is below.

A value counts as a REG_DWORD or as the four bytes of a REG_BINARY (old ADM templates write the flags that way).

To repeat the screenshot check (build machine `wexgpo`, root; needs `Xvfb`, `xdotool` and ImageMagick's `import`): build
with `scripts/lab/wex-run.sh build`, then with `B=/root/WE-prefetch/build/wow64`, `WINEPREFIX` a scratch prefix,
`DISPLAY=:99` and the policy key `K='HKCU\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer'`:
start `Xvfb :99 -screen 0 1024x768x24 &`; `$B/wine reg add "$K" /v NoRun /t REG_DWORD /d 1 /f` (and `NoClose`,
`NoLogoff`, `NoFind`); start the shell as the session does, `$B/wine explorer.exe /desktop=Shell,1024x768 /shell &`;
after about 12 s click the Start button (`xdotool mousemove 30 755 click 1`) and take `import -window root x.png`.
Without the values the menu shows Search, Run, Log Off and Turn Off Computer; with them those are gone and the
footer is empty. Stop with `$B/wine wineboot -k` and delete the values again (plain `explorer.exe` without
`/desktop=... /shell` opens a folder window, not the shell).

#### More restrictions, and the shell reading them after a refresh (2026-11-16)

`NoChangeStartMenu` (the pin, remove and show-on-desktop verbs of the Start panel's menu are grayed), `NoWinKeys` (the
combinations with the Windows key do nothing; the key alone still opens Start) and `NoNetHood` (no My Network Places
on the desktop, none in the Start menu) are carried out as well, which makes thirteen in the editor's "applies" list.
`NoFolderOptions`, `NoViewContextMenu`, `NoFileAssociate` and `ClearRecentDocsOnExit` stay "stored": they belong to
the folder windows and to the log off, which this shell does not make from these policies yet.

A user's policy is applied after the shell is up (the session script runs `gpupdate /target:user` five seconds after
the shell starts), so the shell has to learn that a refresh has applied. It does not poll, and a broadcast does not do
it (the shell's desktop is a desktop of its own, `explorer.exe /desktop=NAME /shell`, and a message from another
process, or from session 0, which is another wineserver, does not reach it). The prior art is "Step 3: refresh
signalling" above: `userenv`'s `RegisterGPNotification`, the call Windows programs use for this, signalled through the
serial that the writer leaves in the registry (a registry change notification reaches every session through session
0). The shell registers two events, for the computer's and for the user's policy (`shell_restrictions_watch`,
`restrictions.c`), and a thread-pool wait posts `WM_WEX_POLICY` to the desktop view, which builds the desktop again and
tells the taskbar (`WM_SETTINGCHANGE`). A restriction, or the removal of one, shows when the refresh ends, with no
logoff (checked under Xvfb by bumping the user serial: the icon went within seconds). The shell makes sure that `HKCU\Software\Policies` exists first, which userenv watches: a user who has had no policy yet would otherwise miss the first refresh. The Start menus read the values each time they are opened.
