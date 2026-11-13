# Group Policy and Linux GPO sync (design note)

Surveyed on 2026-11-11. Status: step 1 (the session 0 policy writer) is implemented, the rest is design. The Linux facts below were checked online on the
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
- Who may write the policy trees. On Windows only administrators and SYSTEM can; `HKLM` is covered by the machine
  root, but a user's own programs can write their own `HKCU\Software\Policies` in Wine today, so user policy is not
  binding until those keys are protected.
- Local policy without a domain: `secpol.msc`, `net accounts` and `NetUserModalsGet/Set` level 3 (a stub in Wine)
  on top of the lockout values, independent of the domain work.
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
