/*
 * WineExplorer host services - PE side
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

#include <stdarg.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winternl.h>

#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL( wexhost );

static BOOL unix_ready;
static HANDLE agent_thread;
static WEX_AGENT_CALLBACK agent_callback;
static void *agent_context;

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( inst );
        unix_ready = !__wine_init_unix_call();
    }
    return TRUE;
}

struct pumped_call
{
    unsigned int code;
    void *params;
};

static DWORD WINAPI pumped_thread( void *arg )
{
    struct pumped_call *call = arg;
    return __wine_unix_call( __wine_unixlib_handle, call->code, call->params );
}

/* Run a unix call that may block on polkit in a helper thread, keeping the owner's UI alive but inert. */
static void run_pumped( HWND owner, unsigned int code, void *params )
{
    struct pumped_call call = { code, params };
    HANDLE thread = CreateThread( NULL, 0, pumped_thread, &call, 0, NULL );
    MSG msg;

    if (!thread) { __wine_unix_call( __wine_unixlib_handle, code, params ); return; }
    if (owner) EnableWindow( owner, FALSE );
    for (;;)
    {
        DWORD r = MsgWaitForMultipleObjects( 1, &thread, FALSE, INFINITE, QS_PAINT | QS_TIMER | QS_POSTMESSAGE | QS_SENDMESSAGE );
        if (r == WAIT_OBJECT_0) break;
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
    }
    if (owner) { EnableWindow( owner, TRUE ); SetForegroundWindow( owner ); }
    CloseHandle( thread );
}

DWORD WINAPI WexTimedateGet( WEX_TIMEDATE *info )
{
    struct timedate_get_params params;

    if (!unix_ready || !info) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( timedate_get, &params );
    if (!params.err) *info = params.info;
    return params.err;
}

DWORD WINAPI WexTimedateSetTime( HWND owner, INT64 unix_usec_utc, BOOL relative )
{
    struct timedate_set_time_params params = { unix_usec_utc, relative };

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    run_pumped( owner, unix_timedate_set_time, &params );
    return params.err;
}

DWORD WINAPI WexTimedateSetTimezone( HWND owner, const char *iana_name )
{
    struct timedate_set_tz_params params;

    if (!unix_ready || !iana_name) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    lstrcpynA( params.timezone, iana_name, sizeof(params.timezone) );
    run_pumped( owner, unix_timedate_set_tz, &params );
    return params.err;
}

DWORD WINAPI WexTimedateSetNtp( HWND owner, BOOL enable )
{
    struct timedate_set_ntp_params params = { enable };

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    run_pumped( owner, unix_timedate_set_ntp, &params );
    return params.err;
}

DWORD WINAPI WexTimedateListTimezones( char *buffer, DWORD size, DWORD *needed )
{
    struct timedate_list_tz_params params;

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    params.buffer = (ULONG_PTR)buffer;
    params.size = size;
    WEXHOST_CALL( timedate_list_tz, &params );
    if (needed) *needed = params.needed;
    return params.err;
}

static DWORD WINAPI agent_event_thread( void *arg )
{
    for (;;)
    {
        struct agent_next_event_params params;

        WEXHOST_CALL( agent_next_event, &params );
        if (params.event.type == WEX_EVENT_STOP) break;
        if (agent_callback) agent_callback( &params.event, agent_context );
    }
    return 0;
}

DWORD WINAPI WexAgentStart( WEX_AGENT_CALLBACK callback, void *context )
{
    struct agent_start_params params;

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    if (agent_thread) return ERROR_ALREADY_EXISTS;
    agent_callback = callback;
    agent_context = context;
    WEXHOST_CALL( agent_start, &params );
    if (params.err) return params.err;
    agent_thread = CreateThread( NULL, 0, agent_event_thread, NULL, 0, NULL );
    return agent_thread ? ERROR_SUCCESS : ERROR_GEN_FAILURE;
}

DWORD WINAPI WexAgentRespond( const char *cookie, const char *user, const char *password, BOOL cancel )
{
    struct agent_respond_params params;
    DWORD ret;

    if (!unix_ready || !cookie) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    lstrcpynA( params.cookie, cookie, sizeof(params.cookie) );
    if (user) lstrcpynA( params.user, user, sizeof(params.user) );
    if (password) lstrcpynA( params.password, password, sizeof(params.password) );
    params.cancel = cancel;
    WEXHOST_CALL( agent_respond, &params );
    ret = params.err;
    SecureZeroMemory( &params, sizeof(params) );
    return ret;
}

void WINAPI WexAgentStop( void )
{
    struct agent_stop_params params;

    if (!unix_ready || !agent_thread) return;
    WEXHOST_CALL( agent_stop, &params );
    WaitForSingleObject( agent_thread, 5000 );
    CloseHandle( agent_thread );
    agent_thread = NULL;
}
