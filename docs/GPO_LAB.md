# The GPO VM lab

Written 2026-11-14. The lab domain in which Group Policy ([GROUP_POLICY.md](GROUP_POLICY.md)) is tested end to
end: a Samba Active Directory controller, the test objects a check needs, and the guests that join it. The scripts
are in `installer/tests/ad/`; `gpo-lab.sh` is the entry point.

## What is in it

| Part | Where | Made by |
|---|---|---|
| Controller `dc1`, realm `AD.WEXLAB.EXAMPLE`, flat name `WEXLAB`, 10.71.0.10 | QEMU guest (Debian 12 cloud image) in the Debian WSL distribution | `dc-create.sh`, `dc-provision.sh` |
| `OU=WexMachines`, domain user `wexuser1` | the controller | `fixtures-guest.sh` |
| GPO "WexLab Domain", linked at the domain | the controller | `fixtures-guest.sh` |
| GPO "WexLab OU", linked at `OU=WexMachines` | the controller | `fixtures-guest.sh` |
| Plain Debian member `member1`, 10.71.0.20 | QEMU guest, Debian distribution | `client-create.sh` |
| Installed WineExplorer machine, 10.71.0.30 | an overlay of an installed disk, where the disk is (wexbuild) | `machine.sh`, `machine-guest.sh` |

The guests share the lab network 10.71.0.0/24, a QEMU multicast socket (230.0.0.71:17071), which crosses WSL
distributions; each also has a user-mode card for packages. The directory `/var/tmp/wex-ad-lab` holds the disks,
the SSH key, and the throwaway passwords of Administrator and `wexuser1` (mode 600, never printed). The older
object "WexLab" of `gpo-create.sh` stays linked at the domain; it has no extension names, which is itself a case
worth having (a client lists it for no extension).

### The test objects

"WexLab Domain":
- `Machine/Registry.pol`: `HKLM\Software\Policies\WexLab` `Limit`=7, `DomainOnly`=1.
- `User/Registry.pol`: `HKCU\Software\Policies\WexLab` `UserLimit`=3.
- `Machine/Microsoft/Windows NT/SecEdit/GptTmpl.inf` (UTF-16LE with a byte order mark, as Windows writes it):
  `LockoutBadCount`=7, `LockoutDuration`=30, `ResetLockoutCount`=30, `MinimumPasswordLength`=8 (not applicable
  here), and the security option `...\CurrentVersion\Policies\System\WexLabOption`=3. Samba's own
  `samba-tool gpo manage security list` reads it back.
- Extension names: machine registry and security, user registry.

"WexLab OU": `Machine/Registry.pol` `Limit`=9, `OuOnly`=1; machine registry extension only. A computer in the OU
applies it after the domain's object, so `Limit` is 9 and both `DomainOnly` and `OuOnly` are set.

Each run of `fixtures` raises both objects' versions, so clients see a change.

## Use

As root. On this Windows host the checkout has CRLF line ends, so the scripts run from an LF copy made by
`scripts/lab/gpo-lab-run.sh` (`wsl --exec`, because `wsl.exe -- ...` lets the default shell expand `$`):

```
wsl.exe -d Debian -u root --exec bash -c 'T=/mnt/d/DeepThought/Projects/<worktree>; WEX_GPO_TREE=$T bash <(sed "s/\r$//" $T/scripts/lab/gpo-lab-run.sh) up'
```

| Command | What it does |
|---|---|
| `up` | start the controller (first time: make it, about 5 minutes) and make or update the fixtures |
| `fixtures` | make or update the OU, the user and the two objects |
| `check` | print the links, extension names, versions, SYSVOL files and the template as Samba reads it |
| `member` | the plain Debian member: joins and checks the join detection |
| `machine SOURCE_VM [TEST_VM]` | Group Policy end to end on an installed machine (`machine.sh`), in the distribution with the disk |
| `status`, `down`, `ssh CMD` | which guests run and what the domain holds; stop them; a command on the controller |

`machine` needs `/var/tmp/wex-ad-lab/admin_password.txt` in its own distribution (copy that one file from Debian)
and a disk installed from an ISO packaged with `WEX_SESSION0=1`. Its guest joins the machine into
`OU=WexMachines` when the controller has it and checks, besides the join and the registry policy of step 4:

- `gpo_order_ou_after_domain`: `Limit`=9, `DomainOnly`=1, `OuOnly`=1;
- `security_template_applied`: the lockout values and the security option (step 5);
- `gpresult_lists_objects_in_order`: "WexLab Domain" before "WexLab OU" (steps 6 and 7);
- `user_gpupdate_runs_a_refresh`: the installed user's `gpupdate` starts `wex-gpupdate.service` through
  `wex-gpupdate.path` and reports the refresh (step 7).

On an image without steps 5 to 7 (no `wex-gpupdate.path`) these are skipped with a note.

## State (2026-11-14)

- The controller and the fixtures: made and checked (`check`) on 2026-11-14.
- `machine` with the new checks: not run yet. It needs an ISO from `claude/group-policy`, which needs the
  wexbuild runner (shared with the other sessions) for a build, `package-wine.sh` with `WEX_SESSION0=1`,
  `build-iso.sh` and an unattended install.

## Next

- An ISO of `claude/group-policy` and a `machine` run with all checks.
- A domain user logging on to WineExplorer (the accounts map has local users only), then user policy from the
  domain end to end with `wexuser1`.
- If the waits on the shared runner grow, a copy of wexbuild for this lab (the owner allowed it on 2026-11-14),
  holding the installed disks next to the controller.
- A Windows Server controller stays out: the evaluation download needs a registration form with personal details.
