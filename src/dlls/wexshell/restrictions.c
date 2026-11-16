/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The shell restrictions of Group Policy (docs/GROUP_POLICY.md, "Local policy").
 */

#include <windows.h>

#include "restrictions.h"

#define POLICY_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer"

static BOOL value_set( HKEY root, const WCHAR *name )
{
    /* a REG_DWORD, or the four bytes of a REG_BINARY that old ADM templates write (a longer one is not a flag) */
    DWORD value = 0, size = sizeof(value);

    return !RegGetValueW( root, POLICY_KEY, name, RRF_RT_REG_DWORD | RRF_RT_REG_BINARY, NULL, &value, &size ) &&
           size == sizeof(value) && value;
}

BOOL shell_restricted( const WCHAR *name )
{
    return value_set( HKEY_CURRENT_USER, name ) || value_set( HKEY_LOCAL_MACHINE, name );
}
