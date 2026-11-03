/*
 * WineExplorer host services - optional XRender compositor
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

/* Original implementation using the public X Composite, Damage, Fixes,
 * Shape and Render APIs. No reference implementation code is copied.
 * All entry points run under x11wm's x_mutex, on its Display and event loop.
 * Only root/desktop children are redirected: Wine owns deeper client surfaces.
 */
#if 0
#pragma makedep unix
#endif

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#if defined(SONAME_LIBX11) && defined(SONAME_LIBXCOMPOSITE) && defined(SONAME_LIBXFIXES) && defined(SONAME_LIBXRENDER)
#define COMPOSE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#undef Status
#undef ControlMask
#endif
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include "winternl.h"
#include "wine/debug.h"
#include "wine/dwm_protocol.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL(wexhost);

static pthread_mutex_t clock_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t clock_cond = PTHREAD_COND_INITIALIZER;
static UINT64 clock_generation;

/* On a Wine thread of the shell: returns once the generation differs from the one given, or after timeout_ms. */
NTSTATUS composition_clock_wait( void *args )
{
    struct composition_clock_wait_params *params = args;
    struct timespec deadline;

    clock_gettime( CLOCK_REALTIME, &deadline );
    deadline.tv_sec += params->timeout_ms / 1000;
    deadline.tv_nsec += (params->timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    pthread_mutex_lock( &clock_mutex );
    while (clock_generation == params->generation)
        if (pthread_cond_timedwait( &clock_cond, &clock_mutex, &deadline )) break;
    params->generation = clock_generation;
    pthread_mutex_unlock( &clock_mutex );
    return STATUS_SUCCESS;
}

#ifdef COMPOSE_X11

/* Wine's configure does not probe libXdamage. These are the public Damage 1.0
 * ABI declarations, kept here to avoid changing stock configure or requiring
 * the optional Xdamage development header. NonEmpty reports accumulated damage;
 * XDamageSubtract transfers the precise region and rearms notification. */
typedef XID Damage;
typedef struct
{
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Drawable drawable;
    Damage damage;
    int level;
    Bool more;
    Time timestamp;
    XRectangle area, geometry;
} compose_damage_event;
static Bool (*pXDamageQueryExtension)( Display *, int *, int * );
static int (*pXDamageQueryVersion)( Display *, int *, int * );
static Damage (*pXDamageCreate)( Display *, Drawable, int );
static void (*pXDamageDestroy)( Display *, Damage );
static void (*pXDamageSubtract)( Display *, Damage, XserverRegion, XserverRegion );

#define X11_FUNCS \
    FUNC(XQueryTree) FUNC(XGetWindowAttributes) FUNC(XFree) FUNC(XSelectInput) \
    FUNC(XSync) FUNC(XInternAtom) FUNC(XGetSelectionOwner) FUNC(XSetSelectionOwner) \
    FUNC(XCreateSimpleWindow) FUNC(XDestroyWindow) FUNC(XGetWindowProperty) \
    FUNC(XCreatePixmap) FUNC(XFreePixmap) FUNC(XTranslateCoordinates) FUNC(XSendEvent) \
    FUNC(XGrabServer) FUNC(XUngrabServer) FUNC(XChangeProperty)
#define COMPOSITE_FUNCS \
    FUNC(XCompositeQueryExtension) FUNC(XCompositeQueryVersion) FUNC(XCompositeRedirectSubwindows) \
    FUNC(XCompositeUnredirectSubwindows) FUNC(XCompositeNameWindowPixmap) \
    FUNC(XCompositeGetOverlayWindow) FUNC(XCompositeReleaseOverlayWindow)
#define FIXES_FUNCS \
    FUNC(XFixesQueryExtension) FUNC(XFixesQueryVersion) FUNC(XFixesCreateRegion) \
    FUNC(XFixesDestroyRegion) FUNC(XFixesSetRegion) FUNC(XFixesUnionRegion) \
    FUNC(XFixesIntersectRegion) FUNC(XFixesSubtractRegion) FUNC(XFixesTranslateRegion) \
    FUNC(XFixesCreateRegionFromWindow) FUNC(XFixesSetPictureClipRegion) \
    FUNC(XFixesSetWindowShapeRegion) FUNC(XFixesFetchRegion)
#define RENDER_FUNCS \
    FUNC(XRenderQueryExtension) FUNC(XRenderQueryVersion) FUNC(XRenderFindVisualFormat) \
    FUNC(XRenderCreatePicture) FUNC(XRenderFreePicture) FUNC(XRenderComposite) \
    FUNC(XRenderFillRectangle) FUNC(XRenderCreateSolidFill) FUNC(XRenderFindStandardFormat) \
    FUNC(XRenderSetPictureTransform) FUNC(XRenderSetPictureFilter)
#define SHAPE_FUNCS FUNC(XShapeQueryExtension) FUNC(XShapeSelectInput) FUNC(XShapeInputSelected)
#define FUNC(name) static typeof(name) *p##name;
X11_FUNCS
COMPOSITE_FUNCS
FIXES_FUNCS
RENDER_FUNCS
SHAPE_FUNCS
#undef FUNC

struct window
{
    struct window *next;
    Window id, parent;
    XWindowAttributes attr;
    int x, y;
    long added_mask;
    Bool added_shape;
    BOOL seen;
    Pixmap pixmap, shadow_pixmap;
    Picture picture, alpha, shadow_picture;
    BOOL shadow;
    unsigned int corner;
    Damage damage;
    unsigned long opacity;
    BOOL glass;
    BOOL frame_glass;
    int client[4];
    int margin[4];
    unsigned int backdrop;   /* WEX_BACKDROP_* */
    unsigned int tint;       /* with WEX_BACKDROP_TINT: 0xAABBGGRR */
    int region[4];           /* with WEX_BACKDROP_REGION: left, top, right, bottom in the client area */
    BOOL blur_expanded;      /* expand_blur_damage: this frame's damage already covers the blur area */
};

static Display *dpy;
static Window root, desktop, owner, overlay;
static Atom selection, opacity_atom, shadow_atom, margins_atom, backdrop_atom;
static int *x_error, damage_base, shape_base;
static BOOL enabled, redirected_root, redirected_desktop, tree_dirty, full_repaint, dirty;
static BOOL libraries_tried, libraries_loaded;
static struct window *windows;
static XserverRegion damage_region, scratch, clip;
static Pixmap back_pixmap;
static Picture back_picture, overlay_picture;
static int width, height;
static long desktop_added_mask;
/* Frame clock (docs/DWM_ARCHITECTURE.md section 2.3): the shared page, set by compose_clock under x_mutex
 * and written by compose_paint/compose_set under the same lock. The WM loop is not a Wine thread and must
 * not call Nt* waits or events: it bumps clock_generation, and a Wine thread of the shell waits for it in
 * compose_clock_wait and pulses the Win32 event. */
static struct dwm_clock_page *clock_page;
/* live thumbnails, replaced as a whole by compose_thumbnails under x_mutex */
static struct compose_thumb thumbs[WEX_THUMB_MAX];
static unsigned int thumb_count;

static BOOL load_libraries(void)
{
    void *lib;
    if (libraries_tried) return libraries_loaded;
    libraries_tried = TRUE;
    /* Handles stay loaded for the lifetime of the Unix library, like x11wm. */
#define FUNC(name) if (!(p##name = dlsym( lib, #name ))) return FALSE;
    if (!(lib = dlopen( SONAME_LIBX11, RTLD_NOW ))) return FALSE;
    X11_FUNCS
    if (!(lib = dlopen( SONAME_LIBXCOMPOSITE, RTLD_NOW ))) return FALSE;
    COMPOSITE_FUNCS
    if (!(lib = dlopen( SONAME_LIBXFIXES, RTLD_NOW ))) return FALSE;
    FIXES_FUNCS
    if (!(lib = dlopen( SONAME_LIBXRENDER, RTLD_NOW ))) return FALSE;
    RENDER_FUNCS
    if (!(lib = dlopen( "libXext.so.6", RTLD_NOW ))) return FALSE;
    SHAPE_FUNCS
    if (!(lib = dlopen( "libXdamage.so.1", RTLD_NOW ))) return FALSE;
    FUNC(XDamageQueryExtension)
    FUNC(XDamageQueryVersion)
    FUNC(XDamageCreate)
    FUNC(XDamageDestroy)
    FUNC(XDamageSubtract)
#undef FUNC
    libraries_loaded = TRUE;
    return TRUE;
}

static BOOL sync_ok(void)
{
    pXSync( dpy, False );
    return !*x_error;
}

static struct window *find_window( Window id )
{
    struct window *w;
    for (w = windows; w; w = w->next) if (w->id == id) return w;
    return NULL;
}

static void free_surface( struct window *w )
{
    if (w->picture) pXRenderFreePicture( dpy, w->picture );
    if (w->pixmap) pXFreePixmap( dpy, w->pixmap );
    if (w->shadow_picture) pXRenderFreePicture( dpy, w->shadow_picture );
    if (w->shadow_pixmap) pXFreePixmap( dpy, w->shadow_pixmap );
    w->picture = w->pixmap = 0;
    w->shadow_picture = w->shadow_pixmap = 0;
}

static void free_window( struct window *w, BOOL destroyed )
{
    free_surface( w );
    if (w->alpha) pXRenderFreePicture( dpy, w->alpha );
    if (w->damage) pXDamageDestroy( dpy, w->damage );
    if (!destroyed)
    {
        XWindowAttributes attr;
        if (pXGetWindowAttributes( dpy, w->id, &attr ))
        {
            /* Remove only bits added by us, preserving WM changes meanwhile. */
            pXSelectInput( dpy, w->id, attr.your_event_mask & ~w->added_mask );
            if (w->added_shape) pXShapeSelectInput( dpy, w->id, 0 );
        }
    }
    free( w );
}

/* The last picture of windows that were unmapped (minimized or hidden), so their thumbnails keep showing them, as on
 * Windows. A named window pixmap stays valid after the unmap. Freed when the window is destroyed or mapped again.
 * ponytail: 16 slots, oldest replaced first; a per-window budget if taskbars with more minimized windows matter. */
#define RETAINED_MAX 16
static struct retained { Window id; Pixmap pixmap; Picture picture; int width, height, depth; UINT64 when; } retained[RETAINED_MAX];
static unsigned int retained_next;

static UINT64 now_ms(void)
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * (UINT64)1000 + ts.tv_nsec / 1000000;
}

static BOOL is_offscreen( const struct window *w )
{
    return w->x + w->attr.width <= 0 || w->y + w->attr.height <= 0;
}

static void free_retained( struct retained *r )
{
    if (r->picture) pXRenderFreePicture( dpy, r->picture );
    if (r->pixmap) pXFreePixmap( dpy, r->pixmap );
    memset( r, 0, sizeof(*r) );
}

static struct retained *find_retained( Window id )
{
    unsigned int i;
    for (i = 0; i < RETAINED_MAX; i++) if (retained[i].id == id) return &retained[i];
    return NULL;
}

static void drop_retained( Window id )
{
    struct retained *r;
    if (id && (r = find_retained( id ))) free_retained( r );
}

/* Wine shrinks a window to its icon size just before unmapping it on minimize: a picture kept at that resize
 * (less than a second ago) is the one to show, not the icon-sized one */
static void retain_surface( struct window *w )
{
    struct retained *r;
    if (!w->picture || (w->parent != desktop && w->parent != root)) return;
    if ((r = find_retained( w->id )) && now_ms() - r->when < 1000) return;
    drop_retained( w->id );
    r = &retained[retained_next++ % RETAINED_MAX];
    if (r->id) free_retained( r );
    r->id = w->id;
    r->pixmap = w->pixmap;
    r->picture = w->picture;
    r->width = w->attr.width + 2 * w->attr.border_width;
    r->height = w->attr.height + 2 * w->attr.border_width;
    r->depth = w->attr.depth;
    r->when = now_ms();
    w->picture = w->pixmap = 0;
}

static void remove_window( Window id, BOOL destroyed )
{
    struct window **entry;
    if (destroyed) drop_retained( id );
    for (entry = &windows; *entry; entry = &(*entry)->next)
        if ((*entry)->id == id)
        {
            struct window *w = *entry;
            *entry = w->next;
            if (!destroyed) retain_surface( w );
            free_window( w, destroyed );
            break;
        }
}

static unsigned long get_cardinal( Window id, Atom atom, unsigned long fallback )
{
    Atom actual;
    int format;
    unsigned long count, left, value = fallback;
    unsigned char *data = NULL;
    if (pXGetWindowProperty( dpy, id, atom, 0, 1, False, XA_CARDINAL,
                             &actual, &format, &count, &left, &data ) == Success &&
        actual == XA_CARDINAL && format == 32 && count == 1)
        value = *(unsigned long *)data;
    if (data) pXFree( data );
    return value;
}

/* _WEX_DWM_BACKDROP: backdrop, tint, region[4] */
static void update_backdrop( struct window *w )
{
    Atom actual;
    int format, i;
    unsigned long count, left;
    unsigned char *data = NULL;
    w->backdrop = 0;
    if (pXGetWindowProperty( dpy, w->id, backdrop_atom, 0, 6, False, XA_CARDINAL,
                             &actual, &format, &count, &left, &data ) == Success &&
        actual == XA_CARDINAL && format == 32 && count == 6)
    {
        unsigned long *values = (unsigned long *)data;
        w->backdrop = values[0];
        w->tint = values[1];
        for (i = 0; i < 4; i++) w->region[i] = (int)values[2 + i];
    }
    if (data) pXFree( data );
}

static void update_opacity( struct window *w )
{
    XRenderColor color = { 0, 0, 0, 0 };
    unsigned long opacity = get_cardinal( w->id, opacity_atom, 0xffffffff );
    if (opacity == w->opacity) return;
    if (w->alpha) pXRenderFreePicture( dpy, w->alpha );
    w->alpha = 0;
    w->opacity = opacity;
    if (opacity != 0xffffffff)
    {
        color.alpha = opacity >> 16;
        w->alpha = pXRenderCreateSolidFill( dpy, &color );
    }
}

/* DWM glass margins (P7a): the client rectangle and the four margins, in window coordinates.
 * A margin of -1 (0xffffffff) means the whole client area is glass. The 9th value is a flags word:
 * bit 0 means the frame is glass too (P10b). */
static void update_margins( struct window *w )
{
    Atom actual;
    int format;
    unsigned long count, left;
    unsigned char *data = NULL;
    if (pXGetWindowProperty( dpy, w->id, margins_atom, 0, 9, False, XA_CARDINAL,
                             &actual, &format, &count, &left, &data ) == Success &&
        actual == XA_CARDINAL && format == 32 && (count == 8 || count == 9))
    {
        unsigned long *values = (unsigned long *)data;
        w->glass = TRUE;
        w->frame_glass = count == 9 && (values[8] & 1);
        w->client[0] = values[0];
        w->client[1] = values[1];
        w->client[2] = values[2];
        w->client[3] = values[3];
        w->margin[0] = (int)values[4];
        w->margin[1] = (int)values[5];
        w->margin[2] = (int)values[6];
        w->margin[3] = (int)values[7];
    }
    else
    {
        w->glass = FALSE;
        w->frame_glass = FALSE;
    }
    if (w->glass) TRACE( "glass %lx depth %d client %d,%d %dx%d frame %d\n", w->id, w->attr.depth,
                         w->client[0], w->client[1], w->client[2], w->client[3], w->frame_glass );
    if (data) pXFree( data );
}

static struct window *track_window( Window id, Window parent )
{
    struct window *w = find_window( id );
    XWindowAttributes attr;
    Window child;
    int x, y;
    long mask = PropertyChangeMask | StructureNotifyMask;
    if (id == overlay || id == owner || !pXGetWindowAttributes( dpy, id, &attr ) ||
        attr.class == InputOnly || attr.map_state != IsViewable) return NULL;
    if (!pXTranslateCoordinates( dpy, id, root, 0, 0, &x, &y, &child )) return NULL;
    if (!w)
    {
        if (!(w = calloc( 1, sizeof(*w) ))) return NULL;
        w->id = id;
        w->opacity = 0xffffffff;
        w->added_mask = mask & ~attr.your_event_mask;
        pXSelectInput( dpy, id, attr.your_event_mask | mask );
        w->added_shape = !pXShapeInputSelected( dpy, id );
        pXShapeSelectInput( dpy, id, ShapeNotifyMask );
        *x_error = 0;
        w->damage = pXDamageCreate( dpy, id, 3 /* XDamageReportNonEmpty */ );
        if (!sync_ok()) { w->damage = 0; free_window( w, FALSE ); return NULL; }
        w->next = windows;
        windows = w;
    }
    if (w->attr.width != attr.width || w->attr.height != attr.height ||
        w->attr.border_width != attr.border_width || w->parent != parent)
        free_surface( w );
    w->attr = attr;
    w->parent = parent;
    w->x = x;
    w->y = y;
    w->seen = TRUE;
    /* mapped again on screen: live pixels from now on (Wine maps a minimized window off screen, at -32000) */
    if (!is_offscreen( w )) drop_retained( id );
    update_opacity( w );
    update_margins( w );
    w->shadow = !!get_cardinal( w->id, shadow_atom, 0 );
    update_backdrop( w );
    return w;
}

static void scan_children( Window parent )
{
    Window dummy, *children = NULL;
    unsigned int i, count;
    if (!pXQueryTree( dpy, parent, &dummy, &dummy, &children, &count )) return;
    for (i = 0; i < count; i++) track_window( children[i], parent );
    if (children) pXFree( children );
}

static void scan_windows(void)
{
    struct window *w, **entry;
    for (w = windows; w; w = w->next) w->seen = FALSE;
    scan_children( root );
    scan_children( desktop );
    for (entry = &windows; *entry; )
    {
        w = *entry;
        if (!w->seen) { *entry = w->next; free_window( w, FALSE ); }
        else entry = &w->next;
    }
    tree_dirty = FALSE;
}

static void free_output(void)
{
    if (overlay_picture) pXRenderFreePicture( dpy, overlay_picture );
    if (back_picture) pXRenderFreePicture( dpy, back_picture );
    if (back_pixmap) pXFreePixmap( dpy, back_pixmap );
    if (overlay) pXCompositeReleaseOverlayWindow( dpy, root );
    overlay = overlay_picture = back_picture = back_pixmap = 0;
}

static BOOL create_output(void)
{
    XWindowAttributes attr;
    XRenderPictFormat *format;
    XserverRegion empty;
    if (!pXGetWindowAttributes( dpy, root, &attr )) return FALSE;
    width = attr.width;
    height = attr.height;
    *x_error = 0;
    overlay = pXCompositeGetOverlayWindow( dpy, root );
    if (!sync_ok() || !overlay || !pXGetWindowAttributes( dpy, overlay, &attr )) return FALSE;
    pXSelectInput( dpy, overlay, ExposureMask );
    empty = pXFixesCreateRegion( dpy, NULL, 0 );
    pXFixesSetWindowShapeRegion( dpy, overlay, ShapeInput, 0, 0, empty );
    pXFixesDestroyRegion( dpy, empty );
    if (!(format = pXRenderFindVisualFormat( dpy, attr.visual ))) return FALSE;
    back_pixmap = pXCreatePixmap( dpy, root, width, height, attr.depth );
    back_picture = pXRenderCreatePicture( dpy, back_pixmap, format, 0, NULL );
    overlay_picture = pXRenderCreatePicture( dpy, overlay, format, 0, NULL );
    return sync_ok();
}

/* Cached Mica (P12a): the desktop picture averaged down by repeated halving (a bilinear sample between four pixels
 * is a 2x2 box filter) and scaled back up with bilinear filtering, then tinted where it is painted. Rebuilt when the
 * desktop is damaged or resized; it never samples other windows. */
#define MICA_STEPS 5
static BOOL name_surface( struct window *w );
static Pixmap mica_pixmap;
static Picture mica_picture;
static BOOL mica_dirty = TRUE;

static void free_mica(void)
{
    if (mica_picture) pXRenderFreePicture( dpy, mica_picture );
    if (mica_pixmap) pXFreePixmap( dpy, mica_pixmap );
    mica_picture = mica_pixmap = 0;
    mica_dirty = TRUE;
}

static void set_scale( Picture picture, double sx, double sy, const char *filter )
{
    XTransform transform = {{{ 0 }}};
    transform.matrix[0][0] = XDoubleToFixed( sx );
    transform.matrix[1][1] = XDoubleToFixed( sy );
    transform.matrix[2][2] = XDoubleToFixed( 1 );
    pXRenderSetPictureTransform( dpy, picture, &transform );
    pXRenderSetPictureFilter( dpy, picture, filter, NULL, 0 );
}

/* The wallpaper: the lowest mapped child covering the whole virtual desktop, which is the shell's desktop window
 * (redirected on its own, so the desktop pixmap does not have it), else the desktop itself. */
static Window mica_source;

static struct window *wallpaper_window( struct window *desk )
{
    Window dummy, *children = NULL;
    unsigned int i, count = 0;
    struct window *w = NULL;

    if (pXQueryTree( dpy, desktop, &dummy, &dummy, &children, &count ))
        for (i = 0; i < count && !w; i++)
            if ((w = find_window( children[i] )) &&
                (w->x != desk->x || w->y != desk->y || w->attr.width != desk->attr.width ||
                 w->attr.height != desk->attr.height)) w = NULL;
    if (children) pXFree( children );
    return w ? w : desk;
}

/* Halve source (width x height) steps times, then scale it back up into dest at (x, y), through mask (a window's
 * opacity) if any; the source's transform is reset afterwards. */
static void blur_into( Picture source, int width, int height, int steps, Picture dest, int x, int y, Picture mask )
{
    XRenderPictureAttributes attr = { 0 };
    XRenderPictFormat *format = pXRenderFindStandardFormat( dpy, PictStandardRGB24 );
    Pixmap pixmap[2] = { 0 };
    Picture level[2] = { 0 }, from = source;
    int i, w = width, h = height;

    if (!format) return;
    attr.repeat = RepeatPad;   /* the upscale samples half a pixel past the edges */
    for (i = 0; i < steps && w > 1 && h > 1; i++)
    {
        int next = i & 1, nw = w / 2, nh = h / 2;
        if (level[next]) pXRenderFreePicture( dpy, level[next] );
        if (pixmap[next]) pXFreePixmap( dpy, pixmap[next] );
        pixmap[next] = pXCreatePixmap( dpy, root, nw, nh, 24 );
        level[next] = pXRenderCreatePicture( dpy, pixmap[next], format, CPRepeat, &attr );
        set_scale( from, (double)w / nw, (double)h / nh, "bilinear" );
        pXRenderComposite( dpy, PictOpSrc, from, None, level[next], 0, 0, 0, 0, 0, 0, nw, nh );
        from = level[next];
        w = nw;
        h = nh;
    }
    set_scale( from, (double)w / width, (double)h / height, "bilinear" );
    pXRenderComposite( dpy, mask ? PictOpOver : PictOpSrc, from, mask, dest, 0, 0, 0, 0, x, y, width, height );
    set_scale( source, 1, 1, "nearest" );
    for (i = 0; i < 2; i++)
    {
        if (level[i]) pXRenderFreePicture( dpy, level[i] );
        if (pixmap[i]) pXFreePixmap( dpy, pixmap[i] );
    }
}

static void build_mica( struct window *desk )
{
    XRenderPictFormat *format;

    free_mica();
    mica_dirty = FALSE;
    if (!desk) return;
    desk = wallpaper_window( desk );
    mica_source = desk->id;
    if (!name_surface( desk ) || !(format = pXRenderFindStandardFormat( dpy, PictStandardRGB24 ))) return;
    *x_error = 0;
    mica_pixmap = pXCreatePixmap( dpy, root, desk->attr.width, desk->attr.height, 24 );
    mica_picture = pXRenderCreatePicture( dpy, mica_pixmap, format, 0, NULL );
    blur_into( desk->picture, desk->attr.width, desk->attr.height, MICA_STEPS, mica_picture, 0, 0, None );
    if (!sync_ok())
    {
        free_mica();
        mica_dirty = FALSE;   /* until the desktop changes again */
    }
    TRACE( "mica from %lx %dx%d depth %d (desktop %lx)\n", desk->id, desk->attr.width, desk->attr.height,
           desk->attr.depth, desktop );
}

/* Live blur (P12b): what is already composed below the window, blurred in place. The back buffer holds this
 * frame's lower windows only where it was repainted, so expand_blur_damage repaints a blurred window's whole
 * area (plus the reach of the filter) whenever any of it is damaged; the window never samples itself. */
#define BLUR_STEPS 3      /* Aero glass and ACCENT_ENABLE_BLURBEHIND */
#define ACRYLIC_STEPS 5   /* Acrylic */
#define BLUR_REACH (1 << ACRYLIC_STEPS)

static BOOL live_blur( const struct window *w )
{
    return (w->backdrop & (WEX_BACKDROP_BLUR | WEX_BACKDROP_ACRYLIC)) && w->glass && w->attr.depth == 32;
}

static XRectangle blur_area( const struct window *w )
{
    XRectangle r;
    int x = max( w->x - BLUR_REACH, 0 ), y = max( w->y - BLUR_REACH, 0 );
    int right = min( w->x + w->attr.width + 2 * w->attr.border_width + BLUR_REACH, width );
    int bottom = min( w->y + w->attr.height + 2 * w->attr.border_width + BLUR_REACH, height );
    r.x = x;
    r.y = y;
    r.width = max( right - x, 0 );
    r.height = max( bottom - y, 0 );
    return r;
}

/* Repeated until nothing grows: one blurred window's expansion may reach another already visited. Each window
 * expands at most once, so this ends. */
static void expand_blur_damage(void)
{
    struct window *w;
    BOOL grown;
    for (w = windows; w; w = w->next) w->blur_expanded = FALSE;
    do
    {
        grown = FALSE;
        for (w = windows; w; w = w->next)
        {
            XRectangle r, *rects;
            int count = 0;
            if (w->blur_expanded || !live_blur( w )) continue;
            r = blur_area( w );
            pXFixesSetRegion( dpy, scratch, &r, 1 );
            pXFixesIntersectRegion( dpy, scratch, scratch, damage_region );
            if ((rects = pXFixesFetchRegion( dpy, scratch, &count ))) pXFree( rects );
            if (!count) continue;
            pXFixesSetRegion( dpy, scratch, &r, 1 );
            pXFixesUnionRegion( dpy, damage_region, damage_region, scratch );
            w->blur_expanded = grown = TRUE;
        }
    } while (grown);
}

/* A premultiplied tint over the back picture, faded with the window's opacity */
static void fill_tint( const struct window *w, XRenderColor tint, int x, int y, int width, int height )
{
    if (w->opacity != 0xffffffff)
    {
        double scale = w->opacity / 4294967295.0;
        tint.alpha *= scale;
        tint.red *= scale;
        tint.green *= scale;
        tint.blue *= scale;
    }
    if (tint.alpha) pXRenderFillRectangle( dpy, PictOpOver, back_picture, &tint, x, y, width, height );
}

/* Under the glass of a window with a live blur or a gradient accent (the back picture's clip is set, and restored
 * to clip afterwards): the blur, narrowed to the box of DwmEnableBlurBehindWindow's region, then the tint
 * (GradientColor, or for Acrylic without one F3F3F3 light, 202020 dark, at 70 %). */
static void paint_accent( const struct window *w )
{
    XRenderPictureAttributes attr = { 0 };
    XRenderPictFormat *format = pXRenderFindStandardFormat( dpy, PictStandardRGB24 );
    XRectangle r = blur_area( w );
    XserverRegion area = clip;
    XRenderColor tint = { 0 };
    BOOL acrylic = !!(w->backdrop & WEX_BACKDROP_ACRYLIC);

    if (!format || !r.width || !r.height) return;
    if (w->backdrop & WEX_BACKDROP_REGION)
    {
        XRectangle box;
        box.x = w->x + w->client[0] + w->region[0];
        box.y = w->y + w->client[1] + w->region[1];
        box.width = max( w->region[2] - w->region[0], 0 );
        box.height = max( w->region[3] - w->region[1], 0 );
        area = pXFixesCreateRegion( dpy, &box, 1 );
        pXFixesIntersectRegion( dpy, area, area, clip );
        pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, area );
    }
    if (live_blur( w ))
    {
        Pixmap pixmap = pXCreatePixmap( dpy, root, r.width, r.height, 24 );
        Picture copy;
        attr.repeat = RepeatPad;
        copy = pXRenderCreatePicture( dpy, pixmap, format, CPRepeat, &attr );
        pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, None );
        pXRenderComposite( dpy, PictOpSrc, back_picture, None, copy, r.x, r.y, 0, 0, 0, 0, r.width, r.height );
        pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, area );
        blur_into( copy, r.width, r.height, acrylic ? ACRYLIC_STEPS : BLUR_STEPS, back_picture, r.x, r.y, w->alpha );
        pXRenderFreePicture( dpy, copy );
        pXFreePixmap( dpy, pixmap );
    }
    /* ponytail: no luminosity layer or noise under the Acrylic tint */
    if (w->backdrop & WEX_BACKDROP_TINT)
    {
        unsigned int a = w->tint >> 24;
        tint.alpha = a * 257;
        tint.red = (w->tint & 0xff) * a * 257 / 255;
        tint.green = (w->tint >> 8 & 0xff) * a * 257 / 255;
        tint.blue = (w->tint >> 16 & 0xff) * a * 257 / 255;
    }
    else if (acrylic)
    {
        unsigned int gray = w->backdrop & WEX_BACKDROP_DARK ? 0x20 : 0xf3;
        tint.alpha = 0xb333;
        tint.red = tint.green = tint.blue = gray * 257 * (tint.alpha / 65535.0);
    }
    fill_tint( w, tint, r.x, r.y, r.width, r.height );
    if (area != clip)
    {
        pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, clip );
        pXFixesDestroyRegion( dpy, area );
    }
}

/* Under the glass of a Mica window (the back picture's clip is set): the cached desktop, then the tint
 * (light F3F3F3 at 50 %, dark 202020 at 80 %, premultiplied). Opaque tint when there is no cached desktop. */
static void paint_mica( const struct window *w )
{
    struct window *desk = find_window( desktop );
    XRenderColor tint;
    unsigned int gray = w->backdrop & WEX_BACKDROP_DARK ? 0x20 : 0xf3;

    if (mica_dirty) build_mica( desk );
    tint.alpha = 0xffff;
    if (mica_picture && desk)
    {
        pXRenderComposite( dpy, w->alpha ? PictOpOver : PictOpSrc, mica_picture, w->alpha, back_picture,
                           0, 0, 0, 0, desk->x, desk->y, desk->attr.width, desk->attr.height );
        tint.alpha = w->backdrop & WEX_BACKDROP_DARK ? 0xcccc : 0x8000;
    }
    tint.red = tint.green = tint.blue = gray * 257 * (tint.alpha / 65535.0);
    fill_tint( w, tint, 0, 0, width, height );
}

static void unredirect(void)
{
    struct window *w;
    unsigned int i;
    free_mica();
    for (w = windows; w; w = w->next) free_surface( w );
    for (i = 0; i < RETAINED_MAX; i++) if (retained[i].id) free_retained( &retained[i] );
    free_output();
    if (redirected_desktop) pXCompositeUnredirectSubwindows( dpy, desktop, CompositeRedirectManual );
    if (redirected_root) pXCompositeUnredirectSubwindows( dpy, root, CompositeRedirectManual );
    redirected_root = redirected_desktop = FALSE;
}

static DWORD redirect(void)
{
    /* Synchronize each level separately: never undo another client's redirect
     * after BadAccess. x11wm's handler records errors on this Display. */
    *x_error = 0;
    pXCompositeRedirectSubwindows( dpy, root, CompositeRedirectManual );
    if (!sync_ok()) return *x_error == BadAccess ? ERROR_ALREADY_EXISTS : ERROR_GEN_FAILURE;
    redirected_root = TRUE;
    *x_error = 0;
    pXCompositeRedirectSubwindows( dpy, desktop, CompositeRedirectManual );
    if (!sync_ok()) return *x_error == BadAccess ? ERROR_ALREADY_EXISTS : ERROR_GEN_FAILURE;
    redirected_desktop = TRUE;
    if (!create_output()) return ERROR_GEN_FAILURE;
    full_repaint = dirty = TRUE;
    return ERROR_SUCCESS;
}

static BOOL is_fullscreen( struct window *w )
{
    XserverRegion shape;
    XRectangle *rects;
    int count;
    BOOL full = FALSE;
    /* Conservative: no ARGB, opacity, border, shape holes or windows above it.
     * An opaque Wine host containing a foreign ARGB child remains a D2 limit. */
    if (!w || w->id == desktop || w->attr.depth == 32 || w->opacity != 0xffffffff ||
        w->attr.border_width || w->x != 0 || w->y != 0 ||
        w->attr.width != width || w->attr.height != height) return FALSE;
    shape = pXFixesCreateRegionFromWindow( dpy, w->id, WindowRegionBounding );
    rects = pXFixesFetchRegion( dpy, shape, &count );
    if (rects && count == 1 && !rects[0].x && !rects[0].y &&
        rects[0].width == width && rects[0].height == height) full = TRUE;
    if (rects) pXFree( rects );
    pXFixesDestroyRegion( dpy, shape );
    return full;
}

static struct window *top_window( Window parent )
{
    Window dummy, *children = NULL;
    unsigned int count;
    struct window *w = NULL;
    if (!pXQueryTree( dpy, parent, &dummy, &dummy, &children, &count )) return NULL;
    while (count && !(w = find_window( children[--count] ))) ;
    if (children) pXFree( children );
    return w;
}

static BOOL bypass_fullscreen(void)
{
    struct window *top = top_window( root );
    if (top && top->id == desktop) top = top_window( desktop );
    return is_fullscreen( top );
}

static BOOL name_surface( struct window *w )
{
    XRenderPictFormat *format;
    XRenderPictureAttributes attr = { 0 };
    if (w->picture) return TRUE;
    if (!(format = pXRenderFindVisualFormat( dpy, w->attr.visual ))) return FALSE;
    *x_error = 0;
    w->pixmap = pXCompositeNameWindowPixmap( dpy, w->id );
    if (!sync_ok()) { w->pixmap = 0; return FALSE; }
    /* The desktop's separately redirected children are painted below. Ordinary
     * hosts retain their nonredirected inferiors in the named window pixmap. */
    attr.subwindow_mode = IncludeInferiors;
    w->picture = pXRenderCreatePicture( dpy, w->pixmap, format, CPSubwindowMode, &attr );
    if (!sync_ok()) { free_surface( w ); return FALSE; }
    return TRUE;
}

/* Cache six overlapping translucent rectangles per size. Their nested edges
 * approximate a short blur, with the body offset four pixels right/down.
 * This is original code; no reference compositor implementation is copied. */
static void paint_shadow( struct window *w, XserverRegion parent_clip )
{
    int i, border = w->attr.border_width;
    int cx = w->attr.width + 2 * border + 8, cy = w->attr.height + 2 * border + 8;
    if (!w->shadow) return;
    if (!w->shadow_picture)
    {
        XRenderColor color = { 0, 0, 0, 0 };
        XRenderPictFormat *format = pXRenderFindStandardFormat( dpy, PictStandardARGB32 );
        if (!format) return;
        *x_error = 0;
        w->shadow_pixmap = pXCreatePixmap( dpy, root, cx, cy, 32 );
        w->shadow_picture = pXRenderCreatePicture( dpy, w->shadow_pixmap, format, 0, NULL );
        pXRenderFillRectangle( dpy, PictOpSrc, w->shadow_picture, &color, 0, 0, cx, cy );
        for (i = 0; i < 6 && cx > 2 * i && cy > 2 * i; i++)
        {
            color.alpha = 600 + i * 650;
            pXRenderFillRectangle( dpy, PictOpOver, w->shadow_picture, &color,
                                  i, i, cx - 2 * i, cy - 2 * i );
        }
        if (!sync_ok())
        {
            if (w->shadow_picture) pXRenderFreePicture( dpy, w->shadow_picture );
            if (w->shadow_pixmap) pXFreePixmap( dpy, w->shadow_pixmap );
            w->shadow_picture = w->shadow_pixmap = 0;
            return;
        }
    }
    pXFixesUnionRegion( dpy, clip, damage_region, damage_region );
    if (parent_clip) pXFixesIntersectRegion( dpy, clip, clip, parent_clip );
    pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, clip );
    /* A fading window fades its shadow too. No extra X window/input surface. */
    pXRenderComposite( dpy, PictOpOver, w->shadow_picture, w->alpha, back_picture,
                       0, 0, 0, 0, w->x - border, w->y - border, cx, cy );
}

static BOOL is_thumbnail_source( Window id )
{
    unsigned int i;
    for (i = 0; i < thumb_count; i++) if (thumbs[i].src == id) return TRUE;
    return FALSE;
}

/* Live thumbnails (P8): after a destination window, scale each visible source into it, inside the clip that
 * window was painted with. Sources are top-level windows tracked here; minimized ones have no pixels yet. */
static void paint_thumbnails( struct window *dest )
{
    unsigned int i;

    for (i = 0; i < thumb_count; i++)
    {
        const struct compose_thumb *t = &thumbs[i];
        struct window *src;
        struct retained *r;
        XTransform transform = {{{ 0 }}};
        XRenderColor color = { 0, 0, 0, 0 };
        Picture mask = 0, picture;
        int sx, sy, sw, sh, depth, dw = t->dest[2] - t->dest[0], dh = t->dest[3] - t->dest[1];

        if (t->dest_window != dest->id || !(t->flags & WEX_THUMB_VISIBLE) || dw <= 0 || dh <= 0) continue;
        if ((src = find_window( t->src )) && !(is_offscreen( src ) && find_retained( t->src )))
        {
            if (src == dest || !name_surface( src )) continue;
            picture = src->picture;
            sw = src->attr.width + 2 * src->attr.border_width;
            sh = src->attr.height + 2 * src->attr.border_width;
            depth = src->attr.depth;
        }
        else if ((r = find_retained( t->src )))   /* minimized: its last picture */
        {
            picture = r->picture;
            sw = r->width;
            sh = r->height;
            depth = r->depth;
        }
        else continue;
        sx = sy = 0;
        if (t->flags & WEX_THUMB_HAS_SOURCE)
        {
            sx = max( t->source[0], 0 );
            sy = max( t->source[1], 0 );
            sw = min( t->source[2], sw ) - sx;
            sh = min( t->source[3], sh ) - sy;
        }
        if (sw <= 0 || sh <= 0) continue;
        transform.matrix[0][0] = XDoubleToFixed( (double)sw / dw );
        transform.matrix[0][2] = XDoubleToFixed( sx );
        transform.matrix[1][1] = XDoubleToFixed( (double)sh / dh );
        transform.matrix[1][2] = XDoubleToFixed( sy );
        transform.matrix[2][2] = XDoubleToFixed( 1 );
        if (t->opacity < 255)
        {
            color.alpha = t->opacity * 257;
            mask = pXRenderCreateSolidFill( dpy, &color );
        }
        pXRenderSetPictureTransform( dpy, picture, &transform );
        pXRenderSetPictureFilter( dpy, picture, "bilinear", NULL, 0 );
        pXRenderComposite( dpy, mask || depth == 32 ? PictOpOver : PictOpSrc, picture, mask,
                           back_picture, 0, 0, 0, 0, dest->x + t->dest[0], dest->y + t->dest[1], dw, dh );
        memset( &transform, 0, sizeof(transform) );
        transform.matrix[0][0] = transform.matrix[1][1] = transform.matrix[2][2] = XDoubleToFixed( 1 );
        pXRenderSetPictureTransform( dpy, picture, &transform );
        pXRenderSetPictureFilter( dpy, picture, "nearest", NULL, 0 );
        if (mask) pXRenderFreePicture( dpy, mask );
    }
}

/* Cut the corners of rectangle r (radius at most 16) out of region. */
static void round_corners( XserverRegion region, const XRectangle *r, unsigned int radius )
{
    XRectangle corner_rects[64];
    int corner_count = 0, y;

    if (radius > 16) radius = 16;
    for (y = 0; y < radius; y++)
    {
        int k = radius - y;
        int m = 4 * (radius * radius - k * (k - 1)) - 1;
        int s = 0, inset;

        while ((s + 1) * (s + 1) <= m) s++;
        inset = radius - (s + 1) / 2;
        if (!inset) continue;

        corner_rects[corner_count].x = r->x;
        corner_rects[corner_count].y = r->y + y;
        corner_rects[corner_count].width = inset;
        corner_rects[corner_count].height = 1;
        corner_count++;

        corner_rects[corner_count].x = r->x + r->width - inset;
        corner_rects[corner_count].y = r->y + y;
        corner_rects[corner_count].width = inset;
        corner_rects[corner_count].height = 1;
        corner_count++;

        corner_rects[corner_count].x = r->x;
        corner_rects[corner_count].y = r->y + r->height - 1 - y;
        corner_rects[corner_count].width = inset;
        corner_rects[corner_count].height = 1;
        corner_count++;

        corner_rects[corner_count].x = r->x + r->width - inset;
        corner_rects[corner_count].y = r->y + r->height - 1 - y;
        corner_rects[corner_count].width = inset;
        corner_rects[corner_count].height = 1;
        corner_count++;
    }
    if (corner_count > 0)
    {
        XserverRegion corners = pXFixesCreateRegion( dpy, corner_rects, corner_count );
        pXFixesSubtractRegion( dpy, region, region, corners );
        pXFixesDestroyRegion( dpy, corners );
    }
}

/* Glass frames (P10b) have invisible resize bands left, right and below, like Windows 10 and 11: the visible frame
 * is the client, the caption and a one-pixel border. Not for a maximized window (its frame is off screen).
 * ponytail: the band is the client's left offset less the border, taken for the right and bottom too (a left
 * scroll bar or uneven frame metrics would skew it); publish the insets if that matters. */
static BOOL frame_bounds( const struct window *w, XRectangle *r )
{
    int inset = w->client[0] - 1;
    int win_w = w->attr.width + 2 * w->attr.border_width, win_h = w->attr.height + 2 * w->attr.border_width;

    if (!w->glass || !w->frame_glass || w->attr.depth != 32 || inset <= 0 ||
        win_w <= 2 * inset || win_h <= inset) return FALSE;
    r->x = inset;
    r->y = 0;
    r->width = win_w - 2 * inset;
    r->height = win_h - inset;
    return w->x + inset > 0 || w->x + inset + r->width < width;
}

/* A soft shadow around the visible frame, slightly lower than above, outside the frame only. */
static void paint_frame_shadow( const struct window *w, const XRectangle *frame, XserverRegion parent_clip )
{
    static const int spread = 10;
    XRenderColor color = { 0, 0, 0, 0x0600 };
    XRectangle r = { w->x + frame->x, w->y + frame->y, frame->width, frame->height };
    XserverRegion hole = pXFixesCreateRegion( dpy, &r, 1 );
    int i;

    pXFixesIntersectRegion( dpy, clip, damage_region, parent_clip ? parent_clip : damage_region );
    pXFixesSubtractRegion( dpy, clip, clip, hole );
    pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, clip );
    for (i = 1; i <= spread; i++)
        pXRenderFillRectangle( dpy, PictOpOver, back_picture, &color, r.x - i, r.y - i + 2,
                               r.width + 2 * i, r.height + 2 * i );
    pXFixesDestroyRegion( dpy, hole );
}

static void paint_window( struct window *w, XserverRegion parent_clip )
{
    XserverRegion shape;
    XRectangle frame;
    BOOL has_frame;
    unsigned int radius;
    int border;
    if (!w || !name_surface( w )) return;
    paint_shadow( w, parent_clip );
    shape = pXFixesCreateRegionFromWindow( dpy, w->id, WindowRegionBounding );
    border = w->attr.border_width;
    radius = w->corner == WEX_CORNER_SQUARE ? 0 : w->corner;
    if ((has_frame = frame_bounds( w, &frame )))
    {
        /* the visible frame is round unless the window asked for square corners, as on Windows 11 */
        XserverRegion visible = pXFixesCreateRegion( dpy, &frame, 1 );
        if (w->corner != WEX_CORNER_SQUARE) round_corners( visible, &frame, radius ? radius : 8 );
        pXFixesIntersectRegion( dpy, shape, shape, visible );
        pXFixesDestroyRegion( dpy, visible );
        paint_frame_shadow( w, &frame, parent_clip );
    }
    else if (radius)
    {
        int win_w = w->attr.width + 2 * border;
        int win_h = w->attr.height + 2 * border;
        int count = 0;
        XRectangle *rects = pXFixesFetchRegion( dpy, shape, &count );

        if (rects)
        {
            if (count == 1 && rects[0].width == win_w && rects[0].height == win_h)
                round_corners( shape, &rects[0], radius );
            pXFree( rects );
        }
    }
    if (w->glass && w->attr.depth == 32)
    {
        XserverRegion glass;
        XRectangle glass_rect, inner_rect;
        BOOL full_glass = (w->margin[0] == -1 || w->margin[1] == -1 ||
                           w->margin[2] == -1 || w->margin[3] == -1);

        if (has_frame) glass_rect = frame;
        else if (w->frame_glass)
        {
            /* the frame is glass too (P10b): the glass region starts as the whole window */
            glass_rect.x = 0;
            glass_rect.y = 0;
            glass_rect.width = w->attr.width + 2 * border;
            glass_rect.height = w->attr.height + 2 * border;
        }
        else
        {
            glass_rect.x = w->client[0];
            glass_rect.y = w->client[1];
            glass_rect.width = w->client[2];
            glass_rect.height = w->client[3];
        }
        glass = pXFixesCreateRegion( dpy, &glass_rect, 1 );
        if (!full_glass)
        {
            /* XRectangle sizes are unsigned: clamp before storing */
            int inner_w = w->client[2] - w->margin[0] - w->margin[1];
            int inner_h = w->client[3] - w->margin[2] - w->margin[3];
            inner_rect.x = w->client[0] + w->margin[0];
            inner_rect.y = w->client[1] + w->margin[2];
            inner_rect.width = max( inner_w, 0 );
            inner_rect.height = max( inner_h, 0 );
            {
                XserverRegion inner = pXFixesCreateRegion( dpy, &inner_rect, 1 );
                pXFixesSubtractRegion( dpy, glass, glass, inner );
                pXFixesDestroyRegion( dpy, inner );
            }
        }
        /* clip the glass region to the window shape, then move both to root coordinates */
        pXFixesIntersectRegion( dpy, glass, glass, shape );
        pXFixesTranslateRegion( dpy, shape, w->x, w->y );
        pXFixesTranslateRegion( dpy, glass, w->x, w->y );
        /* the opaque part (shape minus glass) keeps its colors: the back buffer has no alpha */
        pXFixesIntersectRegion( dpy, clip, damage_region, shape );
        if (parent_clip) pXFixesIntersectRegion( dpy, clip, clip, parent_clip );
        pXFixesSubtractRegion( dpy, clip, clip, glass );
        pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, clip );
        pXRenderComposite( dpy, PictOpSrc, w->picture, w->alpha, back_picture, 0, 0, 0, 0,
                           w->x - border, w->y - border,
                           w->attr.width + 2 * border, w->attr.height + 2 * border );
        /* the glass part blends with what is below, so the desktop shows through */
        pXFixesIntersectRegion( dpy, clip, damage_region, glass );
        if (parent_clip) pXFixesIntersectRegion( dpy, clip, clip, parent_clip );
        pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, clip );
        if (w->backdrop & WEX_BACKDROP_MICA) paint_mica( w );
        else if (w->backdrop & (WEX_BACKDROP_BLUR | WEX_BACKDROP_ACRYLIC | WEX_BACKDROP_GRADIENT)) paint_accent( w );
        pXRenderComposite( dpy, PictOpOver, w->picture, w->alpha, back_picture, 0, 0, 0, 0,
                           w->x - border, w->y - border,
                           w->attr.width + 2 * border, w->attr.height + 2 * border );
        pXFixesDestroyRegion( dpy, glass );
    }
    else
    {
        pXFixesTranslateRegion( dpy, shape, w->x, w->y );
        pXFixesIntersectRegion( dpy, clip, damage_region, shape );
        if (parent_clip) pXFixesIntersectRegion( dpy, clip, clip, parent_clip );
        pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, clip );
        /* A uniform alpha mask also requires Over for an otherwise opaque visual. */
        pXRenderComposite( dpy, w->attr.depth == 32 || w->alpha ? PictOpOver : PictOpSrc,
                           w->picture, w->alpha, back_picture, 0, 0, 0, 0,
                           w->x - border, w->y - border,
                           w->attr.width + 2 * border, w->attr.height + 2 * border );
    }
    pXFixesDestroyRegion( dpy, shape );
    paint_thumbnails( w );
}

/* Peek (P11c): while set, only the desktop, the peeked window and the shell's own windows are drawn */
#define PEEK_KEEP_MAX 8
static Window peek_target, peek_keep[PEEK_KEEP_MAX];
static unsigned int peek_keep_count;

static BOOL peek_hides( Window id )
{
    unsigned int i;
    if (!peek_target || id == peek_target) return FALSE;
    for (i = 0; i < peek_keep_count; i++) if (peek_keep[i] == id) return FALSE;
    return TRUE;
}

static void paint_children( Window parent, BOOL skip_desktop, XserverRegion parent_clip )
{
    Window dummy, *children = NULL;
    unsigned int i, count, first = 0;
    if (!pXQueryTree( dpy, parent, &dummy, &dummy, &children, &count )) return;
    /* Root siblings below an opaque virtual desktop must stay below it. */
    if (skip_desktop && find_window( desktop ))
        for (i = 0; i < count; i++) if (children[i] == desktop) { first = i + 1; break; }
    for (i = first; i < count; i++)
        if ((!skip_desktop || children[i] != desktop) && !peek_hides( children[i] ))
            paint_window( find_window( children[i] ), parent_clip );
    if (children) pXFree( children );
}

DWORD compose_peek( unsigned long target, const UINT64 *keep, unsigned int count )
{
    unsigned int i;
    if (!enabled) return target ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS;
    if (count > PEEK_KEEP_MAX) return ERROR_INVALID_PARAMETER;
    peek_target = target;
    for (i = 0; i < count; i++) peek_keep[i] = keep[i];
    peek_keep_count = count;
    full_repaint = dirty = TRUE;
    return ERROR_SUCCESS;
}

BOOL compose_enabled(void)
{
    return enabled;
}

DWORD compose_shadow( unsigned long id, BOOL shadow )
{
    unsigned long value = !!shadow;
    XWindowAttributes attr;
    if (!enabled) return ERROR_NOT_SUPPORTED;
    if (!id || id == root || id == desktop || id == overlay || id == owner)
        return ERROR_INVALID_PARAMETER;
    *x_error = 0;
    if (!pXGetWindowAttributes( dpy, id, &attr )) return ERROR_INVALID_WINDOW_HANDLE;
    pXChangeProperty( dpy, id, shadow_atom, XA_CARDINAL, 32, PropModeReplace,
                      (unsigned char *)&value, 1 );
    tree_dirty = full_repaint = dirty = TRUE;
    return sync_ok() ? ERROR_SUCCESS : ERROR_INVALID_WINDOW_HANDLE;
}

/* kept as a property on the X window, like the shadow, so it survives unmapping (minimize) */
DWORD compose_backdrop( unsigned long id, unsigned int backdrop, unsigned int tint, const INT *region )
{
    unsigned long value[6] = { backdrop, tint, region[0], region[1], region[2], region[3] };
    XWindowAttributes attr;
    if (!enabled) return ERROR_NOT_SUPPORTED;
    if (backdrop & ~(WEX_BACKDROP_MICA | WEX_BACKDROP_BLUR | WEX_BACKDROP_ACRYLIC | WEX_BACKDROP_GRADIENT |
                     WEX_BACKDROP_DARK | WEX_BACKDROP_TINT | WEX_BACKDROP_REGION))
        return ERROR_INVALID_PARAMETER;
    if (!id || id == root || id == desktop || id == overlay || id == owner)
        return ERROR_INVALID_PARAMETER;
    *x_error = 0;
    if (!pXGetWindowAttributes( dpy, id, &attr )) return ERROR_INVALID_WINDOW_HANDLE;
    pXChangeProperty( dpy, id, backdrop_atom, XA_CARDINAL, 32, PropModeReplace,
                      (unsigned char *)value, 6 );
    full_repaint = dirty = TRUE;
    return sync_ok() ? ERROR_SUCCESS : ERROR_INVALID_WINDOW_HANDLE;
}

DWORD compose_corner( unsigned long id, unsigned int radius )
{
    struct window *w;

    if (!enabled) return ERROR_NOT_SUPPORTED;
    if (radius > 16 && radius != WEX_CORNER_SQUARE) return ERROR_INVALID_PARAMETER;
    if (!id || id == root || id == desktop || id == overlay || id == owner)
        return ERROR_INVALID_PARAMETER;
    if (!(w = find_window( id )))
    {
        if (tree_dirty) scan_windows();
        if (!(w = find_window( id ))) return ERROR_FILE_NOT_FOUND;
    }
    w->corner = radius;
    tree_dirty = full_repaint = dirty = TRUE;
    return ERROR_SUCCESS;
}

static void clock_signal(void)
{
    pthread_mutex_lock( &clock_mutex );
    clock_generation++;
    pthread_cond_broadcast( &clock_cond );
    pthread_mutex_unlock( &clock_mutex );
}

/* seqlock write: make seq odd, publish the fields, make seq even. The release barriers order the field
 * writes before the even seq, which the readers acquire. There is a single writer (the WM loop thread). */
static void clock_publish( UINT32 flags, UINT64 frame, INT64 qpc_composed )
{
    struct dwm_clock_page *page = clock_page;
    if (!page) return;
    __atomic_add_fetch( &page->seq, 1, __ATOMIC_RELEASE );
    page->flags = flags;
    page->frame = frame;
    page->qpc_composed = qpc_composed;
    __atomic_add_fetch( &page->seq, 1, __ATOMIC_RELEASE );
}

/* one composed frame: bump the counter, stamp it, and wake the waiters */
static void clock_tick(void)
{
    LARGE_INTEGER now;
    if (!clock_page) return;
    NtQueryPerformanceCounter( &now, NULL );
    clock_publish( clock_page->flags | DWM_CLOCK_COMPOSED, clock_page->frame + 1, now.QuadPart );
    clock_signal();
}

/* composition went off: stop claiming composed frames and wake any waiter once */
static void clock_disable(void)
{
    if (!clock_page) return;
    clock_publish( clock_page->flags & ~DWM_CLOCK_COMPOSED, clock_page->frame, clock_page->qpc_composed );
    clock_signal();
}

DWORD compose_thumbnails( const struct compose_thumb *list, unsigned int count )
{
    if (!enabled) return count ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS;
    if (count > WEX_THUMB_MAX) return ERROR_INVALID_PARAMETER;
    if (count == thumb_count && !memcmp( thumbs, list, count * sizeof(*list) )) return ERROR_SUCCESS;
    memcpy( thumbs, list, count * sizeof(*list) );
    thumb_count = count;
    full_repaint = dirty = TRUE;
    return ERROR_SUCCESS;
}

DWORD compose_clock( void *page, HANDLE event )
{
    /* readers that still map an old page must stop waiting on it */
    if (page != clock_page) clock_disable();
    clock_page = page;
    return ERROR_SUCCESS;
}

long compose_event_mask( unsigned long id, long wm_mask )
{
    struct window *w;
    long required = PropertyChangeMask | StructureNotifyMask;
    if (!enabled) return wm_mask;
    if (id == desktop)
    {
        desktop_added_mask = SubstructureNotifyMask & ~wm_mask;
        wm_mask |= SubstructureNotifyMask;
    }
    if ((w = find_window( id )))
    {
        /* The WM may adopt or release a host after we tracked it. Remember its
         * latest mask, so disabling restores the WM's current requirements. */
        w->added_mask = required & ~wm_mask;
        wm_mask |= required;
    }
    return wm_mask;
}

DWORD compose_set( void *display, unsigned long root_window, unsigned long desktop_window,
                   int *error, BOOL enable )
{
    struct window *w;
    XWindowAttributes attr;
    int event, err, major, minor;
    char name[40];
    DWORD result;
    if (enable == enabled) return ERROR_SUCCESS;
    if (!enable)
    {
        unredirect();
        while ((w = windows)) { windows = w->next; free_window( w, FALSE ); }
        if (pXGetWindowAttributes( dpy, desktop, &attr ))
            pXSelectInput( dpy, desktop, attr.your_event_mask & ~desktop_added_mask );
        pXFixesDestroyRegion( dpy, damage_region );
        pXFixesDestroyRegion( dpy, scratch );
        pXFixesDestroyRegion( dpy, clip );
        if (pXGetSelectionOwner( dpy, selection ) == owner)
            pXSetSelectionOwner( dpy, selection, None, CurrentTime );
        pXDestroyWindow( dpy, owner );
        pXSync( dpy, False );
        owner = damage_region = scratch = clip = 0;
        enabled = dirty = tree_dirty = full_repaint = FALSE;
        clock_disable();
        return ERROR_SUCCESS;
    }
    if (!load_libraries()) return ERROR_NOT_SUPPORTED;
    dpy = display;
    root = root_window;
    desktop = desktop_window;
    x_error = error;
    if (!desktop || !pXGetWindowAttributes( dpy, desktop, &attr )) return ERROR_NOT_SUPPORTED;
    if (!pXCompositeQueryExtension( dpy, &event, &err ) ||
        !pXFixesQueryExtension( dpy, &event, &err ) ||
        !pXRenderQueryExtension( dpy, &event, &err ) ||
        !pXDamageQueryExtension( dpy, &damage_base, &err ) ||
        !pXShapeQueryExtension( dpy, &shape_base, &err )) return ERROR_NOT_SUPPORTED;
    major = minor = 0;
    if (!pXCompositeQueryVersion( dpy, &major, &minor ) || (!major && minor < 4)) return ERROR_NOT_SUPPORTED;
    major = 5; minor = 0;
    if (!pXFixesQueryVersion( dpy, &major, &minor ) || major < 2) return ERROR_NOT_SUPPORTED;
    major = 0; minor = 11;
    if (!pXRenderQueryVersion( dpy, &major, &minor ) || (!major && minor < 10)) return ERROR_NOT_SUPPORTED;
    major = 1; minor = 1;
    if (!pXDamageQueryVersion( dpy, &major, &minor ) || major < 1) return ERROR_NOT_SUPPORTED;
    snprintf( name, sizeof(name), "_NET_WM_CM_S%d", DefaultScreen( dpy ) );
    selection = pXInternAtom( dpy, name, False );
    opacity_atom = pXInternAtom( dpy, "_NET_WM_WINDOW_OPACITY", False );
    shadow_atom = pXInternAtom( dpy, "_WEX_DROPSHADOW", False );
    margins_atom = pXInternAtom( dpy, "_WEX_DWM_MARGINS", False );
    backdrop_atom = pXInternAtom( dpy, "_WEX_DWM_BACKDROP", False );
    /* Selection check/claim is atomic with respect to other X clients. */
    pXGrabServer( dpy );
    if (pXGetSelectionOwner( dpy, selection ))
    {
        pXUngrabServer( dpy );
        return ERROR_ALREADY_EXISTS;
    }
    owner = pXCreateSimpleWindow( dpy, root, -1, -1, 1, 1, 0, 0, 0 );
    pXSetSelectionOwner( dpy, selection, owner, CurrentTime );
    if (pXGetSelectionOwner( dpy, selection ) != owner)
    {
        pXDestroyWindow( dpy, owner );
        owner = 0;
        pXUngrabServer( dpy );
        return ERROR_ALREADY_EXISTS;
    }
    pXUngrabServer( dpy );
    desktop_added_mask = SubstructureNotifyMask & ~attr.your_event_mask;
    pXSelectInput( dpy, desktop, attr.your_event_mask | SubstructureNotifyMask );
    damage_region = pXFixesCreateRegion( dpy, NULL, 0 );
    scratch = pXFixesCreateRegion( dpy, NULL, 0 );
    clip = pXFixesCreateRegion( dpy, NULL, 0 );
    enabled = TRUE; /* cleanup is now valid even after partial failure */
    result = redirect();
    if (result)
    {
        compose_set( display, root, desktop, error, FALSE );
        return result;
    }
    tree_dirty = TRUE;
    /* ICCCM manager announcement for clients watching compositor ownership. */
    {
        XEvent message = { 0 };
        message.xclient.type = ClientMessage;
        message.xclient.window = root;
        message.xclient.message_type = pXInternAtom( dpy, "MANAGER", False );
        message.xclient.format = 32;
        message.xclient.data.l[0] = CurrentTime;
        message.xclient.data.l[1] = selection;
        message.xclient.data.l[2] = owner;
        pXSendEvent( dpy, root, False, StructureNotifyMask, &message );
    }
    return ERROR_SUCCESS;
}

void compose_event( void *xevent )
{
    XEvent *event = xevent;
    struct window *w;
    Window id = 0;
    if (!enabled) return;
    if (event->type == SelectionClear && event->xselectionclear.selection == selection)
    {
        compose_set( dpy, root, desktop, x_error, FALSE );
        return;
    }
    if (event->type == damage_base)
    {
        compose_damage_event *damage = (compose_damage_event *)event;
        if (!(w = find_window( damage->drawable )) || w->damage != damage->damage) return;
        pXDamageSubtract( dpy, w->damage, None, scratch );
        pXFixesTranslateRegion( dpy, scratch, w->x, w->y );
        pXFixesUnionRegion( dpy, damage_region, damage_region, scratch );
        if (w->id == desktop || w->id == mica_source)
        {
            mica_dirty = TRUE;
            if (mica_picture) full_repaint = TRUE;
        }
        /* ponytail: a damaged thumbnail source repaints everything; damage only its destinations if it costs */
        if (is_thumbnail_source( w->id )) full_repaint = TRUE;
        dirty = TRUE;
        return;
    }
    if (event->type == shape_base + ShapeNotify)
    {
        if (find_window( ((XShapeEvent *)event)->window )) full_repaint = dirty = TRUE;
        return;
    }
    switch (event->type)
    {
    case CreateNotify: id = event->xcreatewindow.window; break;
    case MapNotify: id = event->xmap.window; break;
    case ConfigureNotify:
        id = event->xconfigure.window;
        if ((w = find_window( id )) &&
            (event->xconfigure.width != w->attr.width || event->xconfigure.height != w->attr.height))
        {
            retain_surface( w );  /* the size before a minimize; see retain_surface */
            free_surface( w );
            if (id == desktop || id == mica_source) mica_dirty = TRUE;
        }
        break;
    case ReparentNotify:
        id = event->xreparent.window;
        remove_window( id, FALSE );
        break;
    case UnmapNotify: id = event->xunmap.window; remove_window( id, FALSE ); break;
    case DestroyNotify:
        id = event->xdestroywindow.window;
        remove_window( id, TRUE );
        if (id == desktop) { compose_set( dpy, root, desktop, x_error, FALSE ); return; }
        break;
    case PropertyNotify:
        if (event->xproperty.atom == shadow_atom && (w = find_window( event->xproperty.window )))
        {
            w->shadow = !!get_cardinal( w->id, shadow_atom, 0 );
            full_repaint = dirty = TRUE;
        }
        if (event->xproperty.atom == opacity_atom && (w = find_window( event->xproperty.window )))
        {
            update_opacity( w );
            full_repaint = dirty = TRUE;
        }
        if (event->xproperty.atom == margins_atom && (w = find_window( event->xproperty.window )))
        {
            update_margins( w );
            full_repaint = dirty = TRUE;
        }
        if (event->xproperty.atom == backdrop_atom && (w = find_window( event->xproperty.window )))
        {
            update_backdrop( w );
            full_repaint = dirty = TRUE;
        }
        return;
    case Expose: if (event->xexpose.window == overlay) full_repaint = dirty = TRUE; return;
    default: return;
    }
    /* Our overlay/selection windows must not recursively trigger rebuilding. */
    if (id == overlay || id == owner) return;
    tree_dirty = full_repaint = dirty = TRUE;
}

void compose_paint(void)
{
    XWindowAttributes attr;
    XRectangle rect;
    XRenderColor black = { 0, 0, 0, 0xffff };
    struct window *desk;
    XserverRegion desktop_clip = 0;
    if (!enabled || !dirty) return;
    if (tree_dirty) scan_windows();
    if (!pXGetWindowAttributes( dpy, root, &attr )) return;
    if (width != attr.width || height != attr.height)
    {
        width = attr.width;
        height = attr.height;
        free_output();
        full_repaint = TRUE;
    }
    if (bypass_fullscreen())
    {
        if (redirected_root) unredirect();
        dirty = FALSE;
        return;
    }
    if (!redirected_root)
    {
        DWORD result = redirect();
        if (result)
        {
            WARN( "cannot resume composition, error %u\n", (unsigned int)result );
            compose_set( dpy, root, desktop, x_error, FALSE );
            return;
        }
    }
    else if (!overlay && !create_output())
    {
        compose_set( dpy, root, desktop, x_error, FALSE );
        return;
    }
    if (full_repaint)
    {
        rect.x = rect.y = 0;
        rect.width = width;
        rect.height = height;
        pXFixesSetRegion( dpy, damage_region, &rect, 1 );
    }
    else expand_blur_damage();
    pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, damage_region );
    pXRenderFillRectangle( dpy, PictOpSrc, back_picture, &black, 0, 0, width, height );
    desk = find_window( desktop );
    paint_window( desk, 0 );
    if (desk)
    {
        desktop_clip = pXFixesCreateRegionFromWindow( dpy, desktop, WindowRegionBounding );
        pXFixesTranslateRegion( dpy, desktop_clip, desk->x, desk->y );
        paint_children( desktop, FALSE, desktop_clip );
        pXFixesDestroyRegion( dpy, desktop_clip );
    }
    paint_children( root, TRUE, 0 );
    pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, damage_region );
    pXFixesSetPictureClipRegion( dpy, overlay_picture, 0, 0, damage_region );
    pXRenderComposite( dpy, PictOpSrc, back_picture, None, overlay_picture,
                       0, 0, 0, 0, 0, 0, width, height );
    pXFixesSetRegion( dpy, damage_region, NULL, 0 );
    dirty = full_repaint = FALSE;
    /* the frame is composed (the caller flushes X right after): publish it and wake the waiters */
    clock_tick();
}

#else

DWORD compose_set( void *display, unsigned long root, unsigned long desktop, int *error, BOOL enabled )
{
    return enabled ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS;
}
BOOL compose_enabled(void) { return FALSE; }
DWORD compose_shadow( unsigned long window, BOOL shadow ) { return ERROR_NOT_SUPPORTED; }
DWORD compose_corner( unsigned long window, unsigned int radius ) { return ERROR_NOT_SUPPORTED; }
DWORD compose_backdrop( unsigned long window, unsigned int backdrop, unsigned int tint, const INT *region )
{ return ERROR_NOT_SUPPORTED; }
DWORD compose_clock( void *page, HANDLE event ) { return page ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS; }
DWORD compose_thumbnails( const struct compose_thumb *list, unsigned int count ) { return count ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS; }
DWORD compose_peek( unsigned long target, const UINT64 *keep, unsigned int count ) { return target ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS; }
void compose_event( void *event ) { }
void compose_paint(void) { }
long compose_event_mask( unsigned long window, long wm_mask ) { return wm_mask; }

#endif
