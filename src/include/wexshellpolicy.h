/*
 * The restrictions of Group Policy that the shell programs read: the values under
 * Software\Microsoft\Windows\CurrentVersion\Policies\Explorer, in the user's hive and in the machine's
 * (docs/GROUP_POLICY.md, "Shell restrictions")
 *
 * Copyright 2026 WineExplorer contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Read at the moment of use, not cached: a policy refresh changes what applies while the shell runs. (shell32's
 * SHRestricted caches a value for the life of the process and reads only the user's hive.) Header only.
 */

#ifndef __WEXSHELLPOLICY_H
#define __WEXSHELLPOLICY_H

#include <windows.h>

#define WEX_EXPLORER_POLICY_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer"

static inline BOOL wex_shell_policy_value( HKEY root, const WCHAR *name )
{
    /* a REG_DWORD, or the four bytes of a REG_BINARY that old ADM templates write (a longer one is not a flag) */
    DWORD value = 0, size = sizeof(value);

    return !RegGetValueW( root, WEX_EXPLORER_POLICY_KEY, name, RRF_RT_REG_DWORD | RRF_RT_REG_BINARY, NULL, &value,
                          &size ) && size == sizeof(value) && value;
}

/* TRUE when the value NAME of Policies\Explorer is not 0 for the user or for the machine */
static inline BOOL wex_shell_restricted( const WCHAR *name )
{
    return wex_shell_policy_value( HKEY_CURRENT_USER, name ) || wex_shell_policy_value( HKEY_LOCAL_MACHINE, name );
}

#endif
