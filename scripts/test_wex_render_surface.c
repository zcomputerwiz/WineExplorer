/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Private rendering-role lifecycle/invalid-parent diagnostic, two connections.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "wex-window-v1-client-protocol.h"

struct connection {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *xdg;
    struct wex_window_manager_v1 *manager;
};
static char parent_handle[33];
static const char *ack_file;
static int configured;
static void ping(void *data, struct xdg_wm_base *xdg, uint32_t serial) { (void)data; xdg_wm_base_pong(xdg, serial); }
static const struct xdg_wm_base_listener xdg_listener = {ping};
static void native_environment(void *data, struct wex_window_manager_v1 *manager, const char *wayland, const char *x11)
{ (void)data; (void)manager; (void)wayland; (void)x11; }
static const struct wex_window_manager_v1_listener manager_listener = {native_environment};
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    struct connection *c = data;
    if (!strcmp(interface, "wl_compositor")) c->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    if (!strcmp(interface, "wl_shm")) c->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    if (!strcmp(interface, "xdg_wm_base")) {
        c->xdg = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(c->xdg, &xdg_listener, NULL);
    }
    if (!strcmp(interface, "wex_window_manager_v1") && version >= 11) {
        c->manager = wl_registry_bind(registry, name, &wex_window_manager_v1_interface, 11);
        wex_window_manager_v1_add_listener(c->manager, &manager_listener, NULL);
    }
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) { (void)data; (void)registry; (void)name; }
static const struct wl_registry_listener registry_listener = {global, removed};
static void connect_owned(struct connection *c)
{
    c->display = wl_display_connect(NULL);
    if (!c->display) exit(2);
    struct wl_registry *registry = wl_display_get_registry(c->display);
    wl_registry_add_listener(registry, &registry_listener, c);
    if (wl_display_roundtrip(c->display) < 0 || !c->manager || !c->xdg || !c->shm || !c->compositor) exit(3);
    wl_registry_destroy(registry);
}
static struct wl_buffer *buffer(struct connection *c, int size, uint32_t color)
{
    static unsigned int next;
    char name[80];
    snprintf(name, sizeof(name), "/wex-render-test-%ld-%u", (long)getpid(), ++next);
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) exit(4);
    shm_unlink(name);
    size_t bytes = (size_t)size * size * 4;
    if (ftruncate(fd, bytes)) exit(4);
    uint32_t *pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) exit(4);
    for (int i = 0; i < size * size; ++i) pixels[i] = color;
    struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, bytes);
    struct wl_buffer *result = wl_shm_pool_create_buffer(pool, 0, size, size, size * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool); close(fd); munmap(pixels, bytes);
    return result;
}
static void configure(void *data, struct xdg_surface *surface, uint32_t serial)
{ (void)data; xdg_surface_ack_configure(surface, serial); configured = 1; }
static const struct xdg_surface_listener surface_listener = {configure};
static void top_configure(void *data, struct xdg_toplevel *top, int32_t width, int32_t height, struct wl_array *states)
{ (void)data; (void)top; (void)width; (void)height; (void)states; }
static void top_close(void *data, struct xdg_toplevel *top) { (void)data; (void)top; exit(8); }
static const struct xdg_toplevel_listener top_listener = {.configure = top_configure, .close = top_close};
static void handle(void *data, struct wex_window_v1 *window, const char *value)
{ (void)data; (void)window; if (strlen(value) == 32) memcpy(parent_handle, value, 33); }
static const struct wex_window_v1_listener window_listener = {.handle = handle};
static void stage(struct connection *a, struct connection *b, const char *name)
{
    if (wl_display_roundtrip(b->display) < 0 || wl_display_roundtrip(a->display) < 0) exit(5);
    printf("STAGE %s\n", name); fflush(stdout);
    for (int i = 0; i < 100; ++i) {
        char ack[80] = {0}; FILE *file = fopen(ack_file, "r");
        if (file) { (void)fread(ack, 1, sizeof(ack)-1, file); fclose(file); }
        if (!strcmp(ack, name)) return;
        struct timespec delay = {0, 100000000}; nanosleep(&delay, NULL);
    }
    exit(6);
}
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    ack_file = argv[1];
    struct connection frame = {0}, producer = {0};
    connect_owned(&frame); connect_owned(&producer);
    struct wl_surface *surface = wl_compositor_create_surface(frame.compositor);
    struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(frame.xdg, surface);
    xdg_surface_add_listener(xdg, &surface_listener, NULL);
    struct xdg_toplevel *top = xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(top, &top_listener, NULL);
    xdg_toplevel_set_title(top, "WEX render protocol diagnostic");
    struct wex_window_v1 *window = wex_window_manager_v1_get_window(frame.manager, top);
    wex_window_v1_add_listener(window, &window_listener, NULL);
    wl_surface_commit(surface);
    while (!configured || !parent_handle[0]) if (wl_display_roundtrip(frame.display) < 0) return 5;
    struct wl_buffer *white = buffer(&frame, 200, 0xffffffff);
    wl_surface_attach(surface, white, 0, 0); wl_surface_damage(surface, 0, 0, 200, 200); wl_surface_commit(surface);
    if (wl_display_roundtrip(frame.display) < 0) return 5;
    struct wl_surface *pixels = wl_compositor_create_surface(producer.compositor);
    struct wex_render_surface_v1 *render = wex_window_manager_v1_get_render_surface(producer.manager, pixels);
    struct wl_buffer *red = buffer(&producer, 64, 0xffff0000);
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 64, 64);
    wl_surface_attach(pixels, red, 0, 0); wl_surface_damage(pixels, 0, 0, 64, 64); wl_surface_commit(pixels);
    stage(&frame, &producer, "attached");
    wex_render_surface_v1_set_parent(render, parent_handle, 40, 30, 64, 64);
    stage(&frame, &producer, "moved");
    wex_render_surface_v1_set_parent(render, parent_handle, -20, -12, 64, 64);
    stage(&frame, &producer, "negative_clip");
    wex_render_surface_v1_set_parent(render, parent_handle, 240, 240, 64, 64);
    stage(&frame, &producer, "outside_parent");
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 20, 30);
    stage(&frame, &producer, "explicit_bounds");
    wex_render_surface_v1_set_parent(render, "", 0, 0, 64, 64);
    stage(&frame, &producer, "empty_parent");
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 32, 32);
    wl_surface_set_buffer_scale(pixels, 2); wl_surface_commit(pixels);
    stage(&frame, &producer, "buffer_scale_two");
    wl_surface_set_buffer_scale(pixels, 1); wl_surface_commit(pixels);
    wex_render_surface_v1_set_parent(render, "00000000000000000000000000000000", 0, 0, 64, 64);
    stage(&frame, &producer, "wrong_parent");
    wex_render_surface_v1_set_parent(render, parent_handle, 180, 180, 64, 64);
    stage(&frame, &producer, "clipped");
    struct wl_buffer *small = buffer(&frame, 100, 0xffffffff);
    wl_surface_attach(surface, small, 0, 0); wl_surface_damage(surface, 0, 0, 200, 200); wl_surface_commit(surface);
    stage(&frame, &producer, "parent_shrunk");
    wl_surface_attach(surface, white, 0, 0); wl_surface_damage(surface, 0, 0, 200, 200); wl_surface_commit(surface);
    stage(&frame, &producer, "parent_grown");
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, -1, 64);
    stage(&frame, &producer, "invalid_size");
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 64, 64);
    stage(&frame, &producer, "reattached");
    char stale[33]; memcpy(stale, parent_handle, 33);
    wex_window_v1_destroy(window);
    stage(&frame, &producer, "parent_revoked");
    wex_render_surface_v1_set_parent(render, stale, 0, 0, 64, 64);
    stage(&frame, &producer, "stale_parent");
    parent_handle[0] = 0;
    window = wex_window_manager_v1_get_window(frame.manager, top);
    wex_window_v1_add_listener(window, &window_listener, NULL);
    while (!parent_handle[0]) if (wl_display_roundtrip(frame.display) < 0) return 5;
    if (!strcmp(stale, parent_handle)) return 7;
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 64, 64);
    stage(&frame, &producer, "new_parent");
    wex_render_surface_v1_destroy(render); wl_surface_destroy(pixels);
    stage(&frame, &producer, "producer_destroyed");
    pixels = wl_compositor_create_surface(producer.compositor);
    render = wex_window_manager_v1_get_render_surface(producer.manager, pixels);
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 64, 64);
    wl_surface_attach(pixels, red, 0, 0); wl_surface_damage(pixels, 0, 0, 64, 64); wl_surface_commit(pixels);
    stage(&frame, &producer, "producer_recreated");
    /* wlroots rejects surface-before-role destruction with a protocol error. */
    wl_surface_destroy(pixels);
    wl_proxy_destroy((struct wl_proxy *)render);
    if (wl_display_roundtrip(producer.display) >= 0 || !wl_display_get_error(producer.display)) return 9;
    wl_display_disconnect(producer.display);
    stage(&frame, &frame, "invalid_surface_order_rejected");
    memset(&producer, 0, sizeof(producer)); connect_owned(&producer);
    pixels = wl_compositor_create_surface(producer.compositor);
    render = wex_window_manager_v1_get_render_surface(producer.manager, pixels);
    red = buffer(&producer, 64, 0xffff0000);
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 64, 64);
    wl_surface_attach(pixels, red, 0, 0); wl_surface_damage(pixels, 0, 0, 64, 64); wl_surface_commit(pixels);
    stage(&frame, &producer, "before_disconnect");
    /* An abrupt connection close destroys live resources in server order. */
    wl_display_disconnect(producer.display);
    stage(&frame, &frame, "producer_disconnected");
    memset(&producer, 0, sizeof(producer)); connect_owned(&producer);
    pixels = wl_compositor_create_surface(producer.compositor);
    render = wex_window_manager_v1_get_render_surface(producer.manager, pixels);
    red = buffer(&producer, 64, 0xffff0000);
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 64, 64);
    wl_surface_attach(pixels, red, 0, 0); wl_surface_damage(pixels, 0, 0, 64, 64); wl_surface_commit(pixels);
    stage(&frame, &producer, "producer_reconnected");
    /* Destroy the actual frame surface while its identity and producer live. */
    xdg_toplevel_destroy(top); xdg_surface_destroy(xdg); wl_surface_destroy(surface);
    stage(&frame, &producer, "parent_surface_destroyed");
    wex_render_surface_v1_set_parent(render, parent_handle, 0, 0, 64, 64);
    stage(&frame, &producer, "destroyed_surface_handle");
    wex_render_surface_v1_destroy(render); wl_surface_destroy(pixels);
    wex_window_v1_destroy(window);
    wl_buffer_destroy(red); wl_buffer_destroy(white); wl_buffer_destroy(small);
    wl_display_roundtrip(producer.display); wl_display_roundtrip(frame.display);
    wl_display_disconnect(producer.display); wl_display_disconnect(frame.display);
    return 0;
}
