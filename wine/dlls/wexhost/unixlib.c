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
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );
