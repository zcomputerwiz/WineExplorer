<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Codex): program elevation with a Vista-style prompt (UAC)

Status: ready, 2026-10-17. Assignee: Codex, after `WAYLAND_POLISH_WORKORDER.md`.

## Goal

A downloaded Windows program can change the shared Windows installation (the machine root:
`/var/lib/wineexplorer/machine`, its HKLM, Program Files and system DLLs) only after the user approved it in a clear
prompt: "Do you want to allow this program to make changes to the computer?", with the program's name, file and
publisher.
- Without approval it cannot write there. That includes programs run by an administrator.
- The user's own prefix stays theirs, as on Windows.
- Linux itself is already out of reach (Wine cannot write `/usr`, `/etc`).

Design so that the later system tier (session 0, `ROADMAP.md`) replaces only the back end (part E). The hooks, the
broker interface and the prompt stay.

## Current state

- Wine's tokens:
  - Wine already has elevation types (`server/token.c`).
  - Programs start with a limited token.
  - `requireAdministrator`/`highestAvailable` manifests (`ntdll/actctx.c`) and the `runas` verb
    (`shell32/shlexec.c`) get a full token **silently**.
  - Every process of a Linux user shares that user's wineserver and its file rights, so the token protects nothing.
- The machine root is `root:sudo` with mode 2775 (prefix split, `PREFIX_SPLIT_WORKORDER.md`):
  - administrators' own Wine processes write it directly, with no prompt possible;
  - limited users get "access denied" (tested on the ISO);
  - administrators' HKLM writes reach the shared `system.reg` through the merge-on-save in ntdll (`save_branch`,
    `flock`, `mod_seq`); limited users' HKLM writes stay in memory.

## Parts

**A. Ownership: admin approval mode.**
- The machine root moves to a system account `wexsys` (group `wexsys`, no login, no members), mode 2755, so all users
  can read it and nobody's normal session can write it.
- Jules adds `usr/lib/sysusers.d/wineexplorer.conf` (check its PR).
- `scripts/setup-machine-root.sh` migrates existing installs: ownership, modes, the default ACLs that now grant
  `g:sudo:rwX` become read-only for users, and the timestamp files.
- Every path that wrote the machine root as an administrator now goes through elevation or runs as `wexsys`:
  - Wine updates (`wineboot -u` of the machine part);
  - theme and font setup;
  - the merge-on-save of HKLM, which runs inside the elevated process (`wexsys`);
  - the existing "Alice installs for all users" test, which becomes an elevated install.
- List each one in the report.

**B. Hooks in Wine (small stock changes, list them in `STOCK_CHANGES.md`).**
- Where Wine now elevates silently, it asks a configured helper instead:
  - `ShellExecuteEx` with `runas`;
  - `CreateProcess` of an image whose manifest requires elevation from a limited token. Return
    `ERROR_ELEVATION_REQUIRED` as Windows does, and do the `ShellExecute` fallback that Windows' shell does.
- The helper is named by `HKLM\Software\Wine\Elevation\Helper`, our `consent.exe`. With no value set, keep stock
  behaviour.
- The change is generic, so it can go upstream as "configurable elevation helper". No WineExplorer names in Wine.

**C. Broker (wexhost, D-Bus).**
- `consent.exe` asks wexhost through one D-Bus method, for example
  `org.wineexplorer.Elevation.Elevate(command line, working directory, show state) -> process handle or error`.
- This interface is the seam that session 0 will reuse unchanged; document it in `docs/ELEVATION.md`.

**D. The prompt.**
- A Vista-style consent dialog (our own Win32 dialog, open art).
- The shield icon comes from Fluent `shield` glyphs, which are allowed; ask Gemini for art if you want XP or Vista
  styling.
- It shows the program name and file, the publisher from the Authenticode signature if present (otherwise
  "Unknown publisher" with the yellow warning style), and "Show details" with the command line.
- The screen behind it is dimmed.
- Two modes, set by an administrator:
  - **Consent** (default for administrators): Yes / No.
  - **Credentials** (always for limited users; optional for administrators): an administrator's name and password.
- **Authentication goes through polkit.** Jules adds the action `org.wineexplorer.elevate`
  (`usr/share/polkit-1/actions/org.wineexplorer.elevate.policy`, `auth_admin`, exec path
  `/usr/local/lib/wineexplorer/wex-elevate-run`).
  - wexhost registers itself as the session's polkit authentication agent
    (`org.freedesktop.PolicyKit1.AuthenticationAgent`) and shows our dialog for it, so there is one prompt, ours,
    with the password checked by polkit and PAM, never by Wine.
  - Consent mode for administrators is a polkit rule (`rules.d`) that allows `sudo` members without a password. In
    that case our dialog is the only confirmation.
- Be honest in `docs/ELEVATION.md`: a program running as the user can in principle fake a click on a consent-only
  prompt, and Windows does not treat UAC as a security boundary either. Credentials mode is the hard boundary.

**E. Back end, v1 (replaced by session 0 later).**
- `wex-elevate-run` (run by `pkexec` as `wexsys`) starts the approved program in `wexsys`'s own prefix, linked to the
  machine root like any user's, with its own wineserver.
- It gets access to the user's display:
  - X11: an `xauth` cookie for the elevated process;
  - Wayland: an ACL on the user's socket, or the compositor's private socket.
- It cannot share Win32 objects with the user's desktop (window messages, clipboard through Wine). That is fine for
  installers; say so in the docs.
- Programs it installs land in the machine root for all users. Start menu shortcuts go to All Users.

**F. Shell polish.**
- The shield overlay on icons of programs that need elevation (manifest or installer heuristics), as on Vista.
- The setting in Control Panel (User Accounts) with the two modes, for administrators only.
- XP's "Run as…" stays as it is.

## Acceptance (report in `docs/ELEVATION.md`, with raw logs)

On a fresh ISO install, in X11 and Wayland, with a 32- and a 64-bit installer (small freeware installers; never in the
ISO or repository):
- an installer that requires administrator rights shows the prompt; on "Yes" it installs into Program Files of the
  machine root, and both users see it;
- on "No" nothing in the machine root changes (checksum before and after);
- a limited user must enter an administrator's password; a wrong password changes nothing;
- a plain program that writes to `C:\Windows` without asking gets "access denied", also for an administrator;
- Wine update, theme and font setup still work after the ownership change;
- the 32/64-bit program checks and the multi-user checks of the install tests pass.

## Boundaries

- Gemini works on fonts and runtimes (`FONTS_RUNTIMES_WORKORDER.md`) and the open Luna. They also write to the
  machine root through `setup-machine-root.sh`, so coordinate on that file: their changes land first, and part A
  adapts them.
- Push verified steps to master, building master at its pinned Wine before every push. Use
  `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never` with timeouts. Never force-push.
- `CLAUDE.md` rules apply: WoW64 builds; GPL headers on new files; no C escapes through heredocs, `sed` or
  `python -c`; stock Wine changes small and listed.
