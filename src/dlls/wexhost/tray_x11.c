/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Event-driven XEmbed capture bridge into the common native tray records.
 * Xlib objects never cross the PE boundary. Calls hold tray.c's mutex.
 */
#if 0
#pragma makedep unix
#endif
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#if defined(SONAME_LIBX11) && defined(SONAME_LIBXCOMPOSITE)
#define TRAY_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#undef Status
#undef ControlMask
#endif
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <winerror.h>
#include "wexhost_unix.h"
#include "wine/debug.h"
WINE_DEFAULT_DEBUG_CHANNEL(wexhost);
#ifdef TRAY_X11
/* Public Damage 1.0 ABI, as in compose.c: Wine does not probe its optional
 * development headers. Keep the runtime library optional on both backends. */
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
} tray_damage_event;
static Bool (*p_XDamageQueryExtension)(Display *, int *, int *);
static Damage (*p_XDamageCreate)(Display *, Drawable, int);
static void (*p_XDamageDestroy)(Display *, Damage);
static void (*p_XDamageSubtract)(Display *, Damage, XID, XID);
#define XLIB_FUNCS \
    X(XOpenDisplay) X(XCloseDisplay) X(XInternAtom) X(XGetSelectionOwner) X(XSetSelectionOwner) \
    X(XCreateSimpleWindow) X(XChangeWindowAttributes) X(XChangeProperty) X(XGetWindowProperty) \
    X(XMapWindow) X(XSelectInput) X(XDestroyWindow) X(XFlush) X(XSync) X(XPending) X(XNextEvent) \
    X(XSendEvent) X(XReparentWindow) X(XMoveResizeWindow) X(XAddToSaveSet) X(XRemoveFromSaveSet) \
    X(XGetWindowAttributes) X(XFetchName) X(XGetClassHint) X(XFree) X(XGetImage) X(XFreePixmap) X(XSetErrorHandler)
#define X(f) static typeof(f) *p_##f;
XLIB_FUNCS
X(XCompositeRedirectWindow) X(XCompositeNameWindowPixmap) X(XCompositeQueryExtension)
#undef X
#define XEMBED_MAX 64
#define TOKEN_MASK 0x40000000u
struct embedded
{
    Window window, parent;
    Damage damage;
    DWORD token;
    BOOL dirty, removed;
};
static struct embedded icons[XEMBED_MAX];
static Display *display;
static Window manager;
static Atom selection, opcode, xembed, manager_atom, opacity;
static int damage_event, x_error;
static DWORD serial;
static BOOL handler_installed;
static int (*previous_error)(Display *, XErrorEvent *);
static int error_handler(Display *dpy, XErrorEvent *error)
{
    if (dpy == display) { x_error = error->error_code; return 0; }
    return previous_error ? previous_error(dpy, error) : 0;
}
static BOOL initialize(void)
{
    static BOOL loaded;
    void *xlib, *composite, *damage;
    Window root;
    XEvent event = {0};
    int unused;
    char name[80], wayland[108], xdisplay[128];
    const char *native_display = getenv("DISPLAY");
    if (!loaded)
    {
        xlib = dlopen(SONAME_LIBX11, RTLD_NOW);
        composite = dlopen("libXcomposite.so.1", RTLD_NOW);
        damage = dlopen("libXdamage.so.1", RTLD_NOW);
        if (!xlib || !composite || !damage) return FALSE;
#define X(f) if (!(p_##f = dlsym(xlib, #f))) return FALSE;
        XLIB_FUNCS
#undef X
#define LOAD(lib, f) if (!(p_##f = dlsym(lib, #f))) return FALSE;
        LOAD(composite, XCompositeRedirectWindow) LOAD(composite, XCompositeNameWindowPixmap) LOAD(composite, XCompositeQueryExtension)
        LOAD(damage, XDamageCreate) LOAD(damage, XDamageDestroy) LOAD(damage, XDamageSubtract) LOAD(damage, XDamageQueryExtension)
#undef LOAD
        loaded = TRUE;
    }
    if ((!native_display || !*native_display) && wayland_wm_native_environment(wayland, xdisplay)) native_display = xdisplay;
    if (!native_display || !*native_display || !(display = p_XOpenDisplay(native_display))) return FALSE;
    if (!p_XCompositeQueryExtension(display, &unused, &unused) || !p_XDamageQueryExtension(display, &damage_event, &unused)) goto failed;
    snprintf(name, sizeof(name), "_NET_SYSTEM_TRAY_S%d", DefaultScreen(display));
    selection = p_XInternAtom(display, name, False);
    if (p_XGetSelectionOwner(display, selection)) goto failed; /* Never steal another tray. */
    if (!handler_installed) { previous_error = p_XSetErrorHandler(error_handler); handler_installed = TRUE; }
    opcode = p_XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    xembed = p_XInternAtom(display, "_XEMBED", False);
    manager_atom = p_XInternAtom(display, "MANAGER", False);
    opacity = p_XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
    root = DefaultRootWindow(display);
    manager = p_XCreateSimpleWindow(display, root, -1000, -1000, 1, 1, 0, 0, 0);
    p_XSetSelectionOwner(display, selection, manager, CurrentTime);
    if (p_XGetSelectionOwner(display, selection) != manager) { p_XDestroyWindow(display, manager); manager = 0; goto failed; }
    {
        Atom orientation = p_XInternAtom(display, "_NET_SYSTEM_TRAY_ORIENTATION", False);
        unsigned long horizontal = 0;
        p_XChangeProperty(display, manager, orientation, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&horizontal, 1);
    }
    event.xclient.type = ClientMessage; event.xclient.window = root;
    event.xclient.message_type = manager_atom; event.xclient.format = 32;
    event.xclient.data.l[0] = CurrentTime; event.xclient.data.l[1] = selection; event.xclient.data.l[2] = manager;
    p_XSendEvent(display, root, False, StructureNotifyMask, &event);
    p_XFlush(display);
    TRACE("XEmbed tray manager %lx on %s\n", manager, native_display);
    return TRUE;
failed:
    p_XCloseDisplay(display); display = NULL;
    return FALSE;
}
static void dock(Window window)
{
    unsigned int i;
    Window root = DefaultRootWindow(display);
    XEvent embedded = {0};
    XSetWindowAttributes attrs = {0};
    unsigned long transparent = 0;
    for (i = 0; i < XEMBED_MAX; i++) if (icons[i].window == window) return;
    for (i = 0; i < XEMBED_MAX; i++) if (!icons[i].token) break;
    if (i == XEMBED_MAX) return;
    x_error = 0;
    icons[i].parent = p_XCreateSimpleWindow(display, root, -1000, -1000, 24, 24, 0, 0, 0);
    attrs.override_redirect = True;
    p_XChangeWindowAttributes(display, icons[i].parent, CWOverrideRedirect, &attrs);
    p_XChangeProperty(display, icons[i].parent, opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&transparent, 1);
    p_XAddToSaveSet(display, window);
    p_XSelectInput(display, window, StructureNotifyMask | PropertyChangeMask | ExposureMask);
    p_XReparentWindow(display, window, icons[i].parent, 0, 0);
    p_XMoveResizeWindow(display, window, 0, 0, 24, 24);
    p_XCompositeRedirectWindow(display, window, CompositeRedirectAutomatic);
    icons[i].damage = p_XDamageCreate(display, window, 3 /* XDamageReportNonEmpty */);
    p_XMapWindow(display, icons[i].parent); p_XMapWindow(display, window);
    embedded.xclient.type = ClientMessage; embedded.xclient.window = window;
    embedded.xclient.message_type = xembed; embedded.xclient.format = 32;
    embedded.xclient.data.l[0] = CurrentTime; embedded.xclient.data.l[1] = 0; /* XEMBED_EMBEDDED_NOTIFY */
    embedded.xclient.data.l[3] = icons[i].parent; embedded.xclient.data.l[4] = 0;
    p_XSendEvent(display, window, False, NoEventMask, &embedded);
    p_XSync(display, False);
    if (x_error)
    {
        p_XDestroyWindow(display, icons[i].parent);
        memset(&icons[i], 0, sizeof(icons[i])); return;
    }
    icons[i].window = window; icons[i].token = TOKEN_MASK | ++serial; icons[i].dirty = TRUE;
}
static DWORD component(unsigned long pixel, unsigned long mask)
{
    unsigned int shift = 0;
    if (!mask) return 0;
    while (!(mask & 1)) { mask >>= 1; shift++; }
    return ((pixel >> shift) & mask) * 255 / mask;
}
static void capture(struct embedded *icon, WEX_TRAY_ITEM *item)
{
    XWindowAttributes attrs;
    XImage *image = NULL;
    Pixmap pixmap;
    unsigned int x, y;
    char *name = NULL;
    XClassHint hint = {0};
    item->event = WEX_TRAY_CHANGED; item->token = icon->token;
    x_error = 0;
    if (p_XGetWindowAttributes(display, icon->window, &attrs) && attrs.width > 0 && attrs.height > 0 && attrs.width <= 256 && attrs.height <= 256)
    {
        pixmap = p_XCompositeNameWindowPixmap(display, icon->window);
        image = p_XGetImage(display, pixmap, 0, 0, attrs.width, attrs.height, AllPlanes, ZPixmap);
        p_XFreePixmap(display, pixmap);
    }
    p_XSync(display, False);
    if (image && !x_error)
    {
        if (!image->red_mask && attrs.visual)
        { image->red_mask = attrs.visual->red_mask; image->green_mask = attrs.visual->green_mask; image->blue_mask = attrs.visual->blue_mask; }
        item->width = min(image->width, 64); item->height = min(image->height, 64);
        for (y = 0; y < item->height; y++) for (x = 0; x < item->width; x++)
        {
            unsigned long p = XGetPixel(image, x * image->width / item->width, y * image->height / item->height);
            DWORD alpha = image->depth == 32 ? (p >> 24) & 255 : 255;
            DWORD r = component(p, image->red_mask), g = component(p, image->green_mask), b = component(p, image->blue_mask);
            /* XRender's ARGB is premultiplied; common tray records are straight alpha. */
            if (alpha && alpha < 255) { r = min(255, r * 255 / alpha); g = min(255, g * 255 / alpha); b = min(255, b * 255 / alpha); }
            item->pixels[y * item->width + x] = (alpha << 24) | (r << 16) | (g << 8) | b;
        }
    }
    if (image) XDestroyImage(image);
    if (p_XFetchName(display, icon->window, &name) && name)
    { snprintf(item->title, sizeof(item->title), "%s", name); p_XFree(name); }
    if (p_XGetClassHint(display, icon->window, &hint))
    {
        snprintf(item->id, sizeof(item->id), "xembed:%s", hint.res_class ? hint.res_class : hint.res_name ? hint.res_name : "icon");
        if (hint.res_name) p_XFree(hint.res_name);
        if (hint.res_class) p_XFree(hint.res_class);
    }
    if (!item->title[0]) snprintf(item->title, sizeof(item->title), "%s", item->id);
    icon->dirty = FALSE;
}
void tray_x11_next(WEX_TRAY_ITEM *item)
{
    XEvent event;
    unsigned int i, count = 0;
    if (!display && !initialize()) return;
    while (count++ < 128 && p_XPending(display))
    {
        p_XNextEvent(display, &event);
        if (event.type == ClientMessage && event.xclient.message_type == opcode && event.xclient.format == 32 && event.xclient.data.l[1] == 0)
            dock(event.xclient.data.l[2]);
        else if (event.type == SelectionClear)
        { for (i = 0; i < XEMBED_MAX; i++) if (icons[i].token) icons[i].removed = TRUE; }
        else for (i = 0; i < XEMBED_MAX; i++) if (icons[i].token)
        {
            if (event.type == damage_event && ((tray_damage_event *)&event)->drawable == icons[i].window)
            { icons[i].dirty = TRUE; p_XDamageSubtract(display, icons[i].damage, None, None); }
            else if (event.type == DestroyNotify && event.xdestroywindow.window == icons[i].window) icons[i].removed = TRUE;
            else if ((event.type == PropertyNotify || event.type == Expose || event.type == ConfigureNotify) && event.xany.window == icons[i].window) icons[i].dirty = TRUE;
        }
    }
    for (i = 0; i < XEMBED_MAX; i++) if (icons[i].token)
    {
        if (icons[i].removed)
        {
            item->event = WEX_TRAY_REMOVED; item->token = icons[i].token;
            p_XDamageDestroy(display, icons[i].damage);
            p_XDestroyWindow(display, icons[i].parent);
            memset(&icons[i], 0, sizeof(icons[i])); break;
        }
        if (icons[i].dirty) { capture(&icons[i], item); break; }
    }
    p_XFlush(display);
}
BOOL tray_x11_action(struct tray_action_params *params)
{
    unsigned int i, button;
    XEvent event = {0};
    if (!(params->token & TOKEN_MASK)) return FALSE;
    params->err = ERROR_NOT_FOUND;
    for (i = 0; i < XEMBED_MAX; i++) if (icons[i].token == params->token && !icons[i].removed) break;
    if (i == XEMBED_MAX) return TRUE;
    button = params->action == WEX_TRAY_ACTIVATE ? 1 : params->action == WEX_TRAY_SECONDARY ? 2 : params->action == WEX_TRAY_CONTEXT ? 3 : 0;
    if (params->action == WEX_TRAY_SCROLL) button = params->y ? (params->value > 0 ? 6 : 7) : (params->value > 0 ? 4 : 5);
    if (!button) { params->err = ERROR_NOT_SUPPORTED; return TRUE; }
    if (params->action != WEX_TRAY_SCROLL)
        p_XMoveResizeWindow(display, icons[i].parent, params->x - 12, params->y - 12, 24, 24);
    event.xbutton.type = ButtonPress; event.xbutton.display = display; event.xbutton.window = icons[i].window;
    event.xbutton.root = DefaultRootWindow(display); event.xbutton.time = CurrentTime;
    event.xbutton.x = event.xbutton.y = 12; event.xbutton.x_root = params->x; event.xbutton.y_root = params->y;
    event.xbutton.button = button; event.xbutton.same_screen = True;
    p_XSendEvent(display, icons[i].window, False, ButtonPressMask, &event);
    event.xbutton.type = ButtonRelease; event.xbutton.state = Button1Mask << (button - 1);
    p_XSendEvent(display, icons[i].window, False, ButtonReleaseMask, &event);
    p_XFlush(display);
    params->err = 0;
    return TRUE;
}
void tray_x11_stop(void)
{
    unsigned int i;
    if (!display) return;
    for (i = 0; i < XEMBED_MAX; i++) if (icons[i].token && !icons[i].removed)
    {
        p_XReparentWindow(display, icons[i].window, DefaultRootWindow(display), 0, 0);
        p_XRemoveFromSaveSet(display, icons[i].window);
        p_XDamageDestroy(display, icons[i].damage); p_XDestroyWindow(display, icons[i].parent);
    }
    p_XDestroyWindow(display, manager); manager = 0;
    p_XCloseDisplay(display); display = NULL;
    memset(icons, 0, sizeof(icons));
}
#else
void tray_x11_next(WEX_TRAY_ITEM *item) {}
BOOL tray_x11_action(struct tray_action_params *params) { return FALSE; }
void tray_x11_stop(void) {}
#endif
