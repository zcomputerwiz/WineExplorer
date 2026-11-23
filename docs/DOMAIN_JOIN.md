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
  `NetRenameMachineInDomain` renames the account and the host name together. `NetValidateName` checks a computer name (valid NetBIOS
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
| `NetJoinDomain`, `NetUnjoinDomain`, `NetValidateName` | done (2026-11-18): `src/dlls/wexjoin`, forwarded from wkscli and netapi32 (stock change in `STOCK_CHANGES.md`); `NetRenameMachineInDomain` renames in the domain (2026-11-22) |
| A workstation service | none: the engine's socket takes its place (see "How the API reaches root") |
| `netdom.exe` | done: `join`, `remove`, `verify`, `reset`, `query DC`; `renamecomputer` (in a domain it renames the account and the host name) |
| `nltest.exe` | done (2026-11-21): `/dsgetdc`, `/dcname`, `/dclist`, `/dsgetsite`, `/query`, `/sc_query`, `/sc_verify`, `/sc_reset`, `/sc_change_pwd` (see below) |
| PowerShell cmdlets | done in part (2026-11-22, docs/POWERSHELL.md): the official PowerShell 7.7.0 runs on the WoW64 build with a console; `mi.dll` (new, `src/dlls/mi`) and `Win32_ComputerSystem` (`Rename`, `JoinDomainOrWorkgroup`, `UnjoinDomainOrWorkgroup`, `PartOfDomain`, `Workgroup`) in `wbemprox` make `Get-CimInstance`, `Invoke-CimMethod` and `Rename-Computer` work; the `WexComputer` module supplies `Add-Computer`, `Remove-Computer`, `Test-ComputerSecureChannel` and `Reset-ComputerMachinePassword`, which PowerShell 7 does not have. Joined, renamed, left and secure-channel-checked from PowerShell against the AD lab (29 checks, join-ps-guest.sh) |
| System Properties > Computer Name | renames the host through wexhost (hostnamed); in a domain the engine renames the account and the host name |
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
[computer] [/Domain] /UserD /PasswordD /Force`, `verify`, `reset`, `query DC`, `renamecomputer`, `help`. Options may be given as `/Name:value` or `-Name:value`, abbreviated as Windows does
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

## Computer Name in System Properties (2026-11-18)

`sysdm.cpl` (Computer Name tab) follows the XP dialogs: "Computer Name Changes" with the computer name, the full name, More...
(the DNS suffix and NetBIOS name, shown and not changed), and "Member of" with Domain and Workgroup. A domain asks first
(`NetValidateName`), then for the user, password and domain of an account that may join ("Enter the name and password of an
account with permission to join the domain."), then joins through `NetJoinDomain` and says "Welcome to the X domain." No
restart is asked for: the join takes effect at once. Leaving uses `NetUnjoinDomain` with the account deleted and says
"Welcome to the WORKGROUP workgroup." A failure shows the error with a Details >> pane holding the engine's text; a domain
controller that cannot be found says so, as Windows does. A standard user is offered an elevated copy of the page.
Checked under Xvfb against a fake engine (validation error, credentials, logon failure). Not done: the Network ID wizard
(the button is disabled), and a real join from the dialog (the AD lab).

## `nltest.exe` (2026-11-21)

The options of Windows' `NLTEST` that make sense for one computer joined with Winbind, with its syntax (`/OPTION:value`):
`/DSGETDC:domain` (DC, address, GUID, domain, forest, sites, flags), `/DCNAME`, `/DCLIST`, `/DSGETSITE`, `/QUERY`,
`/SC_QUERY`, `/SC_VERIFY`, `/SC_RESET`, `/SC_CHANGE_PWD`, `/SERVER:` (this computer only); the flags of `/DSGETDC` are
accepted and ignored; every other option says it is not supported. The domain must be the one the computer is joined to
(NetBIOS or DNS name; none means that one), else "Status = 1355 0x54b ERROR_NO_SUCH_DOMAIN". The engine answers two
questions for it: `dsgetdc` (what the controller says about itself, `net ads lookup`: name, address, GUID, domain, DNS domain,
forest, sites, DS_..._FLAG as hex) and `scquery` (the controller of the secure channel and `net ads testjoin`).

The format was taken from running the Windows tool on a machine that is not in a domain: results on standard output, failures
as "Getting DC name failed: Status = N 0xN NAME" or "I_NetLogonControl failed: Status = N 0xN NAME" on standard error with
exit status 1, a success ends with "The command completed successfully" (no full stop), and `/DCLIST`'s failure exits with 0.
Not checked against Windows, because only a joined machine shows them: the exact lines of a successful `/DSGETDC` (the
labels are from memory of the tool), the names and order of the flags, the lines of `/SC_RESET` and `/DCLIST`.

## The Network Identification Wizard (2026-11-21)

The Network ID button of the Computer Name tab opens the XP wizard (`src/dlls/sysdm.cpl/netid.c`), pages and texts from the
XP VM: Welcome; "Connecting to the Network" (business or home); "What kind of network do you use?" (with or without a
domain); Network Information; User Account and Domain Information (user, password, domain; the defaults are the user and
the computer's name); Computer Domain (when no account for the computer is found: its name and domain; Next waits for a
domain); Workgroup; Completing. The join is the Change dialog's (`NetValidateName`, then `NetJoinDomain`, the error dialog
with Details, the offer of an elevated System Properties), and so is leaving ("without a domain" on a joined computer asks
for the account and leaves). Not built, because they do not apply: the pages that create a local user account (a domain user
is admitted at the first logon, `DOMAIN_LOGON.md`), and a workgroup's name (Samba's setting: shown, not changed). No restart
is asked for. The pictures on the first and last pages are drawn in the code (a network of nodes), not taken from Windows.

## Computer name

The Change dialog renames the computer through the host's hostname service (polkit, as on any desktop) and the new name is
in force at once, so no restart is asked for. In a domain the rename goes to the join engine (`op=rename`), which does
the account and the host name together; the credentials of an account that may rename the computer object are asked, and
the engine runs as root, so there is no second authorisation prompt. `netdom renamecomputer /NewName:N /UserD:U
/PasswordD:*` does the same. Leaving the domain and renaming in one step also works: it leaves first.

## Renaming a computer that stays in a domain

Windows renames the account and the computer together (`NetRenameMachineInDomain`). Samba's `net` has no such command, so
the engine does what Windows does with the tools that exist (it needs `ldap-utils` and `libsasl2-modules-gssapi-mit`,
which are not in the image, like Samba and Winbind: install them with `apt-get install ldap-utils
libsasl2-modules-gssapi-mit`; `wex-domain-join.sh rename` answers 50 and names them when they are missing). Nothing runs on the controller.

1. Ticket for the admin (as the join does), then over LDAP with the Kerberos bind (`ldapmodify`, `ldapsearch -Y GSSAPI`,
   `SASL_NOCANON`): the new name must be free (52), the object is renamed (`modrdn` of the CN), and `sAMAccountName`
   (`NEWNAME$`) and `dNSHostName` are set.
2. `netbios name` in `smb.conf`, then the machine password is changed with the machine's own credentials
   (`net ads changetrustpw`, a ticket cache of its own, so the administrator's ticket stays).
3. The keytab is flushed (that takes the service principal names off the account), the `HOST/` and `RestrictedKrbHost/`
   names are set, the keytab is created again, Winbind is restarted and `net ads testjoin` must pass.
4. The host name is set (`hostnamectl`, as root).

When a step fails, the engine puts back the host name, `smb.conf` and the account under its old name and answers 31.
The DNS record of the new name is updated best effort (`net ads dns register` with a 30 second limit; in the lab it
does not complete, which only gives a warning); the old record stays until the controller scavenges it.

Tested: `scripts/test_domain_join.sh` (order of the steps, the refusals 52, 87, 1210, 1326, 2692, the roll-back) and the
AD lab (`installer/tests/ad/join-guest.sh`, stage 9: the Change dialog renames to a new name, then `netdom
renamecomputer` back, with the account looked up on the controller, `testjoin`, the keytab, `smb.conf` and Winbind).

## Samba options

From Samba's `net` manual page: `createcomputer=` takes the OU as a path (outermost first, without `OU=`, separated by `/`),
which the engine builds from the distinguished name Windows takes; `-k` is deprecated for `--use-kerberos=required` (used
when `net --help` lists it); `net ads leave --keep-account` is used when a leave does not ask for `ACCT_DELETE` (Windows
disables the account instead). The join passes `osName=WineExplorer` and `osVer=` (Debian's `VERSION_ID`), so the account's operating system shows in the directory
(lab check `account_os_name`). Not used yet: `--no-dns-updates`, `dnshostname=`, `createupn=`, `machinepass=`.

## The layers still to build (order)

1. **A privileged service and the API (done; "How the API reaches root" below is what was built).** `NetJoinDomain`
   and the others are wexjoin.dll's (netapi32 and wkscli forward to it), which sends the request to the engine's socket.
   The planned session 0 workstation service and D-Bus helper were not built: the socket's group is the check.
   The stock-Wine part is the exports in netapi32 and wkscli, listed in `STOCK_CHANGES.md`. `NetValidateName` answers
   from DNS and Samba (`net ads` / `host`), `NetRenameMachineInDomain` from `net ads` and the host name.
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
The request is lines of `key=value` ended by an empty line (`op`, `domain`, `user`, `userdomain`, `password`, `ou`,
`workgroup`, `name`, `nametype`, `force`, `flags`); the answer is `status=N` (a Win32 or network error code of the table
above) and `message=TEXT`. The message has one form for every operation (2026-11-23): items separated by `||`. A
change's items are lines of text (verify and reset, and each note of a join, a leave or a rename is an item of its
own); a question's are fields `name=value` (`dsgetdc`, `scquery`) or names (`dclist`, one controller each); a
failure's message is one item, the reason. What comes from outside (a controller's or DNS's words) is cut of `|` and
control characters before it goes in. `wexjoin.h` reads it for netdom and nltest (`wex_join_item`, `wex_join_field`),
which print an item as a line. The client is wexjoin.dll: its unix side connects to the socket (`src/dlls/wexjoin/unixlib.c`),
and `src/include/wexjoin.h` declares the API, the `NETSETUP_` flags and the operations of `WexJoinSecureChannel` for
`netdom`, `nltest` and System Properties, with `wex_join_secure_channel` and `wex_join_last_message`, which load
wexjoin.dll when asked.

The questions (2026-11-23). On Windows any user may call `NetValidateName` and `DsGetDcName`, list the controllers and
query the secure channel (`nltest /sc_query`); only verify and reset, `I_NetLogonControl2`'s other codes, need an
administrator. So the questions have a socket of their own that every local user reaches, `/run/wex-domain-query.sock`
(mode 0666, `wex-domain-query.socket`, enabled beside the change socket). Its `wex-domain-query@.service` runs
`wex-domain-join.sh serve-query`: the engine answers `validate`, `dsgetdc`, `dclist`, `scquery` and `status` there,
refuses every other operation with 5 (access denied) and any request with a password with 87, and logs the request as
a `query`. It does not run as root: every question works for any user (DNS, a CLDAP ping, the state tool reading
`smb.conf`, and for the channel Winbind's NETLOGON ping with the machine account, `wbinfo --ping-dc`, which is what
`/sc_query` reports; `testjoin` would read the machine secret, root's file, and is no longer used for it). So its
unit runs the engine as a user that exists only while it runs (`DynamicUser`), with `ProtectSystem=strict`, no
capabilities and `NoNewPrivileges`: a fault in reading a request from a socket every user reaches runs as nobody in
particular. One user can hold the eight slots (`MaxConnections`) for up to `RuntimeMaxSec` (60 s), which delays other
users' questions and nothing else; that is accepted (the unit says so). wexjoin sends
`NetValidateName` and `WexJoinSecureChannel`'s `DCLIST`, `DSGETDC` and `SCQUERY` there, and everything else to the
change socket; a computer without the query socket (installed before it) answers the questions on the change socket.
Since a name's check no longer tells whether the caller may change the membership, the Change dialog and the Network
Identification Wizard ask that first, `WEX_JOIN_OP_CANCHANGE`, the status asked on the change socket, so a standard user
is offered the elevated copy before typing a password, as before. Checked on the lab member under systemd (a user who
is not root: the questions answered by an engine that is not root, the channel's state with Winbind running and
stopped, changes and passwords refused, `/etc` unchanged)
and by `test_domain_join.sh` and `test_sysrun_server.sh` (a standard user's `NetValidateName` and `netdom query dc`
answered, its join and leave refused, the fallback).

The account (2026-11-23). `DOMAIN\user` is sent as `user` and `userdomain`. The engine takes the user in the realm of
the domain being joined when `userdomain` is that domain (its NetBIOS or DNS name); a user of another domain given by its
DNS name is taken in that realm (a trust); one given by another NetBIOS name is refused (1355), as the engine cannot know
its realm: such a user gives `user@dns.name`. A `user@suffix` name is a user principal name, given to `kinit -E` (an
enterprise name, which the controller resolves whatever the case of the suffix and for an alternative UPN suffix).

Times and order (2026-11-23). One change of the machine's authentication at a time: `join`, `leave`, `rename` and
`reset` take the engine's lock (`flock`, `/run/wex-domain-join.lock`; without `flock` nothing is changed, error 50).
A request from the socket waits 5 seconds for it and is then answered `ERROR_BUSY` (170): it does not queue behind
another join that its caller's wait would not cover. The questions (`validate`, `status`, `dclist`, `dsgetdc`,
`scquery`, `verify`) take no lock, and the socket serves up to 8 connections, so they run beside a join. The unit runs a
request at most 300 seconds (`RuntimeMaxSec`) and then has 90 (`TimeoutStopSec`) to answer: a rename stopped part-way
(by that limit or by `systemctl stop`) puts the account, smb.conf and the host name back in its `SIGTERM` trap and
answers 1460. The caller waits `WEX_JOIN_WAIT_SECONDS` (420, `wexjoin.h`), more than both, so a time out it reports
is never a request that then succeeded.

What the join changes, the leave takes back (2026-11-23). `smb.conf` and `krb5.conf` are replaced by a rename of a new
file (their mode kept), never written in place, so Samba, Kerberos, the state tool and Group Policy never read half a
file. The join writes down the PAM profiles it turned on (`/etc/wineexplorer/domain-join-added`: `winbind`, and
`mkhomedir` when it was off); the leave turns off those and no others, removes `winbind` from `nsswitch.conf` and
drops the join's first copy of it, so a later failed join cannot put back a file from before the first one. The
machine's keys go too, on this machine: `/etc/krb5.keytab` is removed when every key in it is one the join made
(`NAME$`, `host/`, `RestrictedKrbHost/`), and kept with a warning when it holds anything else. `net ads keytab flush`
cannot do it after the leave (it needs the deleted account), and it would change the account in the domain, which
`leave --force` must not do. Once the
host's configuration has changed, Windows programs are told (`wex-domain-state.sh`, `HKLM\Software\WineExplorer\Domain`)
on every way out, a failure included: a join that `net ads testjoin` does not confirm yet is still a join, and the
dialogs read it as one.
