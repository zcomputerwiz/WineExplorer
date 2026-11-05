/*
 * WineExplorer Desktop Environment - move the Wayland pointer (headless hover helper)
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

/* The p5 Wayland harness has no mouse: this client creates a wlroots virtual pointer and
 * moves the compositor's pointer to absolute positions so a taskbar button (and then the
 * preview popup above it) can be hovered. A plain wl_pointer client cannot move the pointer
 * (motion is an event, not a request), so we use zwlr_virtual_pointer_v1.motion_absolute,
 * which takes x,y in global output space plus the output extent. With the single 1280x720
 * headless output at (0,0), extent 1280x720 maps 1:1 to global coordinates.
 *
 * Each argument is "x,y,ms": move there, then wait ms. The pointer stays on the last
 * position. A "click" prefix ("click:x,y,ms") presses and releases button 1 there first.
 * Usage: wayland-pointer-move x,y,ms [x,y,ms ...]
 * Build: gcc -o wayland-pointer-move wayland-pointer-move.c $(pkg-config --cflags --libs wayland-client) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

/* ---- zwlr_virtual_pointer_v1 (hand-written; no generated header needed) ---- */
/* request order: 0 motion, 1 motion_absolute, 2 button, 3 axis, 4 frame,
 *                5 axis_source, 6 axis_stop, 7 axis_discrete, 8 destroy */
static const struct wl_interface zwlr_virtual_pointer_v1_interface;

/* per-request object-argument interface tables (indexed by argument position) */
static const struct wl_interface *vp_motion_types[] = { NULL, NULL, NULL };
static const struct wl_interface *vp_motion_abs_types[] = { NULL, NULL, NULL, NULL, NULL };
static const struct wl_interface *vp_button_types[] = { NULL, NULL, NULL };
static const struct wl_interface *vp_axis_types[] = { NULL, NULL };
static const struct wl_interface *vp_frame_types[] = { NULL };
static const struct wl_interface *vp_axis_source_types[] = { NULL };
static const struct wl_interface *vp_axis_stop_types[] = { NULL };
static const struct wl_interface *vp_axis_discrete_types[] = { NULL, NULL };
static const struct wl_interface *vp_destroy_types[] = { NULL };

static const struct wl_message zwlr_vp_requests[] = {
    { "motion", "uff", vp_motion_types },
    { "motion_absolute", "uuuuu", vp_motion_abs_types },
    { "button", "uuu", vp_button_types },
    { "axis", "ui", vp_axis_types },
    { "frame", "", vp_frame_types },
    { "axis_source", "u", vp_axis_source_types },
    { "axis_stop", "", vp_axis_stop_types },
    { "axis_discrete", "ui", vp_axis_discrete_types },
    { "destroy", "", vp_destroy_types },
};

static const struct wl_interface zwlr_virtual_pointer_v1_interface = {
    "zwlr_virtual_pointer_v1", 2,
    9, zwlr_vp_requests,
    0, NULL,
};

/* ---- zwlr_virtual_pointer_manager_v1 ---- */
/* request 0: create_virtual_pointer(seat, id) — seat is a nullable wl_seat object (a suggestion to
 * the compositor), id is the new zwlr_virtual_pointer_v1. Signature "?on": "?o" = nullable object,
 * "n" = new_id. The compositor rejects the request with "invalid arguments" if the seat argument is
 * missing, so it must be declared here and passed to the marshal call. */
static const struct wl_interface *vpm_create_types[] = {
    &wl_seat_interface,
    &zwlr_virtual_pointer_v1_interface
};

static const struct wl_message zwlr_vpm_requests[] = {
    { "create_virtual_pointer", "?on", vpm_create_types },
};

static const struct wl_interface zwlr_virtual_pointer_manager_v1_interface = {
    "zwlr_virtual_pointer_manager_v1", 1,
    1, zwlr_vpm_requests,
    0, NULL,
};

static struct wl_display *display;
static struct wl_proxy *vpm;
static struct wl_proxy *vp;
static struct wl_seat *seat;
static int ext_w = 1280, ext_h = 720;   /* the global output extent (WPM_EXT_W / WPM_EXT_H) */

static uint32_t now_ms( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void registry_global( void *data, struct wl_registry *reg, uint32_t name,
                             const char *interface, uint32_t version )
{
    if (!strcmp( interface, "zwlr_virtual_pointer_manager_v1" ))
        vpm = wl_registry_bind( reg, name, &zwlr_virtual_pointer_manager_v1_interface, 1 );
    else if (!strcmp( interface, "wl_seat" ))
        seat = wl_registry_bind( reg, name, &wl_seat_interface, 1 );
}
static void registry_global_remove( void *data, struct wl_registry *reg, uint32_t name ) {}

static const struct wl_registry_listener registry_listener = { registry_global, registry_global_remove };

static void move_to( int x, int y )
{
    /* a few steps so the compositor sees a motion path, not a single jump */
    static int cx, cy, have;
    int from_x = have ? cx : 0, from_y = have ? cy : 0, i;

    for (i = 1; i <= 8; i++)
    {
        int nx = from_x + (x - from_x) * i / 8;
        int ny = from_y + (y - from_y) * i / 8;
        wl_proxy_marshal( vp, 1, now_ms(), nx, ny, ext_w, ext_h ); /* motion_absolute */
        wl_proxy_marshal( vp, 4 );                              /* frame */
        wl_display_flush( display );
        wl_display_dispatch_pending( display );
    }
    cx = x;
    cy = y;
    have = 1;
    printf( "pointer at %d,%d\n", x, y );
    fflush( stdout );
}

static void click_at( int x, int y )
{
    move_to( x, y );
    wl_proxy_marshal( vp, 2, now_ms(), 1, 1 ); /* button press (button 1, state 1) */
    wl_proxy_marshal( vp, 4 );                 /* frame */
    wl_display_flush( display );
    wl_display_dispatch_pending( display );
    usleep( 80 * 1000 );
    wl_proxy_marshal( vp, 2, now_ms(), 1, 0 ); /* button release */
    wl_proxy_marshal( vp, 4 );                 /* frame */
    wl_display_flush( display );
    wl_display_dispatch_pending( display );
    printf( "clicked at %d,%d\n", x, y );
    fflush( stdout );
}

int main( int argc, char **argv )
{
    int i;

    if (argc < 2)
    {
        fprintf( stderr, "usage: %s x,y,ms [x,y,ms ...]\n", argv[0] );
        return 1;
    }
    if (getenv( "WPM_EXT_W" )) ext_w = atoi( getenv( "WPM_EXT_W" ) );
    if (getenv( "WPM_EXT_H" )) ext_h = atoi( getenv( "WPM_EXT_H" ) );
    display = wl_display_connect( NULL );
    if (!display)
    {
        fprintf( stderr, "cannot connect to the Wayland display\n" );
        return 1;
    }
    {
        struct wl_registry *reg = wl_display_get_registry( display );
        wl_registry_add_listener( reg, &registry_listener, NULL );
        wl_display_dispatch( display );
        wl_display_roundtrip( display );
        if (!vpm)
        {
            fprintf( stderr, "no zwlr_virtual_pointer_manager_v1 global\n" );
            return 1;
        }
        printf( "vpm %p seat %p\n", (void *)vpm, (void *)seat );
        fflush( stdout );
        /* create_virtual_pointer(seat, id): the variadic args are the request's non-new_id arguments
         * in order (here just the seat object) followed by a trailing NULL (the array pointer). The
         * constructor returns the new proxy. */
        vp = wl_proxy_marshal_flags( vpm, 0, &zwlr_virtual_pointer_v1_interface,
                                     wl_proxy_get_version( vpm ), 0, seat, NULL );
        printf( "created virtual pointer %p\n", (void *)vp );
        fflush( stdout );
        wl_display_flush( display );
        /* give the compositor a moment to process the create request; a protocol error closes the
         * connection, which makes the next dispatch return -1. */
        for (int i = 0; i < 10; i++)
        {
            usleep( 20 * 1000 );
            if (wl_display_dispatch_pending( display ) < 0)
            {
                fprintf( stderr, "compositor rejected create_virtual_pointer (protocol error)\n" );
                return 1;
            }
        }
    }
    for (i = 1; i < argc; i++)
    {
        int x, y, ms, click = 0;
        const char *arg = argv[i];
        if (!strncmp( arg, "click:", 6 ))
        {
            click = 1;
            arg += 6;
        }
        if (sscanf( arg, "%d,%d,%d", &x, &y, &ms ) != 3)
        {
            fprintf( stderr, "bad step %s (want x,y,ms or click:x,y,ms)\n", argv[i] );
            return 1;
        }
        if (click)
            click_at( x, y );
        else
            move_to( x, y );
        if (ms > 0)
        {
            uint32_t deadline = now_ms() + (uint32_t)ms;
            while (now_ms() < deadline)
            {
                if (wl_display_dispatch_pending( display ))
                    wl_display_dispatch( display );
                else
                    usleep( 20 * 1000 );
            }
        }
    }
    wl_display_disconnect( display );
    return 0;
}
