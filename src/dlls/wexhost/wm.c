/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Backend dispatch for native window management. ABI arguments are fixed-size
 * PE/Unix records, shared by both WoW64 architectures.
 */
#if 0
#pragma makedep unix
#endif
#include "config.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <winerror.h>
#include "wexhost_unix.h"

static pthread_mutex_t backend_mutex = PTHREAD_MUTEX_INITIALIZER;
static const struct wm_backend_ops *backend;

static const struct wm_backend_ops *get_backend(void)
{
    const struct wm_backend_ops *ops;
    pthread_mutex_lock(&backend_mutex);
    ops = backend;
    pthread_mutex_unlock(&backend_mutex);
    return ops;
}

NTSTATUS wm_start_ex(void *args)
{
    struct wm_start_ex_params *params = args;
    struct wm_start_params start = {params->desktop};
    DWORD selected = params->backend;
    const struct wm_backend_ops *ops = NULL;

    if (selected == WEX_WM_BACKEND_AUTO)
        selected = params->desktop ? WEX_WM_BACKEND_X11 : WEX_WM_BACKEND_WAYLAND;
    if (selected == WEX_WM_BACKEND_X11) ops = &x11_wm_backend;
    /* Explicit selection enables observation/control tests. AUTO remains
     * disabled until the frame binding capability is implemented. */
    if (selected == WEX_WM_BACKEND_WAYLAND && params->backend != WEX_WM_BACKEND_AUTO)
        ops = &wayland_wm_backend;
    if (selected > WEX_WM_BACKEND_WAYLAND) params->err = ERROR_INVALID_PARAMETER;
    else if (!ops) params->err = ERROR_NOT_SUPPORTED;
    else
    {
        pthread_mutex_lock(&backend_mutex);
        if (backend) params->err = ERROR_ALREADY_EXISTS;
        else
        {
            ops->start(&start);
            params->err = start.err;
            if (!start.err) backend = ops;
        }
        pthread_mutex_unlock(&backend_mutex);
    }
    return STATUS_SUCCESS;
}

NTSTATUS wm_start(void *args)
{
    struct wm_start_params *params = args;
    struct wm_start_ex_params extended = {params->desktop, WEX_WM_BACKEND_AUTO};
    wm_start_ex(&extended);
    params->err = extended.err;
    return STATUS_SUCCESS;
}

NTSTATUS wm_capabilities(void *args)
{
    struct wm_capabilities_params *params = args;
    const struct wm_backend_ops *ops = get_backend();
    params->backend = ops ? ops->backend : WEX_WM_BACKEND_AUTO;
    if (ops && ops->get_capabilities) return ops->get_capabilities(args);
    params->capabilities = ops ? ops->capabilities : 0;
    params->err = ops ? ERROR_SUCCESS : ERROR_NOT_READY;
    return STATUS_SUCCESS;
}

NTSTATUS wm_next_event(void *args)
{
    struct wm_next_event_params *params = args;
    const struct wm_backend_ops *ops = get_backend();
    if (ops) return ops->next_event(args);
    memset(&params->event, 0, sizeof(params->event));
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}

#define DISPATCH(name, type, member) \
NTSTATUS name(void *args) \
{ \
    const struct wm_backend_ops *ops = get_backend(); \
    if (ops) return ops->member(args); \
    ((type *)args)->err = ERROR_NOT_READY; \
    return STATUS_SUCCESS; \
}
DISPATCH(wm_manage, struct wm_manage_params, manage)
DISPATCH(wm_configure, struct wm_window_params, configure)
DISPATCH(wm_set_state, struct wm_window_params, set_state)
DISPATCH(wm_close, struct wm_window_params, close)
DISPATCH(wm_release, struct wm_window_params, release)
DISPATCH(wm_get_title, struct wm_get_title_params, get_title)
DISPATCH(wm_get_icon, struct wm_get_icon_params, get_icon)
DISPATCH(wm_focus, struct wm_window_params, focus)

#define OPTIONAL_DISPATCH(name, params_type, op) \
NTSTATUS name(void *args) \
{ \
    const struct wm_backend_ops *ops = get_backend(); \
    if (ops && ops->op) return ops->op(args); \
    ((params_type *)args)->err = ops ? ERROR_NOT_SUPPORTED : ERROR_NOT_READY; \
    return STATUS_SUCCESS; \
}
OPTIONAL_DISPATCH(wm_effects, struct wm_effects_params, effects)
OPTIONAL_DISPATCH(wm_thumbnails, struct wm_thumbnails_params, thumbnails)
OPTIONAL_DISPATCH(wm_peek, struct wm_peek_params, peek)
OPTIONAL_DISPATCH(wm_clock, struct wm_clock_params, clock)
OPTIONAL_DISPATCH(wm_clock_wait, struct wm_clock_wait_params, wait_frame)

/* Both producers wake the existing shared generation wait. Translate records
 * explicitly; the legacy wait has no err member. No backend lock is held. */
NTSTATUS wm_wait_frame(void *args)
{
    struct wm_clock_wait_params *params = args;
    struct composition_clock_wait_params request = { params->generation, params->timeout_ms };
    composition_clock_wait( &request );
    params->generation = request.generation;
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS wm_backend_thumbnails(void *args)
{
    struct wm_thumbnails_params *params = args;
    if (params->backend == WEX_WM_BACKEND_X11) return x11_wm_backend.thumbnails(args);
    return wm_thumbnails(args);
}

NTSTATUS wm_backend_peek(void *args)
{
    struct wm_peek_params *params = args;
    if (params->backend == WEX_WM_BACKEND_X11) return x11_wm_backend.peek(args);
    return wm_peek(args);
}

/* Identity routing is deliberate: the old bridge always sent an available XID
 * to composition, regardless of the selected WM backend. Wayland still uses
 * the selected backend, preserving NOT_READY/NOT_SUPPORTED on missing/X11 WMs.
 * get_backend releases its mutex before either backend takes its own lock. */
NTSTATUS wm_apply_effects(void *args)
{
    struct wm_apply_effects_params *params = args;
    const struct wm_backend_ops *ops = params->backend == WEX_WM_BACKEND_X11 ? &x11_wm_backend : get_backend();
    if (ops && ops->apply_effects) return ops->apply_effects(args);
    params->err = ops ? ERROR_NOT_SUPPORTED : ERROR_NOT_READY;
    return STATUS_SUCCESS;
}

/* Wine pixels -> surface units. -1 floor, 0 nearest/ties away, 1 ceil. */
INT wm_effect_coord(INT value, DWORD scale, int rounding)
{
    LONGLONG pixels = (LONGLONG)value * 1000;
    LONGLONG result = pixels / scale, remainder = pixels % scale;

    if (rounding < 0 && remainder < 0) result--;
    else if (rounding > 0 && remainder > 0) result++;
    else if (!rounding)
    {
        if (remainder * 2 >= scale) result++;
        else if (-remainder * 2 >= scale) result--;
    }
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return result;
}

NTSTATUS wm_native_info(void *args)
{
    struct wm_native_info_params *params = args;
    const struct wm_backend_ops *ops = get_backend();
    memset(&params->info, 0, sizeof(params->info));
    if (ops && ops->native_info) return ops->native_info(args);
    params->err = ops ? ERROR_NOT_SUPPORTED : ERROR_NOT_READY;
    return STATUS_SUCCESS;
}

NTSTATUS wm_binding(void *args)
{
    struct wm_binding_params *params = args;
    const struct wm_backend_ops *ops = get_backend();
    memset(params->token, 0, sizeof(params->token));
    memset(params->controller, 0, sizeof(params->controller));
    if (ops && ops->binding) return ops->binding(args);
    params->err = ops ? ERROR_NOT_SUPPORTED : ERROR_NOT_READY;
    return STATUS_SUCCESS;
}

NTSTATUS wm_stop(void *args)
{
    struct wm_stop_params *params = args;
    const struct wm_backend_ops *ops = get_backend();
    params->err = ERROR_SUCCESS;
    /* next_event must be allowed to finish while the backend wakes/joins its
     * reader. Never hold the dispatch lock across that blocking operation. */
    if (ops) ops->stop(args);
    pthread_mutex_lock(&backend_mutex);
    if (backend == ops) backend = NULL;
    pthread_mutex_unlock(&backend_mutex);
    return STATUS_SUCCESS;
}
