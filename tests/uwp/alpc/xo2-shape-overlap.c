/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Lab test (Linux, Xlib), for B136: two overlapping top-levels drawn with XPutImage, the top one shaped from a mask pixmap as
 * winex11 does; reads the root under it. Based on a test by the Integration session. Build: cc -o xo2 xo2-shape-overlap.c -lX11 -lXext
 */
/* xo.c (Integration's overlap test) with the bounding shape set the way winex11 did in the black run: ShapeMask from a depth-1
 * pixmap of the surface size (768x640 for a 668x522 window), all bits set, filled with XPutImage (XYBitmap via a 1-bit image).
 * usage: xo2 [rects|mask]   (default mask) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
static int use_mask = 1;
static void shape(Display *d, Window win, int w, int h)
{
    if (!use_mask)
    {
        XRectangle r = {0, 0, (unsigned short)w, (unsigned short)h};
        XShapeCombineRectangles(d, win, ShapeBounding, 0, 0, &r, 1, ShapeSet, YXBanded);
        return;
    }
    {
        int pw = (w + 127) / 128 * 128, ph = (h + 127) / 128 * 128;   /* rounded up, like the surface size */
        Pixmap pm = XCreatePixmap(d, win, pw, ph, 1);
        GC gc = XCreateGC(d, pm, 0, NULL);
        int bpl = (pw + 7) / 8;
        char *bits = malloc((size_t)bpl * ph);
        XImage *img;

        memset(bits, 0xff, (size_t)bpl * ph);
        img = XCreateImage(d, DefaultVisual(d, 0), 1, ZPixmap, 0, bits, pw, ph, 8, bpl);
        XPutImage(d, pm, gc, img, 0, 0, 0, 0, pw, ph);
        XShapeCombineMask(d, win, ShapeBounding, 0, 0, pm, ShapeSet);
        XFreeGC(d, gc);
        XFreePixmap(d, pm);
    }
}
static Window make(Display *d,int x,int y,int w,int h,unsigned long c,int su){
 XSetWindowAttributes a={0}; Window win; XImage *img; char *b=malloc((size_t)w*h*4); int i;
 a.save_under=su; a.background_pixel=0;
 win=XCreateWindow(d,DefaultRootWindow(d),x,y,w,h,0,24,InputOutput,DefaultVisual(d,0),CWSaveUnder|CWBackPixel,&a);
 XMapWindow(d,win); XSync(d,False); usleep(300000);
 shape(d,win,w,h);
 for(i=0;i<w*h;i++)((unsigned int*)b)[i]=c;
 img=XCreateImage(d,DefaultVisual(d,0),24,ZPixmap,0,b,w,h,32,0);
 XPutImage(d,win,DefaultGC(d,0),img,0,0,0,0,w,h); XSync(d,False); return win;}
int main(int argc,char **argv){
 Display *d=XOpenDisplay(NULL); XImage *root; unsigned long px;
 if(argc>1&&!strcmp(argv[1],"rects")) use_mask=0;
 if(!d){fprintf(stderr,"no display\n");return 2;}
 make(d,0,0,800,600,0x808080,0); make(d,0,0,668,522,0xd0d0d0,1); sleep(1);
 root=XGetImage(d,DefaultRootWindow(d),334,261,1,1,AllPlanes,ZPixmap); px=XGetPixel(root,0,0)&0xffffff;
 printf("%s: root at the dialog's middle = %06lx (%s)\n",use_mask?"mask":"rects",px,px==0xd0d0d0?"PASS":"FAIL");
 return px==0xd0d0d0?0:1;}
