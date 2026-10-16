/*
 * WineExplorer host services - Linux programs (.desktop entries) through GIO
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

/* GIO reads the freedesktop menu entries (localized names, OnlyShowIn/NotShowIn for XDG_CURRENT_DESKTOP, Hidden,
 * NoDisplay, TryExec) and launches them (Exec field codes, Terminal=true, D-Bus activation) as GNOME and others do.
 * It is loaded at run time, so there is no build dependency; without it the calls return ERROR_NOT_SUPPORTED. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL(wexhost);

typedef struct list_node { void *data; struct list_node *next, *prev; } GList;

static GList *(*p_g_app_info_get_all)(void);
static const char *(*p_g_app_info_get_id)(void *);
static const char *(*p_g_app_info_get_display_name)(void *);
static const char *(*p_g_app_info_get_description)(void *);
static int (*p_g_app_info_should_show)(void *);
static const char *(*p_g_desktop_app_info_get_categories)(void *);
static void *(*p_g_app_info_get_icon)(void *);
static char *(*p_g_icon_to_string)(void *);
static void *(*p_g_desktop_app_info_new)(const char *);
static int (*p_g_app_info_launch)(void *, GList *, void *, void **);
static void *(*p_g_app_launch_context_new)(void);
static void (*p_g_app_launch_context_setenv)(void *, const char *, const char *);
static void (*p_g_app_launch_context_unsetenv)(void *, const char *);
static void (*p_g_object_unref)(void *);
static void (*p_g_list_free)(GList *);
static void (*p_g_free)(void *);
static void (*p_g_error_free)(void *);

static pthread_once_t once = PTHREAD_ONCE_INIT;
static BOOL loaded;

static void load_gio(void)
{
    void *gio = dlopen( "libgio-2.0.so.0", RTLD_NOW ), *gobject = dlopen( "libgobject-2.0.so.0", RTLD_NOW );
    void *glib = dlopen( "libglib-2.0.so.0", RTLD_NOW );

    if (!gio || !gobject || !glib) goto failed;
#define LOAD(lib, name) if (!(p_##name = dlsym( lib, #name ))) goto failed;
    LOAD( gio, g_app_info_get_all )
    LOAD( gio, g_app_info_get_id )
    LOAD( gio, g_app_info_get_display_name )
    LOAD( gio, g_app_info_get_description )
    LOAD( gio, g_app_info_should_show )
    LOAD( gio, g_desktop_app_info_get_categories )
    LOAD( gio, g_app_info_get_icon )
    LOAD( gio, g_icon_to_string )
    LOAD( gio, g_desktop_app_info_new )
    LOAD( gio, g_app_info_launch )
    LOAD( gio, g_app_launch_context_new )
    LOAD( gio, g_app_launch_context_setenv )
    LOAD( gio, g_app_launch_context_unsetenv )
    LOAD( gobject, g_object_unref )
    LOAD( glib, g_list_free )
    LOAD( glib, g_free )
    LOAD( glib, g_error_free )
#undef LOAD
    loaded = TRUE;
    return;
failed:
    WARN( "GIO application services unavailable: %s\n", dlerror() );
    if (glib) dlclose( glib );
    if (gobject) dlclose( gobject );
    if (gio) dlclose( gio );
}

/* a PNG for an icon name from the hicolor theme or pixmaps (no SVG renderer here) */
static BOOL find_icon( const char *icon, char *path, size_t size )
{
    static const char *const sizes[] = { "48x48", "32x32", "64x64", "128x128", "256x256", "24x24", "16x16" };
    const char *home = getenv( "HOME" ), *bases[2];
    char name[256];
    unsigned int i, j;
    size_t len;

    if (!icon || !*icon) return FALSE;
    if (!strncmp( icon, ". GThemedIcon ", 14 ))   /* several names: the first one */
    {
        snprintf( name, sizeof(name), "%s", icon + 14 );
        name[strcspn( name, " " )] = 0;
    }
    else snprintf( name, sizeof(name), "%s", icon );
    if (name[0] == '/')
    {
        len = strlen( name );
        if (len < 4 || strcmp( name + len - 4, ".png" ) || access( name, R_OK )) return FALSE;
        snprintf( path, size, "%s", name );
        return TRUE;
    }
    bases[0] = home ? home : "";
    bases[1] = "";
    for (j = 0; j < 2; j++)
        for (i = 0; i < ARRAYSIZE(sizes); i++)
        {
            snprintf( path, size, "%s%s/icons/hicolor/%s/apps/%s.png", bases[j],
                      j ? "/usr/share" : "/.local/share", sizes[i], name );
            if (!access( path, R_OK )) return TRUE;
        }
    snprintf( path, size, "/usr/share/pixmaps/%s.png", name );
    return !access( path, R_OK );
}

static void append_field( char **out, char *end, const char *text, char separator )
{
    while (text && *text && *out < end - 1)
    {
        *(*out)++ = (*text == '\t' || *text == '\n' || *text == '\r') ? ' ' : *text;
        text++;
    }
    if (*out < end) *(*out)++ = separator;
}

/* One line per program: id, name, comment, categories, icon file (UTF-8, tab separated) */
NTSTATUS apps_list( void *args )
{
    struct apps_list_params *params = args;
    char *buffer = (char *)(ULONG_PTR)params->buffer, *out, *end, line[8192], icon_path[1024];
    GList *apps, *entry;
    size_t needed = 1;

    params->needed = 0;
    pthread_once( &once, load_gio );
    if (!loaded) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }

    apps = p_g_app_info_get_all();
    for (entry = apps; entry; entry = entry->next)
    {
        void *app = entry->data, *icon;
        const char *id = p_g_app_info_get_id( app );
        char *icon_name = NULL;

        if (id && *id && strlen( id ) < WEX_APP_ID_MAX && p_g_app_info_should_show( app ))
        {
            out = line;
            end = line + sizeof(line);
            icon = p_g_app_info_get_icon( app );
            if (icon) icon_name = p_g_icon_to_string( icon );
            /* Reserve the remaining separators even for unusually large metadata. */
            append_field( &out, end - 4, id, '\t' );
            append_field( &out, end - 3, p_g_app_info_get_display_name( app ), '\t' );
            append_field( &out, end - 2, p_g_app_info_get_description( app ), '\t' );
            append_field( &out, end - 1, p_g_desktop_app_info_get_categories( app ), '\t' );
            append_field( &out, end, find_icon( icon_name, icon_path, sizeof(icon_path) ) ? icon_path : "", '\n' );
            if (buffer && needed - 1 + (out - line) < params->size) memcpy( buffer + needed - 1, line, out - line );
            needed += out - line;
            if (icon_name) p_g_free( icon_name );
        }
        p_g_object_unref( app );
    }
    p_g_list_free( apps );

    params->needed = needed;
    if (!buffer || needed > params->size) params->err = ERROR_MORE_DATA;
    else
    {
        buffer[needed - 1] = 0;
        params->err = ERROR_SUCCESS;
    }
    return STATUS_SUCCESS;
}

/* Linux programs start without Wine's environment: WINEPREFIX, WINEDLLOVERRIDES and the like would change any
 * Wine or Proton they start in turn. */
NTSTATUS apps_launch( void *args )
{
    struct apps_launch_params *params = args;
    extern char **environ;
    void *app, *context, *error = NULL;
    char *name, wayland[108], xdisplay[128];
    char **env;

    pthread_once( &once, load_gio );
    if (!loaded) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
    params->id[sizeof(params->id) - 1] = 0;
    if (!(app = p_g_desktop_app_info_new( params->id ))) { params->err = ERROR_FILE_NOT_FOUND; return STATUS_SUCCESS; }

    context = p_g_app_launch_context_new();
    for (env = environ; *env; env++)
    {
        if (strncmp( *env, "WINE", 4 ) && strncmp( *env, "WEX_", 4 )) continue;
        if (!(name = strndup( *env, strcspn( *env, "=" ) )))
        {
            p_g_object_unref( context );
            p_g_object_unref( app );
            params->err = ERROR_NOT_ENOUGH_MEMORY;
            return STATUS_SUCCESS;
        }
        p_g_app_launch_context_unsetenv( context, name );
        free( name );
    }
    /* The shell connects to the privileged socket. Native applications must use
     * the compositor's ordinary socket, including before any native window exists. */
    if (wayland_wm_native_environment( wayland, xdisplay ))
    {
        p_g_app_launch_context_setenv( context, "WAYLAND_DISPLAY", wayland );
        if (xdisplay[0]) p_g_app_launch_context_setenv( context, "DISPLAY", xdisplay );
        else p_g_app_launch_context_unsetenv( context, "DISPLAY" );
    }
    if (p_g_app_info_launch( app, NULL, context, &error )) params->err = ERROR_SUCCESS;
    else
    {
        WARN( "cannot start %s\n", params->id );
        params->err = ERROR_GEN_FAILURE;
        if (error) p_g_error_free( error );
    }
    p_g_object_unref( context );
    p_g_object_unref( app );
    return STATUS_SUCCESS;
}
