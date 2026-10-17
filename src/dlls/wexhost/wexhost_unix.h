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
/* x11wm calls these with x_mutex held; Display/XEvent stay on the Unix side. */
extern DWORD compose_set(void *display, unsigned long root, unsigned long desktop, int *x_error, BOOL enabled);
extern BOOL compose_enabled(void);
extern DWORD compose_shadow(unsigned long window, BOOL shadow);
extern void compose_event(void *event);
extern void compose_paint(void);
extern long compose_event_mask(unsigned long window, long wm_mask);

struct tray_params { WEX_TRAY_ITEM item; DWORD err; };
struct tray_action_params { DWORD token, action; INT x, y, value; DWORD err; };
struct tray_menu_params { DWORD token; WEX_TRAY_MENU menu; DWORD err; };
extern NTSTATUS tray_start(void *args);
extern NTSTATUS tray_next(void *args);
extern NTSTATUS tray_stop(void *args);
extern NTSTATUS tray_action(void *args);
extern NTSTATUS tray_menu(void *args);
#ifdef WINE_UNIX_LIB
extern void tray_x11_next(WEX_TRAY_ITEM *item);
extern BOOL tray_x11_action(struct tray_action_params *params);
extern void tray_x11_stop(void);
#endif

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
    unix_funcs_count
};

#define WEXHOST_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )

#endif
