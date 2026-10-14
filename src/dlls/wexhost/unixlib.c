/*
 * WineExplorer host services - Unix side: D-Bus clients for systemd timedate1 and a polkit agent
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * libdbus is loaded at run time (like mountmgr and winebth do), so a machine without it, or without the
 * target service, just reports ERROR_NOT_SUPPORTED and the caller hides the feature.
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef SONAME_LIBDBUS_1
# include <dlfcn.h>
# include <dbus/dbus.h>
#endif

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <winternl.h>

#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL( wexhost );

#ifdef SONAME_LIBDBUS_1

#define DBUS_FUNCS \
    DO_FUNC( dbus_bus_get_private ); \
    DO_FUNC( dbus_bus_name_has_owner ); \
    DO_FUNC( dbus_connection_close ); \
    DO_FUNC( dbus_connection_flush ); \
    DO_FUNC( dbus_connection_pop_message ); \
    DO_FUNC( dbus_connection_read_write ); \
    DO_FUNC( dbus_connection_send ); \
    DO_FUNC( dbus_connection_send_with_reply_and_block ); \
    DO_FUNC( dbus_connection_set_exit_on_disconnect ); \
    DO_FUNC( dbus_connection_unref ); \
    DO_FUNC( dbus_error_free ); \
    DO_FUNC( dbus_error_has_name ); \
    DO_FUNC( dbus_error_init ); \
    DO_FUNC( dbus_error_is_set ); \
    DO_FUNC( dbus_message_append_args ); \
    DO_FUNC( dbus_message_get_interface ); \
    DO_FUNC( dbus_message_get_member ); \
    DO_FUNC( dbus_message_get_type ); \
    DO_FUNC( dbus_message_iter_append_basic ); \
    DO_FUNC( dbus_message_iter_close_container ); \
    DO_FUNC( dbus_message_iter_get_arg_type ); \
    DO_FUNC( dbus_message_iter_get_basic ); \
    DO_FUNC( dbus_message_iter_get_fixed_array ); \
    DO_FUNC( dbus_message_iter_append_fixed_array ); \
    DO_FUNC( dbus_message_iter_init ); \
    DO_FUNC( dbus_message_iter_init_append ); \
    DO_FUNC( dbus_message_iter_next ); \
    DO_FUNC( dbus_message_iter_open_container ); \
    DO_FUNC( dbus_message_iter_recurse ); \
    DO_FUNC( dbus_message_new_error ); \
    DO_FUNC( dbus_message_new_method_call ); \
    DO_FUNC( dbus_message_new_method_return ); \
    DO_FUNC( dbus_message_ref ); \
    DO_FUNC( dbus_message_unref ); \
    DO_FUNC( dbus_threads_init_default )

#define DO_FUNC( f ) static typeof( f ) (*p_##f)
DBUS_FUNCS;
#undef DO_FUNC

static BOOL load_dbus( void )
{
    static int state;   /* 0 = untried, 1 = ok, -1 = failed */
    void *handle;

    if (state) return state > 0;
    state = -1;
    if (!(handle = dlopen( SONAME_LIBDBUS_1, RTLD_NOW ))) { WARN( "libdbus not available: %s\n", dlerror() ); return FALSE; }
#define DO_FUNC( f ) if (!(p_##f = dlsym( handle, #f ))) { ERR( "missing %s\n", #f ); return FALSE; }
    DBUS_FUNCS;
#undef DO_FUNC
    p_dbus_threads_init_default();
    state = 1;
    return TRUE;
}

static DWORD map_error( DBusError *err )
{
    if (p_dbus_error_has_name( err, DBUS_ERROR_ACCESS_DENIED ) ||
        p_dbus_error_has_name( err, "org.freedesktop.PolicyKit1.Error.NotAuthorized" )) return ERROR_ACCESS_DENIED;
    if (p_dbus_error_has_name( err, "org.freedesktop.PolicyKit1.Error.Cancelled" )) return ERROR_CANCELLED;
    if (p_dbus_error_has_name( err, DBUS_ERROR_SERVICE_UNKNOWN ) || p_dbus_error_has_name( err, DBUS_ERROR_NAME_HAS_NO_OWNER ) ||
        p_dbus_error_has_name( err, DBUS_ERROR_NO_SERVER ) || p_dbus_error_has_name( err, DBUS_ERROR_DISCONNECTED ) ||
        p_dbus_error_has_name( err, DBUS_ERROR_FILE_NOT_FOUND ) || p_dbus_error_has_name( err, DBUS_ERROR_UNKNOWN_METHOD ))
        return ERROR_NOT_SUPPORTED;
    if (p_dbus_error_has_name( err, DBUS_ERROR_INVALID_ARGS )) return ERROR_INVALID_PARAMETER;
    WARN( "D-Bus error %s: %s\n", err->name, err->message );
    return ERROR_GEN_FAILURE;
}

static DBusConnection *system_bus;
static pthread_mutex_t bus_mutex = PTHREAD_MUTEX_INITIALIZER;

static DBusConnection *get_system_bus( void )
{
    DBusError err;

    pthread_mutex_lock( &bus_mutex );
    if (!system_bus && load_dbus())
    {
        p_dbus_error_init( &err );
        if ((system_bus = p_dbus_bus_get_private( DBUS_BUS_SYSTEM, &err )))
            p_dbus_connection_set_exit_on_disconnect( system_bus, FALSE );
        else
        {
            WARN( "cannot connect to the system bus: %s\n", err.message );
            p_dbus_error_free( &err );
        }
    }
    pthread_mutex_unlock( &bus_mutex );
    return system_bus;
}

/* Blocks without a time limit: polkit may be waiting for the user to type a password. */
static DWORD call_system( DBusMessage *msg, DBusMessage **reply_out )
{
    DBusConnection *bus = get_system_bus();
    DBusMessage *reply;
    DBusError err;
    DWORD ret = ERROR_SUCCESS;

    if (!bus || !msg) return ERROR_NOT_SUPPORTED;
    p_dbus_error_init( &err );
    reply = p_dbus_connection_send_with_reply_and_block( bus, msg, DBUS_TIMEOUT_INFINITE, &err );
    if (!reply) { ret = map_error( &err ); p_dbus_error_free( &err ); }
    if (reply_out) *reply_out = reply; else if (reply) p_dbus_message_unref( reply );
    return ret;
}

/* ------------------------------------------------------------------ timedate1 */

#define TD_DEST  "org.freedesktop.timedate1"
#define TD_PATH  "/org/freedesktop/timedate1"
#define TD_IFACE "org.freedesktop.timedate1"

static NTSTATUS unix_init_impl( void *args )
{
    struct init_params *params = args;
    params->err = get_system_bus() ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static NTSTATUS timedate_get( void *args )
{
    struct timedate_get_params *params = args;
    DBusMessage *msg, *reply;
    DBusMessageIter it, arr, entry, var;
    const char *iface = TD_IFACE;

    memset( &params->info, 0, sizeof(params->info) );
    msg = load_dbus() ? p_dbus_message_new_method_call( TD_DEST, TD_PATH, "org.freedesktop.DBus.Properties", "GetAll" ) : NULL;
    if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID );
    params->err = call_system( msg, &reply );
    if (msg) p_dbus_message_unref( msg );
    if (params->err) return STATUS_SUCCESS;

    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &it, &arr );
        while (p_dbus_message_iter_get_arg_type( &arr ) == DBUS_TYPE_DICT_ENTRY)
        {
            const char *key;
            p_dbus_message_iter_recurse( &arr, &entry );
            p_dbus_message_iter_get_basic( &entry, &key );
            p_dbus_message_iter_next( &entry );
            p_dbus_message_iter_recurse( &entry, &var );
            if (!strcmp( key, "Timezone" ) && p_dbus_message_iter_get_arg_type( &var ) == DBUS_TYPE_STRING)
            {
                const char *s;
                p_dbus_message_iter_get_basic( &var, &s );
                snprintf( params->info.timezone, sizeof(params->info.timezone), "%s", s );
            }
            else if (p_dbus_message_iter_get_arg_type( &var ) == DBUS_TYPE_BOOLEAN)
            {
                dbus_bool_t b;
                p_dbus_message_iter_get_basic( &var, &b );
                if (!strcmp( key, "NTP" )) params->info.ntp_enabled = b;
                else if (!strcmp( key, "CanNTP" )) params->info.can_ntp = b;
                else if (!strcmp( key, "NTPSynchronized" )) params->info.ntp_synchronized = b;
                else if (!strcmp( key, "LocalRTC" )) params->info.local_rtc = b;
            }
            p_dbus_message_iter_next( &arr );
        }
    }
    p_dbus_message_unref( reply );
    return STATUS_SUCCESS;
}

static NTSTATUS timedate_set_time( void *args )
{
    struct timedate_set_time_params *params = args;
    DBusMessage *msg;
    dbus_int64_t usec = params->unix_usec;
    dbus_bool_t relative = params->relative, interactive = TRUE;

    msg = load_dbus() ? p_dbus_message_new_method_call( TD_DEST, TD_PATH, TD_IFACE, "SetTime" ) : NULL;
    if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_INT64, &usec, DBUS_TYPE_BOOLEAN, &relative,
                                         DBUS_TYPE_BOOLEAN, &interactive, DBUS_TYPE_INVALID );
    params->err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    return STATUS_SUCCESS;
}

static NTSTATUS timedate_set_tz( void *args )
{
    struct timedate_set_tz_params *params = args;
    DBusMessage *msg;
    const char *tz = params->timezone;
    dbus_bool_t interactive = TRUE;

    params->timezone[WEX_TZ_NAME_MAX - 1] = 0;
    msg = load_dbus() ? p_dbus_message_new_method_call( TD_DEST, TD_PATH, TD_IFACE, "SetTimezone" ) : NULL;
    if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &tz, DBUS_TYPE_BOOLEAN, &interactive, DBUS_TYPE_INVALID );
    params->err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    return STATUS_SUCCESS;
}

static NTSTATUS timedate_set_ntp( void *args )
{
    struct timedate_set_ntp_params *params = args;
    DBusMessage *msg;
    dbus_bool_t on = params->enable, interactive = TRUE;

    msg = load_dbus() ? p_dbus_message_new_method_call( TD_DEST, TD_PATH, TD_IFACE, "SetNTP" ) : NULL;
    if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_BOOLEAN, &on, DBUS_TYPE_BOOLEAN, &interactive, DBUS_TYPE_INVALID );
    params->err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    return STATUS_SUCCESS;
}

static NTSTATUS timedate_list_tz( void *args )
{
    struct timedate_list_tz_params *params = args;
    DBusMessage *msg, *reply;
    DBusMessageIter it, arr;
    char *out = (char *)(ULONG_PTR)params->buffer;
    DWORD used = 0;

    params->needed = 0;
    msg = load_dbus() ? p_dbus_message_new_method_call( TD_DEST, TD_PATH, TD_IFACE, "ListTimezones" ) : NULL;
    params->err = call_system( msg, &reply );
    if (msg) p_dbus_message_unref( msg );
    if (params->err) return STATUS_SUCCESS;

    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &it, &arr );
        while (p_dbus_message_iter_get_arg_type( &arr ) == DBUS_TYPE_STRING)
        {
            const char *s;
            size_t len;
            p_dbus_message_iter_get_basic( &arr, &s );
            len = strlen( s ) + 1;
            if (out && used + len < params->size) { memcpy( out + used, s, len - 1 ); out[used + len - 1] = '\n'; }
            used += len;
            p_dbus_message_iter_next( &arr );
        }
    }
    p_dbus_message_unref( reply );
    if (out && used < params->size) out[used] = 0;
    params->needed = used + 1;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ generic property access */

struct prop { const char *name; int type; void *out; size_t size; };   /* type: DBUS_TYPE_* */

/* GetAll(iface) on dest/path and store the listed properties */
static DWORD get_properties( const char *dest, const char *path, const char *iface, struct prop *props, int count )
{
    DBusMessage *msg, *reply;
    DBusMessageIter it, arr, entry, var;
    DWORD err;

    if (!load_dbus()) return ERROR_NOT_SUPPORTED;
    msg = p_dbus_message_new_method_call( dest, path, "org.freedesktop.DBus.Properties", "GetAll" );
    p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID );
    err = call_system( msg, &reply );
    p_dbus_message_unref( msg );
    if (err) return err;

    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &it, &arr );
        while (p_dbus_message_iter_get_arg_type( &arr ) == DBUS_TYPE_DICT_ENTRY)
        {
            const char *key;
            int i;
            p_dbus_message_iter_recurse( &arr, &entry );
            p_dbus_message_iter_get_basic( &entry, &key );
            p_dbus_message_iter_next( &entry );
            p_dbus_message_iter_recurse( &entry, &var );
            for (i = 0; i < count; i++)
            {
                union { const char *s; dbus_bool_t b; double d; dbus_uint32_t u; dbus_int64_t x; dbus_uint64_t t; } v;
                if (strcmp( key, props[i].name ) || p_dbus_message_iter_get_arg_type( &var ) != props[i].type) continue;
                if (props[i].type == DBUS_TYPE_ARRAY)          /* "ay": copy the bytes as a string (e.g. an SSID) */
                {
                    DBusMessageIter sub;
                    const unsigned char *bytes = NULL;
                    int n = 0;
                    p_dbus_message_iter_recurse( &var, &sub );
                    if (p_dbus_message_iter_get_arg_type( &sub ) == DBUS_TYPE_BYTE)
                    {
                        p_dbus_message_iter_get_fixed_array( &sub, &bytes, &n );
                        n = min( n, (int)props[i].size - 1 );
                        if (n > 0) memcpy( props[i].out, bytes, n );
                        ((char *)props[i].out)[max( n, 0 )] = 0;
                    }
                    continue;
                }
                p_dbus_message_iter_get_basic( &var, &v );
                switch (props[i].type)
                {
                case DBUS_TYPE_STRING: case DBUS_TYPE_OBJECT_PATH:
                    snprintf( props[i].out, props[i].size, "%s", v.s ); break;
                case DBUS_TYPE_BOOLEAN: *(int *)props[i].out = v.b; break;
                case DBUS_TYPE_BYTE: *(unsigned char *)props[i].out = (unsigned char)v.u; break;
                case DBUS_TYPE_DOUBLE: *(double *)props[i].out = v.d; break;
                case DBUS_TYPE_UINT32: *(dbus_uint32_t *)props[i].out = v.u; break;
                case DBUS_TYPE_INT64: *(dbus_int64_t *)props[i].out = v.x; break;
                case DBUS_TYPE_UINT64: *(dbus_uint64_t *)props[i].out = v.t; break;
                }
            }
            p_dbus_message_iter_next( &arr );
        }
    }
    p_dbus_message_unref( reply );
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ login1: power actions */

#define LOGIN_DEST  "org.freedesktop.login1"
#define LOGIN_PATH  "/org/freedesktop/login1"
#define LOGIN_IFACE "org.freedesktop.login1.Manager"

static const char *const power_methods[] = { "PowerOff", "Reboot", "Suspend", "Hibernate" };

static BOOL find_session_id( DBusConnection *conn, char *out, size_t size );

#define GDM_DEST "org.gnome.DisplayManager"
#define LIGHTDM_DEST "org.freedesktop.DisplayManager"
#define LIGHTDM_IFACE "org.freedesktop.DisplayManager.Seat"

/* Log off, Switch User and Lock: our own login session and the display manager's greeter */
static BOOL session_can( DWORD action )
{
    char session[64];
    const char *seat = getenv( "XDG_SEAT_PATH" );
    DBusConnection *bus = get_system_bus();

    if (!bus) return FALSE;
    if (action == WEX_POWER_LOGOFF) return find_session_id( bus, session, sizeof(session) );
    if (seat && p_dbus_bus_name_has_owner( bus, LIGHTDM_DEST, NULL )) return TRUE;
    return action == WEX_POWER_SWITCHUSER && p_dbus_bus_name_has_owner( bus, GDM_DEST, NULL );
}

static DWORD session_do( DWORD action )
{
    const char *seat = getenv( "XDG_SEAT_PATH" );
    DBusConnection *bus = get_system_bus();
    DBusMessage *msg;
    DWORD err;

    if (!bus) return ERROR_NOT_SUPPORTED;
    if (action == WEX_POWER_LOGOFF)
    {
        char session[64];
        const char *id = session;

        if (!find_session_id( bus, session, sizeof(session) )) return ERROR_NOT_SUPPORTED;
        msg = p_dbus_message_new_method_call( LOGIN_DEST, LOGIN_PATH, LOGIN_IFACE, "TerminateSession" );
        if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID );
    }
    else if (seat && p_dbus_bus_name_has_owner( bus, LIGHTDM_DEST, NULL ))
        msg = p_dbus_message_new_method_call( LIGHTDM_DEST, seat, LIGHTDM_IFACE,
                                               action == WEX_POWER_LOCK ? "Lock" : "SwitchToGreeter" );
    else if (action == WEX_POWER_LOCK) return ERROR_NOT_SUPPORTED;
    else msg = p_dbus_message_new_method_call( GDM_DEST, "/org/gnome/DisplayManager/LocalDisplayFactory",
                                               "org.gnome.DisplayManager.LocalDisplayFactory", "CreateTransientDisplay" );
    err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    return err;
}

static NTSTATUS power_can( void *args )
{
    struct power_can_params *params = args;
    char name[32];
    DBusMessage *msg, *reply;
    DBusMessageIter it;
    const char *s;

    params->result = WEX_POWER_NO;
    if (params->action == WEX_POWER_LOGOFF || params->action == WEX_POWER_SWITCHUSER || params->action == WEX_POWER_LOCK)
    {
        params->err = load_dbus() ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
        if (!params->err && session_can( params->action )) params->result = WEX_POWER_YES;
        return STATUS_SUCCESS;
    }
    if (params->action >= ARRAYSIZE(power_methods)) { params->err = ERROR_INVALID_PARAMETER; return STATUS_SUCCESS; }
    snprintf( name, sizeof(name), "Can%s", power_methods[params->action] );
    msg = load_dbus() ? p_dbus_message_new_method_call( LOGIN_DEST, LOGIN_PATH, LOGIN_IFACE, name ) : NULL;
    params->err = call_system( msg, &reply );
    if (msg) p_dbus_message_unref( msg );
    if (params->err) return STATUS_SUCCESS;

    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_STRING)
    {
        p_dbus_message_iter_get_basic( &it, &s );
        if (!strcmp( s, "yes" )) params->result = WEX_POWER_YES;
        else if (!strcmp( s, "challenge" )) params->result = WEX_POWER_CHALLENGE;
    }
    p_dbus_message_unref( reply );
    return STATUS_SUCCESS;
}

static NTSTATUS power_do( void *args )
{
    struct power_do_params *params = args;
    DBusMessage *msg;
    dbus_bool_t interactive = TRUE;

    if (params->action == WEX_POWER_LOGOFF || params->action == WEX_POWER_SWITCHUSER || params->action == WEX_POWER_LOCK)
    {
        params->err = load_dbus() ? session_do( params->action ) : ERROR_NOT_SUPPORTED;
        return STATUS_SUCCESS;
    }
    if (params->action >= ARRAYSIZE(power_methods)) { params->err = ERROR_INVALID_PARAMETER; return STATUS_SUCCESS; }
    msg = load_dbus() ? p_dbus_message_new_method_call( LOGIN_DEST, LOGIN_PATH, LOGIN_IFACE, power_methods[params->action] ) : NULL;
    if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_BOOLEAN, &interactive, DBUS_TYPE_INVALID );
    params->err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ UPower: battery */

static NTSTATUS battery_get( void *args )
{
    struct battery_get_params *params = args;
    int present = 0, on_battery = 0, lid = 0;
    double percentage = 0;
    dbus_uint32_t state = 0;
    dbus_int64_t to_empty = 0, to_full = 0;
    struct prop root[] = { { "OnBattery", DBUS_TYPE_BOOLEAN, &on_battery, 0 } };
    struct prop disp[] = {
        { "IsPresent", DBUS_TYPE_BOOLEAN, &present, 0 }, { "Percentage", DBUS_TYPE_DOUBLE, &percentage, 0 },
        { "State", DBUS_TYPE_UINT32, &state, 0 }, { "TimeToEmpty", DBUS_TYPE_INT64, &to_empty, 0 },
        { "TimeToFull", DBUS_TYPE_INT64, &to_full, 0 } };

    memset( &params->info, 0, sizeof(params->info) );
    (void)lid;
    params->err = get_properties( "org.freedesktop.UPower", "/org/freedesktop/UPower", "org.freedesktop.UPower",
                                  root, ARRAYSIZE(root) );
    if (params->err) return STATUS_SUCCESS;
    params->err = get_properties( "org.freedesktop.UPower", "/org/freedesktop/UPower/devices/DisplayDevice",
                                  "org.freedesktop.UPower.Device", disp, ARRAYSIZE(disp) );
    if (params->err) return STATUS_SUCCESS;

    params->info.has_battery = present;
    params->info.on_battery = on_battery;
    params->info.percent = (DWORD)(percentage + 0.5);
    params->info.state = state;        /* 1 charging, 2 discharging, 3 empty, 4 fully charged, 5/6 pending */
    params->info.seconds_to_empty = (to_empty > 0) ? (int)to_empty : -1;
    params->info.seconds_to_full = (to_full > 0) ? (int)to_full : -1;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ hostname1 */

#define HN_DEST  "org.freedesktop.hostname1"
#define HN_PATH  "/org/freedesktop/hostname1"
#define HN_IFACE "org.freedesktop.hostname1"

static NTSTATUS hostname_get( void *args )
{
    struct hostname_get_params *params = args;
    struct prop props[] = {
        { "StaticHostname", DBUS_TYPE_STRING, params->info.static_name, sizeof(params->info.static_name) },
        { "PrettyHostname", DBUS_TYPE_STRING, params->info.pretty_name, sizeof(params->info.pretty_name) },
        { "Hostname", DBUS_TYPE_STRING, params->info.transient_name, sizeof(params->info.transient_name) } };

    memset( &params->info, 0, sizeof(params->info) );
    params->err = get_properties( HN_DEST, HN_PATH, HN_IFACE, props, ARRAYSIZE(props) );
    return STATUS_SUCCESS;
}

static NTSTATUS hostname_set( void *args )
{
    struct hostname_set_params *params = args;
    DBusMessage *msg;
    const char *name = params->name;
    dbus_bool_t interactive = TRUE;

    params->name[sizeof(params->name) - 1] = 0;
    msg = load_dbus() ? p_dbus_message_new_method_call( HN_DEST, HN_PATH, HN_IFACE,
                                                        params->pretty ? "SetPrettyHostname" : "SetStaticHostname" ) : NULL;
    if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_BOOLEAN, &interactive, DBUS_TYPE_INVALID );
    params->err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ AccountsService: the logged-in user */

static NTSTATUS user_get( void *args )
{
    struct user_get_params *params = args;
    DBusMessage *msg, *reply;
    DBusMessageIter it;
    dbus_int64_t uid = getuid();
    char path[128] = "";
    struct prop props[] = {
        { "UserName", DBUS_TYPE_STRING, params->info.name, sizeof(params->info.name) },
        { "RealName", DBUS_TYPE_STRING, params->info.real_name, sizeof(params->info.real_name) },
        { "IconFile", DBUS_TYPE_STRING, params->info.icon_file, sizeof(params->info.icon_file) } };

    memset( &params->info, 0, sizeof(params->info) );
    msg = load_dbus() ? p_dbus_message_new_method_call( "org.freedesktop.Accounts", "/org/freedesktop/Accounts",
                                                        "org.freedesktop.Accounts", "FindUserById" ) : NULL;
    if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_INT64, &uid, DBUS_TYPE_INVALID );
    params->err = call_system( msg, &reply );
    if (msg) p_dbus_message_unref( msg );
    if (params->err) return STATUS_SUCCESS;

    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_OBJECT_PATH)
    {
        const char *s;
        p_dbus_message_iter_get_basic( &it, &s );
        snprintf( path, sizeof(path), "%s", s );
    }
    p_dbus_message_unref( reply );
    if (!path[0]) { params->err = ERROR_NOT_FOUND; return STATUS_SUCCESS; }
    params->err = get_properties( "org.freedesktop.Accounts", path, "org.freedesktop.Accounts.User", props, ARRAYSIZE(props) );
    return STATUS_SUCCESS;
}

/* logind's users that have a session now (Welcome screen: "Logged on"), as "\nname\n" entries in out */
static void logged_on_users( char *out, size_t size )
{
    DBusMessage *msg, *reply;
    DBusMessageIter it, arr, st;
    size_t used = 1;

    snprintf( out, size, "\n" );
    msg = p_dbus_message_new_method_call( LOGIN_DEST, LOGIN_PATH, LOGIN_IFACE, "ListUsers" );
    if (call_system( msg, &reply )) { if (msg) p_dbus_message_unref( msg ); return; }
    p_dbus_message_unref( msg );
    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &it, &arr );
        while (p_dbus_message_iter_get_arg_type( &arr ) == DBUS_TYPE_STRUCT)   /* (uid, name, path) */
        {
            const char *name;
            int n;
            p_dbus_message_iter_recurse( &arr, &st );
            p_dbus_message_iter_next( &st );
            p_dbus_message_iter_get_basic( &st, &name );
            n = snprintf( out + used, size - used, "%s\n", name );
            if (n > 0 && used + n < size) used += n;
            p_dbus_message_iter_next( &arr );
        }
    }
    p_dbus_message_unref( reply );
}

/* The accounts the logon screen offers: AccountsService's cached users without system accounts, one per line as
 * "name\treal name\ticon file\tlogged on (0 or 1)".  needed gets the size including the terminating NUL. */
static NTSTATUS users_list( void *args )
{
    struct users_list_params *params = args;
    char *out = (char *)(ULONG_PTR)params->buffer;
    DBusMessage *msg, *reply;
    DBusMessageIter it, arr;
    size_t used = 0;
    char logged[4096];

    params->needed = 0;
    if (load_dbus()) logged_on_users( logged, sizeof(logged) );
    msg = load_dbus() ? p_dbus_message_new_method_call( "org.freedesktop.Accounts", "/org/freedesktop/Accounts",
                                                        "org.freedesktop.Accounts", "ListCachedUsers" ) : NULL;
    params->err = call_system( msg, &reply );
    if (msg) p_dbus_message_unref( msg );
    if (params->err) return STATUS_SUCCESS;

    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &it, &arr );
        while (p_dbus_message_iter_get_arg_type( &arr ) == DBUS_TYPE_OBJECT_PATH)
        {
            const char *path;
            char name[64] = "", real[128] = "", icon[260] = "", line[512], key[80];
            int system = 0, n;
            struct prop props[] = {
                { "UserName", DBUS_TYPE_STRING, name, sizeof(name) },
                { "RealName", DBUS_TYPE_STRING, real, sizeof(real) },
                { "IconFile", DBUS_TYPE_STRING, icon, sizeof(icon) },
                { "SystemAccount", DBUS_TYPE_BOOLEAN, &system, 0 } };

            p_dbus_message_iter_get_basic( &arr, &path );
            p_dbus_message_iter_next( &arr );
            if (get_properties( "org.freedesktop.Accounts", path, "org.freedesktop.Accounts.User", props, ARRAYSIZE(props) )
                || system || !name[0] || strpbrk( name, "\t\n" ) || strpbrk( real, "\t\n" ) || strpbrk( icon, "\t\n" ))
                continue;
            snprintf( key, sizeof(key), "\n%s\n", name );
            n = snprintf( line, sizeof(line), "%s\t%s\t%s\t%d\n", name, real, icon, strstr( logged, key ) != NULL );
            if (n <= 0 || n >= sizeof(line)) continue;
            if (out && used + n < params->size) memcpy( out + used, line, n );
            used += n;
        }
    }
    p_dbus_message_unref( reply );
    params->needed = used + 1;
    if (!out || used >= params->size) params->err = ERROR_MORE_DATA;
    else out[used] = 0;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ Setup: locale, accounts (setup.exe, oobe.exe)
 * The setup account is allowed these actions by installer/systemd/49-wexsetup.rules while Setup runs. */

#define ACC_DEST  "org.freedesktop.Accounts"
#define ACC_PATH  "/org/freedesktop/Accounts"
#define ACC_USER  "org.freedesktop.Accounts.User"
#define LOC_DEST  "org.freedesktop.locale1"
#define LOC_PATH  "/org/freedesktop/locale1"

/* crypt(3) SHA-512 with a random salt, for AccountsService SetPassword; libcrypt is loaded at run time */
static BOOL hash_password( const char *password, char *out, size_t size )
{
    static const char chars[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static char *(*p_crypt)( const char *, const char * );
    unsigned char raw[16];
    char salt[3 + 16 + 2];
    const char *hash;
    FILE *f;
    int i;

    if (!p_crypt)
    {
        void *lib = dlopen( "libcrypt.so.1", RTLD_NOW );
        if (!lib) lib = dlopen( "libcrypt.so", RTLD_NOW );
        if (lib) p_crypt = dlsym( lib, "crypt" );
        if (!p_crypt) return FALSE;
    }
    if (!(f = fopen( "/dev/urandom", "rb" ))) return FALSE;
    i = fread( raw, 1, sizeof(raw), f );
    fclose( f );
    if (i != sizeof(raw)) return FALSE;
    strcpy( salt, "$6$" );
    for (i = 0; i < sizeof(raw); i++) salt[3 + i] = chars[raw[i] % 64];
    strcpy( salt + 3 + sizeof(raw), "$" );
    hash = p_crypt( password, salt );
    if (!hash || strncmp( hash, "$6$", 3 ) || strlen( hash ) >= size) return FALSE;   /* "*0" and the like: failure */
    strcpy( out, hash );
    return TRUE;
}

/* the AccountsService object of a user name */
static BOOL find_user_path( const char *name, char *path, size_t size )
{
    DBusMessage *msg, *reply;
    DBusMessageIter it;
    const char *s;
    BOOL ok = FALSE;

    msg = p_dbus_message_new_method_call( ACC_DEST, ACC_PATH, "org.freedesktop.Accounts", "FindUserByName" );
    if (!msg) return FALSE;
    p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID );
    if (!call_system( msg, &reply ))
    {
        if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_OBJECT_PATH)
        {
            p_dbus_message_iter_get_basic( &it, &s );
            ok = snprintf( path, size, "%s", s ) < size;
        }
        p_dbus_message_unref( reply );
    }
    p_dbus_message_unref( msg );
    return ok;
}

static DWORD call_user( const char *path, const char *method, int type, const void *value )
{
    DBusMessage *msg = p_dbus_message_new_method_call( ACC_DEST, path, ACC_USER, method );
    DWORD err;
    if (msg) p_dbus_message_append_args( msg, type, value, DBUS_TYPE_INVALID );
    err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    return err;
}

static DWORD set_password( const char *path, char *password )
{
    char hash[256];
    const char *h = hash, *hint = "";
    DBusMessage *msg;
    DWORD err;

    if (!hash_password( password, hash, sizeof(hash) )) return ERROR_NOT_SUPPORTED;
    msg = p_dbus_message_new_method_call( ACC_DEST, path, ACC_USER, "SetPassword" );
    if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &h, DBUS_TYPE_STRING, &hint, DBUS_TYPE_INVALID );
    err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    explicit_bzero( hash, sizeof(hash) );
    return err;
}

/* locale1: SetLocale(["LANG=<locale>"]) and SetX11Keyboard(layout), which also sets the console keymap */
static NTSTATUS locale_set( void *args )
{
    struct locale_set_params *params = args;
    dbus_bool_t interactive = TRUE, convert = TRUE;
    DBusMessage *msg;

    params->locale[sizeof(params->locale) - 1] = params->layout[sizeof(params->layout) - 1] = 0;
    params->err = load_dbus() ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
    if (!params->err && params->locale[0])
    {
        char env[96];
        const char *s = env;
        DBusMessageIter it, arr;

        snprintf( env, sizeof(env), "LANG=%s", params->locale );
        msg = p_dbus_message_new_method_call( LOC_DEST, LOC_PATH, LOC_DEST, "SetLocale" );
        if (msg)
        {
            p_dbus_message_iter_init_append( msg, &it );
            p_dbus_message_iter_open_container( &it, DBUS_TYPE_ARRAY, "s", &arr );
            p_dbus_message_iter_append_basic( &arr, DBUS_TYPE_STRING, &s );
            p_dbus_message_iter_close_container( &it, &arr );
            p_dbus_message_iter_append_basic( &it, DBUS_TYPE_BOOLEAN, &interactive );
        }
        params->err = call_system( msg, NULL );
        if (msg) p_dbus_message_unref( msg );
    }
    if (!params->err && params->layout[0])
    {
        const char *layout = params->layout, *model = "pc105", *empty = "";
        msg = p_dbus_message_new_method_call( LOC_DEST, LOC_PATH, LOC_DEST, "SetX11Keyboard" );
        if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &layout, DBUS_TYPE_STRING, &model,
                                             DBUS_TYPE_STRING, &empty, DBUS_TYPE_STRING, &empty,
                                             DBUS_TYPE_BOOLEAN, &convert, DBUS_TYPE_BOOLEAN, &interactive,
                                             DBUS_TYPE_INVALID );
        params->err = call_system( msg, NULL );
        if (msg) p_dbus_message_unref( msg );
    }
    return STATUS_SUCCESS;
}

/* the Administrator password: root's password, through AccountsService (an empty one leaves root locked) */
static NTSTATUS root_pass_set( void *args )
{
    struct root_pass_set_params *params = args;
    char path[128];

    params->password[sizeof(params->password) - 1] = 0;
    if (!params->password[0]) params->err = ERROR_SUCCESS;
    else if (!load_dbus()) params->err = ERROR_NOT_SUPPORTED;
    else if (!find_user_path( "root", path, sizeof(path) )) params->err = ERROR_NO_SUCH_USER;
    else params->err = set_password( path, params->password );
    explicit_bzero( params->password, sizeof(params->password) );
    return STATUS_SUCCESS;
}

/* CreateUser (account type 1: administrator, the sudo group), the WineExplorer session, and the password when one is
 * given; AccountsService copies /etc/skel, including the template Wine prefix, into the new home */
static NTSTATUS user_create( void *args )
{
    struct user_create_params *params = args;
    const char *name = params->name, *real = params->real_name, *session = "wine-explorer";
    dbus_int32_t type = params->account_type;
    DBusMessage *msg;
    char path[128];

    params->name[sizeof(params->name) - 1] = params->real_name[sizeof(params->real_name) - 1] = 0;
    params->password[sizeof(params->password) - 1] = 0;
    if (!load_dbus()) params->err = ERROR_NOT_SUPPORTED;
    else if (!name[0]) params->err = ERROR_INVALID_PARAMETER;
    else
    {
        msg = p_dbus_message_new_method_call( ACC_DEST, ACC_PATH, "org.freedesktop.Accounts", "CreateUser" );
        if (msg) p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &real,
                                             DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID );
        params->err = call_system( msg, NULL );
        if (msg) p_dbus_message_unref( msg );
        if (!params->err && !find_user_path( name, path, sizeof(path) )) params->err = ERROR_NO_SUCH_USER;
        if (!params->err)
        {
            call_user( path, "SetXSession", DBUS_TYPE_STRING, &session );
            call_user( path, "SetSession", DBUS_TYPE_STRING, &session );
            if (params->password[0]) params->err = set_password( path, params->password );
            else
            {
                /* no password: logs on without one, as on Windows (useradd leaves the account locked) */
                dbus_int32_t mode = 2;
                params->err = call_user( path, "SetPasswordMode", DBUS_TYPE_INT32, &mode );
            }
        }
    }
    explicit_bzero( params->password, sizeof(params->password) );
    return STATUS_SUCCESS;
}

/* the user's session and automatic logon (AccountsService writes the display manager's setting) */
static NTSTATUS user_set_session( void *args )
{
    struct user_set_session_params *params = args;
    const char *session = params->session;
    dbus_bool_t autologin = params->autologin;
    char path[128];

    params->name[sizeof(params->name) - 1] = params->session[sizeof(params->session) - 1] = 0;
    if (!load_dbus()) params->err = ERROR_NOT_SUPPORTED;
    else if (!find_user_path( params->name, path, sizeof(path) )) params->err = ERROR_NO_SUCH_USER;
    else
    {
        params->err = session[0] ? call_user( path, "SetXSession", DBUS_TYPE_STRING, &session ) : ERROR_SUCCESS;
        if (!params->err) params->err = call_user( path, "SetAutomaticLogin", DBUS_TYPE_BOOLEAN, &autologin );
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ NetworkManager */

#define NM_DEST   "org.freedesktop.NetworkManager"
#define NM_PATH   "/org/freedesktop/NetworkManager"
#define NM_IFACE  "org.freedesktop.NetworkManager"
#define NM_DEV    "org.freedesktop.NetworkManager.Device"
#define NM_WIRED  "org.freedesktop.NetworkManager.Device.Wired"
#define NM_WIFI   "org.freedesktop.NetworkManager.Device.Wireless"
#define NM_AP     "org.freedesktop.NetworkManager.AccessPoint"

#define NM_DEVICE_TYPE_ETHERNET 1
#define NM_DEVICE_TYPE_WIFI     2
#define NM_DEVICE_STATE_ACTIVATED 100

#define AP_FLAG_PRIVACY   0x1
#define SEC_PSK           0x100
#define SEC_8021X         0x200
#define SEC_SAE           0x400

#define MAX_DEVICES 16

struct nm_device { char path[128]; char name[32]; char driver[32]; dbus_uint32_t type, state; };

/* All devices we care about (Ethernet and Wi-Fi). */
static int nm_get_devices( struct nm_device *devs, int max )
{
    DBusMessage *msg, *reply;
    DBusMessageIter it, arr;
    int count = 0;

    msg = load_dbus() ? p_dbus_message_new_method_call( NM_DEST, NM_PATH, NM_IFACE, "GetDevices" ) : NULL;
    if (call_system( msg, &reply )) { if (msg) p_dbus_message_unref( msg ); return -1; }
    p_dbus_message_unref( msg );

    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &it, &arr );
        while (p_dbus_message_iter_get_arg_type( &arr ) == DBUS_TYPE_OBJECT_PATH && count < max)
        {
            const char *path;
            struct nm_device *d = &devs[count];
            struct prop props[] = {
                { "Interface", DBUS_TYPE_STRING, d->name, sizeof(d->name) },
                { "Driver", DBUS_TYPE_STRING, d->driver, sizeof(d->driver) },
                { "DeviceType", DBUS_TYPE_UINT32, &d->type, 0 },
                { "State", DBUS_TYPE_UINT32, &d->state, 0 } };

            p_dbus_message_iter_get_basic( &arr, &path );
            memset( d, 0, sizeof(*d) );
            snprintf( d->path, sizeof(d->path), "%s", path );
            if (!get_properties( NM_DEST, d->path, NM_DEV, props, ARRAYSIZE(props) ) &&
                (d->type == NM_DEVICE_TYPE_ETHERNET || d->type == NM_DEVICE_TYPE_WIFI))
                count++;
            p_dbus_message_iter_next( &arr );
        }
    }
    p_dbus_message_unref( reply );
    return count;
}

static DWORD nm_call_path( const char *path, const char *iface, const char *method )
{
    DBusMessage *msg = load_dbus() ? p_dbus_message_new_method_call( NM_DEST, path, iface, method ) : NULL;
    DWORD err = call_system( msg, NULL );
    if (msg) p_dbus_message_unref( msg );
    return err;
}

static NTSTATUS net_get_status( void *args )
{
    struct net_get_status_params *params = args;
    struct nm_device devs[MAX_DEVICES];
    int count, i, pick = -1;
    WEX_NET_STATUS *st = &params->status;

    memset( st, 0, sizeof(*st) );
    if (!load_dbus()) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
    count = nm_get_devices( devs, MAX_DEVICES );
    if (count < 0) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
    params->err = ERROR_SUCCESS;

    for (i = 0; i < count; i++)
        if (devs[i].state == NM_DEVICE_STATE_ACTIVATED) { pick = i; break; }
    if (pick < 0 && count) pick = 0;
    if (pick < 0) return STATUS_SUCCESS;

    st->present = TRUE;
    st->type = devs[pick].type;
    st->state = devs[pick].state;
    st->connected = (devs[pick].state == NM_DEVICE_STATE_ACTIVATED);
    snprintf( st->ifname, sizeof(st->ifname), "%s", devs[pick].name );
    snprintf( st->driver, sizeof(st->driver), "%s", devs[pick].driver );

    if (devs[pick].type == NM_DEVICE_TYPE_ETHERNET)
    {
        dbus_uint32_t speed = 0;
        struct prop props[] = { { "Speed", DBUS_TYPE_UINT32, &speed, 0 } };
        if (!get_properties( NM_DEST, devs[pick].path, NM_WIRED, props, 1 )) st->speed_mbps = speed;
    }
    else
    {
        char ap_path[128] = "";
        dbus_uint32_t bitrate = 0;
        struct prop props[] = { { "ActiveAccessPoint", DBUS_TYPE_OBJECT_PATH, ap_path, sizeof(ap_path) },
                                { "Bitrate", DBUS_TYPE_UINT32, &bitrate, 0 } };

        if (!get_properties( NM_DEST, devs[pick].path, NM_WIFI, props, ARRAYSIZE(props) ))
        {
            st->speed_mbps = bitrate / 1000;           /* NM reports kb/s */
            if (ap_path[0] && strcmp( ap_path, "/" ))
            {
                unsigned char strength = 0;
                struct prop ap[] = { { "Ssid", DBUS_TYPE_ARRAY, st->ssid, sizeof(st->ssid) },
                                     { "Strength", DBUS_TYPE_BYTE, &strength, 0 } };
                if (!get_properties( NM_DEST, ap_path, NM_AP, ap, ARRAYSIZE(ap) )) st->signal = strength;
            }
        }
    }
    return STATUS_SUCCESS;
}

static const char *security_name( dbus_uint32_t flags, dbus_uint32_t wpa, dbus_uint32_t rsn )
{
    if (rsn & SEC_SAE) return "WPA3";
    if (rsn & SEC_8021X) return "WPA2-Enterprise";
    if (rsn & SEC_PSK) return "WPA2-PSK";
    if (wpa & SEC_8021X) return "WPA-Enterprise";
    if (wpa & SEC_PSK) return "WPA-PSK";
    if (flags & AP_FLAG_PRIVACY) return "WEP";
    return "";
}

static BOOL nm_first_wifi( char *path, size_t size )
{
    struct nm_device devs[MAX_DEVICES];
    int count = nm_get_devices( devs, MAX_DEVICES ), i;

    for (i = 0; i < count; i++)
        if (devs[i].type == NM_DEVICE_TYPE_WIFI) { snprintf( path, size, "%s", devs[i].path ); return TRUE; }
    return FALSE;
}

static NTSTATUS net_scan_wifi( void *args )
{
    struct net_scan_wifi_params *params = args;
    WEX_WIFI_AP *out = (WEX_WIFI_AP *)(ULONG_PTR)params->buffer;
    char dev[128], active[128] = "";
    DBusMessage *msg, *reply;
    DBusMessageIter it, arr, opts;
    struct prop dprops[] = { { "ActiveAccessPoint", DBUS_TYPE_OBJECT_PATH, active, sizeof(active) } };
    DWORD n = 0;

    params->count = 0;
    if (!load_dbus() || !nm_first_wifi( dev, sizeof(dev) )) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }

    /* ask for a fresh scan; the results below are whatever NetworkManager already knows */
    msg = p_dbus_message_new_method_call( NM_DEST, dev, NM_WIFI, "RequestScan" );
    p_dbus_message_iter_init_append( msg, &it );
    p_dbus_message_iter_open_container( &it, DBUS_TYPE_ARRAY, "{sv}", &opts );
    p_dbus_message_iter_close_container( &it, &opts );
    call_system( msg, NULL );
    p_dbus_message_unref( msg );

    get_properties( NM_DEST, dev, NM_WIFI, dprops, 1 );

    msg = p_dbus_message_new_method_call( NM_DEST, dev, NM_WIFI, "GetAllAccessPoints" );
    params->err = call_system( msg, &reply );
    p_dbus_message_unref( msg );
    if (params->err) return STATUS_SUCCESS;

    if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &it, &arr );
        while (p_dbus_message_iter_get_arg_type( &arr ) == DBUS_TYPE_OBJECT_PATH)
        {
            const char *path;
            char ssid[64] = "";
            unsigned char strength = 0;
            dbus_uint32_t flags = 0, wpa = 0, rsn = 0;
            struct prop ap[] = { { "Ssid", DBUS_TYPE_ARRAY, ssid, sizeof(ssid) }, { "Strength", DBUS_TYPE_BYTE, &strength, 0 },
                                 { "Flags", DBUS_TYPE_UINT32, &flags, 0 }, { "WpaFlags", DBUS_TYPE_UINT32, &wpa, 0 },
                                 { "RsnFlags", DBUS_TYPE_UINT32, &rsn, 0 } };
            DWORD i;

            p_dbus_message_iter_get_basic( &arr, &path );
            p_dbus_message_iter_next( &arr );
            if (get_properties( NM_DEST, path, NM_AP, ap, ARRAYSIZE(ap) ) || !ssid[0]) continue;

            for (i = 0; i < n; i++) if (!strcmp( out[i].ssid, ssid )) break;   /* same network, several access points */
            if (i < n)
            {
                if (strength > out[i].strength) out[i].strength = strength;
                if (!strcmp( path, active )) out[i].connected = TRUE;
                continue;
            }
            if (n >= params->max) continue;
            memset( &out[n], 0, sizeof(out[n]) );
            snprintf( out[n].ssid, sizeof(out[n].ssid), "%s", ssid );
            out[n].strength = strength;
            snprintf( out[n].security, sizeof(out[n].security), "%s", security_name( flags, wpa, rsn ) );
            out[n].secured = out[n].security[0] != 0;
            out[n].sae_only = (rsn & SEC_SAE) && !(rsn & SEC_PSK);
            out[n].connected = !strcmp( path, active );
            n++;
        }
    }
    p_dbus_message_unref( reply );
    params->count = n;
    return STATUS_SUCCESS;
}

static void append_str_var( DBusMessageIter *dict, const char *key, const char *value )
{
    DBusMessageIter entry, var;

    p_dbus_message_iter_open_container( dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry );
    p_dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &key );
    p_dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, "s", &var );
    p_dbus_message_iter_append_basic( &var, DBUS_TYPE_STRING, &value );
    p_dbus_message_iter_close_container( &entry, &var );
    p_dbus_message_iter_close_container( dict, &entry );
}

static void append_bytes_var( DBusMessageIter *dict, const char *key, const char *bytes, int len )
{
    DBusMessageIter entry, var, arr;

    p_dbus_message_iter_open_container( dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry );
    p_dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &key );
    p_dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, "ay", &var );
    p_dbus_message_iter_open_container( &var, DBUS_TYPE_ARRAY, "y", &arr );
    p_dbus_message_iter_append_fixed_array( &arr, DBUS_TYPE_BYTE, &bytes, len );
    p_dbus_message_iter_close_container( &var, &arr );
    p_dbus_message_iter_close_container( &entry, &var );
    p_dbus_message_iter_close_container( dict, &entry );
}

static void begin_section( DBusMessageIter *conn, DBusMessageIter *sec_entry, DBusMessageIter *sec_dict, const char *name )
{
    p_dbus_message_iter_open_container( conn, DBUS_TYPE_DICT_ENTRY, NULL, sec_entry );
    p_dbus_message_iter_append_basic( sec_entry, DBUS_TYPE_STRING, &name );
    p_dbus_message_iter_open_container( sec_entry, DBUS_TYPE_ARRAY, "{sv}", sec_dict );
}

static void end_section( DBusMessageIter *conn, DBusMessageIter *sec_entry, DBusMessageIter *sec_dict )
{
    p_dbus_message_iter_close_container( sec_entry, sec_dict );
    p_dbus_message_iter_close_container( conn, sec_entry );
}

/* Connect to a network: create a profile (with the passphrase when given) and activate it on the Wi-Fi device. */
static NTSTATUS net_connect_wifi( void *args )
{
    struct net_connect_wifi_params *params = args;
    char dev[128];
    DBusMessage *msg;
    DBusMessageIter it, conn, se, sd;
    const char *any = "/", *ssid = params->ssid, *devp = dev;

    params->ssid[sizeof(params->ssid) - 1] = 0;
    params->passphrase[sizeof(params->passphrase) - 1] = 0;
    if (!load_dbus() || !nm_first_wifi( dev, sizeof(dev) )) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }

    msg = p_dbus_message_new_method_call( NM_DEST, NM_PATH, NM_IFACE, "AddAndActivateConnection" );
    p_dbus_message_iter_init_append( msg, &it );
    p_dbus_message_iter_open_container( &it, DBUS_TYPE_ARRAY, "{sa{sv}}", &conn );

    begin_section( &conn, &se, &sd, "connection" );
    append_str_var( &sd, "id", ssid );
    append_str_var( &sd, "type", "802-11-wireless" );
    end_section( &conn, &se, &sd );

    begin_section( &conn, &se, &sd, "802-11-wireless" );
    append_bytes_var( &sd, "ssid", ssid, (int)strlen( ssid ) );
    end_section( &conn, &se, &sd );

    if (params->passphrase[0])
    {
        begin_section( &conn, &se, &sd, "802-11-wireless-security" );
        append_str_var( &sd, "key-mgmt", params->sae ? "sae" : "wpa-psk" );
        append_str_var( &sd, "psk", params->passphrase );
        end_section( &conn, &se, &sd );
    }
    p_dbus_message_iter_close_container( &it, &conn );
    p_dbus_message_iter_append_basic( &it, DBUS_TYPE_OBJECT_PATH, &devp );
    p_dbus_message_iter_append_basic( &it, DBUS_TYPE_OBJECT_PATH, &any );
    params->err = call_system( msg, NULL );
    p_dbus_message_unref( msg );
    memset( params->passphrase, 0, sizeof(params->passphrase) );
    return STATUS_SUCCESS;
}

static BOOL nm_primary_path( char *path, size_t size )
{
    struct nm_device devs[MAX_DEVICES];
    int count = nm_get_devices( devs, MAX_DEVICES ), i, pick = -1;

    for (i = 0; i < count; i++) if (devs[i].state == NM_DEVICE_STATE_ACTIVATED) { pick = i; break; }
    if (pick < 0 && count) pick = 0;
    if (pick < 0) return FALSE;
    snprintf( path, size, "%s", devs[pick].path );
    return TRUE;
}

static NTSTATUS net_disconnect( void *args )
{
    struct net_disconnect_params *params = args;
    char path[128];

    if (!load_dbus() || !nm_primary_path( path, sizeof(path) )) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
    params->err = nm_call_path( path, NM_DEV, "Disconnect" );
    return STATUS_SUCCESS;
}

/* "Enable": let NetworkManager activate the best connection on the device again. */
static NTSTATUS net_enable( void *args )
{
    struct net_enable_params *params = args;
    char path[128];
    DBusMessage *msg;
    const char *none = "/", *dev = path;

    if (!load_dbus() || !nm_primary_path( path, sizeof(path) )) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
    msg = p_dbus_message_new_method_call( NM_DEST, NM_PATH, NM_IFACE, "ActivateConnection" );
    p_dbus_message_append_args( msg, DBUS_TYPE_OBJECT_PATH, &none, DBUS_TYPE_OBJECT_PATH, &dev,
                                DBUS_TYPE_OBJECT_PATH, &none, DBUS_TYPE_INVALID );
    params->err = call_system( msg, NULL );
    p_dbus_message_unref( msg );
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ polkit authentication agent */

#define PK_DEST   "org.freedesktop.PolicyKit1"
#define PK_PATH   "/org/freedesktop/PolicyKit1/Authority"
#define PK_IFACE  "org.freedesktop.PolicyKit1.Authority"
#define AGENT_PATH "/org/wineexplorer/PolicyKit1/AuthenticationAgent"
#define AGENT_IFACE "org.freedesktop.PolicyKit1.AuthenticationAgent"
#define EVENT_QUEUE 16
#define MAX_PENDING 8

static DBusConnection *agent_conn;
static pthread_t agent_thread;
static BOOL agent_running;
static pthread_mutex_t agent_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t agent_cond = PTHREAD_COND_INITIALIZER;
static WEX_AGENT_EVENT event_queue[EVENT_QUEUE];
static unsigned int ev_head, ev_count;
static struct { char cookie[WEX_COOKIE_MAX]; DBusMessage *msg; } pending[MAX_PENDING];

static void push_event( const WEX_AGENT_EVENT *ev )   /* agent_mutex held */
{
    if (ev_count == EVENT_QUEUE) return;
    event_queue[(ev_head + ev_count++) % EVENT_QUEUE] = *ev;
    pthread_cond_signal( &agent_cond );
}

static void add_identity( WEX_AGENT_EVENT *ev, const char *name )
{
    DWORD i;
    for (i = 0; i < ev->identity_count; i++) if (!strcmp( ev->identities[i], name )) return;
    if (ev->identity_count < WEX_MAX_IDENTITIES)
        snprintf( ev->identities[ev->identity_count++], WEX_USER_MAX, "%s", name );
}

/* unix-user: {uid}; unix-group: {gid} expands to the group's members */
static void parse_identity( DBusMessageIter *st, WEX_AGENT_EVENT *ev )
{
    DBusMessageIter dict, entry, var;
    const char *kind;

    p_dbus_message_iter_get_basic( st, &kind );
    p_dbus_message_iter_next( st );
    if (p_dbus_message_iter_get_arg_type( st ) != DBUS_TYPE_ARRAY) return;
    p_dbus_message_iter_recurse( st, &dict );
    while (p_dbus_message_iter_get_arg_type( &dict ) == DBUS_TYPE_DICT_ENTRY)
    {
        const char *key;
        dbus_uint32_t id;
        p_dbus_message_iter_recurse( &dict, &entry );
        p_dbus_message_iter_get_basic( &entry, &key );
        p_dbus_message_iter_next( &entry );
        p_dbus_message_iter_recurse( &entry, &var );
        if (p_dbus_message_iter_get_arg_type( &var ) == DBUS_TYPE_UINT32)
        {
            p_dbus_message_iter_get_basic( &var, &id );
            if (!strcmp( kind, "unix-user" ) && !strcmp( key, "uid" ))
            {
                struct passwd *pw = getpwuid( id );
                if (pw) add_identity( ev, pw->pw_name );
            }
            else if (!strcmp( kind, "unix-group" ) && !strcmp( key, "gid" ))
            {
                struct group *gr = getgrgid( id );
                if (gr) for (char **m = gr->gr_mem; *m; m++) add_identity( ev, *m );
            }
        }
        p_dbus_message_iter_next( &dict );
    }
}

static void handle_begin( DBusMessage *msg )
{
    DBusMessageIter it, arr, st;
    WEX_AGENT_EVENT ev;
    const char *action, *message, *icon, *cookie = "";
    int i;

    memset( &ev, 0, sizeof(ev) );
    ev.type = WEX_EVENT_BEGIN;
    if (!p_dbus_message_iter_init( msg, &it )) goto bad;
    p_dbus_message_iter_get_basic( &it, &action );  p_dbus_message_iter_next( &it );
    p_dbus_message_iter_get_basic( &it, &message ); p_dbus_message_iter_next( &it );
    p_dbus_message_iter_get_basic( &it, &icon );    p_dbus_message_iter_next( &it );
    p_dbus_message_iter_next( &it );                                   /* details a{ss}: unused */
    p_dbus_message_iter_get_basic( &it, &cookie );  p_dbus_message_iter_next( &it );
    snprintf( ev.action_id, sizeof(ev.action_id), "%s", action );
    snprintf( ev.message, sizeof(ev.message), "%s", message );
    snprintf( ev.cookie, sizeof(ev.cookie), "%s", cookie );
    if (p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &it, &arr );
        while (p_dbus_message_iter_get_arg_type( &arr ) == DBUS_TYPE_STRUCT)
        {
            p_dbus_message_iter_recurse( &arr, &st );
            parse_identity( &st, &ev );
            p_dbus_message_iter_next( &arr );
        }
    }
    if (!ev.identity_count) goto bad;

    pthread_mutex_lock( &agent_mutex );
    for (i = 0; i < MAX_PENDING; i++) if (!pending[i].msg) break;
    if (i == MAX_PENDING) { pthread_mutex_unlock( &agent_mutex ); goto bad; }
    snprintf( pending[i].cookie, sizeof(pending[i].cookie), "%s", ev.cookie );
    pending[i].msg = p_dbus_message_ref( msg );
    push_event( &ev );
    pthread_mutex_unlock( &agent_mutex );
    return;

bad:
    {
        DBusMessage *r = p_dbus_message_new_error( msg, "org.freedesktop.PolicyKit1.Error.Failed", "cannot authenticate" );
        p_dbus_connection_send( agent_conn, r, NULL );
        p_dbus_message_unref( r );
    }
}

static void handle_cancel( DBusMessage *msg )
{
    DBusMessageIter it;
    const char *cookie;
    WEX_AGENT_EVENT ev;
    DBusMessage *r;

    if (p_dbus_message_iter_init( msg, &it ))
    {
        memset( &ev, 0, sizeof(ev) );
        ev.type = WEX_EVENT_CANCEL;
        p_dbus_message_iter_get_basic( &it, &cookie );
        snprintf( ev.cookie, sizeof(ev.cookie), "%s", cookie );
        pthread_mutex_lock( &agent_mutex );
        push_event( &ev );
        pthread_mutex_unlock( &agent_mutex );
    }
    r = p_dbus_message_new_method_return( msg );
    p_dbus_connection_send( agent_conn, r, NULL );
    p_dbus_message_unref( r );
}

static void *agent_loop( void *arg )
{
    while (agent_running)
    {
        DBusMessage *msg;
        p_dbus_connection_read_write( agent_conn, 200 );
        while ((msg = p_dbus_connection_pop_message( agent_conn )))
        {
            if (p_dbus_message_get_type( msg ) == DBUS_MESSAGE_TYPE_METHOD_CALL)
            {
                const char *iface = p_dbus_message_get_interface( msg ), *member = p_dbus_message_get_member( msg );
                if (iface && member && !strcmp( iface, AGENT_IFACE ))
                {
                    if (!strcmp( member, "BeginAuthentication" )) handle_begin( msg );
                    else if (!strcmp( member, "CancelAuthentication" )) handle_cancel( msg );
                }
            }
            p_dbus_message_unref( msg );
        }
    }
    return NULL;
}

/* "/org/freedesktop/login1/session/_31" -> "1": components escape non-alphanumerics as _xx */
static BOOL session_id_from_path( const char *path, char *out, size_t size )
{
    const char *p = strrchr( path, '/' );
    size_t n = 0;

    if (!p || !*++p) return FALSE;
    while (*p && n + 1 < size)
    {
        if (*p == '_' && p[1] && p[2]) { char hex[3] = { p[1], p[2], 0 }; out[n++] = (char)strtol( hex, NULL, 16 ); p += 3; }
        else out[n++] = *p++;
    }
    out[n] = 0;
    return n > 0;
}

static BOOL find_session_id( DBusConnection *conn, char *out, size_t size )
{
    const char *env = getenv( "XDG_SESSION_ID" );
    DBusMessage *msg, *reply;
    DBusError err;
    dbus_uint32_t pid = getpid();
    const char *path;
    BOOL ok = FALSE;

    if (env && *env) { snprintf( out, size, "%s", env ); return TRUE; }

    msg = p_dbus_message_new_method_call( "org.freedesktop.login1", "/org/freedesktop/login1",
                                          "org.freedesktop.login1.Manager", "GetSessionByPID" );
    p_dbus_message_append_args( msg, DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID );
    p_dbus_error_init( &err );
    reply = p_dbus_connection_send_with_reply_and_block( conn, msg, 5000, &err );
    p_dbus_message_unref( msg );
    if (!reply) { p_dbus_error_free( &err ); return FALSE; }
    {
        DBusMessageIter it;
        if (p_dbus_message_iter_init( reply, &it ) && p_dbus_message_iter_get_arg_type( &it ) == DBUS_TYPE_OBJECT_PATH)
        {
            p_dbus_message_iter_get_basic( &it, &path );
            ok = session_id_from_path( path, out, size );
        }
    }
    p_dbus_message_unref( reply );
    return ok;
}

static NTSTATUS agent_start( void *args )
{
    struct agent_start_params *params = args;
    DBusError err;
    DBusMessage *msg, *reply;
    DBusMessageIter it, st, arr, dict, var;
    char session[64];
    const char *kind = "unix-session", *key = "session-id", *locale = "en_US.UTF-8", *objpath = AGENT_PATH, *sid = session;

    params->err = ERROR_SUCCESS;
    if (agent_running) return STATUS_SUCCESS;
    if (!load_dbus()) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }

    p_dbus_error_init( &err );
    if (!(agent_conn = p_dbus_bus_get_private( DBUS_BUS_SYSTEM, &err )))
    {
        params->err = map_error( &err );
        p_dbus_error_free( &err );
        return STATUS_SUCCESS;
    }
    p_dbus_connection_set_exit_on_disconnect( agent_conn, FALSE );

    if (!find_session_id( agent_conn, session, sizeof(session)))
    {
        WARN( "no login session found, cannot register as polkit agent\n" );
        params->err = ERROR_NOT_SUPPORTED;
        goto fail;
    }

    msg = p_dbus_message_new_method_call( PK_DEST, PK_PATH, PK_IFACE, "RegisterAuthenticationAgent" );
    p_dbus_message_iter_init_append( msg, &it );
    p_dbus_message_iter_open_container( &it, DBUS_TYPE_STRUCT, NULL, &st );
    p_dbus_message_iter_append_basic( &st, DBUS_TYPE_STRING, &kind );
    p_dbus_message_iter_open_container( &st, DBUS_TYPE_ARRAY, "{sv}", &arr );
    p_dbus_message_iter_open_container( &arr, DBUS_TYPE_DICT_ENTRY, NULL, &dict );
    p_dbus_message_iter_append_basic( &dict, DBUS_TYPE_STRING, &key );
    p_dbus_message_iter_open_container( &dict, DBUS_TYPE_VARIANT, "s", &var );
    p_dbus_message_iter_append_basic( &var, DBUS_TYPE_STRING, &sid );
    p_dbus_message_iter_close_container( &dict, &var );
    p_dbus_message_iter_close_container( &arr, &dict );
    p_dbus_message_iter_close_container( &st, &arr );
    p_dbus_message_iter_close_container( &it, &st );
    p_dbus_message_append_args( msg, DBUS_TYPE_STRING, &locale, DBUS_TYPE_STRING, &objpath, DBUS_TYPE_INVALID );

    p_dbus_error_init( &err );
    reply = p_dbus_connection_send_with_reply_and_block( agent_conn, msg, 10000, &err );
    p_dbus_message_unref( msg );
    if (!reply)
    {
        WARN( "RegisterAuthenticationAgent failed: %s\n", err.message );
        params->err = map_error( &err );
        p_dbus_error_free( &err );
        goto fail;
    }
    p_dbus_message_unref( reply );

    agent_running = TRUE;
    pthread_create( &agent_thread, NULL, agent_loop, NULL );
    TRACE( "polkit agent registered for session %s\n", session );
    return STATUS_SUCCESS;

fail:
    p_dbus_connection_close( agent_conn );
    p_dbus_connection_unref( agent_conn );
    agent_conn = NULL;
    return STATUS_SUCCESS;
}

static NTSTATUS agent_next_event( void *args )
{
    struct agent_next_event_params *params = args;

    pthread_mutex_lock( &agent_mutex );
    while (!ev_count && agent_running) pthread_cond_wait( &agent_cond, &agent_mutex );
    if (ev_count)
    {
        params->event = event_queue[ev_head];
        ev_head = (ev_head + 1) % EVENT_QUEUE;
        ev_count--;
    }
    else
    {
        memset( &params->event, 0, sizeof(params->event) );
        params->event.type = WEX_EVENT_STOP;
    }
    pthread_mutex_unlock( &agent_mutex );
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}

static const char *helper_path( void )
{
    static const char *const paths[] = { "/usr/lib/polkit-1/polkit-agent-helper-1",
                                         "/usr/libexec/polkit-1/polkit-agent-helper-1",
                                         "/usr/lib/policykit-1/polkit-agent-helper-1" };
    unsigned int i;
    for (i = 0; i < ARRAYSIZE(paths); i++) if (!access( paths[i], X_OK )) return paths[i];
    return NULL;
}

/* polkit-agent-helper-1 speaks a line protocol: we send the cookie, it asks PAM prompts
 * ("PAM_PROMPT_ECHO_OFF text"), we answer with the password, it ends with SUCCESS or FAILURE and, on
 * success, tells polkitd itself. Returns TRUE if authorized. */
static BOOL run_helper( const char *user, const char *cookie, const char *password )
{
    const char *helper = helper_path();
    int to_child[2], from_child[2];
    pid_t pid;
    char line[512];
    size_t len = 0;
    BOOL ok = FALSE, answered = FALSE, done = FALSE;

    if (!helper || pipe( to_child ) || pipe( from_child )) return FALSE;
    if ((pid = fork()) < 0) return FALSE;
    if (!pid)
    {
        dup2( to_child[0], 0 );
        dup2( from_child[1], 1 );
        close( to_child[0] ); close( to_child[1] ); close( from_child[0] ); close( from_child[1] );
        execl( helper, helper, user, (char *)NULL );
        _exit( 127 );
    }
    close( to_child[0] );
    close( from_child[1] );

    dprintf( to_child[1], "%s\n", cookie );
    while (!done)
    {
        struct pollfd pfd = { from_child[0], POLLIN, 0 };
        char c;
        ssize_t n;

        if (poll( &pfd, 1, 60000 ) <= 0) break;
        if ((n = read( from_child[0], &c, 1 )) <= 0) break;
        if (c != '\n') { if (len < sizeof(line) - 1) line[len++] = c; continue; }
        line[len] = 0; len = 0;

        if (!strncmp( line, "PAM_PROMPT_ECHO_OFF", 19 ) || !strncmp( line, "PAM_PROMPT_ECHO_ON", 18 ))
        {
            if (answered) break;                                  /* asked again: wrong password, let it fail */
            dprintf( to_child[1], "%s\n", password );
            answered = TRUE;
        }
        else if (!strcmp( line, "SUCCESS" )) { ok = TRUE; done = TRUE; }
        else if (!strcmp( line, "FAILURE" )) done = TRUE;
    }
    close( to_child[1] );
    close( from_child[0] );
    waitpid( pid, NULL, 0 );
    return ok;
}

static void finish_pending( int i, const char *error_name )
{
    DBusMessage *r;
    if (error_name) r = p_dbus_message_new_error( pending[i].msg, error_name, "cancelled" );
    else r = p_dbus_message_new_method_return( pending[i].msg );
    p_dbus_connection_send( agent_conn, r, NULL );
    p_dbus_connection_flush( agent_conn );
    p_dbus_message_unref( r );
    p_dbus_message_unref( pending[i].msg );
    pending[i].msg = NULL;
}

static NTSTATUS agent_respond( void *args )
{
    struct agent_respond_params *params = args;
    int i;

    params->cookie[WEX_COOKIE_MAX - 1] = 0;
    params->user[WEX_USER_MAX - 1] = 0;
    params->password[WEX_PASSWORD_MAX - 1] = 0;

    pthread_mutex_lock( &agent_mutex );
    for (i = 0; i < MAX_PENDING; i++) if (pending[i].msg && !strcmp( pending[i].cookie, params->cookie )) break;
    pthread_mutex_unlock( &agent_mutex );
    if (i == MAX_PENDING) { params->err = ERROR_NOT_FOUND; return STATUS_SUCCESS; }

    if (params->cancel)
    {
        finish_pending( i, "org.freedesktop.PolicyKit1.Error.Cancelled" );
        params->err = ERROR_CANCELLED;
    }
    else if (run_helper( params->user, params->cookie, params->password ))
    {
        finish_pending( i, NULL );
        params->err = ERROR_SUCCESS;
    }
    else params->err = ERROR_ACCESS_DENIED;        /* leave it pending so the dialog can retry */

    memset( params->password, 0, sizeof(params->password) );
    return STATUS_SUCCESS;
}

static NTSTATUS agent_stop( void *args )
{
    struct agent_stop_params *params = args;
    int i;

    params->err = ERROR_SUCCESS;
    if (!agent_running) return STATUS_SUCCESS;
    pthread_mutex_lock( &agent_mutex );
    agent_running = FALSE;
    pthread_cond_broadcast( &agent_cond );
    pthread_mutex_unlock( &agent_mutex );
    pthread_join( agent_thread, NULL );
    for (i = 0; i < MAX_PENDING; i++) if (pending[i].msg) finish_pending( i, "org.freedesktop.PolicyKit1.Error.Cancelled" );
    p_dbus_connection_close( agent_conn );
    p_dbus_connection_unref( agent_conn );
    agent_conn = NULL;
    return STATUS_SUCCESS;
}

#else /* SONAME_LIBDBUS_1 */

#define STUB( name, params_type ) static NTSTATUS name( void *args ) { ((params_type *)args)->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
STUB( unix_init_impl, struct init_params )
STUB( timedate_get, struct timedate_get_params )
STUB( timedate_set_time, struct timedate_set_time_params )
STUB( timedate_set_tz, struct timedate_set_tz_params )
STUB( timedate_set_ntp, struct timedate_set_ntp_params )
STUB( timedate_list_tz, struct timedate_list_tz_params )
STUB( agent_start, struct agent_start_params )
STUB( agent_next_event, struct agent_next_event_params )
STUB( agent_respond, struct agent_respond_params )
STUB( agent_stop, struct agent_stop_params )
STUB( power_can, struct power_can_params )
STUB( power_do, struct power_do_params )
STUB( battery_get, struct battery_get_params )
STUB( hostname_get, struct hostname_get_params )
STUB( hostname_set, struct hostname_set_params )
STUB( user_get, struct user_get_params )
STUB( net_get_status, struct net_get_status_params )
STUB( net_scan_wifi, struct net_scan_wifi_params )
STUB( net_connect_wifi, struct net_connect_wifi_params )
STUB( net_disconnect, struct net_disconnect_params )
STUB( net_enable, struct net_enable_params )
STUB( users_list, struct users_list_params )
STUB( locale_set, struct locale_set_params )
STUB( root_pass_set, struct root_pass_set_params )
STUB( user_create, struct user_create_params )
STUB( user_set_session, struct user_set_session_params )

#endif

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    unix_init_impl,
    timedate_get,
    timedate_set_time,
    timedate_set_tz,
    timedate_set_ntp,
    timedate_list_tz,
    agent_start,
    agent_next_event,
    agent_respond,
    agent_stop,
    power_can,
    power_do,
    battery_get,
    hostname_get,
    hostname_set,
    user_get,
    net_get_status,
    net_scan_wifi,
    net_connect_wifi,
    net_disconnect,
    net_enable,
    display_get,
    display_set,
    wm_start,
    wm_next_event,
    wm_manage,
    wm_configure,
    wm_set_state,
    wm_close,
    wm_release,
    wm_get_title,
    wm_get_icon,
    wm_stop,
    wm_focus,
    apps_list,
    apps_launch,
    hw_list,
    users_list,
    greeter_connect,
    greeter_authenticate,
    greeter_respond,
    greeter_cancel,
    greeter_start_session,
    greeter_read,
    composition_set,
    composition_get,
    composition_shadow,
    locale_set,
    root_pass_set,
    user_create,
    user_set_session,
    wm_start_ex,
    wm_capabilities,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );

#ifdef _WIN64
/* The bridge ABI uses fixed-width scalars and ULONG64 pointer slots, so both
 * PE architectures use the same entry points without pointer conversion. */
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    unix_init_impl,
    timedate_get,
    timedate_set_time,
    timedate_set_tz,
    timedate_set_ntp,
    timedate_list_tz,
    agent_start,
    agent_next_event,
    agent_respond,
    agent_stop,
    power_can,
    power_do,
    battery_get,
    hostname_get,
    hostname_set,
    user_get,
    net_get_status,
    net_scan_wifi,
    net_connect_wifi,
    net_disconnect,
    net_enable,
    display_get,
    display_set,
    wm_start,
    wm_next_event,
    wm_manage,
    wm_configure,
    wm_set_state,
    wm_close,
    wm_release,
    wm_get_title,
    wm_get_icon,
    wm_stop,
    wm_focus,
    apps_list,
    apps_launch,
    hw_list,
    users_list,
    greeter_connect,
    greeter_authenticate,
    greeter_respond,
    greeter_cancel,
    greeter_start_session,
    greeter_read,
    composition_set,
    composition_get,
    composition_shadow,
    locale_set,
    root_pass_set,
    user_create,
    user_set_session,
    wm_start_ex,
    wm_capabilities,
};
C_ASSERT( ARRAYSIZE(__wine_unix_call_wow64_funcs) == unix_funcs_count );
#endif
