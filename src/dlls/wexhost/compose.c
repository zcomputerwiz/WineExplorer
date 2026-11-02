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
#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL(wexhost);

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
    FUNC(XRenderFillRectangle) FUNC(XRenderCreateSolidFill) FUNC(XRenderFindStandardFormat)
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
};

static Display *dpy;
static Window root, desktop, owner, overlay;
static Atom selection, opacity_atom, shadow_atom;
static int *x_error, damage_base, shape_base;
static BOOL enabled, redirected_root, redirected_desktop, tree_dirty, full_repaint, dirty;
static BOOL libraries_tried, libraries_loaded;
static struct window *windows;
static XserverRegion damage_region, scratch, clip;
static Pixmap back_pixmap;
static Picture back_picture, overlay_picture;
static int width, height;
static long desktop_added_mask;

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

static void remove_window( Window id, BOOL destroyed )
{
    struct window **entry;
    for (entry = &windows; *entry; entry = &(*entry)->next)
        if ((*entry)->id == id)
        {
            struct window *w = *entry;
            *entry = w->next;
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
    update_opacity( w );
    w->shadow = !!get_cardinal( w->id, shadow_atom, 0 );
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

static void unredirect(void)
{
    struct window *w;
    for (w = windows; w; w = w->next) free_surface( w );
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

static void paint_window( struct window *w, XserverRegion parent_clip )
{
    XserverRegion shape;
    int border;
    if (!w || !name_surface( w )) return;
    paint_shadow( w, parent_clip );
    shape = pXFixesCreateRegionFromWindow( dpy, w->id, WindowRegionBounding );
    border = w->attr.border_width;
    if (w->corner > 0)
    {
        int win_w = w->attr.width + 2 * border;
        int win_h = w->attr.height + 2 * border;
        int count = 0;
        XRectangle *rects = pXFixesFetchRegion( dpy, shape, &count );

        if (rects)
        {
            if (count == 1 && rects[0].width == win_w && rects[0].height == win_h)
            {
                XRectangle corner_rects[64];
                int corner_count = 0, y;
                unsigned int r = w->corner;

                if (r > 16) r = 16;
                for (y = 0; y < r; y++)
                {
                    int k = r - y;
                    int m = 4 * (r * r - k * (k - 1)) - 1;
                    int s = 0, inset;

                    while ((s + 1) * (s + 1) <= m) s++;
                    inset = r - (s + 1) / 2;
                    if (!inset) continue;

                    corner_rects[corner_count].x = rects[0].x;
                    corner_rects[corner_count].y = rects[0].y + y;
                    corner_rects[corner_count].width = inset;
                    corner_rects[corner_count].height = 1;
                    corner_count++;

                    corner_rects[corner_count].x = rects[0].x + win_w - inset;
                    corner_rects[corner_count].y = rects[0].y + y;
                    corner_rects[corner_count].width = inset;
                    corner_rects[corner_count].height = 1;
                    corner_count++;

                    corner_rects[corner_count].x = rects[0].x;
                    corner_rects[corner_count].y = rects[0].y + win_h - 1 - y;
                    corner_rects[corner_count].width = inset;
                    corner_rects[corner_count].height = 1;
                    corner_count++;

                    corner_rects[corner_count].x = rects[0].x + win_w - inset;
                    corner_rects[corner_count].y = rects[0].y + win_h - 1 - y;
                    corner_rects[corner_count].width = inset;
                    corner_rects[corner_count].height = 1;
                    corner_count++;
                }
                if (corner_count > 0)
                {
                    XserverRegion corners = pXFixesCreateRegion( dpy, corner_rects, corner_count );
                    pXFixesSubtractRegion( dpy, shape, shape, corners );
                    pXFixesDestroyRegion( dpy, corners );
                }
            }
            pXFree( rects );
        }
    }
    pXFixesTranslateRegion( dpy, shape, w->x, w->y );
    pXFixesIntersectRegion( dpy, clip, damage_region, shape );
    if (parent_clip) pXFixesIntersectRegion( dpy, clip, clip, parent_clip );
    pXFixesSetPictureClipRegion( dpy, back_picture, 0, 0, clip );
    /* A uniform alpha mask also requires Over for an otherwise opaque visual. */
    pXRenderComposite( dpy, w->attr.depth == 32 || w->alpha ? PictOpOver : PictOpSrc,
                       w->picture, w->alpha, back_picture, 0, 0, 0, 0,
                       w->x - border, w->y - border,
                       w->attr.width + 2 * border, w->attr.height + 2 * border );
    pXFixesDestroyRegion( dpy, shape );
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
        if (!skip_desktop || children[i] != desktop) paint_window( find_window( children[i] ), parent_clip );
    if (children) pXFree( children );
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

DWORD compose_corner( unsigned long id, unsigned int radius )
{
    struct window *w;

    if (!enabled) return ERROR_NOT_SUPPORTED;
    if (radius > 16) return ERROR_INVALID_PARAMETER;
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
            (event->xconfigure.width != w->attr.width || event->xconfigure.height != w->attr.height)) free_surface( w );
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
}

#else

DWORD compose_set( void *display, unsigned long root, unsigned long desktop, int *error, BOOL enabled )
{
    return enabled ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS;
}
BOOL compose_enabled(void) { return FALSE; }
DWORD compose_shadow( unsigned long window, BOOL shadow ) { return ERROR_NOT_SUPPORTED; }
DWORD compose_corner( unsigned long window, unsigned int radius ) { return ERROR_NOT_SUPPORTED; }
void compose_event( void *event ) { }
void compose_paint(void) { }
long compose_event_mask( unsigned long window, long wm_mask ) { return wm_mask; }

#endif
