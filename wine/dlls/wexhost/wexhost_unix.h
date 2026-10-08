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
    unix_funcs_count
};

#define WEXHOST_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )

#endif
