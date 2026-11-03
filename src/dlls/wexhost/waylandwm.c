/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Privileged native inventory and standard foreign-toplevel control.
 */
#if 0
#pragma makedep unix
#endif
#include "config.h"
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <winerror.h>
#include "wexhost_unix.h"
#include "wex-window-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

struct native_window
{
    struct native_window *next;
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    struct wex_foreign_window_v1 *foreign;
    WEX_WM_EVENT snapshot;
    char title[1024], app_id[256], token[33], controller[33];
    BOOL standard_ready, supplemental_ready, announced, rejected, dead, released, foreign_closed;
};
struct queued_event { struct queued_event *next; WEX_WM_EVENT event; };
static pthread_mutex_t wl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_seat *seat;
static struct wex_window_manager_v1 *manager;
static struct zwlr_foreign_toplevel_manager_v1 *inventory;
static struct native_window *windows;
static struct queued_event *queue_head, *queue_tail;
static char native_socket[108], native_xdisplay[128];
static pthread_t reader;
static BOOL running, reader_started, queue_stopped;
static UINT64 next_id; /* never reuse an identifier after stop/restart */

BOOL wayland_wm_native_environment(char wayland[108], char xdisplay[128])
{
    BOOL available;
    pthread_mutex_lock(&wl_mutex);
    available = running && native_socket[0];
    if (available)
    {
        memcpy(wayland,native_socket,sizeof(native_socket));
        memcpy(xdisplay,native_xdisplay,sizeof(native_xdisplay));
    }
    pthread_mutex_unlock(&wl_mutex);
    return available;
}

static void stop_queue(void)
{
    pthread_mutex_lock(&queue_mutex);
    queue_stopped = TRUE;
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}
static void queue_event(struct native_window *window, DWORD type)
{
    struct queued_event *node;
    struct queued_event *last=NULL;
    pthread_mutex_lock(&queue_mutex);
    if (type == WEX_WM_OBSERVED || type == WEX_WM_TITLE || type == WEX_WM_ICON)
        for (node=queue_head;node;node=node->next)
            if (node->event.window == window->snapshot.window && node->event.type == type)
                last=node;
    /* State transitions acknowledge shell requests. Coalescing across one can
     * discard the only acknowledgement and leave a later restore pending forever.
     * Geometry/title bursts may still replace the latest matching-state sample. */
    if (last && (type != WEX_WM_OBSERVED || last->event.states == window->snapshot.states))
    { last->event = window->snapshot; last->event.type = type; pthread_mutex_unlock(&queue_mutex); return; }
    if (!(node = malloc(sizeof(*node))))
    { pthread_mutex_unlock(&queue_mutex); running = FALSE; stop_queue(); return; }
    node->event = window->snapshot;
    node->event.type = type;
    node->next = NULL;
    if (queue_tail) queue_tail->next = node;
    else queue_head = node;
    queue_tail = node;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}
static void publish(struct native_window *window)
{
    if (window->dead || window->rejected || window->released ||
        !window->standard_ready || !window->supplemental_ready || !window->token[0] ||
        (wex_window_manager_v1_get_version(manager) >= 4 && !window->controller[0])) return;
    queue_event(window, window->announced ? WEX_WM_OBSERVED : WEX_WM_MAP);
    window->announced = TRUE;
}
static void withdraw(struct native_window *window)
{
    if (window->announced && !window->released) queue_event(window, WEX_WM_WITHDRAW);
    window->announced = FALSE;
}
static void collect_closed(struct native_window *window)
{
    struct native_window **cursor;
    if (!window->dead || !window->foreign_closed) return;
    for (cursor=&windows;*cursor;cursor=&(*cursor)->next)
        if (*cursor == window) { *cursor = window->next; break; }
    if (window->foreign) wex_foreign_window_v1_destroy(window->foreign);
    zwlr_foreign_toplevel_handle_v1_destroy(window->handle);
    free(window);
}
static void got_token(void *data, struct wex_foreign_window_v1 *obj, const char *token)
{
    struct native_window *window = data;
    if (strlen(token) != 32 || strspn(token,"0123456789abcdef") != 32)
    { window->rejected = TRUE; return; }
    memcpy(window->token,token,33);
}
static void got_geometry(void *data, struct wex_foreign_window_v1 *obj,
                         int32_t x, int32_t y, int32_t width, int32_t height)
{
    struct native_window *window = data;
    window->supplemental_ready = FALSE;
    window->snapshot.rect = (RECT){x,y,x+width,y+height};
}
static void got_controller(void *data, struct wex_foreign_window_v1 *obj, const char *handle)
{
    struct native_window *window = data;
    if (strlen(handle) != 32 || strspn(handle,"0123456789abcdef") != 32)
    { window->rejected = TRUE; return; }
    memcpy(window->controller, handle, sizeof(window->controller));
    publish(window);
}
static void got_decoration(void *data, struct wex_foreign_window_v1 *obj, uint32_t mode)
{
    struct native_window *window = data;
    window->snapshot.flags = WEX_WM_POSITION;
    if (mode != WEX_FOREIGN_WINDOW_V1_DECORATION_SERVER) window->snapshot.flags |= WEX_WM_NO_BORDER;
}
static void got_hints(void *data, struct wex_foreign_window_v1 *obj,
    int32_t min_width,int32_t min_height,int32_t width_inc,int32_t height_inc,int32_t base_width,int32_t base_height)
{
    struct native_window *window = data;
    window->snapshot.min_size = (SIZE){min_width,min_height};
}
static void got_maximum_size(void *data, struct wex_foreign_window_v1 *obj, int32_t width, int32_t height)
{
    struct native_window *window = data;
    window->snapshot.max_size = (SIZE){width,height};
}
static void got_done(void *data, struct wex_foreign_window_v1 *obj)
{
    struct native_window *window = data;
    window->supplemental_ready = TRUE;
    publish(window);
}
static void got_closed(void *data, struct wex_foreign_window_v1 *obj)
{
    struct native_window *window = data;
    window->rejected = TRUE;
    window->foreign_closed = TRUE;
    withdraw(window);
    collect_closed(window);
}
static const struct wex_foreign_window_v1_listener supplement_listener = {
    .token=got_token, .geometry=got_geometry, .decoration=got_decoration,
    .size_hints=got_hints, .done=got_done, .closed=got_closed,
    .controller_handle=got_controller,
    .maximum_size=got_maximum_size,
};
static void title(void *data, struct zwlr_foreign_toplevel_handle_v1 *obj, const char *title)
{
    struct native_window *window = data;
    snprintf(window->title,sizeof(window->title),"%s",title);
    if (window->announced && !window->released) queue_event(window,WEX_WM_TITLE);
}
static void app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *obj, const char *id)
{
    struct native_window *window = data;
    snprintf(window->app_id,sizeof(window->app_id),"%s",id);
    if (window->announced && !window->released) queue_event(window,WEX_WM_ICON);
}
static void output(void *data, struct zwlr_foreign_toplevel_handle_v1 *obj, struct wl_output *output) { }
static void state(void *data, struct zwlr_foreign_toplevel_handle_v1 *obj, struct wl_array *values)
{
    struct native_window *window = data;
    uint32_t *value;
    window->snapshot.states = 0;
    wl_array_for_each(value,values)
    {
        switch (*value)
        {
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED: window->snapshot.states |= WEX_WM_STATE_MAXIMIZED; break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED: window->snapshot.states |= WEX_WM_STATE_MINIMIZED; break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED: window->snapshot.states |= WEX_WM_STATE_FOCUSED; break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN: window->snapshot.states |= WEX_WM_STATE_FULLSCREEN; break;
        }
    }
}
static void standard_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *obj)
{
    struct native_window *window = data;
    window->standard_ready = TRUE;
    publish(window);
}
static void standard_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *obj)
{
    struct native_window *window = data;
    window->dead = TRUE;
    withdraw(window);
    collect_closed(window);
}
static void parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *obj,
                   struct zwlr_foreign_toplevel_handle_v1 *parent)
{
    struct native_window *window = data;
    struct native_window *owner = parent ? wl_proxy_get_user_data((struct wl_proxy *)parent) : NULL;
    window->snapshot.owner = owner ? owner->snapshot.window : 0;
    window->snapshot.window_type = owner ? WEX_WM_TYPE_DIALOG : WEX_WM_TYPE_NORMAL;
}
static const struct zwlr_foreign_toplevel_handle_v1_listener standard_listener = {
    .title=title, .app_id=app_id, .output_enter=output, .output_leave=output,
    .state=state, .done=standard_done, .closed=standard_closed, .parent=parent,
};
static void toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *obj,
                     struct zwlr_foreign_toplevel_handle_v1 *handle)
{
    struct native_window *window;
    if (!manager || !(window = calloc(1,sizeof(*window))))
    { zwlr_foreign_toplevel_handle_v1_destroy(handle); running = FALSE; return; }
    window->snapshot.window = ++next_id;
    window->handle = handle;
    window->next = windows;
    windows = window;
    zwlr_foreign_toplevel_handle_v1_add_listener(handle,&standard_listener,window);
    window->foreign = wex_window_manager_v1_get_foreign(manager,handle);
    wex_foreign_window_v1_add_listener(window->foreign,&supplement_listener,window);
}
static void finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *obj) { running = FALSE; }
static const struct zwlr_foreign_toplevel_manager_v1_listener inventory_listener = { toplevel,finished };
static void environment(void *data, struct wex_window_manager_v1 *obj,const char *socket,const char *xdisplay)
{
    snprintf(native_socket,sizeof(native_socket),"%s",socket);
    snprintf(native_xdisplay,sizeof(native_xdisplay),"%s",xdisplay);
}
static const struct wex_window_manager_v1_listener manager_listener = { environment };
static void global(void *data, struct wl_registry *registry,uint32_t name,const char *interface,uint32_t version)
{
    if (!strcmp(interface,"wex_window_manager_v1") && version >= 3)
    {
        manager = wl_registry_bind(registry,name,&wex_window_manager_v1_interface,version < 13 ? version : 13);
        wex_window_manager_v1_add_listener(manager,&manager_listener,NULL);
    }
    else if (!strcmp(interface,"zwlr_foreign_toplevel_manager_v1") && version >= 3)
    {
        inventory = wl_registry_bind(registry,name,&zwlr_foreign_toplevel_manager_v1_interface,3);
        zwlr_foreign_toplevel_manager_v1_add_listener(inventory,&inventory_listener,NULL);
    }
    else if (!strcmp(interface,"wl_seat") && !seat) seat = wl_registry_bind(registry,name,&wl_seat_interface,1);
}
static void removed(void *data,struct wl_registry *registry,uint32_t name) { }
static const struct wl_registry_listener registry_listener = { global,removed };

static void *read_events(void *unused)
{
    struct native_window *window;
    pthread_mutex_lock(&wl_mutex);
    while (running)
    {
        struct pollfd fd = {wl_display_get_fd(display),POLLIN,0};
        int ret;
        while (running && wl_display_prepare_read(display) < 0)
            if (wl_display_dispatch_pending(display) < 0) running = FALSE;
        if (!running) break;
        ret = wl_display_flush(display);
        if (ret < 0 && errno != EAGAIN) { wl_display_cancel_read(display); break; }
        if (ret < 0) fd.events |= POLLOUT;
        pthread_mutex_unlock(&wl_mutex);
        ret = poll(&fd,1,50);
        pthread_mutex_lock(&wl_mutex);
        if (ret > 0 && (fd.revents & POLLIN)) ret = wl_display_read_events(display);
        else { wl_display_cancel_read(display); if (ret < 0 && errno == EINTR) ret = 0; }
        if (ret < 0 || (fd.revents & (POLLERR|POLLHUP|POLLNVAL)) || wl_display_dispatch_pending(display) < 0) break;
    }
    running = FALSE;
    /* The event thread drains these before STOP. Future shell adoption must
     * destroy its proxies when the host connection disappears, too. */
    for (window=windows;window;window=window->next) withdraw(window);
    pthread_mutex_unlock(&wl_mutex);
    stop_queue();
    return NULL;
}
static NTSTATUS wayland_stop(void *args)
{
    struct native_window *window;
    struct queued_event *event;
    pthread_mutex_lock(&wl_mutex);
    running = FALSE;
    pthread_mutex_unlock(&wl_mutex);
    if (reader_started) pthread_join(reader,NULL);
    reader_started = FALSE;
    stop_queue();
    pthread_mutex_lock(&wl_mutex);
    while ((window = windows))
    {
        windows = window->next;
        if (window->foreign) wex_foreign_window_v1_destroy(window->foreign);
        zwlr_foreign_toplevel_handle_v1_destroy(window->handle);
        free(window);
    }
    if (inventory) wl_proxy_destroy((struct wl_proxy *)inventory);
    if (manager) wex_window_manager_v1_destroy(manager);
    if (seat) wl_seat_destroy(seat);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
    display = NULL; registry = NULL; manager = NULL; inventory = NULL; seat = NULL;
    native_socket[0] = native_xdisplay[0] = 0;
    pthread_mutex_unlock(&wl_mutex);
    pthread_mutex_lock(&queue_mutex);
    while ((event = queue_head)) { queue_head = event->next; free(event); }
    queue_tail = NULL;
    pthread_mutex_unlock(&queue_mutex);
    ((struct wm_stop_params *)args)->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}
static NTSTATUS wayland_start(void *args)
{
    struct wm_start_params *params = args;
    struct wm_stop_params stop;
    unsigned i;
    params->err = ERROR_NOT_SUPPORTED;
    if (!(display = wl_display_connect(NULL))) return STATUS_SUCCESS;
    running = TRUE;
    queue_stopped = FALSE;
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry,&registry_listener,NULL);
    for (i=0;i<3;i++) if (wl_display_roundtrip(display) < 0 || !running) goto failed;
    if (!manager || !inventory || !native_socket[0]) goto failed;
    if (pthread_create(&reader,NULL,read_events,NULL)) { params->err = ERROR_NOT_ENOUGH_MEMORY; goto failed; }
    reader_started = TRUE;
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
failed:
    wayland_stop(&stop);
    return STATUS_SUCCESS;
}
static NTSTATUS wayland_next_event(void *args)
{
    struct wm_next_event_params *params = args;
    struct queued_event *node;
    memset(&params->event,0,sizeof(params->event));
    pthread_mutex_lock(&queue_mutex);
    while (!queue_head && !queue_stopped) pthread_cond_wait(&queue_cond,&queue_mutex);
    if ((node = queue_head))
    {
        params->event = node->event;
        queue_head = node->next;
        if (!queue_head) queue_tail = NULL;
        free(node);
    }
    pthread_mutex_unlock(&queue_mutex);
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}
static struct native_window *find_window(UINT64 id)
{
    struct native_window *window;
    for (window=windows;window;window=window->next)
        if (window->snapshot.window == id && !window->dead && !window->rejected && !window->released) return window;
    return NULL;
}
static NTSTATUS wayland_title(void *args)
{
    struct wm_get_title_params *params = args;
    struct native_window *window;
    pthread_mutex_lock(&wl_mutex);
    params->title[0] = 0;
    params->err = ERROR_INVALID_WINDOW_HANDLE;
    if ((window = find_window(params->window)))
    { memcpy(params->title,window->title,sizeof(params->title)); params->err = ERROR_SUCCESS; }
    pthread_mutex_unlock(&wl_mutex);
    return STATUS_SUCCESS;
}
static NTSTATUS wayland_info(void *args)
{
    struct wm_native_info_params *params = args;
    struct native_window *window;
    pthread_mutex_lock(&wl_mutex);
    params->err = running ? ERROR_INVALID_WINDOW_HANDLE : ERROR_NOT_READY;
    if (running && (window = find_window(params->window)) && window->announced)
    {
        memcpy(params->info.token,window->token,sizeof(params->info.token));
        memcpy(params->info.app_id,window->app_id,sizeof(params->info.app_id));
        memcpy(params->info.wayland_display,native_socket,sizeof(native_socket));
        memcpy(params->info.xwayland_display,native_xdisplay,sizeof(native_xdisplay));
        params->err = ERROR_SUCCESS;
    }
    pthread_mutex_unlock(&wl_mutex);
    return STATUS_SUCCESS;
}
static NTSTATUS control(void *args, unsigned action)
{
    struct wm_window_params *params = args;
    struct native_window *window;
    pthread_mutex_lock(&wl_mutex);
    params->err = running ? ERROR_INVALID_WINDOW_HANDLE : ERROR_NOT_READY;
    if (running && (window = find_window(params->window)))
    {
        params->err = ERROR_SUCCESS;
        if (action == 0) zwlr_foreign_toplevel_handle_v1_close(window->handle);
        else if (action == 1)
        {
            if (!seat) params->err = ERROR_NOT_SUPPORTED;
            else
            {
                zwlr_foreign_toplevel_handle_v1_unset_minimized(window->handle);
                zwlr_foreign_toplevel_handle_v1_activate(window->handle,seat);
            }
        }
        else if (action == 2)
        {
            window->released = TRUE;
            window->foreign_closed = TRUE;
            if (window->foreign) wex_foreign_window_v1_destroy(window->foreign);
            window->foreign = NULL;
        }
        else if (params->value & WEX_WM_STATE_ABOVE) params->err = ERROR_NOT_SUPPORTED;
        else
        {
            if (params->value & WEX_WM_STATE_MAXIMIZED) zwlr_foreign_toplevel_handle_v1_set_maximized(window->handle);
            else zwlr_foreign_toplevel_handle_v1_unset_maximized(window->handle);
            if (params->value & WEX_WM_STATE_FULLSCREEN) zwlr_foreign_toplevel_handle_v1_set_fullscreen(window->handle,NULL);
            else zwlr_foreign_toplevel_handle_v1_unset_fullscreen(window->handle);
            if (params->value & WEX_WM_STATE_MINIMIZED) zwlr_foreign_toplevel_handle_v1_set_minimized(window->handle);
            else zwlr_foreign_toplevel_handle_v1_unset_minimized(window->handle);
        }
        if (wl_display_flush(display) < 0 && errno != EAGAIN) params->err = ERROR_GEN_FAILURE;
    }
    pthread_mutex_unlock(&wl_mutex);
    return STATUS_SUCCESS;
}
/* DWM effects of any Wine window by its shared handle (v13): the shell is the privileged client, the window's own
 * connection need not be. An older compositor has no such request. */
static NTSTATUS wayland_effects(void *args)
{
    struct wm_effects_params *params = args;
    unsigned i;
    pthread_mutex_lock(&wl_mutex);
    params->err = running ? ERROR_SUCCESS : ERROR_NOT_READY;
    if (running)
    {
        params->handle[32] = 0;
        for (i = 0; i < 32; i++)
            if (!((params->handle[i] >= '0' && params->handle[i] <= '9') ||
                  (params->handle[i] >= 'a' && params->handle[i] <= 'f')))
                params->err = ERROR_INVALID_PARAMETER;
        if (params->err) {}
        else if (!manager || wex_window_manager_v1_get_version(manager) < 13) params->err = ERROR_NOT_SUPPORTED;
        else
        {
            wex_window_manager_v1_set_effects(manager,params->handle,params->radius,params->backdrop,params->flags,
                                              params->tint,params->region[0],params->region[1],params->region[2],
                                              params->region[3]);
            if (wl_display_flush(display) < 0 && errno != EAGAIN) params->err = ERROR_GEN_FAILURE;
        }
    }
    pthread_mutex_unlock(&wl_mutex);
    return STATUS_SUCCESS;
}
static NTSTATUS wayland_close(void *args) { return control(args,0); }
static NTSTATUS wayland_focus(void *args) { return control(args,1); }
static NTSTATUS wayland_release(void *args) { return control(args,2); }
static NTSTATUS wayland_state(void *args) { return control(args,3); }
static NTSTATUS wayland_manage(void *args) { ((struct wm_manage_params *)args)->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
static NTSTATUS wayland_configure(void *args) { ((struct wm_window_params *)args)->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
static NTSTATUS wayland_icon(void *args)
{
    struct wm_get_icon_params *params = args;
    params->width = params->height = 0;
    params->err = ERROR_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}
static NTSTATUS wayland_binding(void *args)
{
    struct wm_binding_params *params = args;
    struct native_window *window;
    pthread_mutex_lock(&wl_mutex);
    params->err = running ? ERROR_INVALID_WINDOW_HANDLE : ERROR_NOT_READY;
    if (running && manager && wex_window_manager_v1_get_version(manager) < 7)
        params->err = ERROR_NOT_SUPPORTED;
    else if (running && (window = find_window(params->window)) && window->announced)
    {
        if (!window->controller[0])
            params->err = ERROR_NOT_SUPPORTED;
        else
        {
            memcpy(params->token,window->token,sizeof(params->token));
            memcpy(params->controller,window->controller,sizeof(params->controller));
            params->err = ERROR_SUCCESS;
        }
    }
    pthread_mutex_unlock(&wl_mutex);
    return STATUS_SUCCESS;
}
static NTSTATUS wayland_capabilities(void *args)
{
    struct wm_capabilities_params *params = args;
    pthread_mutex_lock(&wl_mutex);
    params->backend = WEX_WM_BACKEND_WAYLAND;
    params->capabilities = WEX_WM_CAP_OBSERVE | WEX_WM_CAP_NATIVE_ENV;
    if (manager && wex_window_manager_v1_get_version(manager) >= 7)
        params->capabilities |= WEX_WM_CAP_FOREIGN_FRAME;
    params->err = running ? ERROR_SUCCESS : ERROR_NOT_READY;
    pthread_mutex_unlock(&wl_mutex);
    return STATUS_SUCCESS;
}
const struct wm_backend_ops wayland_wm_backend = {
    WEX_WM_BACKEND_WAYLAND, WEX_WM_CAP_OBSERVE | WEX_WM_CAP_NATIVE_ENV,
    wayland_start,wayland_next_event,wayland_manage,wayland_configure,wayland_state,
    wayland_close,wayland_release,wayland_title,wayland_icon,wayland_stop,wayland_focus,wayland_info,
    wayland_binding,wayland_capabilities,wayland_effects,
};
