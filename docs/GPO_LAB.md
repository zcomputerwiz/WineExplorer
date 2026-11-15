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
| `parse` | on the member: `wex-gpupdate.sh`'s reading of the real `net ads gpo list` (`parse-guest.sh`), for the machine and for `wexuser1` |
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
`WEX_LAB_TREE_GPUPDATE=1` puts this tree's `wex-gpupdate.sh` into the overlay in place of the image's, so a
change to the script is tested without a new image.

## The build machine

`wexgpo` is a WSL distribution of its own (made 2026-11-14 with the owner's agreement), so that Group Policy work
does not wait for the shared wexbuild. It is a fresh Debian 13 (`wsl --install Debian --name wexgpo --location
D:\DeepThought\WSL\wexgpo --no-launch`), not a copy of wexbuild (196 GB, mostly VM disks), set up by:

1. `scripts/lab/wexgpo-provision.sh LIST`: wexbuild's `apt-mark showmanual` (LIST) with i386 added, systemd on,
   root by default, `/root` at 755 and a user with UID 1000 (two differences from a fresh install that the
   first runs found: the suites run the build in `/root` as their test users, and the broker's unit test stands
   in UID 1000 for the caller); then `wsl --terminate wexgpo`. Everything installed except `winetricks`
   (contrib).
2. `scripts/lab/wexgpo-setup-tree.sh BRANCH WINE_BRANCH`: `/root/WE-prefetch` as `wex-run.sh` expects it (shallow
   clones from GitHub, the overlay assembled, a WoW64 configure, a first build; about 30 minutes).

Then `scripts/lab/wex-run.sh` runs there as on wexbuild:
`wsl.exe -d wexgpo -u root --exec bash /mnt/d/DeepThought/Projects/<worktree>/scripts/lab/wex-run.sh normal`.
First result (2026-11-14, `claude/group-policy` 33731fd3 and later): normal suite 350 checks and the broker
test passed, as on wexbuild. The lab build (`lab`, `full`) needs `/root/owned-r8` (`scripts/lab/r8-lab-build.sh`), not set up yet. The
installed-machine disks and ISOs are not there either; an image is the next thing to make there.

## State (2026-11-14)

- The controller and the fixtures: made and checked (`check`) on 2026-11-14.
- `parse` passed on 2026-11-14 against Samba 4.17 (Debian 12). What the real output showed: Samba lists its own
  "Local Policy" first (not a GUID, no paths; its `link_type` 5 has no name, so the next label follows on the same
  line), which the parser skips; the domain's objects follow newest link first ("WexLab Domain", "WexLab",
  "Default Domain Policy"). Windows applies one container's links the other way round (MS-GPOL; GROUP_POLICY.md,
  "The order of several links on one container"); since 2026-11-15 `wex-gpupdate.sh` orders them as Windows
  does, and `parse` checks that "WexLab Domain" (link order 1) comes last.
- `machine` passed on 2026-11-15 with all 14 checks, on a disk installed on wexgpo from an ISO of
  `claude/group-policy` e1c34ecb (`scripts/lab/wexgpo-iso.sh`: build, package with `WEX_SESSION0=1`, ISO,
  unattended install; about an hour): the join, the domain's registry policy, the OU's object after the
  domain's, the security template, gpresult, a user's gpupdate through gpsvc and `wex-gpupdate.path`.
  Two lab findings on the way: a search for the OU before the join finds nothing, and an account left from an
  earlier run keeps its container at a join, so the guest moves its account into the OU with `samba-tool
  computer move` after the join and reads its real place back.

## Next

- A domain user logging on to WineExplorer (the accounts map has local users only), then user policy from the
  domain end to end with `wexuser1`.
- If the waits on the shared runner grow, a copy of wexbuild for this lab (the owner allowed it on 2026-11-14),
  holding the installed disks next to the controller.
- A Windows Server controller stays out: the evaluation download needs a registration form with personal details.
