/* Read-only X11/DRM scanout check. In a VM this verifies the guest connector,
 * not the host monitor. Build: cc display-output-probe.c -o display-output-probe
 * $(pkg-config --cflags --libs x11 xrandr libdrm)
 * Usage: display-output-probe [expected-width expected-height] [card-path]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main(int argc, char **argv)
{
    const char *card = argc > 3 ? argv[3] : "/dev/dri/card0";
    unsigned expected_width = argc > 1 ? strtoul(argv[1], NULL, 10) : 0;
    unsigned expected_height = argc > 2 ? strtoul(argv[2], NULL, 10) : 0;
    unsigned x_width = 0, x_height = 0, drm_width = 0, drm_height = 0;
    int i, x_active = 0, drm_active = 0, failed = 0, fd;
    Display *display = XOpenDisplay(NULL);
    XRRScreenResources *x_resources;
    drmModeRes *resources;

    if (!display) { fprintf(stderr, "Cannot open X11 display\n"); return 2; }
    x_resources = XRRGetScreenResourcesCurrent(display, DefaultRootWindow(display));
    if (!x_resources) { XCloseDisplay(display); return 2; }
    for (i = 0; i < x_resources->noutput; i++)
    {
        XRROutputInfo *output = XRRGetOutputInfo(display, x_resources, x_resources->outputs[i]);
        XRRCrtcInfo *crtc;
        if (!output) continue;
        crtc = output->connection == RR_Connected && output->crtc ?
               XRRGetCrtcInfo(display, x_resources, output->crtc) : NULL;
        if (crtc && crtc->mode)
        {
            x_active++; x_width = crtc->width; x_height = crtc->height;
            printf("X11 %.*s: %ux%u at %d,%d\n", output->nameLen, output->name,
                   x_width, x_height, crtc->x, crtc->y);
            if (crtc->x || crtc->y || crtc->rotation != RR_Rotate_0) failed = 1;
        }
        if (crtc) XRRFreeCrtcInfo(crtc);
        XRRFreeOutputInfo(output);
    }
    XRRFreeScreenResources(x_resources); XCloseDisplay(display);
    fd = open(card, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror(card); return 2; }
    resources = drmModeGetResources(fd);
    if (!resources) { perror("drmModeGetResources"); close(fd); return 2; }
    for (i = 0; i < resources->count_connectors; i++)
    {
        drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[i]);
        drmModeEncoder *encoder;
        drmModeCrtc *crtc;
        if (!connector) continue;
        encoder = connector->connection == DRM_MODE_CONNECTED && connector->encoder_id ?
                  drmModeGetEncoder(fd, connector->encoder_id) : NULL;
        crtc = encoder && encoder->crtc_id ? drmModeGetCrtc(fd, encoder->crtc_id) : NULL;
        if (crtc && crtc->mode_valid)
        {
            uint64_t rate = 0;
            drm_active++; drm_width = crtc->mode.hdisplay; drm_height = crtc->mode.vdisplay;
            if (crtc->mode.htotal && crtc->mode.vtotal)
            {
                rate = (uint64_t)crtc->mode.clock * 1000000 /
                       ((uint64_t)crtc->mode.htotal * crtc->mode.vtotal);
                if (crtc->mode.flags & DRM_MODE_FLAG_INTERLACE) rate *= 2;
                if (crtc->mode.flags & DRM_MODE_FLAG_DBLSCAN) rate /= 2;
            }
            printf("DRM %s connector %u: %ux%u at %u,%u, %llu mHz\n", card,
                   connector->connector_id, drm_width, drm_height, crtc->x, crtc->y,
                   (unsigned long long)rate);
            if (crtc->x || crtc->y) failed = 1;
        }
        if (crtc) drmModeFreeCrtc(crtc);
        if (encoder) drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
    }
    drmModeFreeResources(resources); close(fd);
    if (x_active != 1 || drm_active != 1 || x_width != drm_width || x_height != drm_height)
        failed = 1;
    if (expected_width && (x_width != expected_width || x_height != expected_height)) failed = 1;
    puts(failed ? "FAIL: X11 and DRM scanout disagree or topology is unsupported" :
                  "PASS: X11 and DRM scanout agree");
    return failed;
}
