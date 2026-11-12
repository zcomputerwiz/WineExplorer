/*
 * The shell's key combinations while a Linux program has the keyboard, shared by both window manager backends.
 * x11wm.c takes them with passive grabs on the X server; waylandwm.c has the compositor take them
 * (wex_window_manager_v1 v15, set_shell_keys). Either way each key comes here and is replayed into Wine as a
 * WEX_WM_KEY event, so Explorer's keyboard hook, its hotkeys and DefWindowProc (Alt+F4) handle the keys as they do
 * for Windows programs (wexshell, linux_windows.c).
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if 0
#pragma makedep unix
#endif
#include "config.h"
#include <string.h>
#include <X11/keysym.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL(wexhost);

/* The combinations, as XP's shell takes them: the Windows key with anything, Ctrl+Esc, Alt+Tab and Alt+Shift+Tab,
 * Alt+F4. keysym values are X11's, which xkb shares. */
const struct shell_key_grab shell_key_grabs[] =
{
    { XK_Super_L, SHELL_MOD_ANY },
    { XK_Super_R, SHELL_MOD_ANY },
    { XK_Escape, SHELL_MOD_CTRL },
    { XK_Tab, SHELL_MOD_ALT },
    { XK_Tab, SHELL_MOD_ALT | SHELL_MOD_SHIFT },
    { XK_F4, SHELL_MOD_ALT },
};
const unsigned int shell_key_grab_count = ARRAY_SIZE(shell_key_grabs);

/* the virtual key of a keysym without modifiers (level 1); 0 for keys a shell combination does not use */
WORD shell_key_vk( unsigned int sym )
{
    if (sym >= XK_a && sym <= XK_z) return 'A' + sym - XK_a;
    if (sym >= XK_0 && sym <= XK_9) return '0' + sym - XK_0;
    if (sym >= XK_F1 && sym <= XK_F24) return VK_F1 + sym - XK_F1;
    switch (sym)
    {
    case XK_Super_L: return VK_LWIN;
    case XK_Super_R: return VK_RWIN;
    case XK_Alt_L: case XK_Meta_L: return VK_LMENU;
    case XK_Alt_R: case XK_Meta_R: case XK_ISO_Level3_Shift: return VK_RMENU;
    case XK_Control_L: return VK_LCONTROL;
    case XK_Control_R: return VK_RCONTROL;
    case XK_Shift_L: return VK_LSHIFT;
    case XK_Shift_R: return VK_RSHIFT;
    case XK_Tab: case XK_ISO_Left_Tab: return VK_TAB;
    case XK_Escape: return VK_ESCAPE;
    case XK_Return: case XK_KP_Enter: return VK_RETURN;
    case XK_space: return VK_SPACE;
    case XK_Left: return VK_LEFT;
    case XK_Right: return VK_RIGHT;
    case XK_Up: return VK_UP;
    case XK_Down: return VK_DOWN;
    case XK_Home: return VK_HOME;
    case XK_End: return VK_END;
    case XK_Pause: case XK_Break: return VK_PAUSE;
    case XK_Delete: return VK_DELETE;
    }
    return 0;
}

static void push_key( const struct shell_key_backend *backend, WORD vk, BOOL up )
{
    WEX_WM_EVENT event;

    TRACE( "replay %02x %s\n", vk, up ? "up" : "down" );
    memset( &event, 0, sizeof(event) );
    event.type = WEX_WM_KEY;
    event.value = vk;
    if (up) event.flags = WEX_WM_KEY_UP;
    backend->push( &event );
}

/* Ends a replay: the modifiers it pressed in Wine come up, and a held keyboard goes back */
void shell_key_end( struct shell_replay *replay, const struct shell_key_backend *backend )
{
    while (replay->held_count) push_key( backend, replay->held[--replay->held_count], TRUE );
    if (replay->alt_tab) backend->hold( FALSE );
    memset( replay, 0, sizeof(*replay) );
}

/* A key the backend took. The first press starts a replay: the modifiers already held are pressed in Wine first,
 * every key until the combination ends follows, and the replay ends with the release of the key that started it
 * (Windows key, Ctrl+Esc, Alt+F4) or with Alt (Alt+Tab, during which the backend holds the keyboard).
 * sym: the keysym without modifiers; key: the backend's identity of the physical key; modifiers: SHELL_MOD_*. */
void shell_key_event( struct shell_replay *replay, const struct shell_key_backend *backend, unsigned int sym,
                      unsigned int key, unsigned int modifiers, BOOL up )
{
    WORD vk = shell_key_vk( sym );

    if (!replay->active)
    {
        if (up) return;
        TRACE( "replay of key %u, keysym %#x, modifiers %#x\n", key, sym, modifiers );
        replay->active = TRUE;
        replay->key = key;
        if (modifiers & SHELL_MOD_CTRL) push_key( backend, replay->held[replay->held_count++] = VK_LCONTROL, FALSE );
        if (modifiers & SHELL_MOD_ALT) push_key( backend, replay->held[replay->held_count++] = VK_LMENU, FALSE );
        if (modifiers & SHELL_MOD_SHIFT) push_key( backend, replay->held[replay->held_count++] = VK_LSHIFT, FALSE );
        /* Alt+Tab lasts until Alt is released: keep the keyboard until then */
        if (sym == XK_Tab && (modifiers & SHELL_MOD_ALT)) replay->alt_tab = backend->hold( TRUE );
    }
    if (vk)
    {
        /* modifiers we pressed for the replay and that are released now need no second release */
        unsigned int i;
        for (i = 0; up && i < replay->held_count; i++)
            if (replay->held[i] == vk) { replay->held[i] = replay->held[--replay->held_count]; break; }
        push_key( backend, vk, up );
    }
    if (up && (replay->alt_tab ? vk == VK_LMENU || vk == VK_RMENU : key == replay->key)) shell_key_end( replay, backend );
}
