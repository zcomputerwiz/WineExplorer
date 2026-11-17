/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Shared PE dispatcher for NetworkManager and UPower state invalidations.
 */
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include "wexhost_unix.h"

static SRWLOCK lifetime = SRWLOCK_INIT, listeners_lock = SRWLOCK_INIT;
static struct listener { HWND window; UINT message; DWORD changes, generation; } listeners[16];
static HANDLE thread;

static DWORD WINAPI watch_thread( void *unused )
{
    struct host_watch_params params;
    unsigned int i;

    for (;;)
    {
        if (WEXHOST_CALL( host_watch_next, &params )) params.err = ERROR_NOT_SUPPORTED;
        if (params.err == ERROR_CANCELLED) break;
        AcquireSRWLockExclusive( &listeners_lock );
        for (i = 0; i < ARRAY_SIZE(listeners); i++)
            if (listeners[i].window && (params.err || (params.changes & listeners[i].changes)))
                PostMessageW( listeners[i].window, listeners[i].message,
                              ++listeners[i].generation, params.err );
        ReleaseSRWLockExclusive( &listeners_lock );
        if (params.err) break;
    }
    return 0;
}

DWORD WINAPI WexHostSubscribe( HWND window, UINT message, DWORD changes )
{
    struct init_params params = {0};
    unsigned int i;
    DWORD err = ERROR_SUCCESS;

    if (!window || message < WM_APP || !changes ||
        (changes & ~(WEX_HOST_CHANGE_NETWORK | WEX_HOST_CHANGE_POWER))) return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive( &lifetime );
    AcquireSRWLockExclusive( &listeners_lock );
    for (i = 0; i < ARRAY_SIZE(listeners); i++)
        if (listeners[i].window == window) { err = ERROR_ALREADY_EXISTS; goto done; }
    for (i = 0; i < ARRAY_SIZE(listeners); i++) if (!listeners[i].window) break;
    if (i == ARRAY_SIZE(listeners)) { err = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    if (!thread)
    {
        if (__wine_init_unix_call()) { err = ERROR_NOT_SUPPORTED; goto done; }
        if (WEXHOST_CALL( host_watch_start, &params )) { err = ERROR_NOT_SUPPORTED; goto done; }
        if ((err = params.err)) goto done;
        thread = CreateThread( NULL, 0, watch_thread, NULL, 0, NULL );
        if (!thread)
        {
            err = GetLastError();
            WEXHOST_CALL( host_watch_close, &params );
            goto done;
        }
    }
    /* Holding listeners_lock until registration means even a signal received
     * while the initial subscription is being set up reaches this listener. */
    listeners[i].window = window;
    listeners[i].message = message;
    listeners[i].changes = changes;
    listeners[i].generation = 0;
done:
    ReleaseSRWLockExclusive( &listeners_lock );
    ReleaseSRWLockExclusive( &lifetime );
    return err;
}

void WINAPI WexHostUnsubscribe( HWND window )
{
    struct init_params params = {0};
    unsigned int i, count = 0;

    AcquireSRWLockExclusive( &lifetime );
    AcquireSRWLockExclusive( &listeners_lock );
    for (i = 0; i < ARRAY_SIZE(listeners); i++)
    {
        if (listeners[i].window == window) listeners[i].window = NULL;
        if (listeners[i].window) count++;
    }
    ReleaseSRWLockExclusive( &listeners_lock );
    if (!count && thread)
    {
        WEXHOST_CALL( host_watch_stop, &params );
        WaitForSingleObject( thread, INFINITE );
        CloseHandle( thread );
        thread = NULL;
        WEXHOST_CALL( host_watch_close, &params );
    }
    ReleaseSRWLockExclusive( &lifetime );
}
