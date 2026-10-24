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
#include <stdlib.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winuser.h>
#include <winternl.h>

#include "wine/debug.h"
#include "wexhost_unix.h"
#include "waylandhost.h"

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

static HANDLE tray_thread;
static WEX_TRAY_CALLBACK tray_callback;
static void *tray_context;
static LONG tray_running;

static DWORD WINAPI tray_events(void *unused)
{
    struct tray_params *params = calloc(1, sizeof(*params));
    if (!params) return ERROR_NOT_ENOUGH_MEMORY;
    while (InterlockedCompareExchange(&tray_running, 1, 1))
    {
        WEXHOST_CALL(tray_next, params);
        if (params->err) break;
        if (params->item.event) tray_callback(&params->item, tray_context);
    }
    free(params);
    return 0;
}

DWORD WINAPI WexTrayStart(WEX_TRAY_CALLBACK callback, void *context)
{
    struct init_params params = {0};
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    if (!callback) return ERROR_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&tray_running, 1, 0)) return ERROR_ALREADY_EXISTS;
    WEXHOST_CALL(tray_start, &params);
    if (params.err) { InterlockedExchange(&tray_running, 0); return params.err; }
    tray_callback = callback;
    tray_context = context;
    tray_thread = CreateThread(NULL, 0, tray_events, NULL, 0, NULL);
    if (!tray_thread)
    {
        DWORD err = GetLastError();
        InterlockedExchange(&tray_running, 0);
        WEXHOST_CALL(tray_stop, &params);
        return err;
    }
    return 0;
}

DWORD WINAPI WexTrayStop(void)
{
    struct init_params params = {0};
    if (!InterlockedExchange(&tray_running, 0)) return 0;
    WaitForSingleObject(tray_thread, INFINITE);
    CloseHandle(tray_thread);
    tray_thread = NULL;
    WEXHOST_CALL(tray_stop, &params);
    return params.err;
}

DWORD WINAPI WexTrayAction(DWORD token, DWORD action, INT x, INT y, INT value)
{
    struct tray_action_params params = {token, action, x, y, value};
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    run_pumped(NULL, unix_tray_action, &params);
    return params.err;
}

DWORD WINAPI WexTrayGetMenu(DWORD token, INT root, WEX_TRAY_MENU *menu)
{
    struct tray_menu_params *params;
    DWORD err;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    if (!menu) return ERROR_INVALID_PARAMETER;
    if (!(params = calloc(1, sizeof(*params)))) return ERROR_NOT_ENOUGH_MEMORY;
    params->token = token;
    params->root = root;
    run_pumped(NULL, unix_tray_menu, params);
    *menu = params->menu;
    err = params->err;
    free(params);
    return err;
}

/* One Wine thread per lease sits in the Unix monitor (it cannot be a pthread there: it must set the event and a
 * foreign thread cannot call the server) and sets the event when the lease broke. */
struct lease_watch { UINT64 lease; HANDLE event, thread; };
static struct lease_watch *lease_watches[64];

static DWORD WINAPI lease_watch_thread( void *arg )
{
    struct lease_watch *w = arg;
    struct lease_monitor_params params = { w->lease };

    WEXHOST_CALL( lease_monitor, &params );
    if (params.broken) SetEvent( w->event );
    return 0;
}

DWORD WINAPI WexLeaseTake( HANDLE file, HANDLE event, UINT64 *lease )
{
    struct lease_take_params params = { (ULONG_PTR)file };
    struct lease_watch *w;

    if (!lease || !event) return ERROR_INVALID_PARAMETER;
    *lease = 0;
    if (!unix_ready) return ERROR_FILE_SYSTEM_LIMITATION;
    WEXHOST_CALL( lease_take, &params );
    if (params.err || !params.lease) return params.err;
    if (!(w = calloc( 1, sizeof(*w) ))) goto fail;
    w->lease = params.lease;
    w->event = event;
    if (!(w->thread = CreateThread( NULL, 0, lease_watch_thread, w, 0, NULL ))) { free( w ); goto fail; }
    lease_watches[params.lease - 1] = w;
    *lease = params.lease;
    return ERROR_SUCCESS;

fail:
    {
        struct lease_release_params rel = { params.lease };
        WEXHOST_CALL( lease_stop, &rel );
        WEXHOST_CALL( lease_release, &rel );
    }
    return ERROR_NOT_ENOUGH_MEMORY;
}

DWORD WINAPI WexLeaseArm( UINT64 lease, DWORD pid )
{
    struct lease_arm_params params = { lease, pid };

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( lease_arm, &params );
    return params.err;
}

DWORD WINAPI WexLeaseRelease( UINT64 lease, BOOL *broken )
{
    struct lease_release_params params = { lease };
    struct lease_watch *w;

    if (broken) *broken = FALSE;
    if (!unix_ready || !lease || lease > ARRAY_SIZE(lease_watches)) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( lease_stop, &params );
    if ((w = InterlockedExchangePointer( (void **)&lease_watches[lease - 1], NULL )))
    {
        WaitForSingleObject( w->thread, INFINITE );
        CloseHandle( w->thread );
        free( w );
    }
    WEXHOST_CALL( lease_release, &params );
    if (broken) *broken = params.broken;
    return params.err;
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

DWORD WINAPI WexLocaleSet( const char *locale, const char *layout )
{
    struct locale_set_params params = {0};
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    if (locale) lstrcpynA( params.locale, locale, sizeof(params.locale) );
    if (layout) lstrcpynA( params.layout, layout, sizeof(params.layout) );
    WEXHOST_CALL( locale_set, &params );
    return params.err;
}

DWORD WINAPI WexRootPasswordSet( const char *password )
{
    struct root_pass_set_params params = {0};
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    if (password && strlen( password ) >= sizeof(params.password)) return ERROR_INVALID_PARAMETER;
    if (password) strcpy( params.password, password );
    WEXHOST_CALL( root_pass_set, &params );
    SecureZeroMemory( params.password, sizeof(params.password) );
    return params.err;
}

DWORD WINAPI WexUserCreate( const char *name, const char *real_name, int account_type, const char *password )
{
    struct user_create_params params = {0};
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    if (!name || strlen( name ) >= sizeof(params.name)) return ERROR_INVALID_PARAMETER;
    if (password && strlen( password ) >= sizeof(params.password)) return ERROR_INVALID_PARAMETER;
    strcpy( params.name, name );
    if (real_name) lstrcpynA( params.real_name, real_name, sizeof(params.real_name) );
    params.account_type = account_type;
    if (password) strcpy( params.password, password );
    WEXHOST_CALL( user_create, &params );
    SecureZeroMemory( params.password, sizeof(params.password) );
    return params.err;
}

DWORD WINAPI WexUserSetSession( const char *name, const char *session, BOOL autologin )
{
    struct user_set_session_params params = {0};
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    if (!name || strlen( name ) >= sizeof(params.name)) return ERROR_INVALID_PARAMETER;
    strcpy( params.name, name );
    if (session) lstrcpynA( params.session, session, sizeof(params.session) );
    params.autologin = autologin;
    WEXHOST_CALL( user_set_session, &params );
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

DWORD WINAPI WexCompositionSetEnabled( BOOL enabled )
{
    struct composition_params params = { enabled };
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( composition_set, &params );
    return params.err;
}

DWORD WINAPI WexCompositionGetEnabled( BOOL *enabled )
{
    struct composition_params params = { 0 };
    if (!enabled) return ERROR_INVALID_PARAMETER;
    *enabled = FALSE;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( composition_get, &params );
    *enabled = params.enabled;
    return params.err;
}

DWORD WINAPI WexCompositionSetShadow( UINT64 window, BOOL shadow )
{
    struct composition_shadow_params params = { window, shadow };
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( composition_shadow, &params );
    return params.err;
}

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
    return WexWmStartEx( WEX_WM_BACKEND_AUTO, desktop_window, callback, context );
}

DWORD WINAPI WexWmStartEx( DWORD backend, UINT64 desktop_window, WEX_WM_CALLBACK callback, void *context )
{
    struct wm_start_ex_params params = { desktop_window, backend };

    if (!unix_ready || !callback) return ERROR_NOT_SUPPORTED;
    if (wm_thread) return ERROR_ALREADY_EXISTS;
    wm_callback = callback;
    wm_context = context;
    WEXHOST_CALL( wm_start_ex, &params );
    if (params.err) return params.err;
    wm_thread = CreateThread( NULL, 0, wm_event_thread, NULL, 0, NULL );
    if (!wm_thread)
    {
        struct wm_stop_params stop;
        WEXHOST_CALL( wm_stop, &stop );
        wm_callback = NULL;
        wm_context = NULL;
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}

DWORD WINAPI WexWmGetCapabilities( DWORD *backend, DWORD *capabilities )
{
    struct wm_capabilities_params params = {0};
    if (!backend || !capabilities) return ERROR_INVALID_PARAMETER;
    *backend = WEX_WM_BACKEND_AUTO;
    *capabilities = 0;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    WEXHOST_CALL( wm_capabilities, &params );
    *backend = params.backend;
    *capabilities = params.capabilities;
    return params.err;
}

DWORD WINAPI WexWmBindHost( UINT64 window, HWND host, const RECT *client, const RECT *screen )
{
    DWORD backend, capabilities, err;
    UINT64 parent;
    if (!IsWindow( host )) return ERROR_INVALID_WINDOW_HANDLE;
    if ((err = WexWmGetCapabilities( &backend, &capabilities ))) return err;
    if (backend == WEX_WM_BACKEND_WAYLAND && (capabilities & WEX_WM_CAP_FOREIGN_FRAME))
        return wayland_host_bind(window,host);
    if (backend != WEX_WM_BACKEND_X11 || !(capabilities & WEX_WM_CAP_X11_PARENT))
        return ERROR_NOT_SUPPORTED;
    parent = (ULONG_PTR)GetPropW( host, L"__wine_x11_whole_window" );
    if (!parent) return ERROR_INVALID_WINDOW_HANDLE;
    return WexWmManage( window, parent, client, screen );
}

DWORD WINAPI WexWmGetNativeInfo( UINT64 window, WEX_NATIVE_WINDOW *info )
{
    struct wm_native_info_params params = {0};
    if (!info) return ERROR_INVALID_PARAMETER;
    memset(info, 0, sizeof(*info));
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.window = window;
    WEXHOST_CALL(wm_native_info, &params);
    if (!params.err) *info = params.info;
    return params.err;
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
    DWORD backend, capabilities;
    if (!WexWmGetCapabilities(&backend,&capabilities) && backend == WEX_WM_BACKEND_WAYLAND)
        return wayland_host_configure(window);
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
    DWORD err=wayland_host_release(window);
    if (err) return err;
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
    wayland_host_stop();
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
