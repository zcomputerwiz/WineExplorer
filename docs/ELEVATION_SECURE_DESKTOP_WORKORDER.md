<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: the secure desktop for consent (plan step 5)

Date: 2026-10-24. Status: scoped; phase R (research) first, then design calls, then implementation. Part of
`ELEVATION_BOUNDARY_PLAN.md` (design D5, step 5). Builds on consent (`ELEVATION_CONSENT_WORKORDER.md`): the broker
starts `consent.exe /broker` as a SYSTEM child on `WinSta0\Default`, where any same-user process can post Approve.

## Goal

The consent dialog runs on a separate desktop, `WinSta0\Winlogon`, that only the broker's system role can open.
While it is the input desktop, programs on the user's desktop cannot send it input, messages, hooks or thread
attachments, cannot switch away from it, and the user's real keyboard and mouse reach it. Every exit path returns to
the user's desktop.

## Policy values (default 5 decided by the user, 2026-10-24; the rest by the main session)

Match Windows' defaults and meanings:
- `ConsentPromptBehaviorAdmin`: default 5 when absent (Windows' default). 5 and 2 both mean click consent; Windows'
  5 also auto-elevates its own signed binaries that ask for it (`autoElevate`). That auto-elevation is not
  implemented: it is a known source of UAC bypasses and nothing here needs it. Documented as a deviation. 0
  (elevate without prompting) is refused like an unknown value (fail closed, 21), since it would make the broker a
  silent elevator; 1 and 3 (credentials) wait for the over-the-shoulder work order.
- `PromptOnSecureDesktop`: default 1 when absent. 1 puts consent on the secure desktop; 0 on the user's desktop
  (Windows' meaning). Both values live under `HKLM\Software\Microsoft\Windows\CurrentVersion\Policies\System`,
  which only administrators can write under enforcement.
- Until this step's enforcement lands, the broker keeps today's behavior; the default changes ship together with it.

## Phase R: research (Sonnet agent, report before design)

Read `server/winstation.c`, `server/queue.c` (`get_hardware_input_desktop`, `set_input_desktop`), `server/hook.c`,
`dlls/win32u/winstation.c` (`NtUserSwitchDesktop`), and the X11 and Wayland drivers' desktop handling; ReactOS
winlogon (`base/system/winlogon/security.c`, `wlx.c`) for the Winlogon desktop's descriptor and creation order (cite,
port only GPL-compatible code). Then probe, in this container with Xvfb (X11 driver), with Wine's virtual desktop
(`explorer /desktop=...`), and with the Wayland driver under a headless compositor if one can be installed (for
example `weston --backend=headless`), each of these, from a process on `Default` against a window on a second
desktop that is the input desktop:
- what `SwitchDesktop` does today: does the second desktop get its own host surface (X11 window, Wayland surface),
  is the first one hidden, does input go to the new one;
- whether `SendInput`, `keybd_event`, `mouse_event`, `PostMessage` / `SendMessage` (including `BM_CLICK`,
  `WM_COMMAND`), `SetWindowsHookEx` (thread and global, low-level), `AttachThreadInput`, `SetForegroundWindow`,
  `EnumWindows` / `FindWindow` across desktops, `OpenDesktop("Winlogon")` and `SwitchDesktop` back by a limited
  process all work today;
- how the WineExplorer session runs Wine (full-screen X11 shell, virtual desktop, Wayland) and what that means for
  presentation;
- whether `CreateDesktop` with a security descriptor is honored by `OpenDesktop`/`SwitchDesktop` access checks.
Write a table (operation x driver: works today / blocked) with file:line for where each is or would be checked, into
"Results, phase R". Do not change product code in phase R.

## Phases after the research (scoped once phase R is in)

1. **Desktop.** The broker creates `WinSta0\Winlogon` with a descriptor admitting only SYSTEM (the broker's system
   role), starts `consent.exe /broker` on it, switches to it for the prompt and back on every exit path (approve,
   cancel, timeout, caller exit, consent crash, broker crash: the server returns the input desktop to `Default` when
   the process that switched dies).
2. **Server enforcement**, while `Winlogon` is the input desktop: injected input from another desktop fails as on
   Windows; messages, hooks and thread attachment do not cross into it; only a system-role process can switch the
   input desktop.
3. **Presentation.** The secure desktop covers the user's session full screen, with a dimmed snapshot of the
   previous screen as background (cosmetic, documented as such), per driver as phase R allows.
4. **Policy values** as decided above.
5. **Limits stated in every status line:** native X11/Wayland clients outside Wine (for example `xdotool`, or
   reading keystrokes on X11) and same-UID native code still reach the host surface (T5).

Accept (plan step 5), with a probe on `Default`: `SendInput` and `PostMessage(BM_CLICK)` aimed at the consent
dialog have no effect; hooks and `AttachThreadInput` fail; the user's real keyboard and mouse reach the dialog (an
X11 input injector such as `xdotool` against Xvfb stands in for the real devices, since it acts as the host, not as
a Wine process); every exit path returns to `Default`; plus the usual regression runs.

## How the work is split

Planning and review: the main session. Phase R and each phase: a Sonnet agent. Design calls after phase R go to the
main session, and to the user where they change behavior.

## Results

(none yet)
