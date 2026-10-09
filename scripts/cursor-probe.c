/* Guest-side X11 cursor diagnostic: gcc cursor-probe.c -o cursor-probe -lX11 -lXfixes */
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    XFixesCursorImage *image;
    unsigned long hash = 5381;
    unsigned int i;

    if (!display) return 1;
    if (!(image = XFixesGetCursorImage(display))) return 1;
    for (i = 0; i < image->width * image->height; ++i)
        hash = hash * 33 + image->pixels[i];
    printf("pos=%d,%d size=%ux%u hotspot=%u,%u serial=%lu hash=%lx\n",
           image->x, image->y, image->width, image->height,
           image->xhot, image->yhot, image->cursor_serial, hash);
    XFree(image);
    XCloseDisplay(display);
    return 0;
}
