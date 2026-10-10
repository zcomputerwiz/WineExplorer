/*
 * WineExplorer host services - X11 window manager for Linux windows
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

/* Programs that are not ours (native Linux programs, Steam, Proton games: anything outside our wineserver) map
 * their windows on the root window.  We are the window manager there: each window goes into the X window of a
 * Win32 "host" window that the shell creates, so it lives in the virtual desktop with the Win32 z-order, frame,
 * taskbar button and activation.  See docs/LINUX_WINDOWS.md.
 *
 * ICCCM/EWMH handling follows dwm (MIT/X Consortium License, (c) 2006-2026 Anselm R Garbe, Hiltjo Posthuma and
 * others: checkotherwm, scan, sendevent, setfocus, the ignored errors) and Gamescope's steamcompmgr (BSD-2-Clause,
 * (c) 2013-2022 Valve Corporation: size hints, unmanaged map and configure requests).  Openbox was consulted for
 * behavior only.
 *
 * Threads: one unix thread reads X events and queues WEX_WM_EVENTs for wm_next_event; the shell's calls run on its
 * own threads.  Every use of our Display holds x_mutex.  Errors on our Display are expected (windows vanish at any
 * time) and only recorded; errors on other displays go to the previous handler (winex11's). */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <dlfcn.h>
#ifdef SONAME_LIBX11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
enum { x_control_mask = ControlMask, x_lock_mask = LockMask, x_numlock_mask = Mod2Mask };
#undef Status
#undef ControlMask
#endif

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL(wexhost);

#ifdef SONAME_LIBX11

#define X11_FUNCS \
    FUNC(XOpenDisplay) FUNC(XCloseDisplay) FUNC(XSetErrorHandler) FUNC(XInternAtoms) FUNC(XSelectInput) \
    FUNC(XSync) FUNC(XFlush) FUNC(XPending) FUNC(XNextEvent) FUNC(XQueryTree) FUNC(XGetWindowAttributes) \
    FUNC(XGetWindowProperty) FUNC(XFree) FUNC(XChangeProperty) FUNC(XDeleteProperty) FUNC(XCreateSimpleWindow) \
    FUNC(XDestroyWindow) FUNC(XMapWindow) FUNC(XReparentWindow) FUNC(XMoveResizeWindow) FUNC(XConfigureWindow) \
    FUNC(XChangeSaveSet) FUNC(XSetInputFocus) FUNC(XGetInputFocus) FUNC(XSendEvent) FUNC(XKillClient) \
    FUNC(XGrabButton) FUNC(XUngrabButton) FUNC(XGrabKey) FUNC(XUngrabKey) FUNC(XAllowEvents) \
    FUNC(XKeysymToKeycode) FUNC(XGetWMNormalHints) FUNC(XGetTransientForHint) FUNC(XGetWMProtocols) \
    FUNC(XGetWMHints) FUNC(XGetTextProperty) FUNC(Xutf8TextPropertyToTextList) FUNC(XFreeStringList) \
    FUNC(XLookupKeysym) FUNC(XGrabPointer) FUNC(XUngrabPointer) FUNC(XCreateFontCursor) FUNC(XFreeCursor) \
    FUNC(XCheckTypedEvent) FUNC(XGrabKeyboard) FUNC(XUngrabKeyboard)
#define FUNC(name) static typeof(name) *p##name;
X11_FUNCS
#undef FUNC

enum atom
{
    WM_PROTOCOLS, WM_DELETE_WINDOW, WM_TAKE_FOCUS, WM_STATE, WM_CHANGE_STATE, UTF8_STRING,
    NET_SUPPORTED, NET_SUPPORTING_WM_CHECK, NET_WM_NAME, NET_WM_ICON, NET_ACTIVE_WINDOW, NET_CLIENT_LIST,
    NET_CLOSE_WINDOW, NET_FRAME_EXTENTS, NET_REQUEST_FRAME_EXTENTS,
    NET_WM_STATE, NET_WM_STATE_FULLSCREEN, NET_WM_STATE_MAXIMIZED_VERT, NET_WM_STATE_MAXIMIZED_HORZ,
    NET_WM_STATE_HIDDEN, NET_WM_STATE_ABOVE, NET_WM_STATE_FOCUSED,
    NET_WM_WINDOW_TYPE, NET_WM_WINDOW_TYPE_NORMAL, NET_WM_WINDOW_TYPE_DIALOG, NET_WM_WINDOW_TYPE_UTILITY,
    NET_WM_WINDOW_TYPE_TOOLBAR, NET_WM_WINDOW_TYPE_SPLASH, NET_WM_WINDOW_TYPE_DOCK, NET_WM_WINDOW_TYPE_DESKTOP,
    NET_WM_WINDOW_TYPE_MENU, NET_WM_WINDOW_TYPE_DROPDOWN_MENU, NET_WM_WINDOW_TYPE_POPUP_MENU,
    NET_WM_WINDOW_TYPE_TOOLTIP, NET_WM_WINDOW_TYPE_NOTIFICATION, NET_WM_WINDOW_TYPE_COMBO, NET_WM_WINDOW_TYPE_DND,
    MOTIF_WM_HINTS, NET_WM_MOVERESIZE, GTK_SHOW_WINDOW_MENU, NET_WM_PID,
    ATOM_COUNT
};

static const char * const atom_names[ATOM_COUNT] =
{
    "WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS", "WM_STATE", "WM_CHANGE_STATE", "UTF8_STRING",
    "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME", "_NET_WM_ICON", "_NET_ACTIVE_WINDOW",
    "_NET_CLIENT_LIST", "_NET_CLOSE_WINDOW", "_NET_FRAME_EXTENTS", "_NET_REQUEST_FRAME_EXTENTS",
    "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
    "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_FOCUSED",
    "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_WM_WINDOW_TYPE_UTILITY",
    "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_DOCK",
    "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_MENU", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
    "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_DND",
    "_MOTIF_WM_HINTS", "_NET_WM_MOVERESIZE", "_GTK_SHOW_WINDOW_MENU", "_NET_WM_PID",
};

#define MWM_HINTS_DECORATIONS 2

struct client
{
    Window window;
    Window parent;        /* the host's X window once managed */
    BOOL   managed;
    int    ignore_unmaps; /* unmaps caused by reparenting a mapped window */
    RECT   screen;        /* client area on the screen, as last configured */
};

#define MAX_CLIENTS 512
#define EVENT_QUEUE 512

static pthread_once_t load_once = PTHREAD_ONCE_INIT;
static BOOL loaded;
static pthread_mutex_t x_mutex = PTHREAD_MUTEX_INITIALIZER;
static Display *display;
static Window root, desktop_window, check_window;
static Atom atoms[ATOM_COUNT];
static int (*previous_handler)( Display *, XErrorEvent * );
static int x_error;
static struct client clients[MAX_CLIENTS];
static unsigned int client_count;
static Window active_window;
static Window drag_window;   /* moved or sized for its program (_NET_WM_MOVERESIZE) */
static Cursor drag_cursor;
static KeyCode super_keys[2], escape_key, tab_key, f4_key;
static pthread_t loop_thread;
static int wake_pipe[2] = { -1, -1 };
static BOOL running;

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static WEX_WM_EVENT queue[EVENT_QUEUE];
static unsigned int queue_head, queue_count;

static void load_library(void)
{
    void *x11 = dlopen( SONAME_LIBX11, RTLD_NOW );
    if (!x11) return;
#define FUNC(name) if (!(p##name = dlsym( x11, #name ))) return;
    X11_FUNCS
#undef FUNC
    loaded = TRUE;
}

/* Windows vanish between an event and our requests about them, so errors on our display are normal. */
static int error_handler( Display *dpy, XErrorEvent *event )
{
    if (dpy != display) return previous_handler ? previous_handler( dpy, event ) : 0;
    TRACE( "X error %d request %d resource %lx\n", event->error_code, event->request_code, event->resourceid );
    x_error = event->error_code;
    return 0;
}

static void push_event( const WEX_WM_EVENT *event )
{
    pthread_mutex_lock( &queue_mutex );
    if (queue_count < EVENT_QUEUE)
    {
        queue[(queue_head + queue_count) % EVENT_QUEUE] = *event;
        queue_count++;
        pthread_cond_signal( &queue_cond );
    }
    else ERR( "event queue full, event %u for %lx dropped\n", event->type, (unsigned long)event->window );
    pthread_mutex_unlock( &queue_mutex );
}

static void push_simple( DWORD type, Window window, DWORD value )
{
    WEX_WM_EVENT event;
    memset( &event, 0, sizeof(event) );
    event.type = type;
    event.window = window;
    event.value = value;
    push_event( &event );
}

/* Events read while answering a shell call stay in Xlib's queue: make the loop look again. */
static void wake_loop(void)
{
    char c = 0;
    if (wake_pipe[1] != -1) write( wake_pipe[1], &c, 1 );
}

static struct client *find_client( Window window )
{
    unsigned int i;
    for (i = 0; i < client_count; i++) if (clients[i].window == window) return &clients[i];
    return NULL;
}

static struct client *find_client_by_parent( Window parent )
{
    unsigned int i;
    for (i = 0; i < client_count; i++) if (clients[i].managed && clients[i].parent == parent) return &clients[i];
    return NULL;
}

static struct client *add_client( Window window )
{
    struct client *client = find_client( window );
    if (client) return client;
    if (client_count == MAX_CLIENTS) return NULL;
    client = &clients[client_count++];
    memset( client, 0, sizeof(*client) );
    client->window = window;
    return client;
}

static void remove_client( struct client *client )
{
    if (client->window == active_window) active_window = 0;
    *client = clients[--client_count];
}

static void update_client_list(void)
{
    Window list[MAX_CLIENTS];
    unsigned int i, count = 0;
    for (i = 0; i < client_count; i++) if (clients[i].managed) list[count++] = clients[i].window;
    pXChangeProperty( display, root, atoms[NET_CLIENT_LIST], XA_WINDOW, 32, PropModeReplace, (unsigned char *)list, count );
}

/* Property of 32-bit items (longs in Xlib); returns the count, *data freed with XFree. */
static unsigned long get_property( Window window, Atom property, Atom type, unsigned long max, unsigned long **data )
{
    Atom actual;
    int format;
    unsigned long count = 0, left;
    unsigned char *value = NULL;

    *data = NULL;
    if (pXGetWindowProperty( display, window, property, 0, max, False, type, &actual, &format, &count, &left,
                             &value ) != Success || !value)
        return 0;
    if (format != 32 || !count) { pXFree( value ); return 0; }
    *data = (unsigned long *)value;
    return count;
}

static Atom get_atom_property( Window window, Atom property )
{
    unsigned long *data;
    Atom ret = None;
    if (get_property( window, property, XA_ATOM, 1, &data )) ret = data[0];
    if (data) pXFree( data );
    return ret;
}

static void set_wm_state( Window window, long state )
{
    long data[2] = { state, None };
    pXChangeProperty( display, window, atoms[WM_STATE], atoms[WM_STATE], 32, PropModeReplace, (unsigned char *)data, 2 );
}

static BOOL has_protocol( Window window, Atom protocol )
{
    Atom *protocols;
    int count;
    BOOL ret = FALSE;
    if (pXGetWMProtocols( display, window, &protocols, &count ))
    {
        while (!ret && count--) ret = protocols[count] == protocol;
        pXFree( protocols );
    }
    return ret;
}

static void send_protocol( Window window, Atom protocol )
{
    XEvent event;
    memset( &event, 0, sizeof(event) );
    event.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = atoms[WM_PROTOCOLS];
    event.xclient.format = 32;
    event.xclient.data.l[0] = protocol;
    event.xclient.data.l[1] = CurrentTime;
    pXSendEvent( display, window, False, NoEventMask, &event );
}

/* ICCCM 4.1.7: windows that take input get the focus, those that ask for WM_TAKE_FOCUS get the message too */
static void focus_client( struct client *client )
{
    XWMHints *hints = pXGetWMHints( display, client->window );
    BOOL input = !hints || !(hints->flags & InputHint) || hints->input;

    if (hints) pXFree( hints );
    if (input) pXSetInputFocus( display, client->window, RevertToParent, CurrentTime );
    if (has_protocol( client->window, atoms[WM_TAKE_FOCUS] )) send_protocol( client->window, atoms[WM_TAKE_FOCUS] );
}

/* ICCCM 4.1.5: tell the program where its window really is on the screen */
static void send_configure( struct client *client )
{
    XEvent event;
    memset( &event, 0, sizeof(event) );
    event.type = ConfigureNotify;
    event.xconfigure.display = display;
    event.xconfigure.event = client->window;
    event.xconfigure.window = client->window;
    event.xconfigure.x = client->screen.left;
    event.xconfigure.y = client->screen.top;
    event.xconfigure.width = client->screen.right - client->screen.left;
    event.xconfigure.height = client->screen.bottom - client->screen.top;
    event.xconfigure.above = None;
    pXSendEvent( display, client->window, False, StructureNotifyMask, &event );
}

static void set_frame_extents( Window window )
{
    long extents[4] = { 0, 0, 0, 0 };   /* the frame is the host's, outside our X tree for the program */
    pXChangeProperty( display, window, atoms[NET_FRAME_EXTENTS], XA_CARDINAL, 32, PropModeReplace,
                      (unsigned char *)extents, 4 );
}

static void grab_input( Window window, BOOL grab )
{
    static const unsigned int locks[] = { 0, x_lock_mask, x_numlock_mask, x_lock_mask | x_numlock_mask };
    unsigned int i;

    if (!grab)
    {
        pXUngrabButton( display, AnyButton, AnyModifier, window );
        pXUngrabKey( display, AnyKey, AnyModifier, window );
        return;
    }
    /* click to activate: the click is replayed to the program afterwards (wm_loop) */
    pXGrabButton( display, AnyButton, AnyModifier, window, False, ButtonPressMask, GrabModeSync, GrabModeAsync,
                  None, None );
    /* the shell keys: the program has the keyboard, so Wine never sees them; key_event replays them into Wine */
    for (i = 0; i < 2; i++)
        if (super_keys[i]) pXGrabKey( display, super_keys[i], AnyModifier, window, True, GrabModeAsync, GrabModeAsync );
    for (i = 0; i < ARRAYSIZE(locks); i++)
    {
        if (escape_key) pXGrabKey( display, escape_key, x_control_mask | locks[i], window, True, GrabModeAsync, GrabModeAsync );
        if (tab_key)
        {
            pXGrabKey( display, tab_key, Mod1Mask | locks[i], window, True, GrabModeAsync, GrabModeAsync );
            pXGrabKey( display, tab_key, Mod1Mask | ShiftMask | locks[i], window, True, GrabModeAsync, GrabModeAsync );
        }
        if (f4_key) pXGrabKey( display, f4_key, Mod1Mask | locks[i], window, True, GrabModeAsync, GrabModeAsync );
    }
}

static DWORD window_type( Window window )
{
    Atom type = get_atom_property( window, atoms[NET_WM_WINDOW_TYPE] );
    if (type == atoms[NET_WM_WINDOW_TYPE_DIALOG]) return WEX_WM_TYPE_DIALOG;
    if (type == atoms[NET_WM_WINDOW_TYPE_UTILITY] || type == atoms[NET_WM_WINDOW_TYPE_TOOLBAR]) return WEX_WM_TYPE_UTILITY;
    if (type == atoms[NET_WM_WINDOW_TYPE_SPLASH]) return WEX_WM_TYPE_SPLASH;
    return WEX_WM_TYPE_NORMAL;
}

/* Docks, desktops, menus and the like are shown as they are, without a host. */
static BOOL is_unmanaged_type( Window window )
{
    Atom type = get_atom_property( window, atoms[NET_WM_WINDOW_TYPE] );
    return type == atoms[NET_WM_WINDOW_TYPE_DOCK] || type == atoms[NET_WM_WINDOW_TYPE_DESKTOP] ||
           type == atoms[NET_WM_WINDOW_TYPE_MENU] || type == atoms[NET_WM_WINDOW_TYPE_DROPDOWN_MENU] ||
           type == atoms[NET_WM_WINDOW_TYPE_POPUP_MENU] || type == atoms[NET_WM_WINDOW_TYPE_TOOLTIP] ||
           type == atoms[NET_WM_WINDOW_TYPE_NOTIFICATION] || type == atoms[NET_WM_WINDOW_TYPE_COMBO] ||
           type == atoms[NET_WM_WINDOW_TYPE_DND];
}

static DWORD net_wm_states( Window window )
{
    unsigned long *data, count, i;
    DWORD states = 0;

    count = get_property( window, atoms[NET_WM_STATE], XA_ATOM, 32, &data );
    for (i = 0; i < count; i++)
    {
        if (data[i] == atoms[NET_WM_STATE_FULLSCREEN]) states |= WEX_WM_STATE_FULLSCREEN;
        else if (data[i] == atoms[NET_WM_STATE_MAXIMIZED_VERT] || data[i] == atoms[NET_WM_STATE_MAXIMIZED_HORZ])
            states |= WEX_WM_STATE_MAXIMIZED;
        else if (data[i] == atoms[NET_WM_STATE_HIDDEN]) states |= WEX_WM_STATE_MINIMIZED;
        else if (data[i] == atoms[NET_WM_STATE_ABOVE]) states |= WEX_WM_STATE_ABOVE;
    }
    if (data) pXFree( data );
    return states;
}

static DWORD state_from_atom( Atom atom )
{
    if (atom == atoms[NET_WM_STATE_FULLSCREEN]) return WEX_WM_STATE_FULLSCREEN;
    if (atom == atoms[NET_WM_STATE_MAXIMIZED_VERT] || atom == atoms[NET_WM_STATE_MAXIMIZED_HORZ]) return WEX_WM_STATE_MAXIMIZED;
    if (atom == atoms[NET_WM_STATE_HIDDEN]) return WEX_WM_STATE_MINIMIZED;
    if (atom == atoms[NET_WM_STATE_ABOVE]) return WEX_WM_STATE_ABOVE;
    return 0;
}

static void get_size_limits( Window window, WEX_WM_EVENT *event )
{
    XSizeHints hints;
    long supplied = 0;

    if (!pXGetWMNormalHints( display, window, &hints, &supplied )) return;
    if (supplied & PMinSize) { event->min_size.cx = hints.min_width; event->min_size.cy = hints.min_height; }
    if (supplied & PMaxSize) { event->max_size.cx = hints.max_width; event->max_size.cy = hints.max_height; }
}

static BOOL no_border( Window window )
{
    unsigned long *data;
    BOOL ret = FALSE;
    /* flags, functions, decorations, input mode, status */
    if (get_property( window, atoms[MOTIF_WM_HINTS], atoms[MOTIF_WM_HINTS], 5, &data ) >= 3)
        ret = (data[0] & MWM_HINTS_DECORATIONS) && !data[2];
    if (data) pXFree( data );
    return ret;
}

/* A window asks to be shown: describe it to the shell, which creates its host and calls wm_manage. */
static void map_request( Window window, BOOL existing )
{
    XWindowAttributes attr;
    XWMHints *hints;
    struct client *client;
    WEX_WM_EVENT event;
    Window owner = None;

    if ((client = find_client( window )))
    {
        if (client->managed) pXMapWindow( display, window );
        return;   /* otherwise the shell is creating its host already */
    }
    if (window == desktop_window || window == check_window || !pXGetWindowAttributes( display, window, &attr ) ||
        attr.override_redirect || is_unmanaged_type( window ) || !(client = add_client( window )))
    {
        if (!existing) pXMapWindow( display, window );
        return;
    }

    pXSelectInput( display, window, PropertyChangeMask | StructureNotifyMask );
    memset( &event, 0, sizeof(event) );
    event.type = WEX_WM_MAP;
    event.window = window;
    event.rect.left = attr.x;
    event.rect.top = attr.y;
    event.rect.right = attr.x + attr.width;
    event.rect.bottom = attr.y + attr.height;
    if (pXGetTransientForHint( display, window, &owner ) && owner != root) event.owner = owner;
    event.window_type = window_type( window );
    get_size_limits( window, &event );
    /* a window placed by its program or the user (-geometry) keeps its place; GTK maps at the origin with a user
     * position when it means "anywhere", so the origin never counts as a choice */
    if (attr.x || attr.y) event.flags |= WEX_WM_POSITION;
    if (no_border( window )) event.flags |= WEX_WM_NO_BORDER;
    if (existing) event.flags |= WEX_WM_EXISTING;
    event.states = net_wm_states( window );
    if ((hints = pXGetWMHints( display, window )))
    {
        if ((hints->flags & StateHint) && hints->initial_state == IconicState) event.states |= WEX_WM_STATE_MINIMIZED;
        pXFree( hints );
    }
    if (existing && attr.map_state != IsUnmapped) client->ignore_unmaps++;
    TRACE( "map %lx %s owner %lx type %u flags %x states %x\n", window, wine_dbgstr_rect( &event.rect ), owner,
           event.window_type, event.flags, event.states );
    push_event( &event );
}

static void end_drag( int x, int y );
static void end_replay_for( Window window );

/* The program withdrew or destroyed its window: hand it back to the root window and tell the shell. */
static void withdraw( struct client *client, BOOL destroyed )
{
    Window window = client->window;
    BOOL managed = client->managed;

    TRACE( "withdraw %lx destroyed %d\n", window, destroyed );
    if (drag_window == window) end_drag( 0, 0 );
    end_replay_for( window );
    if (!destroyed)
    {
        pXSelectInput( display, window, NoEventMask );
        if (managed)
        {
            grab_input( window, FALSE );
            pXReparentWindow( display, window, root, client->screen.left, client->screen.top );
            pXChangeSaveSet( display, window, SetModeDelete );
        }
        set_wm_state( window, WithdrawnState );
    }
    if (client->parent) pXSelectInput( display, client->parent, NoEventMask );
    remove_client( client );
    if (managed) update_client_list();
    push_simple( WEX_WM_WITHDRAW, window, 0 );
}

static void configure_request( XConfigureRequestEvent *request )
{
    struct client *client = find_client( request->window );
    XWindowChanges changes;
    WEX_WM_EVENT event;

    if (!client || !client->managed)
    {
        changes.x = request->x;
        changes.y = request->y;
        changes.width = request->width;
        changes.height = request->height;
        changes.border_width = request->border_width;
        changes.sibling = request->above;
        changes.stack_mode = request->detail;
        pXConfigureWindow( display, request->window, request->value_mask, &changes );
        return;
    }
    memset( &event, 0, sizeof(event) );
    event.type = WEX_WM_CONFIGURE;
    event.window = client->window;
    event.rect = client->screen;
    if (request->value_mask & (CWX | CWY))
    {
        event.flags |= WEX_WM_MOVE;
        if (request->value_mask & CWX)
        {
            event.rect.right += request->x - event.rect.left;
            event.rect.left = request->x;
        }
        if (request->value_mask & CWY)
        {
            event.rect.bottom += request->y - event.rect.top;
            event.rect.top = request->y;
        }
    }
    if (request->value_mask & (CWWidth | CWHeight))
    {
        event.flags |= WEX_WM_SIZE;
        if (request->value_mask & CWWidth) event.rect.right = event.rect.left + request->width;
        if (request->value_mask & CWHeight) event.rect.bottom = event.rect.top + request->height;
    }
    /* ICCCM 4.1.5: the answer to a request that changes nothing (for now) is the current geometry */
    send_configure( client );
    if (event.flags) push_event( &event );
}

static void push_point( DWORD type, Window window, DWORD value, int x, int y )
{
    WEX_WM_EVENT event;
    memset( &event, 0, sizeof(event) );
    event.type = type;
    event.window = window;
    event.value = value;
    event.rect.left = event.rect.right = x;
    event.rect.top = event.rect.bottom = y;
    push_event( &event );
}

static void end_drag( int x, int y )
{
    pXUngrabPointer( display, CurrentTime );
    if (drag_cursor) pXFreeCursor( display, drag_cursor );
    drag_cursor = 0;
    push_point( WEX_WM_DRAG, drag_window, 1, x, y );
    drag_window = 0;
}

/* EWMH _NET_WM_MOVERESIZE from a window that draws its own title bar (GTK, Steam): as window managers do, we grab
 * the pointer and the shell moves or sizes the host while it moves; Wine's own move loop cannot be used, it never
 * saw the button go down. */
static void start_drag( struct client *client, long direction, int x, int y )
{
    static const unsigned int cursors[9] =
    {
        XC_top_left_corner, XC_top_side, XC_top_right_corner, XC_right_side, XC_bottom_right_corner,
        XC_bottom_side, XC_bottom_left_corner, XC_left_side, XC_fleur
    };

    if (drag_window || direction < 0 || direction > 8) return;
    drag_cursor = pXCreateFontCursor( display, cursors[direction] );
    if (pXGrabPointer( display, root, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
                       None, drag_cursor, CurrentTime ) != GrabSuccess)
    {
        WARN( "cannot grab the pointer to move %lx\n", client->window );
        pXFreeCursor( display, drag_cursor );
        drag_cursor = 0;
        return;
    }
    drag_window = client->window;
    push_point( WEX_WM_MOVERESIZE, drag_window, direction, x, y );
}

static void client_message( XClientMessageEvent *message )
{
    struct client *client = find_client( message->window );
    WEX_WM_EVENT event;

    if (message->message_type == atoms[NET_REQUEST_FRAME_EXTENTS])
    {
        set_frame_extents( message->window );
        return;
    }
    if (!client) return;
    if (message->message_type == atoms[NET_WM_STATE])
    {
        memset( &event, 0, sizeof(event) );
        event.type = WEX_WM_STATE;
        event.window = client->window;
        event.value = message->data.l[0];
        event.states = state_from_atom( message->data.l[1] ) | state_from_atom( message->data.l[2] );
        if (event.states) push_event( &event );
    }
    else if (message->message_type == atoms[NET_ACTIVE_WINDOW]) push_simple( WEX_WM_ACTIVATE, client->window, 0 );
    else if (message->message_type == atoms[WM_CHANGE_STATE] && message->data.l[0] == IconicState)
        push_simple( WEX_WM_MINIMIZE, client->window, 0 );
    else if (message->message_type == atoms[NET_CLOSE_WINDOW])
    {
        if (has_protocol( client->window, atoms[WM_DELETE_WINDOW] )) send_protocol( client->window, atoms[WM_DELETE_WINDOW] );
        else pXKillClient( display, client->window );
    }
    else if (message->message_type == atoms[NET_WM_MOVERESIZE] && client->managed)
    {
        /* x, y, direction (0-7 edges, 8 move, 9-10 keyboard, 11 cancel), button, source */
        if (message->data.l[2] == 11) { if (drag_window == client->window) end_drag( message->data.l[0], message->data.l[1] ); }
        else start_drag( client, message->data.l[2], message->data.l[0], message->data.l[1] );
    }
    else if (message->message_type == atoms[GTK_SHOW_WINDOW_MENU] && client->managed)
        push_point( WEX_WM_MENU, client->window, 0, message->data.l[1], message->data.l[2] );
    else TRACE( "unhandled client message %lu for %lx\n", message->message_type, message->window );
}

static void property_notify( XPropertyEvent *property )
{
    struct client *client = find_client( property->window );
    WEX_WM_EVENT event;

    if (!client || property->state != PropertyNewValue) return;
    if (property->atom == atoms[NET_WM_NAME] || property->atom == XA_WM_NAME)
        push_simple( WEX_WM_TITLE, client->window, 0 );
    else if (property->atom == atoms[NET_WM_ICON]) push_simple( WEX_WM_ICON, client->window, 0 );
    else if (property->atom == XA_WM_NORMAL_HINTS)
    {
        memset( &event, 0, sizeof(event) );
        event.type = WEX_WM_HINTS;
        event.window = client->window;
        get_size_limits( client->window, &event );
        push_event( &event );
    }
}

static WORD vk_from_keysym( KeySym sym )
{
    if (sym >= XK_a && sym <= XK_z) return 'A' + sym - XK_a;
    if (sym >= XK_0 && sym <= XK_9) return '0' + sym - XK_0;
    if (sym >= XK_F1 && sym <= XK_F24) return VK_F1 + sym - XK_F1;
    switch (sym)
    {
    case XK_Super_L: return VK_LWIN;
    case XK_Super_R: return VK_RWIN;
    case XK_Alt_L: case XK_Meta_L: return VK_LMENU;
    case XK_Alt_R: case XK_Meta_R: case XK_ISO_Level3_Shift: return VK_RMENU;
    case XK_Control_L: return VK_LCONTROL;
    case XK_Control_R: return VK_RCONTROL;
    case XK_Shift_L: return VK_LSHIFT;
    case XK_Shift_R: return VK_RSHIFT;
    case XK_Tab: case XK_ISO_Left_Tab: return VK_TAB;
    case XK_Escape: return VK_ESCAPE;
    case XK_Return: case XK_KP_Enter: return VK_RETURN;
    case XK_space: return VK_SPACE;
    case XK_Left: return VK_LEFT;
    case XK_Right: return VK_RIGHT;
    case XK_Up: return VK_UP;
    case XK_Down: return VK_DOWN;
    case XK_Home: return VK_HOME;
    case XK_End: return VK_END;
    case XK_Pause: case XK_Break: return VK_PAUSE;
    case XK_Delete: return VK_DELETE;
    }
    return 0;
}

static void push_key( WORD vk, BOOL up )
{
    WEX_WM_EVENT event;
    TRACE( "replay %02x %s\n", vk, up ? "up" : "down" );
    memset( &event, 0, sizeof(event) );
    event.type = WEX_WM_KEY;
    event.value = vk;
    if (up) event.flags = WEX_WM_KEY_UP;
    push_event( &event );
}

/* The shell keys reach Wine as if a Win32 window had had the keyboard, so Explorer's keyboard hook, its hotkeys and
 * DefWindowProc (Alt+F4) handle them as they do for Windows programs.  A grab of ours starts a replay: the modifiers
 * already held are pressed in Wine first, every key until the combination ends follows, and the replay ends with
 * the key that started it (Windows key, Ctrl+Esc, Alt+F4) or with Alt (Alt+Tab, during which we hold the keyboard). */
static struct
{
    BOOL  active;
    BOOL  alt_tab;
    KeyCode key;        /* the key that started it */
    Window window;      /* the window that had the keyboard */
    WORD  held[3];      /* modifiers we pressed in Wine */
    unsigned int held_count;
} replay;

static void end_replay(void)
{
    while (replay.held_count) push_key( replay.held[--replay.held_count], TRUE );
    if (replay.alt_tab) pXUngrabKeyboard( display, CurrentTime );
    memset( &replay, 0, sizeof(replay) );
}

/* Alt+F4 closes the window before its F4 comes up: end the replay with the window, or it would never end */
static void end_replay_for( Window window )
{
    if (replay.active && !replay.alt_tab && replay.window == window) end_replay();
}

static void key_event( XKeyEvent *key )
{
    WORD vk = vk_from_keysym( pXLookupKeysym( key, 0 ) );
    BOOL up = key->type == KeyRelease;

    if (!replay.active)
    {
        if (up) return;
        TRACE( "replay of keycode %u, state %#x\n", key->keycode, key->state );
        replay.active = TRUE;
        replay.key = key->keycode;
        replay.window = key->window;
        if (key->state & x_control_mask) push_key( replay.held[replay.held_count++] = VK_LCONTROL, FALSE );
        if (key->state & Mod1Mask) push_key( replay.held[replay.held_count++] = VK_LMENU, FALSE );
        if (key->state & ShiftMask) push_key( replay.held[replay.held_count++] = VK_LSHIFT, FALSE );
        /* Alt+Tab lasts until Alt is released: keep the keyboard until then */
        if (key->keycode == tab_key && (key->state & Mod1Mask))
            replay.alt_tab = pXGrabKeyboard( display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime ) == GrabSuccess;
    }
    if (vk)
    {
        /* modifiers we pressed for the replay and that are released now need no second release */
        unsigned int i;
        for (i = 0; up && i < replay.held_count; i++)
            if (replay.held[i] == vk) { replay.held[i] = replay.held[--replay.held_count]; break; }
        push_key( vk, up );
    }
    if (up && (replay.alt_tab ? vk == VK_LMENU || vk == VK_RMENU : key->keycode == replay.key)) end_replay();
}

static void handle_event( XEvent *event )
{
    struct client *client;

    switch (event->type)
    {
    case MapRequest:
        map_request( event->xmaprequest.window, FALSE );
        break;
    case ConfigureRequest:
        configure_request( &event->xconfigurerequest );
        break;
    case UnmapNotify:
        /* reported to the window itself and to its parent: count the window's own report (or ICCCM's synthetic one) */
        if (event->xunmap.event != event->xunmap.window && !event->xunmap.send_event) break;
        if (!(client = find_client( event->xunmap.window ))) break;
        if (client->ignore_unmaps && !event->xunmap.send_event) client->ignore_unmaps--;
        else withdraw( client, FALSE );
        break;
    case DestroyNotify:
        if ((client = find_client( event->xdestroywindow.window ))) withdraw( client, TRUE );
        break;
    case ClientMessage:
        client_message( &event->xclient );
        break;
    case PropertyNotify:
        property_notify( &event->xproperty );
        break;
    case FocusIn:
        /* Wine focused the host's X window: the keyboard belongs to the program inside */
        if (event->xfocus.detail != NotifyInferior && event->xfocus.detail != NotifyPointer &&
            (client = find_client_by_parent( event->xfocus.window )))
        {
            Window focus;
            int revert;
            /* A queued FocusIn may outlive a switch to another Win32 process.
             * Also leave focus on GTK child widgets instead of moving it back. */
            pXGetInputFocus( display, &focus, &revert );
            if (focus == client->parent) focus_client( client );
        }
        break;
    case ButtonPress:
        TRACE( "button %u on %lx, active %lx\n", event->xbutton.button, event->xbutton.window, active_window );
        pXAllowEvents( display, ReplayPointer, CurrentTime );
        /* Win32 focus can change in another process before its deactivation
         * reaches us. A cached active_window is not enough to suppress a click. */
        if ((client = find_client( event->xbutton.window )))
            push_simple( WEX_WM_ACTIVATE, client->window, 0 );
        break;
    case MotionNotify:
        if (!drag_window) break;
        while (pXCheckTypedEvent( display, MotionNotify, event )) ;   /* only the latest position matters */
        push_point( WEX_WM_DRAG, drag_window, 0, event->xmotion.x_root, event->xmotion.y_root );
        break;
    case ButtonRelease:
        if (drag_window) end_drag( event->xbutton.x_root, event->xbutton.y_root );
        break;
    case KeyPress:
    case KeyRelease:
        key_event( &event->xkey );
        break;
    }
}

static void *wm_loop( void *arg )
{
    struct pollfd fds[2];

    fds[0].fd = ConnectionNumber( display );
    fds[1].fd = wake_pipe[0];
    while (running)
    {
        pthread_mutex_lock( &x_mutex );
        while (running && pXPending( display ))
        {
            XEvent event;
            pXNextEvent( display, &event );
            handle_event( &event );
        }
        pXFlush( display );
        pthread_mutex_unlock( &x_mutex );

        fds[0].events = fds[1].events = POLLIN;
        if (poll( fds, 2, 1000 ) > 0 && (fds[1].revents & POLLIN))
        {
            char buffer[64];
            while (read( wake_pipe[0], buffer, sizeof(buffer) ) > 0) ;
        }
    }
    return NULL;
}

static void set_supported(void)
{
    static const enum atom supported[] =
    {
        NET_SUPPORTED, NET_SUPPORTING_WM_CHECK, NET_WM_NAME, NET_WM_ICON, NET_ACTIVE_WINDOW, NET_CLIENT_LIST,
        NET_CLOSE_WINDOW, NET_FRAME_EXTENTS, NET_REQUEST_FRAME_EXTENTS, NET_WM_STATE, NET_WM_STATE_FULLSCREEN,
        NET_WM_STATE_MAXIMIZED_VERT, NET_WM_STATE_MAXIMIZED_HORZ, NET_WM_STATE_HIDDEN, NET_WM_STATE_ABOVE,
        NET_WM_STATE_FOCUSED, NET_WM_WINDOW_TYPE, NET_WM_WINDOW_TYPE_NORMAL, NET_WM_WINDOW_TYPE_DIALOG,
        NET_WM_WINDOW_TYPE_UTILITY, NET_WM_WINDOW_TYPE_SPLASH, NET_WM_MOVERESIZE, GTK_SHOW_WINDOW_MENU,
    };
    Atom list[ARRAYSIZE(supported)];
    static const char name[] = "WineExplorer";
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(supported); i++) list[i] = atoms[supported[i]];
    check_window = pXCreateSimpleWindow( display, root, -1, -1, 1, 1, 0, 0, 0 );
    pXChangeProperty( display, check_window, atoms[NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace,
                      (unsigned char *)&check_window, 1 );
    pXChangeProperty( display, check_window, atoms[NET_WM_NAME], atoms[UTF8_STRING], 8, PropModeReplace,
                      (const unsigned char *)name, sizeof(name) - 1 );
    pXChangeProperty( display, root, atoms[NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace,
                      (unsigned char *)&check_window, 1 );
    pXChangeProperty( display, root, atoms[NET_SUPPORTED], XA_ATOM, 32, PropModeReplace, (unsigned char *)list,
                      ARRAYSIZE(list) );
    pXDeleteProperty( display, root, atoms[NET_CLIENT_LIST] );
    pXDeleteProperty( display, root, atoms[NET_ACTIVE_WINDOW] );
}

/* windows already on the screen, owners before their transients */
static void scan(void)
{
    Window dummy, owner, *children = NULL;
    unsigned int i, count = 0, pass;
    XWindowAttributes attr;

    if (!pXQueryTree( display, root, &dummy, &dummy, &children, &count )) return;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < count; i++)
        {
            if (!pXGetWindowAttributes( display, children[i], &attr ) || attr.map_state != IsViewable) continue;
            if ((pXGetTransientForHint( display, children[i], &owner ) != 0) != (pass == 1)) continue;
            map_request( children[i], TRUE );
        }
    if (children) pXFree( children );
}

NTSTATUS wm_start( void *args )
{
    struct wm_start_params *params = args;

    pthread_once( &load_once, load_library );
    if (!loaded) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
    if (running) { params->err = ERROR_ALREADY_EXISTS; return STATUS_SUCCESS; }
    if (!(display = pXOpenDisplay( NULL ))) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }

    pthread_mutex_lock( &x_mutex );
    root = DefaultRootWindow( display );
    desktop_window = params->desktop;
    previous_handler = pXSetErrorHandler( error_handler );
    /* only one client may redirect the root window's children: fails while another window manager runs */
    x_error = 0;
    pXSelectInput( display, root, SubstructureRedirectMask | SubstructureNotifyMask );
    pXSync( display, False );
    if (x_error)
    {
        pthread_mutex_unlock( &x_mutex );
        WARN( "another window manager is running\n" );
        pXCloseDisplay( display );
        display = NULL;
        params->err = ERROR_ALREADY_EXISTS;
        return STATUS_SUCCESS;
    }
    pXInternAtoms( display, (char **)atom_names, ATOM_COUNT, False, atoms );
    super_keys[0] = pXKeysymToKeycode( display, XK_Super_L );
    super_keys[1] = pXKeysymToKeycode( display, XK_Super_R );
    escape_key = pXKeysymToKeycode( display, XK_Escape );
    tab_key = pXKeysymToKeycode( display, XK_Tab );
    f4_key = pXKeysymToKeycode( display, XK_F4 );
    set_supported();
    scan();
    pXSync( display, False );
    pthread_mutex_unlock( &x_mutex );

    if (pipe( wake_pipe ))
    {
        wake_pipe[0] = wake_pipe[1] = -1;
    }
    else
    {
        fcntl( wake_pipe[0], F_SETFL, O_NONBLOCK );
        fcntl( wake_pipe[1], F_SETFL, O_NONBLOCK );
    }
    running = TRUE;
    pthread_create( &loop_thread, NULL, wm_loop, NULL );
    TRACE( "managing the display, desktop window %lx\n", desktop_window );
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS wm_next_event( void *args )
{
    struct wm_next_event_params *params = args;

    pthread_mutex_lock( &queue_mutex );
    while (!queue_count && running) pthread_cond_wait( &queue_cond, &queue_mutex );
    if (queue_count)
    {
        params->event = queue[queue_head];
        queue_head = (queue_head + 1) % EVENT_QUEUE;
        queue_count--;
    }
    else
    {
        memset( &params->event, 0, sizeof(params->event) );
        params->event.type = WEX_WM_STOP;
    }
    pthread_mutex_unlock( &queue_mutex );
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}

/* Starts a shell call on our display; NULL when the window manager does not run. */
static BOOL begin_call( DWORD *err )
{
    pthread_mutex_lock( &x_mutex );
    /* Stop can win the lock after a caller arrives. Check while holding it so
     * a late configure/focus request cannot use the closed X connection. */
    if (!running)
    {
        pthread_mutex_unlock( &x_mutex );
        *err = ERROR_NOT_SUPPORTED;
        return FALSE;
    }
    x_error = 0;
    return TRUE;
}

static void end_call(void)
{
    pXFlush( display );
    pthread_mutex_unlock( &x_mutex );
    wake_loop();
}

NTSTATUS wm_manage( void *args )
{
    struct wm_manage_params *params = args;
    struct client *client;
    Window window = params->window, focus;
    int revert;
    RECT *rc = &params->client;

    if (!begin_call( &params->err )) return STATUS_SUCCESS;
    params->err = ERROR_SUCCESS;
    if (!(client = find_client( window )))
        params->err = ERROR_INVALID_WINDOW_HANDLE;
    else if (!params->parent)
    {
        remove_client( client );
        pXMapWindow( display, window );
    }
    else
    {
        XWindowChanges changes = { .border_width = 0 };
        /* the save set brings the window back to the root if we go away */
        pXChangeSaveSet( display, window, SetModeInsert );
        pXConfigureWindow( display, window, CWBorderWidth, &changes );
        pXReparentWindow( display, window, params->parent, rc->left, rc->top );
        pXMoveResizeWindow( display, window, rc->left, rc->top, max( 1, rc->right - rc->left ), max( 1, rc->bottom - rc->top ) );
        pXSelectInput( display, params->parent, FocusChangeMask );
        pXSync( display, False );
        if (x_error)
        {
            /* the host's X window is not on the server yet, or the program's window is gone */
            pXChangeSaveSet( display, window, SetModeDelete );
            params->err = ERROR_INVALID_WINDOW_HANDLE;
        }
        else
        {
            client->managed = TRUE;
            client->parent = params->parent;
            client->screen = params->screen;
            grab_input( window, TRUE );
            pXSync( display, False );
            if (x_error) WARN( "input grabs on %lx failed, error %d\n", window, x_error );
            set_wm_state( window, NormalState );
            set_frame_extents( window );
            pXMapWindow( display, window );
            send_configure( client );
            update_client_list();
            pXGetInputFocus( display, &focus, &revert );
            if (focus == params->parent) focus_client( client );
            TRACE( "managing %lx in %lx at %s\n", window, params->parent, wine_dbgstr_rect( rc ) );
        }
    }
    end_call();
    return STATUS_SUCCESS;
}

NTSTATUS wm_configure( void *args )
{
    struct wm_window_params *params = args;
    struct client *client;
    RECT *rc = &params->client;

    if (!begin_call( &params->err )) return STATUS_SUCCESS;
    params->err = ERROR_SUCCESS;
    if (!(client = find_client( params->window )) || !client->managed) params->err = ERROR_INVALID_WINDOW_HANDLE;
    else
    {
        pXMoveResizeWindow( display, client->window, rc->left, rc->top, max( 1, rc->right - rc->left ),
                            max( 1, rc->bottom - rc->top ) );
        client->screen = params->screen;
        send_configure( client );
    }
    end_call();
    return STATUS_SUCCESS;
}

NTSTATUS wm_set_state( void *args )
{
    struct wm_window_params *params = args;
    struct client *client;
    DWORD states = params->value;
    Atom list[6];
    unsigned int count = 0;

    if (!begin_call( &params->err )) return STATUS_SUCCESS;
    params->err = ERROR_SUCCESS;
    if (!(client = find_client( params->window )) || !client->managed) params->err = ERROR_INVALID_WINDOW_HANDLE;
    else
    {
        if (states & WEX_WM_STATE_FULLSCREEN) list[count++] = atoms[NET_WM_STATE_FULLSCREEN];
        if (states & WEX_WM_STATE_MAXIMIZED)
        {
            list[count++] = atoms[NET_WM_STATE_MAXIMIZED_VERT];
            list[count++] = atoms[NET_WM_STATE_MAXIMIZED_HORZ];
        }
        if (states & WEX_WM_STATE_MINIMIZED) list[count++] = atoms[NET_WM_STATE_HIDDEN];
        if (states & WEX_WM_STATE_ABOVE) list[count++] = atoms[NET_WM_STATE_ABOVE];
        if (states & WEX_WM_STATE_FOCUSED) list[count++] = atoms[NET_WM_STATE_FOCUSED];
        pXChangeProperty( display, client->window, atoms[NET_WM_STATE], XA_ATOM, 32, PropModeReplace,
                          (unsigned char *)list, count );
        set_wm_state( client->window, (states & WEX_WM_STATE_MINIMIZED) ? IconicState : NormalState );
        if (states & WEX_WM_STATE_FOCUSED)
        {
            focus_client( client );
            active_window = client->window;
            pXChangeProperty( display, root, atoms[NET_ACTIVE_WINDOW], XA_WINDOW, 32, PropModeReplace,
                              (unsigned char *)&active_window, 1 );
        }
        else if (active_window == client->window)
        {
            active_window = 0;
            pXChangeProperty( display, root, atoms[NET_ACTIVE_WINDOW], XA_WINDOW, 32, PropModeReplace,
                              (unsigned char *)&active_window, 1 );
        }
    }
    end_call();
    return STATUS_SUCCESS;
}

NTSTATUS wm_focus( void *args )
{
    struct wm_window_params *params = args;
    struct client *client;
    XWindowAttributes attrs;

    if (!begin_call( &params->err )) return STATUS_SUCCESS;
    params->err = ERROR_SUCCESS;
    client = find_client( params->window );
    if (!client) client = find_client_by_parent( params->window );
    if (!pXGetWindowAttributes( display, params->window, &attrs ) || attrs.map_state != IsViewable)
        params->err = ERROR_INVALID_WINDOW_HANDLE;
    else if (client) focus_client( client );
    else pXSetInputFocus( display, params->window, RevertToParent, CurrentTime );
    end_call();
    return STATUS_SUCCESS;
}

NTSTATUS wm_close( void *args )
{
    struct wm_window_params *params = args;
    struct client *client;

    if (!begin_call( &params->err )) return STATUS_SUCCESS;
    params->err = ERROR_SUCCESS;
    if (!(client = find_client( params->window ))) params->err = ERROR_INVALID_WINDOW_HANDLE;
    else if (has_protocol( client->window, atoms[WM_DELETE_WINDOW] )) send_protocol( client->window, atoms[WM_DELETE_WINDOW] );
    else pXKillClient( display, client->window );
    end_call();
    return STATUS_SUCCESS;
}

static void release_client( struct client *client, const RECT *screen )
{
    Window window = client->window;

    pXSelectInput( display, window, NoEventMask );
    if (client->managed)
    {
        grab_input( window, FALSE );
        pXReparentWindow( display, window, root, screen->left, screen->top );
        pXChangeSaveSet( display, window, SetModeDelete );
        pXSelectInput( display, client->parent, NoEventMask );
    }
    else pXMapWindow( display, window );   /* its host was never made: show it as it is */
    remove_client( client );
}

NTSTATUS wm_release( void *args )
{
    struct wm_window_params *params = args;
    struct client *client;

    if (!begin_call( &params->err )) return STATUS_SUCCESS;
    params->err = ERROR_SUCCESS;
    if (!(client = find_client( params->window ))) params->err = ERROR_INVALID_WINDOW_HANDLE;
    else
    {
        release_client( client, &params->screen );
        update_client_list();
    }
    end_call();
    return STATUS_SUCCESS;
}

NTSTATUS wm_get_title( void *args )
{
    struct wm_get_title_params *params = args;
    XTextProperty text;
    char **list = NULL;
    int count;

    if (!begin_call( &params->err )) return STATUS_SUCCESS;
    params->err = ERROR_NOT_FOUND;
    params->title[0] = 0;
    if ((pXGetTextProperty( display, params->window, &text, atoms[NET_WM_NAME] ) && text.nitems) ||
        (pXGetTextProperty( display, params->window, &text, XA_WM_NAME ) && text.nitems))
    {
        if (pXutf8TextPropertyToTextList( display, &text, &list, &count ) >= Success && count > 0 && *list)
        {
            snprintf( params->title, sizeof(params->title), "%s", *list );
            params->err = ERROR_SUCCESS;
        }
        if (list) pXFreeStringList( list );
        pXFree( text.value );
    }
    end_call();
    return STATUS_SUCCESS;
}

NTSTATUS wm_get_icon( void *args )
{
    struct wm_get_icon_params *params = args;
    unsigned long *data, count, pos, best = 0, best_size = 0, size;
    DWORD *pixels = (DWORD *)(ULONG_PTR)params->pixels, i;

    if (!begin_call( &params->err )) return STATUS_SUCCESS;
    params->err = ERROR_NOT_FOUND;
    /* width, height, then width * height ARGB pixels, for each size */
    count = get_property( params->window, atoms[NET_WM_ICON], XA_CARDINAL, 1 << 22, &data );
    for (pos = 0; pos + 2 < count; pos += 2 + data[pos] * data[pos + 1])
    {
        if (!data[pos] || !data[pos + 1] || data[pos] > 1024 || data[pos + 1] > 1024 ||
            pos + 2 + data[pos] * data[pos + 1] > count)
            break;
        size = data[pos];
        /* the smallest one at least as large as asked, else the largest */
        if (!best_size || (best_size < params->size ? size > best_size : (size >= params->size && size < best_size)))
        {
            best = pos;
            best_size = size;
        }
    }
    if (best_size)
    {
        params->width = data[best];
        params->height = data[best + 1];
        if (params->width * params->height > params->max) params->err = ERROR_MORE_DATA;
        else
        {
            for (i = 0; i < params->width * params->height; i++) pixels[i] = data[best + 2 + i];
            params->err = ERROR_SUCCESS;
        }
    }
    if (data) pXFree( data );
    end_call();
    return STATUS_SUCCESS;
}

NTSTATUS wm_stop( void *args )
{
    struct wm_stop_params *params = args;

    params->err = ERROR_SUCCESS;
    if (!running) return STATUS_SUCCESS;
    pthread_mutex_lock( &x_mutex );
    while (client_count) release_client( &clients[0], &clients[0].screen );
    pXDeleteProperty( display, root, atoms[NET_SUPPORTING_WM_CHECK] );
    pXDeleteProperty( display, root, atoms[NET_CLIENT_LIST] );
    pXDeleteProperty( display, root, atoms[NET_ACTIVE_WINDOW] );
    pXDestroyWindow( display, check_window );
    pXSelectInput( display, root, NoEventMask );
    pXSync( display, False );
    running = FALSE;
    pthread_mutex_unlock( &x_mutex );
    wake_loop();
    pthread_join( loop_thread, NULL );

    pthread_mutex_lock( &queue_mutex );
    pthread_cond_broadcast( &queue_cond );
    pthread_mutex_unlock( &queue_mutex );

    pXCloseDisplay( display );
    display = NULL;
    close( wake_pipe[0] );
    close( wake_pipe[1] );
    wake_pipe[0] = wake_pipe[1] = -1;
    return STATUS_SUCCESS;
}

#else /* SONAME_LIBX11 */

#define STUB( name, params_type ) \
    NTSTATUS name( void *args ) { ((params_type *)args)->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
STUB( wm_start, struct wm_start_params )
STUB( wm_manage, struct wm_manage_params )
STUB( wm_configure, struct wm_window_params )
STUB( wm_set_state, struct wm_window_params )
STUB( wm_close, struct wm_window_params )
STUB( wm_release, struct wm_window_params )
STUB( wm_get_title, struct wm_get_title_params )
STUB( wm_get_icon, struct wm_get_icon_params )
STUB( wm_stop, struct wm_stop_params )
STUB( wm_focus, struct wm_window_params )

NTSTATUS wm_next_event( void *args )
{
    struct wm_next_event_params *params = args;
    memset( &params->event, 0, sizeof(params->event) );
    params->event.type = WEX_WM_STOP;
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}

#endif
