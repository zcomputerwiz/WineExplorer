# Safe Mode: an administrator's desktop runs elevated (design)

Owner decision (2026-11-17): in Safe Mode an administrator's whole desktop runs elevated, as on Windows, where
User Account Control does not run in Safe Mode and an administrator's logon token is not split: Explorer has the full
token and everything started from it inherits it. A standard user's session is unchanged, as is every normal start.

Already done (a2d4ed34): session 0 runs in Safe Mode, and the broker approves an administrator's request without a
prompt there (wexelevate `safe_boot()`). Programs that ask for elevation get it silently; programs that do not ask
(regedit, cmd, a control panel started plainly) still run limited. This document is the step after that.

## What "elevated" needs here

An elevated program today (one the broker starts) has two things:

1. the full half of the administrator's token (`TokenElevationTypeFull`, High label), and
2. the system group in its Unix process, so that it may write the machine root (files are `root:wexsys`, group
   writable); `ntdll` keeps the group for its children while their token is Full (`sysrun_must_drop_group`), and
   the server keeps the raised file access only for an elevated token (`sysrun_process_created`).

The broker's programs run in the machine prefix. The Safe Mode desktop must stay in the administrator's own
prefix (their HKCU, profile and desktop), so it cannot simply be started through the broker.

## Design

1. **The boot mode in the server.** A helper in `server/sysrun.c`, `sysrun_safe_boot()`, reads
   `/run/wineexplorer/bootmode` once: a regular file that root owns and others cannot write, in a root-owned
   directory that others cannot write (`O_NOFOLLOW`, checked on the descriptor), first word `safe`, `safenet` or
   `safecmd`. This is the same source of truth as the broker's; `SafeBoot\Option` is not used (an elevated program
   could write it).

2. **The first token of an administrator's session in Safe Mode is the full one.** `server/process.c`, the first
   interactive token: an administrator gets `token_make_filtered` only outside Safe Mode; in Safe Mode the token
   keeps its groups and privileges and becomes `TokenElevationTypeFull` (a new `token_make_full`), its linked token
   the filtered half (as the broker's full token has). A standard user gets the standard token as before.

3. **A launcher mode for the desktop, `safe-shell`.** `wex-sysrun safe-shell WIDTH HEIGHT` starts the machine's
   `explorer.exe /desktop=Default,WIDTHxHEIGHT /shell` through the Wine loader, for the caller's own prefix
   (`<passwd home>/.wine`), with the system group as effective group, in the environment the launcher builds (as
   `wineboot-init`). It refuses unless
   - the boot mode file says `safe` or `safenet` (the same checks as the server's; `safecmd` has no desktop), and
   - the caller is an administrator by the shared rule: the launcher runs `wex-is-admin` for the real user, with
     the system group dropped in the child, and accepts only status 0 (the same rule and parser as the server).
   WIDTH and HEIGHT are numbers in a fixed range; nothing else of the caller's arguments or environment is used.
   The desktop's `WEXMODE` (which Explorer, wexhost and logonui read for the Safe Mode labels) is the verified boot
   mode, set by the launcher.

4. **The server makes that root process the administrator's elevated desktop, not SYSTEM.** Today a
   launcher-started root process whose main image is a system program of the machine root gets the SYSTEM token
   (`sysrun_main_image`). For `explorer.exe`, in Safe Mode, for a server whose user is an administrator, the root
   process instead keeps the session's (full) token and is protected as an elevated process
   (`SYSRUN_ACCOUNT_ELEVATED`), exactly as a process the broker starts. Any other image keeps today's rule. Both
   sides check: the launcher (Safe Mode, administrator) and the server (Safe Mode, administrator, image).

5. **The session script.** `wine-explorer-session.sh` (X11) in `safe` or `safenet`: when `wex-is-admin` says the
   user is an administrator, the desktop starts through `wex-sysrun safe-shell` instead of `wine explorer.exe`;
   otherwise as today. When the launcher refuses, or the desktop ends within 15 seconds, the usual desktop starts, so
   Safe Mode always gives one. `safecmd` keeps the recovery console. Not yet in the Wayland session (its shell runs
   as `explorer.exe /shell` under the compositor; a follow-up).

6. **The gates.** The launcher checks Safe Mode and runs `wex-is-admin` with the system group kept as its effective
   and saved group, so that the helper stays non-dumpable and the caller (who shares its ids) cannot trace it or
   write its memory to change the answer. ntdll is the second gate: a process the launcher started in `safe-shell`
   mode drops the system group for good in its first steps (`sysrun_safe_shell_gate`, before anything of the prefix
   is loaded) unless its token is the full one, which the server gives only to an administrator's programs in Safe
   Mode. The server then gives the elevated desktop only to the machine root's `explorer.exe` with that token, and
   ends any other image. The desktop's threads get the elevated descriptor too.

## Effects and limits

- Everything an administrator starts from the desktop in Safe Mode is elevated and may write the machine root, as
  on Windows. That includes Linux programs started from it (they inherit the group); the same administrator could
  already do all of this without a prompt in Safe Mode. Files the desktop's programs create in the user's own
  profile get the system group as their group (readable by it, as SYSTEM may read a profile on Windows).
- In Safe Mode every parentless program of an administrator has the full token (the server's rule), including ones
  started outside the desktop; without the system group they still cannot write the machine root.
- `TokenElevationType` is Full, where Windows reports Default for an unsplit token; `TokenElevation` is TRUE in
  both, and that is what programs check. The Full type keeps one kind of elevated token in the system tier.
- A standard user's Safe Mode session, every normal start, and machines without enforced elevation do not change.
- Risks to review: the new launcher mode (setgid code), the server's root-process decision, and that no path lets a
  non-administrator, or a normal start, reach either.
- Tests (2026-11-17): `scripts/test_sysrun_server.sh` checks that the launcher refuses `safe-shell` outside Safe Mode
  and with bad arguments. `installer/tests/safe-mode.sh` (an image with session 0 and enforced elevation, BIOS):
  in `safe` and `safenet` the desktop has `WEXMODE` and the system group, and the 32- and 64-bit probes report
  `elevated=1` and the Safe Mode refusal of the spooler (1084); a normal start reports `elevated=0`. Not yet in the
  VM: a standard user's Safe Mode session, and a write to `C:\windows` from a program of the desktop.
