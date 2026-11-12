/*
 * Host display modes in a Wayland session: the compositor's outputs through wlr-output-management (wexcomp, as
 * any wlroots compositor, implements it). XRandR there only reaches XWayland's emulated outputs.
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if 0
#pragma makedep unix
#endif
#include "config.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include "wexhost_unix.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

#define MAX_HEADS 8

struct out_mode
{
    struct zwlr_output_mode_v1 *obj;
    int width, height, refresh;   /* refresh in millihertz, as WEX_DISPLAY_MODE */
};

struct out_head
{
    struct zwlr_output_head_v1 *obj;
    char name[64];
    int enabled;
    struct zwlr_output_mode_v1 *current;
    unsigned int count;
    struct out_mode modes[WEX_DISPLAY_MAX_MODES];
};

struct outputs
{
    struct wl_display *display;
    struct wl_registry *registry;
    struct zwlr_output_manager_v1 *manager;
    unsigned int count;
    struct out_head heads[MAX_HEADS];
    uint32_t serial;
    int done;     /* the manager's done event: the state is complete */
    int result;   /* a configuration: 0 waiting, 1 succeeded, -1 failed or cancelled */
};

/* ---- zwlr_output_mode_v1 ---- */

static void mode_size( void *data, struct zwlr_output_mode_v1 *obj, int32_t width, int32_t height )
{
    struct out_mode *mode = data;
    if (mode) { mode->width = width; mode->height = height; }
}

static void mode_refresh( void *data, struct zwlr_output_mode_v1 *obj, int32_t refresh )
{
    struct out_mode *mode = data;
    if (mode) mode->refresh = refresh;
}

static void mode_preferred( void *data, struct zwlr_output_mode_v1 *obj ) { }

static void mode_finished( void *data, struct zwlr_output_mode_v1 *obj )
{
    struct out_mode *mode = data;
    if (mode) mode->obj = NULL;
}

static const struct zwlr_output_mode_v1_listener mode_listener = { mode_size, mode_refresh, mode_preferred,
                                                                   mode_finished };

/* ---- zwlr_output_head_v1 ---- */

static void head_name( void *data, struct zwlr_output_head_v1 *obj, const char *name )
{
    struct out_head *head = data;
    snprintf( head->name, sizeof(head->name), "%s", name );
}

static void head_description( void *data, struct zwlr_output_head_v1 *obj, const char *text ) { }
static void head_physical_size( void *data, struct zwlr_output_head_v1 *obj, int32_t w, int32_t h ) { }

static void head_mode( void *data, struct zwlr_output_head_v1 *obj, struct zwlr_output_mode_v1 *mode_obj )
{
    struct out_head *head = data;
    struct out_mode *mode = NULL;

    if (head->count < WEX_DISPLAY_MAX_MODES)
    {
        mode = &head->modes[head->count++];
        mode->obj = mode_obj;
    }
    zwlr_output_mode_v1_add_listener( mode_obj, &mode_listener, mode );
}

static void head_enabled( void *data, struct zwlr_output_head_v1 *obj, int32_t enabled )
{
    struct out_head *head = data;
    head->enabled = enabled;
}

static void head_current_mode( void *data, struct zwlr_output_head_v1 *obj, struct zwlr_output_mode_v1 *mode )
{
    struct out_head *head = data;
    head->current = mode;
}

static void head_position( void *data, struct zwlr_output_head_v1 *obj, int32_t x, int32_t y ) { }
static void head_transform( void *data, struct zwlr_output_head_v1 *obj, int32_t transform ) { }
static void head_scale( void *data, struct zwlr_output_head_v1 *obj, wl_fixed_t scale ) { }

static void head_finished( void *data, struct zwlr_output_head_v1 *obj )
{
    struct out_head *head = data;
    head->obj = NULL;
    head->enabled = 0;
}

static void head_text( void *data, struct zwlr_output_head_v1 *obj, const char *text ) { }
static void head_adaptive_sync( void *data, struct zwlr_output_head_v1 *obj, uint32_t state ) { }

static const struct zwlr_output_head_v1_listener head_listener =
{
    head_name, head_description, head_physical_size, head_mode, head_enabled, head_current_mode, head_position,
    head_transform, head_scale, head_finished, head_text, head_text, head_text, head_adaptive_sync
};

/* ---- zwlr_output_manager_v1 ---- */

static void manager_head( void *data, struct zwlr_output_manager_v1 *obj, struct zwlr_output_head_v1 *head_obj )
{
    struct outputs *out = data;

    if (out->count >= MAX_HEADS)
    {
        zwlr_output_head_v1_destroy( head_obj );
        return;
    }
    memset( &out->heads[out->count], 0, sizeof(out->heads[0]) );
    out->heads[out->count].obj = head_obj;
    zwlr_output_head_v1_add_listener( head_obj, &head_listener, &out->heads[out->count] );
    out->count++;
}

static void manager_done( void *data, struct zwlr_output_manager_v1 *obj, uint32_t serial )
{
    struct outputs *out = data;
    out->serial = serial;
    out->done = 1;
}

static void manager_finished( void *data, struct zwlr_output_manager_v1 *obj ) { }

static const struct zwlr_output_manager_v1_listener manager_listener = { manager_head, manager_done,
                                                                         manager_finished };

static void registry_global( void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                             uint32_t version )
{
    struct outputs *out = data;

    if (!out->manager && !strcmp( interface, zwlr_output_manager_v1_interface.name ))
    {
        out->manager = wl_registry_bind( registry, name, &zwlr_output_manager_v1_interface, version < 4 ? version : 4 );
        zwlr_output_manager_v1_add_listener( out->manager, &manager_listener, out );
    }
}

static void registry_global_remove( void *data, struct wl_registry *registry, uint32_t name ) { }

static const struct wl_registry_listener registry_listener = { registry_global, registry_global_remove };

/* ---- zwlr_output_configuration_v1 ---- */

static void config_succeeded( void *data, struct zwlr_output_configuration_v1 *obj )
{
    struct outputs *out = data;
    out->result = 1;
}

static void config_failed( void *data, struct zwlr_output_configuration_v1 *obj )
{
    struct outputs *out = data;
    out->result = -1;
}

static const struct zwlr_output_configuration_v1_listener config_listener = { config_succeeded, config_failed,
                                                                              config_failed };

static void disconnect( struct outputs *out )
{
    unsigned int i, j;

    /* client-side proxies only: the connection ends next */
    for (i = 0; i < out->count; i++)
    {
        for (j = 0; j < out->heads[i].count; j++)
            if (out->heads[i].modes[j].obj) zwlr_output_mode_v1_destroy( out->heads[i].modes[j].obj );
        if (out->heads[i].obj) zwlr_output_head_v1_destroy( out->heads[i].obj );
    }
    if (out->manager) zwlr_output_manager_v1_destroy( out->manager );
    if (out->registry) wl_registry_destroy( out->registry );
    wl_display_disconnect( out->display );
}

/* Connects to the session's compositor and reads its outputs. FALSE when this is no Wayland session or the
 * compositor has no output manager: then the caller uses XRandR. */
static BOOL connect_outputs( struct outputs *out )
{
    int i;

    memset( out, 0, sizeof(*out) );
    if (!getenv( "WAYLAND_DISPLAY" ) || !(out->display = wl_display_connect( NULL ))) return FALSE;
    out->registry = wl_display_get_registry( out->display );
    wl_registry_add_listener( out->registry, &registry_listener, out );
    for (i = 0; i < 5 && !out->done; i++)
        if (wl_display_roundtrip( out->display ) < 0) break;
    if (!out->manager || !out->done)
    {
        disconnect( out );
        return FALSE;
    }
    return TRUE;
}

/* The one enabled output, or NULL: as with XRandR, more than one is a separate feature, and none is changed then */
static struct out_head *single_output( struct outputs *out )
{
    struct out_head *found = NULL;
    unsigned int i;

    for (i = 0; i < out->count; i++)
    {
        if (!out->heads[i].obj || !out->heads[i].enabled) continue;
        if (found) return NULL;
        found = &out->heads[i];
    }
    return found;
}

static WEX_DISPLAY_MODE describe( const struct out_mode *mode )
{
    WEX_DISPLAY_MODE value = { mode->width, mode->height, mode->refresh };
    return value;
}

/* TRUE when this is a Wayland session with an output manager; *err is then the answer */
BOOL wl_outputs_get( WEX_DISPLAY *display, DWORD *err )
{
    struct outputs *out;
    struct out_head *head;
    unsigned int i;

    memset( display, 0, sizeof(*display) );
    if (!(out = calloc( 1, sizeof(*out) ))) return FALSE;
    if (!connect_outputs( out )) { free( out ); return FALSE; }
    *err = ERROR_NOT_SUPPORTED;
    if ((head = single_output( out )))
    {
        snprintf( display->output, sizeof(display->output), "%s", head->name );
        *err = ERROR_SUCCESS;
        for (i = 0; i < head->count; i++)
        {
            if (!head->modes[i].obj || !head->modes[i].width || !head->modes[i].height || !head->modes[i].refresh)
                continue;
            if (display->count == WEX_DISPLAY_MAX_MODES) { *err = ERROR_MORE_DATA; break; }
            display->modes[display->count++] = describe( &head->modes[i] );
            if (head->modes[i].obj == head->current) display->current = describe( &head->modes[i] );
        }
        if (!*err && (!display->count || !display->current.refresh)) *err = ERROR_NOT_SUPPORTED;
    }
    disconnect( out );
    free( out );
    return TRUE;
}

/* TRUE when this is a Wayland session with an output manager; *err is then the answer. The mode must be one of the
 * output's own, as WexDisplayGet lists them. */
BOOL wl_outputs_set( const WEX_DISPLAY_MODE *mode, DWORD *err )
{
    struct zwlr_output_configuration_v1 *config;
    struct zwlr_output_configuration_head_v1 *config_head;
    struct outputs *out;
    struct out_head *head;
    struct out_mode *target = NULL;
    unsigned int i;

    if (!(out = calloc( 1, sizeof(*out) ))) return FALSE;
    if (!connect_outputs( out )) { free( out ); return FALSE; }
    *err = ERROR_NOT_SUPPORTED;
    if (!(head = single_output( out ))) goto done;
    *err = ERROR_INVALID_PARAMETER;
    for (i = 0; i < head->count; i++)
    {
        WEX_DISPLAY_MODE value = describe( &head->modes[i] );
        if (!head->modes[i].obj || memcmp( &value, mode, sizeof(value) )) continue;
        target = &head->modes[i];
        break;
    }
    if (!target) goto done;
    *err = ERROR_SUCCESS;
    if (target->obj == head->current) goto done;

    /* a configuration names every head: the one to change, the others as they are */
    config = zwlr_output_manager_v1_create_configuration( out->manager, out->serial );
    zwlr_output_configuration_v1_add_listener( config, &config_listener, out );
    for (i = 0; i < out->count; i++)
    {
        if (!out->heads[i].obj) continue;
        if (&out->heads[i] == head)
        {
            config_head = zwlr_output_configuration_v1_enable_head( config, head->obj );
            zwlr_output_configuration_head_v1_set_mode( config_head, target->obj );
        }
        else if (out->heads[i].enabled) zwlr_output_configuration_v1_enable_head( config, out->heads[i].obj );
        else zwlr_output_configuration_v1_disable_head( config, out->heads[i].obj );
    }
    zwlr_output_configuration_v1_apply( config );
    for (i = 0; i < 10 && !out->result; i++)
        if (wl_display_roundtrip( out->display ) < 0) break;
    zwlr_output_configuration_v1_destroy( config );
    if (out->result != 1) *err = ERROR_GEN_FAILURE;
done:
    disconnect( out );
    free( out );
    return TRUE;
}
