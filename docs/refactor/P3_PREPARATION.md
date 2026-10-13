<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Explorer plugin preparation

This is preparation from code inspection, not P3 acceptance. Complete the P2
assembled build and runtime checks before extracting code.

The initial move must preserve a single Explorer process and the existing
shell gate. `manage_desktop()` currently owns Wine's desktop creation, display
setup, clipboard thread and message loop alongside shell startup. Keep those
stock responsibilities in Explorer. Move only the shell initialization,
shutdown, hotkeys, painting and desktop-view handling into the plugin.

The plugin needs a versioned start/command/stop interface, with explicit
success or unhandled results. A missing DLL, incompatible interface or failed
start must take the original stock paths. Command routing must preserve
ordinary folder browsing without the plugin, while the enabled shell routes
its current browser, search and applet commands through the plugin. Inspect
`parse_command_line()` before choosing where to intercept: it currently
performs some actions while parsing, before `wWinMain()` initializes COM.

The enabled plugin can subclass the desktop to own shell-specific messages.
Preserve the original procedure and restore it on stop. The current shell
handles sent versus posted WM_CLOSE differently, forwards display changes to
the original procedure before updating the desktop view, and avoids repainting
the wallpaper over child windows. Those are behavior contracts, not cleanup
opportunities during P3. Keep the stock appbar and tray implementation for the
no-plugin path; move the enhanced shell tray with the shell.

Resource ownership needs a deliberate mechanical adjustment. Browser code
uses `explorer_hInstance` for strings, menus and window classes; other moved
code uses `GetModuleHandleW(NULL)`. A DLL move changes which module contains
those resources. Give the moved code the plugin instance consistently and
preserve its existing internal variable names for this phase. Audit every
module-handle use: resource lookup/class registration and global keyboard
hooks have different requirements. Keep the GUID definitions in exactly one
translation unit (`explorer.c` currently includes initguid.h first).

All shell features remain together in the initial wexshell move. Do not mix
in the P3b subsystem extraction or rename files. Validate the missing-plugin
path independently, then compare the desktop, both Start menu styles, tray,
folder window and Turn Off dialog with the pre-move assembled build. Record
the actual stock-line delta and any necessary interface change rather than
silently expanding the planned hook.
