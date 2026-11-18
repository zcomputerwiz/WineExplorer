# Joining a domain: how Windows does it, and the tools WineExplorer offers (2026-11-18)

Winbind is the supported join (decided by the owner, 2026-11-18); so is our own engine (not realmd). Domain logon (`DOMAIN_LOGON.md`), the accounts map,
domain groups in tokens and Group Policy (`GROUP_POLICY.md`) are built on Samba's tools. A machine joined with SSSD
(`realm join`) is told so and is not touched: `wex-domain-state.sh --kind` says `sssd`, `wex-domain-join.sh join` refuses,
`wex-gpupdate.sh` does local policy only, and domain logon is not offered (the admission step needs Winbind).

Goal (owner): the user-facing utilities do what Windows' do, with the same names, options and dialogs, on top of this.

## How Windows does it

Sources: the Microsoft pages on the netapi32 join functions (`NetJoinDomain`, `NetUnjoinDomain`,
`NetRenameMachineInDomain`, `NetGetJoinInformation`, `NetValidateName`), on `netdom`, `Add-Computer`,
`Remove-Computer`, `Rename-Computer`, `nltest`, `whoami`, the Windows XP / Server 2003 and Windows 10 procedures for the
Computer Name tab, and the workstation service protocol (MS-WKST). Not confirmed from any page: the exact texts of the
dialogs (the "Welcome to the domain" and restart messages, the credential prompt, the error dialogs). Those come from the
XP VM, which is the reference for parity (`CLAUDE.md`).

**The API.** One call does the work: `NetJoinDomain(server, domain, ou, account, password, flags)`.
- `server` NULL is this computer; `domain` is a NetBIOS or DNS name (a preferred controller can be named as
  `domain\controller`); `ou` is an optional distinguished name; `account` is `DOMAIN\user` or a UPN; the password is the
  account's (or the machine password, for a join to an account that was made beforehand).
- Flags: join a domain (1), create the account (2), delete it at unjoin (4), join even if joined (0x20), unsecured join to
  a pre-made account (0x40) with the machine password passed (0x80), defer the service principal names (0x100), join with
  the new name that `SetComputerNameEx` queued (0x400, Vista and later), read-only (0x800), and others up to 0x20000.
- The caller must be in the local Administrators group; the account must be allowed to add workstations (every
  authenticated user, up to 10 computers, unless the domain says otherwise).
- A restart completes it. Errors seen by users: access denied, no such domain (1355 when the controller cannot be
  contacted), logon failure (1326), account exists (2224), already joined (2691), not joined (2692), the join hardening's
  account re-use block (2732).
- `NetUnjoinDomain(server, account, password, flags)` leaves; the only option disables (does not delete) the account.
  `NetRenameMachineInDomain` renames and updates the account. `NetValidateName` checks a computer name (valid NetBIOS
  name, not in use), a workgroup, a domain (exists) or a DNS name. `NetGetJoinInformation` answers unjoined, workgroup or
  domain with the name; that is the only one implemented here.

**What the workstation service does.** The API is a client of the Workstation service (wkssvc, RPC on a named pipe). The
service checks the caller's right to change the configuration (local Administrators), finds a controller, opens a session
with the caller's credentials, learns the domain's name and SID, makes the machine password (120 random characters for a
normal join; the first 14 letters of the name, lower case, for an unsecured one), creates the computer object in the given
OU (the Computers container by default) with the workstation trust account control, the DNS host name and the `HOST/`
service principal names, configures the secure channel, time service and DNS registration, adds Domain Admins to the
local Administrators and Domain Users to Users, and keeps the new state for the next start. An unjoin undoes this, disables
the account and removes the group memberships.

**The command line.**
- `netdom join COMPUTER /Domain:D [/OU:DN] [/UserD:dom\user] [/PasswordD:pw|*] [/UserO:u /PasswordO:*] [/PasswordM:*]
  [/ReadOnly] [/REBoot[:seconds]]`; `netdom remove COMPUTER [/Domain] [/UserD] [/PasswordD] [/Force] [/REBoot]`;
  `netdom renamecomputer COMPUTER /NewName:N [/UserD ...] [/Force] [/REBoot]`; `netdom verify` and `netdom reset` check and
  repair the secure channel; `netdom query WORKSTATION|SERVER|DC|OU|PDC|FSMO|TRUST`; `netdom resetpwd` is for controllers.
  `*` asks for the password without echo.
- PowerShell: `Add-Computer -DomainName D -Credential C [-OUPath DN] [-NewName N] [-Restart]` (also `-WorkgroupName`),
  `Remove-Computer -UnjoinDomainCredential C [-WorkgroupName] [-Restart]`, `Rename-Computer -NewName N [-DomainCredential C]`.
- WMI: `Win32_ComputerSystem.JoinDomainOrWorkgroup`; `nltest /dsgetdc:D`, `/sc_verify:D`, `/sc_query:D`; `whoami /fqdn`,
  `/upn`; `net config workstation`; `systeminfo`.

**The window.** System Properties > Computer Name tab > Change (XP also has the Network ID wizard on that tab): in
"Computer Name Changes", "Member of" is Domain or Workgroup, with More for the DNS suffix; OK asks for the name and password
of an account that may join; a successful join says "Welcome to the domain", then asks to restart (nothing takes effect
before); leaving for a workgroup disables the account. After the restart the logon dialog has a "Log on to" box. Computer
names are 15 characters for NetBIOS (up to 63 for DNS): letters, digits and hyphens.

## What WineExplorer has

| Part | State |
|---|---|
| `NetGetJoinInformation` (wkscli, stock Wine semi-stub) | reads `HKLM\Software\WineExplorer\Domain`, written by `wex-domain-state.sh` |
| `NetJoinDomain`, `NetUnjoinDomain`, `NetValidateName` | done (2026-11-18): `src/dlls/wexjoin`, forwarded from wkscli and netapi32 (stock change in `STOCK_CHANGES.md`); `NetRenameMachineInDomain` answers "not supported" |
| A workstation service | none: the engine's socket takes its place (see "How the API reaches root") |
| `netdom.exe` | done: `join`, `remove`, `verify`, `reset`, `query DC`; `renamecomputer` is "not supported yet" (see below) |
| `nltest`, PowerShell cmdlets | none (Wine has `net`, `whoami`, `powershell`, `wmic` stubs) |
| System Properties > Computer Name | renames the host through wexhost (hostnamed); no domain choice |
| Join engine (root) | `installer/scripts/wex-domain-join.sh` (this change): `join`, `leave`, `status` with Winbind, tested with fakes (`scripts/test_domain_join.sh`) |
| Logon with `DOMAIN\user`, groups, Group Policy | built (`DOMAIN_LOGON.md`, `GROUP_POLICY.md`) |

## The engine: `wex-domain-join.sh`

`join DOMAIN --user NAME --password-stdin [--workgroup NB] [--ou DN]`: refuses a bad name, an existing join (and an SSSD
join, naming it), a domain that DNS does not know; writes `smb.conf` (the NetBIOS name is the host name cut to 15, the
domain's is the first label unless `--workgroup`), `krb5.conf` and the `winbind` entries of `nsswitch.conf`, keeping the first
versions as `*.wex-pre-join`; takes a Kerberos ticket from the password on standard input (never on a command line) into a
private cache and joins with it (`net ads join -k`, `createcomputer=` for the OU); a failure before the machine account
exists puts the files back; then PAM (`winbind`, `mkhomedir`), the home folder of the domain, the `winbind` service and
`net ads testjoin`. `leave --user NAME --password-stdin` removes the account (`net ads leave -k`), `leave --force` only this
machine's side; both undo nsswitch and PAM and put the first files back. `status` prints the kind, domain, service and
`testjoin`. The same engine serves every utility below; what Windows calls the Workstation service's work is this plus the
Windows-side state (`wex-domain-state.sh`).

## `netdom.exe` (2026-11-18)

The syntax of Windows' `NETDOM` (from Microsoft's pages for each command and from transcripts in forum threads), for this
computer only (another computer is refused with "not supported"): `join [computer] /Domain /UserD /PasswordD /OU`, `remove
[computer] [/Domain] /UserD /PasswordD /Force`, `verify`, `reset`, `query DC`, `renamecomputer` (the engine cannot rename the
machine account yet), `help`. Options may be given as `/Name:value` or `-Name:value`, abbreviated as Windows does
(`/UD`, `/PD`, `/UO`, `/PO`, `/PM`, `/RO`, `/REB`, `/SPP`); `/REBoot`, `/SecurePasswordPrompt`, `/Verbose`, `/Server`, `/Verify`
are accepted and change nothing; `/UserO`, `/PasswordO`, `/UserF`, `/PasswordF`, `/ReadOnly` and `/PasswordM` are refused as
not supported. A password of `*`, or none with a user, is asked for ("Type the password associated with the domain user:",
without echo at a console, one line from standard input otherwise). The other commands of Windows (`add`, `move`, `resetpwd`,
`trust`, `time`, `computername`, `query` of other objects) say they are not supported on this system.

Output: a success ends with "The command completed successfully."; a failure prints the text of the error (Wine's message
table; the engine's reason follows in parentheses when it differs) and "The command failed to complete successfully."; a
wrong command line prints "The parameter is incorrect." and `Try "NetDom HELP" for more information.`; `verify` prints "The
secure channel from COMPUTER to the domain DOMAIN has been verified." and "The connection is with the machine DC.";
`query DC` prints "List of domain controllers with accounts in the domain:" and the names. The exit status is 0, or the
error code of the failure: forum threads show batch files testing it (2224, account exists), but no Microsoft page documents
it. Not found online, so these are our own: the line after a successful join (none is printed: no restart is needed),
the text of `reset`'s success ("has been reset"), and the wording of the help screen. Wine's text for error 5 is "Access
denied." where Windows says "Access is denied." (stock message table, not changed).

Checked in `scripts/test_domain_join.sh` (the engine's operations), `st_join_api_reaches_the_engine_only_from_the_system_group` (the
API and `netdom` against a fake engine, from the system group and from a standard user). Not checked: a real join to a domain
from `netdom` (the AD lab), and `netdom` on a console (the password without echo).

## The layers still to build (order)

1. **A privileged service and the API (done, see above).** `NetJoinDomain` and the others in wkscli call a WineExplorer workstation service in
   session 0 (local RPC, our own interface: nothing else speaks MS-WKST here), which checks the caller as Windows does
   (local Administrators: an elevated administrator; the consent prompt is the elevation), maps the flags (join,
   create account, `DOMAIN_JOIN_IF_JOINED`, unsecured join with the machine password through `net ads join` with a
   pre-made account, `JOIN_WITH_NEW_NAME` through the host name, `DEFER_SPN_SET`), and hands the request to root through
   a D-Bus method of a small root helper that only session 0's account may call (a policy file), because the engine is
   root's work and the password must not touch a file or a command line. The error codes of the API are the ones above.
   The stock-Wine part is the stub replacement in wkscli and the exports in netapi32, listed in `STOCK_CHANGES.md`.
   `NetValidateName` answers from DNS and Samba (`net ads` / `host`), `NetRenameMachineInDomain` from `net ads` and the
   host name.
2. **Tools.** `netdom.exe` (done), `nltest.exe` (`/dsgetdc`, `/sc_query`, `/sc_verify`, `/sc_reset` over `net ads` and `wbinfo`),
   the PowerShell cmdlets when the project's `powershell` can host them, `whoami /fqdn /upn`, `net config workstation`.
3. **The window.** The Computer Name page of System Properties gets Change..., "Computer Name Changes" (name, Domain or
   Workgroup, More), the credential prompt, the success and failure messages, the restart question, and the Network ID
   wizard; texts and layout copied from the XP VM. The logon dialog's "Log on to" box already takes `DOMAIN\user`
   (`logonui`).
4. **No restart** (owner, 2026-11-18: "if it takes effect immediately, no prompt for restart is needed"). Windows needs one;
   here the join takes effect when the engine ends (the Windows-side state is rewritten at once by `wex-domain-state.sh`),
   so the tools and dialogs do not ask for a restart and do not offer `/REBoot`-style options beyond accepting them
   silently (`netdom ... /REBoot` is accepted and does nothing), and the success message is the "Welcome to the domain"
   one only. If a step proves to need one (a service that reads its configuration at start), the engine restarts that
   service itself.

Decided (owner, 2026-11-18): the engine stays our own script, not `realmd` (a standard D-Bus service with polkit, which can
join with Winbind, but writes its own Samba configuration, while domain logon here needs the rid mapping from 20000 and
`DOMAIN\user` names, which realmd does not offer to configure). `ms-DS-MachineAccountQuota` and the join hardening of
current Windows are the domain's business; the tools only report the errors.

## How the API reaches root

The engine is root's work and takes a password, which must reach it without a file or a command line. As for the policy
spool (`GROUP_POLICY.md`), the process that asks is an elevated administrator's (the system group): the engine listens
on a Unix socket, `/run/wex-domain-join.sock`, owned by root with the system group and mode 0660 (a `wex-domain-join.socket`
unit that starts `wex-domain-join@.service`, which runs `wex-domain-join.sh serve` on the connection, one request each).
A process outside the group cannot connect: that is Windows' "access denied for a caller who is not an administrator".
The request is lines of `key=value` ended by an empty line (`op`, `domain`, `user`, `password`, `ou`, `workgroup`, `flags`);
the answer is `status=N` (a Win32 or network error code of the table above) and `message=TEXT`. The client is a header
(`src/include/wexjoinclient.h`) that `wkscli` (NetJoinDomain and the others) and the tools include; it connects with
winsock's `AF_UNIX` where the build of Wine supports it (probed first, 2026-11-18).
