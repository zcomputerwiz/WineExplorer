/*
 * WineExplorer Desktop Environment - animated characters (Microsoft Agent .acs files)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_EXPLORER_AGENT_CHAR_H
#define __WINE_EXPLORER_AGENT_CHAR_H

#include <windows.h>

struct agent_char;

/* Load a character file (.acs); NULL if it is missing or not a character */
struct agent_char *agent_open(const WCHAR *path);
void agent_close(struct agent_char *agent);
SIZE agent_size(const struct agent_char *agent);

/* Name, description and greeting ("I love to play fetch ...") in the user's language; FALSE if the file has none */
BOOL agent_names(const struct agent_char *agent, WCHAR *name, WCHAR *description, WCHAR *greeting, unsigned int len);

/* Play an animation ("Searching") or one of the animations of a state ("IdlingLevel1"), after the current one has
 * found its way back to rest (exit branches or return animation).  Once nothing is left to play, the character
 * idles.  FALSE if the character has no such animation. */
BOOL agent_play(struct agent_char *agent, const WCHAR *name);
BOOL agent_play_state(struct agent_char *agent, const WCHAR *state);

/* Move the animation on to the current time; returns how many milliseconds until the picture changes again
 * (INFINITE while it stands still) and sets *changed when it changed now */
DWORD agent_advance(struct agent_char *agent, BOOL *changed);

/* Draw the current picture with its transparency */
void agent_draw(struct agent_char *agent, HDC hdc, int x, int y);

/* TRUE if the point (relative to the character) is on the character, not on a transparent pixel */
BOOL agent_hit(struct agent_char *agent, int x, int y);

#endif
