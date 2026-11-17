/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The shell restrictions of Group Policy (docs/GROUP_POLICY.md, "Local policy").
 */

#include <windows.h>
#include <userenv.h>

#include "restrictions.h"
#include "wexshellpolicy.h"

BOOL shell_restricted( const WCHAR *name )
{
    return wex_shell_restricted( name );
}

static struct
{
    HWND window;
    UINT message;
    HANDLE event[2];   /* the computer's policy, the user's */
    HANDLE wait[2];
} watch;

static void CALLBACK policy_applied( void *context, BOOLEAN timed_out )
{
    PostMessageW( watch.window, watch.message, 0, 0 );
}

/* userenv's notification (RegisterGPNotification, the way Windows programs learn that a refresh has applied policy):
 * the writer, in session 0, leaves a serial in the registry after every apply and userenv waits for a change of it,
 * which reaches every session (docs/GROUP_POLICY.md, "Step 3: refresh signalling"). No timer, no polling. */
BOOL shell_restrictions_watch( HWND window, UINT message )
{
    int i;

    if (watch.window) return TRUE;
    /* userenv watches HKCU\Software\Policies for the user's serial; a user who has had no policy yet has no such key,
     * and the first refresh would then go unnoticed */
    {
        HKEY policies;

        if (!RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Policies", 0, NULL, 0, KEY_NOTIFY, NULL, &policies, NULL ))
            RegCloseKey( policies );
    }
    watch.window = window;
    watch.message = message;
    for (i = 0; i < 2; i++)
    {
        if (!(watch.event[i] = CreateEventW( NULL, FALSE, FALSE, NULL ))) return FALSE;
        if (!RegisterGPNotification( watch.event[i], i == 0 )) return FALSE;
        if (!RegisterWaitForSingleObject( &watch.wait[i], watch.event[i], policy_applied, NULL, INFINITE,
                                          WT_EXECUTEDEFAULT ))
            return FALSE;
    }
    return TRUE;
}
