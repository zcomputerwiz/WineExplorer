/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * A real native client for the PE/Unix backend ABI probe.
 */
#include <X11/Xlib.h>
#include <unistd.h>
int main(void)
{
    Display *display = XOpenDisplay(NULL);
    Window window;
    if (!display) return 1;
    window = XCreateSimpleWindow(display, DefaultRootWindow(display),
            20,20,100,100,0,0,0x448855);
    XStoreName(display,window,"Wex backend native ABI");
    XMapWindow(display,window);
    XFlush(display);
    sleep(2);
    XDestroyWindow(display,window);
    XCloseDisplay(display);
    return 0;
}
