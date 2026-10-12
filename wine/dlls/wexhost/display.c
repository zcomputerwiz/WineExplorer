/*
 * Host display modes: XRandR on X11 and compositor-advertised Xwayland outputs.
 * There is no display-management D-Bus service common to all these sessions.
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

#if 0
#pragma makedep unix
#endif
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>
#include <dlfcn.h>
#if defined(SONAME_LIBX11) && defined(SONAME_LIBXRANDR)
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#undef Status
#undef ControlMask
#endif
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include "wexhost_unix.h"

#if defined(SONAME_LIBX11) && defined(SONAME_LIBXRANDR)
#define X11_FUNCS \
    FUNC(XOpenDisplay) FUNC(XCloseDisplay) FUNC(XDefaultRootWindow) \
    FUNC(XDisplayWidth) FUNC(XDisplayHeight) FUNC(XDisplayWidthMM) FUNC(XDisplayHeightMM) \
    FUNC(XGrabServer) FUNC(XUngrabServer) FUNC(XSync)
#define RANDR_FUNCS \
    FUNC(XRRQueryVersion) FUNC(XRRGetScreenResourcesCurrent) FUNC(XRRFreeScreenResources) \
    FUNC(XRRGetOutputPrimary) FUNC(XRRGetOutputInfo) FUNC(XRRFreeOutputInfo) \
    FUNC(XRRGetCrtcInfo) FUNC(XRRFreeCrtcInfo) FUNC(XRRGetScreenSizeRange) \
    FUNC(XRRSetScreenSize) FUNC(XRRSetCrtcConfig)
#define FUNC(name) static typeof(name) *p##name;
X11_FUNCS
RANDR_FUNCS
#undef FUNC
static pthread_once_t once=PTHREAD_ONCE_INIT;
static BOOL loaded;

static void load_libraries(void)
{
    void *x11=dlopen(SONAME_LIBX11,RTLD_NOW),*randr=dlopen(SONAME_LIBXRANDR,RTLD_NOW);
    if(!x11 || !randr) return;
#define FUNC(name) if(!(p##name=dlsym(x11,#name))) return;
    X11_FUNCS
#undef FUNC
#define FUNC(name) if(!(p##name=dlsym(randr,#name))) return;
    RANDR_FUNCS
#undef FUNC
    loaded=TRUE;
}

typedef struct {
    Display *display;
    Window root;
    XRRScreenResources *resources;
    XRROutputInfo *output;
    XRRCrtcInfo *crtc;
    RROutput output_id;
    int screen;
} DISPLAY_CONTEXT;

static void release(DISPLAY_CONTEXT *ctx)
{
    if(ctx->crtc) pXRRFreeCrtcInfo(ctx->crtc);
    if(ctx->output) pXRRFreeOutputInfo(ctx->output);
    if(ctx->resources) pXRRFreeScreenResources(ctx->resources);
    if(ctx->display) pXCloseDisplay(ctx->display);
}

static WEX_DISPLAY_MODE describe(const XRRModeInfo *mode)
{
    WEX_DISPLAY_MODE result={mode->width,mode->height,0};
    double rate;
    if(mode->hTotal && mode->vTotal)
    {
        rate=(double)mode->dotClock*1000/mode->hTotal/mode->vTotal;
        if(mode->modeFlags&RR_Interlace) rate*=2;
        if(mode->modeFlags&RR_DoubleScan) rate/=2;
        result.refresh=(DWORD)(rate+0.5);
    }
    return result;
}

/* A separate connection belongs to this call; it never consumes Wine's events. */
static DWORD query(DISPLAY_CONTEXT *ctx,WEX_DISPLAY *display)
{
    int major=1,minor=3,i,j,active=0;
    RROutput primary;
    pthread_once(&once,load_libraries);
    memset(ctx,0,sizeof(*ctx)); memset(display,0,sizeof(*display));
    if(!loaded || !(ctx->display=pXOpenDisplay(NULL))) return ERROR_NOT_SUPPORTED;
    ctx->screen=DefaultScreen(ctx->display); ctx->root=pXDefaultRootWindow(ctx->display);
    if(!pXRRQueryVersion(ctx->display,&major,&minor) || major<1 || (major==1 && minor<3)) return ERROR_NOT_SUPPORTED;
    if(!(ctx->resources=pXRRGetScreenResourcesCurrent(ctx->display,ctx->root))) return ERROR_NOT_SUPPORTED;
    primary=pXRRGetOutputPrimary(ctx->display,ctx->root);
    for(i=0;i<ctx->resources->noutput;i++)
    {
        XRROutputInfo *output=pXRRGetOutputInfo(ctx->display,ctx->resources,ctx->resources->outputs[i]);
        if(!output) continue;
        if(output->connection==RR_Connected && output->crtc)
        {
            active++;
            if(!ctx->output || ctx->resources->outputs[i]==primary)
            {
                if(ctx->output) pXRRFreeOutputInfo(ctx->output);
                ctx->output=output; ctx->output_id=ctx->resources->outputs[i]; output=NULL;
            }
        }
        if(output) pXRRFreeOutputInfo(output);
    }
    /* Multi-output placement is a separate feature. Never alter another monitor. */
    if(active!=1 || !ctx->output) return ERROR_NOT_SUPPORTED;
    if(!(ctx->crtc=pXRRGetCrtcInfo(ctx->display,ctx->resources,ctx->output->crtc))) return ERROR_NOT_SUPPORTED;
    if(ctx->crtc->x || ctx->crtc->y || ctx->crtc->rotation!=RR_Rotate_0) return ERROR_NOT_SUPPORTED;
    snprintf(display->output,sizeof(display->output),"%.*s",ctx->output->nameLen,ctx->output->name);
    for(i=0;i<ctx->output->nmode;i++)
    {
        for(j=0;j<ctx->resources->nmode;j++)
        {
            XRRModeInfo *mode=&ctx->resources->modes[j]; WEX_DISPLAY_MODE value;
            if(mode->id!=ctx->output->modes[i]) continue;
            value=describe(mode);
            if(!value.width || !value.height || !value.refresh) break;
            if(display->count==WEX_DISPLAY_MAX_MODES) return ERROR_MORE_DATA;
            display->modes[display->count++]=value;
            if(mode->id==ctx->crtc->mode) display->current=value;
            break;
        }
    }
    return display->count && display->current.refresh ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}

static DWORD persist_mode(const WEX_DISPLAY_MODE *mode,const XRRModeInfo *native)
{
    char dir[4096],path[4096],temp[4096]; const char *root=getenv("XDG_CONFIG_HOME"),*home=getenv("HOME"); FILE *file;
    if(root && root[0]=='/') snprintf(dir,sizeof(dir),"%s",root);
    else if(home) snprintf(dir,sizeof(dir),"%s/.config",home);
    else return ERROR_NOT_SUPPORTED;
    if(mkdir(dir,0700) && errno!=EEXIST) return ERROR_ACCESS_DENIED;
    if(strlen(dir)+32>=sizeof(dir)) return ERROR_FILENAME_EXCED_RANGE;
    strcat(dir,"/wineexplorer");
    if(mkdir(dir,0700) && errno!=EEXIST) return ERROR_ACCESS_DENIED;
    snprintf(path,sizeof(path),"%s/display",dir); snprintf(temp,sizeof(temp),"%s/display.tmp.%ld",dir,(long)getpid());
    if(!(file=fopen(temp,"w"))) return ERROR_ACCESS_DENIED;
    fprintf(file,"%u %u %u %.*s\n",mode->width,mode->height,mode->refresh,native->nameLength,native->name);
    if(fclose(file) || rename(temp,path)) { unlink(temp); return ERROR_WRITE_FAULT; }
    return ERROR_SUCCESS;
}

NTSTATUS display_get(void *args)
{
    struct display_get_params *params=args; DISPLAY_CONTEXT ctx;
    params->err=query(&ctx,&params->display); release(&ctx);
    return STATUS_SUCCESS;
}

NTSTATUS display_set(void *args)
{
    struct display_set_params *params=args;
    WEX_DISPLAY display; DISPLAY_CONTEXT ctx; XRRModeInfo *mode=NULL;
    int i,j,width,height,mm_width,mm_height,min_width,min_height,max_width,max_height,status;
    params->err=query(&ctx,&display);
    if(params->err) goto done;
    /* Resolve a currently advertised native mode, not an index from an old list. */
    for(i=0;i<ctx.output->nmode;i++)
    {
        for(j=0;j<ctx.resources->nmode;j++)
        {
            XRRModeInfo *candidate=&ctx.resources->modes[j]; WEX_DISPLAY_MODE value;
            if(candidate->id!=ctx.output->modes[i]) continue;
            value=describe(candidate);
            if(!memcmp(&value,&params->mode,sizeof(value)))
            {
                if(!mode || candidate->id==ctx.crtc->mode) mode=candidate;
            }
            break;
        }
    }
    if(!mode) { params->err=ERROR_INVALID_PARAMETER; goto done; }
    if(mode->id!=ctx.crtc->mode)
    {
        pXRRGetScreenSizeRange(ctx.display,ctx.root,&min_width,&min_height,&max_width,&max_height);
        if(mode->width<min_width || mode->height<min_height || mode->width>max_width || mode->height>max_height)
        { params->err=ERROR_INVALID_PARAMETER; goto done; }
        width=pXDisplayWidth(ctx.display,ctx.screen); height=pXDisplayHeight(ctx.display,ctx.screen);
        mm_width=max(1,pXDisplayWidthMM(ctx.display,ctx.screen)); mm_height=max(1,pXDisplayHeightMM(ctx.display,ctx.screen));
        pXGrabServer(ctx.display);
        /* Enlarge the framebuffer before growing the CRTC; shrink it afterward. */
        pXRRSetScreenSize(ctx.display,ctx.root,max(width,mode->width),max(height,mode->height),mm_width,mm_height);
        status=pXRRSetCrtcConfig(ctx.display,ctx.resources,ctx.output->crtc,CurrentTime,0,0,mode->id,
                               ctx.crtc->rotation,ctx.crtc->outputs,ctx.crtc->noutput);
        if(status==RRSetConfigSuccess)
            pXRRSetScreenSize(ctx.display,ctx.root,mode->width,mode->height,mm_width,mm_height);
        else
            pXRRSetScreenSize(ctx.display,ctx.root,width,height,mm_width,mm_height);
        pXUngrabServer(ctx.display); pXSync(ctx.display,False);
        if(status!=RRSetConfigSuccess) { params->err=ERROR_GEN_FAILURE; goto done; }
        {
            XRRCrtcInfo *actual=pXRRGetCrtcInfo(ctx.display,ctx.resources,ctx.output->crtc);
            if(!actual || actual->mode!=mode->id || actual->width!=mode->width || actual->height!=mode->height)
                params->err=ERROR_GEN_FAILURE;
            if(actual) pXRRFreeCrtcInfo(actual);
        }
    }
    if(!params->err && params->persist) params->err=persist_mode(&params->mode,mode);
done:
    release(&ctx);
    return STATUS_SUCCESS;
}
#else
NTSTATUS display_get(void *args)
{
    ((struct display_get_params *)args)->err=ERROR_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}
NTSTATUS display_set(void *args)
{
    ((struct display_set_params *)args)->err=ERROR_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}
#endif
