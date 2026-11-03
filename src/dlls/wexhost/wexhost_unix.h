/*
 * WineExplorer host services - PE/Unix interface
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WEXHOST_UNIX_H
#define __WEXHOST_UNIX_H

#include <stdarg.h>

#include <windef.h>
#include <winternl.h>

#include "wine/unixlib.h"
#include "wexhost.h"

/* Every call returns STATUS_SUCCESS; the Win32 outcome (0, ERROR_ACCESS_DENIED, ...) is in params->err. */

struct init_params { DWORD err; };

struct timedate_get_params { WEX_TIMEDATE info; DWORD err; };

struct timedate_set_time_params { INT64 unix_usec; BOOL relative; DWORD err; };

struct timedate_set_tz_params { char timezone[WEX_TZ_NAME_MAX]; DWORD err; };

struct timedate_set_ntp_params { BOOL enable; DWORD err; };

struct timedate_list_tz_params { ULONG64 buffer; DWORD size; DWORD needed; DWORD err; };

struct agent_start_params { DWORD err; };

struct agent_next_event_params { WEX_AGENT_EVENT event; DWORD err; };

struct agent_respond_params
{
    char cookie[WEX_COOKIE_MAX];
    char user[WEX_USER_MAX];
    char password[WEX_PASSWORD_MAX];
    BOOL cancel;
    DWORD err;
};

struct agent_stop_params { DWORD err; };

struct power_can_params { DWORD action; DWORD result; DWORD err; };
struct power_do_params { DWORD action; DWORD err; };
struct battery_get_params { WEX_BATTERY info; DWORD err; };
struct hostname_get_params { WEX_HOSTNAME info; DWORD err; };
struct hostname_set_params { char name[128]; BOOL pretty; DWORD err; };
struct user_get_params { WEX_USER info; DWORD err; };

struct net_get_status_params { WEX_NET_STATUS status; DWORD err; };
struct net_scan_wifi_params { ULONG64 buffer; DWORD max; DWORD count; DWORD err; };
struct net_connect_wifi_params { char ssid[64]; char passphrase[128]; BOOL sae; DWORD err; };
struct net_disconnect_params { DWORD err; };
struct net_enable_params { DWORD err; };
struct display_get_params { WEX_DISPLAY display; DWORD err; };
struct display_set_params { WEX_DISPLAY_MODE mode; BOOL persist; DWORD err; };
extern NTSTATUS display_get(void *args);
extern NTSTATUS display_set(void *args);

struct apps_list_params { ULONG64 buffer; DWORD size; DWORD needed; DWORD err; };
struct apps_launch_params { char id[WEX_APP_ID_MAX]; DWORD err; };
extern NTSTATUS apps_list(void *args);
extern NTSTATUS apps_launch(void *args);

struct hw_list_params { ULONG64 buffer; DWORD size; DWORD needed; DWORD err; };
extern NTSTATUS hw_list(void *args);

struct users_list_params { ULONG64 buffer; DWORD size; DWORD needed; DWORD err; };
struct locale_set_params { char locale[64]; char layout[64]; DWORD err; };
struct root_pass_set_params { char password[128]; DWORD err; };
struct user_create_params { char name[64]; char real_name[128]; int account_type; char password[128]; DWORD err; };
struct user_set_session_params { char name[64]; char session[64]; BOOL autologin; DWORD err; };
struct greeter_params { char text[512]; DWORD value; DWORD err; };
struct greeter_read_params { ULONG64 buffer; DWORD size; DWORD timeout; DWORD err; };
extern NTSTATUS greeter_connect(void *args);
extern NTSTATUS greeter_authenticate(void *args);
extern NTSTATUS greeter_respond(void *args);
extern NTSTATUS greeter_cancel(void *args);
extern NTSTATUS greeter_start_session(void *args);
extern NTSTATUS greeter_read(void *args);

struct wm_start_params { UINT64 desktop; DWORD err; };
struct wm_next_event_params { WEX_WM_EVENT event; DWORD err; };
struct wm_manage_params { UINT64 window; UINT64 parent; RECT client; RECT screen; DWORD err; };
struct wm_window_params { UINT64 window; RECT client; RECT screen; DWORD value; DWORD err; };
struct wm_get_title_params { UINT64 window; char title[1024]; DWORD err; };
struct wm_get_icon_params { UINT64 window; DWORD size; ULONG64 pixels; DWORD max; DWORD width; DWORD height; DWORD err; };
struct wm_stop_params { DWORD err; };
struct wm_start_ex_params { UINT64 desktop; DWORD backend; DWORD err; };
struct wm_capabilities_params { DWORD backend; DWORD capabilities; DWORD err; };
struct wm_native_info_params { UINT64 window; WEX_NATIVE_WINDOW info; DWORD err; };
struct wm_binding_params { UINT64 window; char token[33], controller[33]; DWORD err; };
/* DWM effects of a Wayland window by its shared handle (wex_window_manager_v1.set_effects, protocol v13) */
struct wm_effects_params { char handle[33]; DWORD radius, backdrop, flags, tint; INT region[4]; DWORD err; };
extern NTSTATUS wm_effects(void *args);
/* New neutral effects call. Identity is resolved on the PE side, XID first as
 * in the original bridge. No HWNDs or native pointers cross this transport.
 * Wayland region is already offset to window coordinates, still in Wine pixels. */
struct wm_apply_effects_params
{
    UINT64 window;
    DWORD backend, scale;
    char handle[36];
    WEX_WINDOW_EFFECT effect;
    DWORD err, reserved;
};
C_ASSERT( sizeof(WEX_WINDOW_EFFECT) == 52 );
C_ASSERT( FIELD_OFFSET(WEX_WINDOW_EFFECT, region) == 16 );
C_ASSERT( FIELD_OFFSET(WEX_WINDOW_EFFECT, materials) == 48 );
C_ASSERT( sizeof(struct wm_apply_effects_params) == 112 );
C_ASSERT( FIELD_OFFSET(struct wm_apply_effects_params, backend) == 8 );
C_ASSERT( FIELD_OFFSET(struct wm_apply_effects_params, handle) == 16 );
C_ASSERT( FIELD_OFFSET(struct wm_apply_effects_params, effect) == 52 );
C_ASSERT( FIELD_OFFSET(struct wm_apply_effects_params, err) == 104 );
extern NTSTATUS wm_apply_effects(void *args);
extern INT wm_effect_coord(INT value, DWORD scale, int rounding);
/* Legacy Wayland records remain at their original offsets. The appended X11
 * pointer slots are zero-extended PE addresses, borrowed only for the synchronous
 * call, exactly like composition_thumbs/peek. backend X11 selects those slots;
 * zero retains the legacy Wayland record/handle contract. */
struct wm_thumbnails_params
{
    DWORD count;
    BYTE records[80 * 104];
    DWORD err;
    UINT64 x11_thumbs;
    DWORD backend, reserved;
};
C_ASSERT( FIELD_OFFSET(struct wm_thumbnails_params, x11_thumbs) == 8328 );
C_ASSERT( FIELD_OFFSET(struct wm_thumbnails_params, records) == 4 );
C_ASSERT( FIELD_OFFSET(struct wm_thumbnails_params, err) == 8324 );
C_ASSERT( sizeof(struct compose_thumb) == 56 );
C_ASSERT( FIELD_OFFSET(struct wm_thumbnails_params, backend) == 8336 );
C_ASSERT( sizeof(struct wm_thumbnails_params) == 8344 );
extern NTSTATUS wm_thumbnails(void *args);
struct wm_peek_params
{
    char target[33];
    DWORD count;
    char keep[64][32];
    DWORD err, padding;
    UINT64 x11_target, x11_keep;
    DWORD backend, reserved;
};
C_ASSERT( FIELD_OFFSET(struct wm_peek_params, x11_target) == 2096 );
C_ASSERT( FIELD_OFFSET(struct wm_peek_params, count) == 36 );
C_ASSERT( FIELD_OFFSET(struct wm_peek_params, keep) == 40 );
C_ASSERT( FIELD_OFFSET(struct wm_peek_params, x11_keep) == 2104 );
C_ASSERT( FIELD_OFFSET(struct wm_peek_params, backend) == 2112 );
C_ASSERT( sizeof(struct wm_peek_params) == 2120 );
extern NTSTATUS wm_peek(void *args);
/* Explicit X11 compatibility routing for the legacy Composition exports; the
 * original handle-based wm_thumbnails/wm_peek calls still use the selected WM. */
extern NTSTATUS wm_backend_thumbnails(void *args);
extern NTSTATUS wm_backend_peek(void *args);
struct wm_clock_params { UINT64 page, event; DWORD err, reserved; };
struct wm_clock_wait_params { UINT64 generation; DWORD timeout_ms, err; };
C_ASSERT( FIELD_OFFSET(struct wm_clock_params, event) == 8 );
C_ASSERT( FIELD_OFFSET(struct wm_clock_params, err) == 16 );
C_ASSERT( sizeof(struct wm_clock_params) == 24 );
C_ASSERT( FIELD_OFFSET(struct wm_clock_wait_params, timeout_ms) == 8 );
C_ASSERT( FIELD_OFFSET(struct wm_clock_wait_params, err) == 12 );
C_ASSERT( sizeof(struct wm_clock_wait_params) == 16 );
extern NTSTATUS wm_clock(void *args);
extern NTSTATUS wm_clock_wait(void *args);
extern NTSTATUS wm_wait_frame(void *args);
/* the Wayland frame clock: the shared page is filled on every frame event of the compositor (DwmFlush) */
extern BOOL wayland_wm_set_clock(void *page);
extern BOOL wayland_wm_composition(void);
extern void compose_clock_wake(void);
extern NTSTATUS wm_binding(void *args);
extern NTSTATUS wm_native_info(void *args);
extern NTSTATUS wm_start_ex(void *args);
extern NTSTATUS wm_capabilities(void *args);
extern NTSTATUS wm_start(void *args);
extern NTSTATUS wm_next_event(void *args);
extern NTSTATUS wm_manage(void *args);
extern NTSTATUS wm_configure(void *args);
extern NTSTATUS wm_set_state(void *args);
extern NTSTATUS wm_close(void *args);
extern NTSTATUS wm_release(void *args);
extern NTSTATUS wm_get_title(void *args);
extern NTSTATUS wm_get_icon(void *args);
extern NTSTATUS wm_stop(void *args);
extern NTSTATUS wm_focus(void *args);

#ifdef WINE_UNIX_LIB
struct wm_backend_ops
{
    DWORD backend, capabilities;
    unixlib_entry_t start, next_event, manage, configure, set_state, close;
    unixlib_entry_t release, get_title, get_icon, stop, focus;
    unixlib_entry_t native_info;
    unixlib_entry_t binding, get_capabilities;
    unixlib_entry_t effects, thumbnails, peek;   /* NULL: not supported by the backend */
    unixlib_entry_t apply_effects;              /* neutral policy, distinct from legacy handle effects */
    unixlib_entry_t clock, wait_frame;
};
extern const struct wm_backend_ops x11_wm_backend;
extern const struct wm_backend_ops wayland_wm_backend;
extern BOOL wayland_wm_native_environment(char wayland[108], char xdisplay[128]);
#endif

struct composition_params { BOOL enabled; DWORD err; };
extern NTSTATUS composition_set(void *args);
extern NTSTATUS composition_get(void *args);
struct composition_shadow_params { UINT64 window; BOOL shadow; DWORD err; };
extern NTSTATUS composition_shadow(void *args);
struct composition_corner_params { UINT64 window; DWORD radius; DWORD err; };
extern NTSTATUS composition_corner(void *args);
struct composition_backdrop_params { UINT64 window; DWORD backdrop; DWORD tint; INT region[4]; DWORD err; };
extern NTSTATUS composition_backdrop(void *args);
struct composition_thumbs_params { UINT64 thumbs; DWORD count; DWORD err; };
extern NTSTATUS composition_thumbs(void *args);
struct composition_peek_params { UINT64 target; UINT64 keep; DWORD count; DWORD err; };
extern NTSTATUS composition_peek(void *args);
/* page is a struct dwm_clock_page (wine/dwm_protocol.h) mapped into this process; a NULL page turns the clock
 * off. event is pulsed once per composed frame. Both are kept on the Unix side and used under x_mutex. */
struct composition_clock_params { UINT64 page; UINT64 event; DWORD err; };
extern NTSTATUS composition_clock(void *args);
struct composition_clock_wait_params { UINT64 generation; DWORD timeout_ms; };
extern NTSTATUS composition_clock_wait(void *args);
/* x11wm calls these with x_mutex held; Display/XEvent stay on the Unix side. */
extern DWORD compose_set(void *display, unsigned long root, unsigned long desktop, int *x_error, BOOL enabled);
extern BOOL compose_enabled(void);
extern DWORD compose_shadow(unsigned long window, BOOL shadow);
extern DWORD compose_corner(unsigned long window, unsigned int radius);
extern DWORD compose_backdrop(unsigned long window, unsigned int backdrop, unsigned int tint, const INT *region);
extern DWORD compose_thumbnails(const struct compose_thumb *thumbs, unsigned int count);
extern DWORD compose_peek(unsigned long target, const UINT64 *keep, unsigned int count);
extern DWORD compose_clock(void *page, HANDLE event);
extern void compose_event(void *event);
extern void compose_paint(void);
extern long compose_event_mask(unsigned long window, long wm_mask);

struct tray_params { WEX_TRAY_ITEM item; DWORD err; };
struct tray_action_params { DWORD token, action; INT x, y, value; DWORD err; };
struct tray_menu_params { DWORD token; INT root; WEX_TRAY_MENU menu; DWORD err; };
extern NTSTATUS tray_start(void *args);
extern NTSTATUS tray_next(void *args);
extern NTSTATUS tray_stop(void *args);
extern NTSTATUS tray_action(void *args);
extern NTSTATUS tray_menu(void *args);

struct lease_take_params { UINT64 file; UINT64 lease; DWORD err; };
struct lease_monitor_params { UINT64 lease; DWORD broken; DWORD err; };
struct lease_arm_params { UINT64 lease; DWORD pid; DWORD err; };
struct lease_release_params { UINT64 lease; DWORD broken; DWORD err; };
extern NTSTATUS lease_take(void *args);
extern NTSTATUS lease_monitor(void *args);
extern NTSTATUS lease_stop(void *args);
extern NTSTATUS lease_arm(void *args);
extern NTSTATUS lease_release(void *args);
#ifdef WINE_UNIX_LIB
extern void tray_x11_next(WEX_TRAY_ITEM *item);
extern BOOL tray_x11_action(struct tray_action_params *params);
extern void tray_x11_stop(void);
#endif

struct oobe_components_params { ULONG64 buffer; DWORD size; DWORD needed; DWORD err; };
struct oobe_terms_params { char verb[32]; char sha256[65]; ULONG64 buffer; DWORD size; DWORD needed; DWORD err; };
struct oobe_install_params { char verb[32]; char sha256[65]; char err_buf[256]; DWORD err; };
struct oobe_receipts_params { ULONG64 buffer; DWORD size; DWORD needed; DWORD err; };

/* Review-only embedded terms. Bounded op-id (coordinator ids are at most
 * "review_" + 31-byte verb + "_" + 32 hex = 73 chars), typed numeric fields,
 * and a fixed fingerprint slot. QUERY/CANCEL are read-mostly: retrying them
 * can never resubmit a start or an install. */
struct oobe_review_info_params { char verb[32]; ULONG64 total; DWORD packages; BOOL cached; DWORD err; };
struct oobe_review_start_params { char verb[32]; char op_id[96]; char err_buf[256]; DWORD err; };
struct oobe_review_query_params { char op_id[96]; char status[16]; ULONG64 done; ULONG64 total; char fingerprint[65]; char err_buf[256]; DWORD err; };
struct oobe_review_cancel_params { char op_id[96]; char err_buf[256]; DWORD err; };

enum wexhost_funcs
{
    unix_init,
    unix_timedate_get,
    unix_timedate_set_time,
    unix_timedate_set_tz,
    unix_timedate_set_ntp,
    unix_timedate_list_tz,
    unix_agent_start,
    unix_agent_next_event,
    unix_agent_respond,
    unix_agent_stop,
    unix_power_can,
    unix_power_do,
    unix_battery_get,
    unix_hostname_get,
    unix_hostname_set,
    unix_user_get,
    unix_net_get_status,
    unix_net_scan_wifi,
    unix_net_connect_wifi,
    unix_net_disconnect,
    unix_net_enable,
    unix_display_get,
    unix_display_set,
    unix_wm_start,
    unix_wm_next_event,
    unix_wm_manage,
    unix_wm_configure,
    unix_wm_set_state,
    unix_wm_close,
    unix_wm_release,
    unix_wm_get_title,
    unix_wm_get_icon,
    unix_wm_stop,
    unix_wm_focus,
    unix_apps_list,
    unix_apps_launch,
    unix_hw_list,
    unix_users_list,
    unix_greeter_connect,
    unix_greeter_authenticate,
    unix_greeter_respond,
    unix_greeter_cancel,
    unix_greeter_start_session,
    unix_greeter_read,
    unix_composition_set,
    unix_composition_get,
    unix_composition_shadow,
    unix_composition_corner,
    unix_composition_clock,
    unix_composition_clock_wait,
    unix_composition_thumbs,
    unix_composition_peek,
    unix_locale_set,
    unix_root_pass_set,
    unix_user_create,
    unix_user_set_session,
    unix_wm_start_ex,
    unix_wm_capabilities,
    unix_wm_native_info,
    unix_wm_binding,
    unix_tray_start,
    unix_tray_next,
    unix_tray_stop,
    unix_tray_action,
    unix_tray_menu,
    unix_lease_take,
    unix_lease_monitor,
    unix_lease_stop,
    unix_lease_arm,
    unix_lease_release,
    unix_oobe_get_components,
    unix_oobe_get_terms,
    unix_oobe_install,
    unix_oobe_get_receipts,
    unix_oobe_review_info,
    unix_oobe_review_start,
    unix_oobe_review_query,
    unix_oobe_review_cancel,
    unix_composition_backdrop,
    unix_wm_effects,
    unix_wm_thumbnails,
    unix_wm_peek,
    unix_wm_apply_effects,
    unix_wm_backend_thumbnails,
    unix_wm_backend_peek,
    unix_wm_clock,
    unix_wm_clock_wait,
    unix_funcs_count
};

#define WEXHOST_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )

#endif
