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
#include <winnls.h>
#include <winuser.h>
#include <winternl.h>

#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL( wexhost );

static BOOL unix_ready;
static HANDLE agent_thread;
static WEX_AGENT_CALLBACK agent_callback;
static void *agent_context;

DWORD WINAPI WexAppsList( char *buffer, DWORD size, DWORD *needed )
{
    struct apps_list_params params = {0};
    if (needed) *needed = 0;
    if (!buffer && size) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.buffer = (ULONG_PTR)buffer;
    params.size = size;
    WEXHOST_CALL( apps_list, &params );
    if (needed) *needed = params.needed;
    return params.err;
}

DWORD WINAPI WexHardwareList( char *buffer, DWORD size, DWORD *needed )
{
    struct hw_list_params params = {0};
    if (needed) *needed = 0;
    if (!buffer && size) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.buffer = (ULONG_PTR)buffer;
    params.size = size;
    WEXHOST_CALL( hw_list, &params );
    if (needed) *needed = params.needed;
    return params.err;
}

DWORD WINAPI WexAppsLaunch( const WCHAR *id )
{
    struct apps_launch_params params = {0};
    int size;
    if (!id || !*id) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    size = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, id, -1, params.id, sizeof(params.id), NULL, NULL );
    if (!size) return GetLastError();
    WEXHOST_CALL( apps_launch, &params );
    return params.err;
}

DWORD WINAPI WexDisplayGet(WEX_DISPLAY *display)
{
    struct display_get_params params;
    if(!display) return ERROR_INVALID_PARAMETER;
    memset(display,0,sizeof(*display));
    if(!unix_ready) return ERROR_NOT_SUPPORTED;
    memset(&params,0,sizeof(params));
    WEXHOST_CALL(display_get,&params);
    if(!params.err) *display=params.display;
    return params.err;
}

DWORD WINAPI WexDisplaySet(const WEX_DISPLAY_MODE *mode, BOOL persist)
{
    struct display_set_params params;
    if(!unix_ready) return ERROR_NOT_SUPPORTED;
    if(!mode) return ERROR_INVALID_PARAMETER;
    params.mode=*mode; params.persist=persist;
    WEXHOST_CALL(display_set,&params);
    return params.err;
}

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

DWORD WINAPI WexPowerCan( DWORD action, DWORD *result )
{
    struct power_can_params params = { action };

    if (!unix_ready || !result) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( power_can, &params );
    *result = params.result;
    return params.err;
}

DWORD WINAPI WexPowerDo( HWND owner, DWORD action )
{
    struct power_do_params params = { action };

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    run_pumped( owner, unix_power_do, &params );
    return params.err;
}

DWORD WINAPI WexBatteryGet( WEX_BATTERY *info )
{
    struct battery_get_params params;

    if (!unix_ready || !info) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( battery_get, &params );
    if (!params.err) *info = params.info;
    return params.err;
}

DWORD WINAPI WexHostnameGet( WEX_HOSTNAME *info )
{
    struct hostname_get_params params;

    if (!unix_ready || !info) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( hostname_get, &params );
    if (!params.err) *info = params.info;
    return params.err;
}

DWORD WINAPI WexHostnameSet( HWND owner, const char *name, BOOL pretty )
{
    struct hostname_set_params params;

    if (!unix_ready || !name) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    lstrcpynA( params.name, name, sizeof(params.name) );
    params.pretty = pretty;
    run_pumped( owner, unix_hostname_set, &params );
    return params.err;
}

DWORD WINAPI WexUserGet( WEX_USER *info )
{
    struct user_get_params params;

    if (!unix_ready || !info) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( user_get, &params );
    if (!params.err) *info = params.info;
    return params.err;
}

DWORD WINAPI WexUsersList( char *buffer, DWORD size, DWORD *needed )
{
    struct users_list_params params = {0};
    if (needed) *needed = 0;
    if (!buffer && size) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.buffer = (ULONG_PTR)buffer;
    params.size = size;
    WEXHOST_CALL( users_list, &params );
    if (needed) *needed = params.needed;
    return params.err;
}

static DWORD greeter_call( unsigned int func, const char *text, DWORD *value )
{
    struct greeter_params params = {0};
    NTSTATUS status;

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    if (text && strlen( text ) >= sizeof(params.text)) return ERROR_INVALID_PARAMETER;
    if (text) strcpy( params.text, text );
    status = WINE_UNIX_CALL( func, &params );
    SecureZeroMemory( params.text, sizeof(params.text) );
    if (status) return ERROR_NOT_SUPPORTED;
    if (value) *value = params.value;
    return params.err;
}

DWORD WINAPI WexGreeterConnect( void ) { return greeter_call( unix_greeter_connect, NULL, NULL ); }
DWORD WINAPI WexGreeterAuthenticate( const char *user, DWORD *sequence ) { return greeter_call( unix_greeter_authenticate, user, sequence ); }
DWORD WINAPI WexGreeterRespond( const char *secret ) { return greeter_call( unix_greeter_respond, secret ? secret : "", NULL ); }
DWORD WINAPI WexGreeterCancel( void ) { return greeter_call( unix_greeter_cancel, NULL, NULL ); }
DWORD WINAPI WexGreeterStartSession( const char *session ) { return greeter_call( unix_greeter_start_session, session, NULL ); }

DWORD WINAPI WexGreeterRead( char *buffer, DWORD size, DWORD timeout )
{
    struct greeter_read_params params = {0};
    if (!buffer || !size) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.buffer = (ULONG_PTR)buffer;
    params.size = size;
    params.timeout = timeout;
    WEXHOST_CALL( greeter_read, &params );
    return params.err;
}

DWORD WINAPI WexNetGetStatus( WEX_NET_STATUS *status )
{
    struct net_get_status_params params;

    if (!unix_ready || !status) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( net_get_status, &params );
    if (!params.err) *status = params.status;
    return params.err;
}

DWORD WINAPI WexNetScanWifi( WEX_WIFI_AP *aps, DWORD max, DWORD *count )
{
    struct net_scan_wifi_params params;

    if (!unix_ready || !aps || !count) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    params.buffer = (ULONG_PTR)aps;
    params.max = max;
    WEXHOST_CALL( net_scan_wifi, &params );
    *count = params.count;
    return params.err;
}

DWORD WINAPI WexNetConnectWifi( const char *ssid, const char *passphrase, BOOL sae )
{
    struct net_connect_wifi_params params;
    DWORD ret;

    if (!unix_ready || !ssid) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    lstrcpynA( params.ssid, ssid, sizeof(params.ssid) );
    if (passphrase) lstrcpynA( params.passphrase, passphrase, sizeof(params.passphrase) );
    params.sae = sae;
    WEXHOST_CALL( net_connect_wifi, &params );
    ret = params.err;
    SecureZeroMemory( &params, sizeof(params) );
    return ret;
}

DWORD WINAPI WexNetDisconnect( void )
{
    struct net_disconnect_params params;

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( net_disconnect, &params );
    return params.err;
}

DWORD WINAPI WexNetEnable( void )
{
    struct net_enable_params params;

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( net_enable, &params );
    return params.err;
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

static HANDLE wm_thread;
static WEX_WM_CALLBACK wm_callback;
static void *wm_context;

static DWORD WINAPI wm_event_thread( void *arg )
{
    for (;;)
    {
        struct wm_next_event_params params;

        WEXHOST_CALL( wm_next_event, &params );
        if (params.event.type == WEX_WM_STOP) break;
        wm_callback( &params.event, wm_context );
    }
    return 0;
}

DWORD WINAPI WexWmStart( UINT64 desktop_window, WEX_WM_CALLBACK callback, void *context )
{
    struct wm_start_params params = { desktop_window };

    if (!unix_ready || !callback) return ERROR_NOT_SUPPORTED;
    if (wm_thread) return ERROR_ALREADY_EXISTS;
    wm_callback = callback;
    wm_context = context;
    WEXHOST_CALL( wm_start, &params );
    if (params.err) return params.err;
    wm_thread = CreateThread( NULL, 0, wm_event_thread, NULL, 0, NULL );
    return wm_thread ? ERROR_SUCCESS : ERROR_GEN_FAILURE;
}

DWORD WINAPI WexWmManage( UINT64 window, UINT64 parent, const RECT *client, const RECT *screen )
{
    struct wm_manage_params params;

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    params.window = window;
    params.parent = parent;
    if (client) params.client = *client;
    if (screen) params.screen = *screen;
    WEXHOST_CALL( wm_manage, &params );
    return params.err;
}

static DWORD wm_window_call( unsigned int code, UINT64 window, const RECT *client, const RECT *screen, DWORD value )
{
    struct wm_window_params params;

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    params.window = window;
    if (client) params.client = *client;
    if (screen) params.screen = *screen;
    params.value = value;
    __wine_unix_call( __wine_unixlib_handle, code, &params );
    return params.err;
}

DWORD WINAPI WexWmConfigure( UINT64 window, const RECT *client, const RECT *screen )
{
    return wm_window_call( unix_wm_configure, window, client, screen, 0 );
}

DWORD WINAPI WexWmSetState( UINT64 window, DWORD states )
{
    return wm_window_call( unix_wm_set_state, window, NULL, NULL, states );
}

DWORD WINAPI WexWmClose( UINT64 window )
{
    return wm_window_call( unix_wm_close, window, NULL, NULL, 0 );
}

DWORD WINAPI WexWmFocus( UINT64 window )
{
    return wm_window_call( unix_wm_focus, window, NULL, NULL, 0 );
}

DWORD WINAPI WexWmRelease( UINT64 window, const RECT *screen )
{
    return wm_window_call( unix_wm_release, window, NULL, screen, 0 );
}

DWORD WINAPI WexWmGetTitle( UINT64 window, WCHAR *title, DWORD count )
{
    struct wm_get_title_params params;

    if (!title || !count) return ERROR_INVALID_PARAMETER;
    title[0] = 0;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.window = window;
    WEXHOST_CALL( wm_get_title, &params );
    if (!params.err && !MultiByteToWideChar( CP_UTF8, 0, params.title, -1, title, count ))
    {
        title[count - 1] = 0;
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) title[0] = 0;
    }
    return params.err;
}

DWORD WINAPI WexWmGetIcon( UINT64 window, DWORD size, DWORD *pixels, DWORD max, DWORD *width, DWORD *height )
{
    struct wm_get_icon_params params;

    if (!pixels || !width || !height) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    memset( &params, 0, sizeof(params) );
    params.window = window;
    params.size = size;
    params.pixels = (ULONG_PTR)pixels;
    params.max = max;
    WEXHOST_CALL( wm_get_icon, &params );
    *width = params.width;
    *height = params.height;
    return params.err;
}

void WINAPI WexWmStop( void )
{
    struct wm_stop_params params;

    MSG msg;

    if (!unix_ready || !wm_thread) return;
    WEXHOST_CALL( wm_stop, &params );
    /* the event thread may be sending its last events to this thread */
    while (MsgWaitForMultipleObjects( 1, &wm_thread, FALSE, 5000, QS_SENDMESSAGE ) == WAIT_OBJECT_0 + 1)
        PeekMessageW( &msg, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE );
    CloseHandle( wm_thread );
    wm_thread = NULL;
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
