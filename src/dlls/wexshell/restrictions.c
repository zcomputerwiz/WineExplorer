/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The shell restrictions of Group Policy (docs/GROUP_POLICY.md, "Local policy").
 */

#include <windows.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

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

/* which of the restrictions the shell carries out are set, one bit each: to see that a policy refresh changed one */
DWORD shell_restrictions_mask(void)
{
    static const WCHAR *names[] =
    {
        L"NoRun", L"NoClose", L"NoLogoff", L"NoControlPanel", L"NoSetFolders", L"NoSetTaskbar", L"NoFind", L"NoSMHelp",
        L"NoSMMyDocs", L"NoRecentDocsMenu", L"NoChangeStartMenu", L"NoWinKeys", L"NoNetHood", L"NoTrayItemsDisplay",
    };
    DWORD mask = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(names); i++) if (shell_restricted( names[i] )) mask |= 1u << i;
    return mask;
}
