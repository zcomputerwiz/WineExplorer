/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Verify native dialog placement under an 800x600 WineExplorer desktop with
 * a 30-pixel taskbar. Run on a separate Xvfb display, not a user's session.
 * Build: cc -Wall -Wextra -o linux-dialog-probe linux-dialog-probe.c -lX11
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static Display *display;
static Window root;

static Window create_window(Window owner, int x, int y, int width, int height, int fixed)
{
    Window window = XCreateSimpleWindow(display, root, x, y, width, height, 0, 0, 0xffffff);
    XSizeHints hints = {0};
    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);

    XStoreName(display, window, "Dialog placement probe");
    if (owner) XSetTransientForHint(display, window, owner);
    XChangeProperty(display, window, XInternAtom(display, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);
    hints.flags = PMinSize;
    hints.min_width = fixed ? width : 300;
    hints.min_height = fixed ? height : 200;
    if (fixed)
    {
        hints.flags |= PMaxSize;
        hints.max_width = width;
        hints.max_height = height;
    }
    XSetWMNormalHints(display, window, &hints);
    XMapWindow(display, window);
    XFlush(display);
    return window;
}

static int check(Window window, const char *label, int oversized, int expected_x, int expected_y)
{
    Window returned_root, parent, *children, child;
    unsigned int count;
    XWindowAttributes attrs, client;
    int x = 0, y = 0, attempt;

    for (attempt = 0; attempt < 40; attempt++)
    {
        usleep(50000);
        XQueryTree(display, window, &returned_root, &parent, &children, &count);
        if (children) XFree(children);
        if (parent != root) break;
    }
    if (parent == root) { fprintf(stderr, "%s: not hosted\n", label); return 1; }
    usleep(300000);
    XGetWindowAttributes(display, parent, &attrs);
    XGetWindowAttributes(display, window, &client);
    XTranslateCoordinates(display, parent, root, 0, 0, &x, &y, &child);
    printf("%s: frame %dx%d at %d,%d\n", label, attrs.width, attrs.height, x, y);
    if (client.x < 0 || client.y < 0 || client.x + client.width > attrs.width ||
        client.y + client.height > attrs.height)
    {
        fprintf(stderr, "%s: client clipped by frame\n", label);
        return 1;
    }
    if (oversized)
        return x != 0 || y != 0 || attrs.width < 1000 || attrs.height < 800;
    return x < 0 || y < 0 || x + attrs.width > 800 || y + attrs.height > 570 ||
           (expected_x >= 0 && x != expected_x) || (expected_y >= 0 && y != expected_y);
}

int main(void)
{
    Window owner, dialog;
    int failed = 0;

    display = XOpenDisplay(NULL);
    if (!display) return 1;
    root = DefaultRootWindow(display);
    owner = XCreateSimpleWindow(display, root, 650, 450, 200, 150, 0, 0, 0xffffff);
    XStoreName(display, owner, "Offscreen owner probe");
    XMapWindow(display, owner);
    XFlush(display);
    usleep(500000);

    dialog = create_window(owner, -162, -153, 804, 577, 0);
    failed |= check(dialog, "LibreOffice-sized dialog", 0, 0, 0);
    XMoveResizeWindow(display, dialog, -400, -300, 950, 750);
    XFlush(display);
    usleep(500000);
    failed |= check(dialog, "Program move/resize request", 0, 0, 0);
    XMoveResizeWindow(display, dialog, 30, 20, 400, 300);
    XFlush(display);
    usleep(500000);
    failed |= check(dialog, "Valid program move/resize", 0, 30, 20);
    {
        XWindowAttributes attrs;
        XGetWindowAttributes(display, dialog, &attrs);
        if (attrs.width != 400 || attrs.height != 300) failed = 1;
    }
    XMoveWindow(display, dialog, -400, -300);
    XFlush(display);
    usleep(500000);
    failed |= check(dialog, "Move-only request", 0, 0, 0);
    XResizeWindow(display, dialog, 950, 750);
    XFlush(display);
    usleep(500000);
    failed |= check(dialog, "Size-only request", 0, 0, 0);
    XDestroyWindow(display, dialog);

    dialog = create_window(owner, 0, 0, 320, 240, 0);
    failed |= check(dialog, "Center on partly offscreen owner", 0, -1, -1);
    XDestroyWindow(display, dialog);

    dialog = create_window(None, 120, 80, 320, 240, 0);
    failed |= check(dialog, "Valid explicit position", 0, 120, 80);
    XDestroyWindow(display, dialog);

    dialog = create_window(owner, -500, -400, 1000, 800, 1);
    failed |= check(dialog, "Oversized fixed-size dialog", 1, 0, 0);
    XDestroyWindow(display, dialog);
    XDestroyWindow(display, owner);
    XCloseDisplay(display);
    return failed;
}
