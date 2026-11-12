# Group Policy and Linux GPO sync (design note)

Surveyed on 2026-11-11. Status: design only, nothing implemented. The Linux facts below were checked online on the
same day (sources at the end); anything marked "unverified" was not.

## Goal

When the Linux host is joined to an Active Directory domain, the domain's Group Policy should reach the Wine prefix
the shell runs in, so policy-aware Windows components see the same `Software\Policies` values they would on an XP
machine. What does not apply to Wine is shown grayed out, never silently ignored.

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

## Design

1. **Join detection (`wexhost`).** Detect the join from `net ads info` or SSSD/realmd state and publish domain name,
   flat name and role over D-Bus (prefer D-Bus over spawning tools). Unjoined hosts keep today's behaviour.
2. **Report the join to Windows programs.** Make `NetGetJoinInformation` return `NetSetupDomainName` and
   `DsRoleGetPrimaryDomainInformation` return a domain member role with the real names, using the `wexhost` data.
   Both are small stock-Wine changes; list them in [STOCK_CHANGES.md](STOCK_CHANGES.md).
3. **Fetch and apply through Samba, not our own parser.** Write a small Wine-registry CSE using Samba's documented
   extension API. It runs inside `samba-gpupdate`, reads the GPO's `Registry.pol` and writes
   `HKLM\Software\Policies` (machine) and `HKCU\Software\Policies` (user) into the Wine prefix, honouring PReg
   delete and `**del.` value markers. `wexhost` only schedules `samba-gpupdate` (and hands the SSSD case to
   `oddjob-gpupdate`).
4. **Refresh signalling.** Port the ReactOS event and mutex code into Wine's `userenv` and export `RefreshPolicy`,
   `RefreshPolicyEx` and the wait calls. After the CSE finishes, `wexhost` sets the refresh-done events.
5. **Applied GPO list.** `GetAppliedGPOListW` returns the list recorded by the CSE (name, GUID, link, version) instead
   of failing, so `gpresult`-style tools have data.
6. **Not applicable to Wine.** Software installation, folder redirection, disk quotas, IPsec and Windows
   security-settings templates are shown grayed out in any policy UI and reported "not applicable" in RSoP output.
7. **Later.** A `gpupdate` command that calls the refresh path, `gpresult` over the recorded list, and a local
   `gpedit.msc` that edits a local `Registry.pol`.

## Open questions

- Per-user policy: whether `samba-gpupdate` runs a user pass for each logon, and in which account, is unverified. Check
  the 4.24 source before designing the `HKCU` half. The wex logon flow and profile hive loading (`e1s_load_profile`)
  decide where the user hive is writable.
- Which GPO scopes SSSD-only hosts can fetch without Winbind (Samba's GPO code reads SYSVOL over SMB with the machine
  account's Kerberos ticket).
- Whether the CSE belongs in this repository or upstream Samba. The Samba API is Python and GPL-3.0-or-later; this
  project's rule is that GPL-3.0 code makes the module containing it GPL-3.0, so keep the CSE a separate file and make
  that licence decision explicitly.
- Test setup: a Samba AD domain controller in the QEMU lab (see [TOOLING.md](TOOLING.md)) with one GPO that sets a
  registry value, a 32-bit program reading it, and a join/unjoin round trip.

## First work order

Join detection in `wexhost` plus the two stock-Wine changes (step 1 and 2), tested with a Samba AD controller in the
lab and a 32-bit program that calls `NetGetJoinInformation`. No policy application yet.

## Sources

- Samba wiki, Group Policy: https://wiki.samba.org/index.php/Group_Policy
- samba-gpupdate man page: https://manpages.ubuntu.com/manpages/jammy/man8/samba-gpupdate.8.html
- Samba GPO management overview: https://deepwiki.com/samba-team/samba/5.3-group-policy-object-management
- Mulder, Group Policy book (CSE API, `Registry.pol`): https://dmulder.github.io/group-policy-book/intro.html
- adsys how-to: https://documentation.ubuntu.com/adsys/stable/how-to/use-gpo
- Red Hat, GPO access control (SSSD): https://docs.redhat.com/zh-cn/documentation/red_hat_enterprise_linux/10/html/integrating_rhel_systems_directly_with_windows_active_directory/applying-group-policy-object-access-control-in-rhel
- MS-GPREG: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gpreg/5c092c22-bf6b-4e7f-b180-b20743d368f5
