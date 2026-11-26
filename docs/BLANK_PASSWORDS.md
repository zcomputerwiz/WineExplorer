# Blank passwords: at the logon screen only

Status: implemented on branch `claude/gpo-blankpw` (2026-11-25), design reviewed the same day (points G1 to G5). Tested: the
helper (`scripts/test_accountsd.py`, fake AccountsService), the root side on a real Debian 13 (`scripts/test_blank_password.sh
--real`: reconcile, PAM diversion, the stacks themselves through `scripts/pamauth.c`), the chain through netapi32
(`s0_accounts_chain`). **Not yet run on the real AccountsService of an installed machine** (`accountsd-guest.sh`, section 5;
KNOWN_BUGS B117 lists what it must show). Until the netapi32 of a build carries this, `NetUserAdd` and `NetUserSetInfo` 1003 with
an empty password gave `NERR_PasswordTooShort` (parameter 3).

## What the owner decided

A blank password works at the logon screen (the greeter) and nowhere else, as on Windows XP with `LimitBlankPasswordUse`
(`su`, `sudo`, `runas`, a network logon and an elevation with credentials refuse it).

## Why crypt("") is not the answer

`crypt("")` is a hash, so the account would have a real password, the empty string, in every PAM stack that asks `pam_unix`:
`su`, `sudo` and the rest would take it. A blank password has to be an **empty shadow field**, with the greeter letting the
account in by a rule of its own, and every other stack refusing an empty field (no `nullok`).

## What the first design got wrong (found while coding)

The design assumed that AccountsService's `SetPasswordMode(NONE)` also puts the user in the group `nopasswdlogin`, which Debian's
greeter stacks use. **It does not** (checked on `accountsservice 23.13.9-7`, Debian 13: `accounts-daemon` runs `passwd`,
`chage` and `usermod` and nothing touches that group). So nothing would have made the account able to log on, and a user who got a
password later by `passwd(1)` would never leave the group. A root-run piece keeps the group now (below).

## The pieces

1. **Client (netapi32, `wexaccounts.c`).** A NULL or empty password is a blank password, as on Windows: the wire carries the
   empty string in `WEXA_CREATE_USER` and `WEXA_SET_PASSWORD`, never `crypt("")`. A password over `PWLEN` is still
   `NERR_PasswordTooShort`. `NetUserAdd` asks for a disabled account in the create itself (a trailing `u32 locked` after the
   hash), so a disabled account is never enabled, even briefly, and the helper takes it away if it cannot finish. (`NetUserSetInfo`
   level 1 with an empty password still means "no password change", as before.)
2. **Helper (`wex-accountsd`).** The empty string is accepted next to the `$6$salt$86 characters` form (any other non-empty string
   is `BAD_ARGUMENT`). A blank is `SetPasswordMode(2)` (NONE) on AccountsService, which empties the shadow field (`passwd -d`). A
   hash is `SetPassword` as before. **Invariant: a user is in `nopasswdlogin` if and only if the shadow field is empty.** The
   helper cannot read `/etc/shadow` or change groups (its account has no rights), so it asks the root-run reconcile unit
   (`wex-accounts-blank.service`, through one polkit grant, `installer/polkit/49-wexaccounts.rules`) and then **checks the result
   in the group file**:
   - a blank is finished when the user is in the group (until then the greeter refuses: fail safe); if the group does not
     change within 8 s the status is `BUSY` and a create is rolled back (the account is deleted again);
   - a hash for a member is finished when the user is out of the group; if not, `BUSY` is returned and the helper logs it (the
     account is passwordless at the greeter until the reconcile runs, as it was before);
   - a blank for a **disabled** account is refused (`BAD_ARGUMENT`): `usermod -L` puts `!` in the field, so the account is not
     blank. A new blank and disabled account is made in one create: NONE mode, then the lock. **Enabling it** (measured on the real
     machine, 2026-11-25): `SetLocked(false)` answers success and leaves the field `!` (shadow's `usermod -U` refuses to leave a
     passwordless account; this AccountsService sets no expiry), so the helper looks at `Locked` a second after an unlock
     and, if the account is still locked (only a field of `!` does that: an account with a hash is unlocked normally), **refuses**
     (`BAD_ARGUMENT`, logged: "set a password first", a blank one included). It does not make the account blank again: a bare `!`
     is also what an ssh-key-only account made by `useradd` has, and enabling it must not give it a passwordless console logon.
     (A follow-up, KNOWN_BUGS B117: remember the accounts the helper itself disabled while blank, and make only those blank again;
     check `Locked` against AccountsService's brief false right after `SetLocked(false)` on the real guest.);
   - once per start of the helper, at its first connection, it starts the reconcile (a change made outside of us never stands).
3. **The reconcile (`installer/blank/wex-accounts-blank-reconcile`, root).** Takes no argument and reads nothing from its caller:
   it makes the members of `nopasswdlogin` exactly the local accounts with an ordinary uid (`login.defs` range, never below 1000)
   and an empty field in `/etc/shadow`. It can only enforce the rule, never put an account that has a password in the group. Run at
   boot (`wex-accounts-blank.service`), when `/etc/shadow` or `/etc/passwd` changes (`wex-accounts-blank.path`, which covers
   `passwd(1)` and hand edits) and on the helper's request.
4. **PAM (`installer/blank/wex-blank-pam.sh apply`, from `setup-machine-root.sh`).** `common-auth` loses `nullok`: Debian 13
   generates it with `pam-auth-update` from `/usr/share/pam-configs/unix`, so that profile is **diverted** (`dpkg-divert`,
   kept as `/usr/share/wineexplorer/pam-config-unix.distrib`, **outside** `pam-configs`, which `pam-auth-update` reads whole: a copy
   there brings `nullok` back as a second profile, found in the first lab run) and a copy without `nullok` takes its place. A
   new `libpam-runtime` writes Debian's profile to the diversion and leaves ours: it fails safe (never `nullok`), at the price of
   not following later changes of Debian's profile. Then `su`, `sudo`, `pkexec`, `login`, `sshd` and our elevation services
   refuse an empty field. The greeter does **not** trust the group alone (reviewer, 2026-11-25): Debian's `auth sufficient pam_succeed_if.so user
   ingroup nopasswdlogin` lets a member in without a password, also one who has a password, whenever the reconcile is late or
   failed. In the greeter service (`WEX_BLANK_PAM_SERVICES`, default `lightdm`; **the greeter is assumed to be lightdm**) that line is
   replaced by `auth [success=ignore default=1] pam_succeed_if.so quiet user ingroup nopasswdlogin` and `auth [success=done
   default=ignore] pam_unix.so nullok`: an empty-field member gets in without a prompt, a member with a password must type it,
   others fall through to `common-auth`. Only that service keeps `nullok`. `pam-auth-update` runs without `--force` and the script
   dies with its message when it refuses. `wex-blank-pam.sh undo` puts Debian's profile back.
   - **The screen locker (G5):** the session has no lock screen yet (`Lock Computer` is not built). When it is, it must call a PAM
     service with the same `pam_succeed_if` line and no `nullok`, or a blank user could not unlock it.
5. **Windows side.** `LogonUser` and `runas` with a blank password fail with `ERROR_ACCOUNT_RESTRICTION` (1327), except for the
   interactive logon of the console. Where our logon path (`logonui`, the consent prompt's credentials) does not exist or does
   not check yet, this is a follow-up, not part of this change. A network logon with a blank password (SMB, a Windows share
   served by the host) is refused by the same rule: it never goes through the greeter's service.

## The invariant, and what fails closed

**A user is in `nopasswdlogin` if and only if the shadow field is empty.** The reconcile (root) is the one writer of the group;
nothing else adds or removes members. Every direction fails safe: a blank not yet in the group is refused by the greeter; a
member who has a password (the reconcile late or failed) still has to type it at the greeter (the pair above); the helper
**refuses to make a blank** (`WEXA_BLANK_REFUSED`, which netapi32 reports as `NERR_PasswordTooShort`, 2245, and the helper logs) while
any `pam_unix` line of `/etc/pam.d/common-auth` has `nullok` or the file cannot be read, so a failed `wex-blank-pam.sh apply`, a later
`pam-auth-update` or a `libpam-runtime` change cannot leave a blank account that `su` takes. netapi32 also honours the machine's
`MinPasswordLength` (the registry policy of `NetUserModalsGet` level 0): when it is above 0 a blank or short password is 2245, as on
Windows. The reconcile compares names as fixed strings (`.` is legal in a name).

A `BUSY` from `NetUserSetInfo` 1003 with a blank password does **not** mean nothing changed: the old password is already gone
(the field is empty) and only the group membership is late; the helper logs that ("is blank (the old password is gone)").

## Tests

- Helper unit (`scripts/test_accountsd.py`, `tests_blank`): the empty string is a blank (`CreateUser`, `SetPasswordMode(2)`, never
  `SetPassword`); the reconcile is asked for once per start of the helper; a hash after a blank takes the user out of the group;
  a blank for a disabled account and for a system account is refused; the atomic create (blank and disabled, hash and disabled,
  the order of the calls, bad flags); enabling a disabled blank account (unlock, blank again, unlock) and a disabled account with a password (one unlock); a failing step rolls
  the create back; a reconcile that does not run makes a blank create fail (after about 8 s) and a hash for a member fail; no
  hash in the helper log.
- Root side (`scripts/test_blank_password.sh`, add `--real` on a Debian machine that may be changed): the reconcile in a
  redirected tree (ordinary uid only, root and system accounts and unknown names removed, a hash or a lock removes a member,
  a missing group is made, a `UID_MIN` of 0 is not trusted); the PAM change in a redirected tree (the profile without `nullok`,
  Debian's kept outside `pam-configs`, the greeter line added once and before `common-auth`, a stack that has it untouched, apply
  twice); **real**: accounts made with `useradd` and `passwd -d`, the reconcile, `apply` (no `nullok` in `/etc/pam.d`, none after
  `apt-get install --reinstall libpam-runtime`), the baseline that Debian's stack takes an empty password, and with
  `scripts/pamauth.c`: the common stack refuses it, a greeter-like stack takes a blank account in `nopasswdlogin`, takes the right
  password of an ordinary one, refuses an empty answer for an account with a password, and needs the password once one is set;
  `undo` restores Debian's profile.
- Chain (`s0_accounts_chain`, `st_netapi_blank_password_is_the_blank_form`): a standard user is refused a blank add and a blank
  change; an elevated administrator's blank `NetUserAdd` gives `SetPasswordMode(2)` and no `SetPassword`, the user is in the group;
  a blank and disabled add is `CreateUser`, `SetPasswordMode`, `SetLocked` in that order and not in the group; a blank change
  for an existing user.
- Real AccountsService (`installer/tests/ad/accountsd-guest.sh`, section 5; run through `installer/tests/ad/machine.sh` on a
  built machine): the units and the polkit grant for the reconcile unit (and its refusal for session 0's account); no `nullok`;
  the greeter stack's group line; `SetPasswordMode(2)` empties the field; whether AccountsService keeps the group itself (a
  note); the reconcile puts the user in the group; a locked blank user leaves the group and whether `SetLocked(false)` is
  refused (a note); a hash takes the user out. **Still to run** (B117).
