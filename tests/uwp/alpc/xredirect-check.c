/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Lab tool (Linux, Xlib): is an X window composite-redirected? XCompositeNameWindowPixmap succeeds only for a redirected window
 * (BadMatch otherwise). Prints the answer for each window id given; with "remap" first, unmaps and maps the first window (waiting for
 * the map) before asking. Build: gcc -o xredirect-check xredirect-check.c -lX11 -lXcomposite
 * Usage: DISPLAY=:99 xredirect-check [remap] 0xWINDOW...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>

static int last_error;

static int handler(Display *dpy, XErrorEvent *e)
{
    last_error = e->error_code;
    return 0;
}

int main(int argc, char **argv)
{
    Display *dpy = XOpenDisplay(NULL);
    int i = 1, ev, err;

    if (!dpy) { printf("no display\n"); return 1; }
    if (!XCompositeQueryExtension(dpy, &ev, &err)) { printf("no Composite\n"); return 1; }
    XSetErrorHandler(handler);
    if (argc > 2 && !strcmp(argv[1], "remap"))
    {
        Window w = strtoul(argv[2], NULL, 0);
        XEvent e;

        XSelectInput(dpy, w, StructureNotifyMask);
        XUnmapWindow(dpy, w);
        XSync(dpy, False);
        XMapWindow(dpy, w);
        XSync(dpy, False);
        for (int k = 0; k < 50 && !XCheckTypedWindowEvent(dpy, w, MapNotify, &e); k++) { XSync(dpy, False); usleep(20000); }
        printf("remapped 0x%lx\n", w);
        i = 2;
    }
    for (; i < argc; i++)
    {
        Window w = strtoul(argv[i], NULL, 0);
        Pixmap p;

        last_error = 0;
        p = XCompositeNameWindowPixmap(dpy, w);
        XSync(dpy, False);
        if (last_error) printf("0x%lx: not redirected (error %d%s)\n", w, last_error, last_error == BadMatch ? ", BadMatch" : "");
        else { printf("0x%lx: REDIRECTED (pixmap 0x%lx)\n", w, p); XFreePixmap(dpy, p); }
    }
    XCloseDisplay(dpy);
    return 0;
}
