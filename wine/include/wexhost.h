/*
 * WineExplorer host services: typed access to Linux system services over D-Bus
 * (systemd timedate1, polkit authentication agent, ...).  See docs/DBUS_PLAN.md.
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

#ifndef __WINE_WEXHOST_H
#define __WINE_WEXHOST_H

#include <windef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEX_TZ_NAME_MAX   64
#define WEX_COOKIE_MAX    128
#define WEX_USER_MAX      64
#define WEX_PASSWORD_MAX  256
#define WEX_TEXT_MAX      256
#define WEX_MAX_IDENTITIES 8

/* Return values are Win32 error codes: ERROR_SUCCESS, ERROR_ACCESS_DENIED (polkit refused or the prompt was
 * dismissed), ERROR_CANCELLED, ERROR_NOT_SUPPORTED (service or D-Bus missing), ERROR_GEN_FAILURE. */

typedef struct
{
    char timezone[WEX_TZ_NAME_MAX];   /* IANA name, e.g. "America/Chicago" */
    BOOL ntp_enabled;
    BOOL can_ntp;
    BOOL ntp_synchronized;
    BOOL local_rtc;
} WEX_TIMEDATE;

DWORD WINAPI WexTimedateGet( WEX_TIMEDATE *info );
/* The Set calls may wait on polkit while the user types a password; the owner window is disabled and its
 * messages are pumped meanwhile. */
DWORD WINAPI WexTimedateSetTime( HWND owner, INT64 unix_usec_utc, BOOL relative );
DWORD WINAPI WexTimedateSetTimezone( HWND owner, const char *iana_name );
DWORD WINAPI WexTimedateSetNtp( HWND owner, BOOL enable );
/* Newline separated list of IANA names; *needed gets the required size (including NUL). */
DWORD WINAPI WexTimedateListTimezones( char *buffer, DWORD size, DWORD *needed );

/* power actions: login1 */
#define WEX_POWER_SHUTDOWN   0
#define WEX_POWER_REBOOT     1
#define WEX_POWER_SUSPEND    2
#define WEX_POWER_HIBERNATE  3

#define WEX_POWER_NO         0
#define WEX_POWER_YES        1
#define WEX_POWER_CHALLENGE  2   /* allowed after authentication */

DWORD WINAPI WexPowerCan( DWORD action, DWORD *result );
DWORD WINAPI WexPowerDo( HWND owner, DWORD action );

/* battery and AC: UPower (the aggregated "display device") */
typedef struct
{
    BOOL  has_battery;
    BOOL  on_battery;          /* running on battery (AC unplugged) */
    DWORD percent;
    DWORD state;               /* UPower: 1 charging, 2 discharging, 3 empty, 4 fully charged */
    int   seconds_to_empty;    /* -1 unknown */
    int   seconds_to_full;
} WEX_BATTERY;

DWORD WINAPI WexBatteryGet( WEX_BATTERY *info );

/* computer name: hostname1 */
typedef struct { char static_name[64]; char pretty_name[128]; char transient_name[64]; } WEX_HOSTNAME;

DWORD WINAPI WexHostnameGet( WEX_HOSTNAME *info );
DWORD WINAPI WexHostnameSet( HWND owner, const char *name, BOOL pretty );

/* network: NetworkManager */
#define WEX_NET_ETHERNET 1
#define WEX_NET_WIFI     2

typedef struct
{
    BOOL  present;            /* an Ethernet or Wi-Fi device exists */
    DWORD type;               /* WEX_NET_* */
    DWORD state;              /* NetworkManager device state, 100 = activated */
    BOOL  connected;
    char  ifname[32];
    char  driver[32];
    char  ssid[64];           /* Wi-Fi: network joined */
    DWORD signal;             /* Wi-Fi: 0..100 */
    DWORD speed_mbps;         /* link speed, 0 if unknown */
} WEX_NET_STATUS;

typedef struct
{
    char  ssid[64];
    DWORD strength;           /* 0..100 */
    BOOL  secured;
    BOOL  connected;
    BOOL  sae_only;           /* WPA3-only network */
    char  security[24];       /* "WPA2-PSK", "WPA3", ... empty when open */
} WEX_WIFI_AP;

DWORD WINAPI WexNetGetStatus( WEX_NET_STATUS *status );
DWORD WINAPI WexNetScanWifi( WEX_WIFI_AP *aps, DWORD max, DWORD *count );
DWORD WINAPI WexNetConnectWifi( const char *ssid, const char *passphrase, BOOL sae );
DWORD WINAPI WexNetDisconnect( void );
DWORD WINAPI WexNetEnable( void );

/* the logged-in user: AccountsService */
typedef struct { char name[64]; char real_name[128]; char icon_file[260]; } WEX_USER;

DWORD WINAPI WexUserGet( WEX_USER *info );

/* polkit authentication agent */
#define WEX_EVENT_STOP    0
#define WEX_EVENT_BEGIN   1
#define WEX_EVENT_CANCEL  2

typedef struct
{
    DWORD type;                          /* WEX_EVENT_* */
    char  cookie[WEX_COOKIE_MAX];
    char  action_id[WEX_COOKIE_MAX];
    char  message[WEX_TEXT_MAX];
    DWORD identity_count;
    char  identities[WEX_MAX_IDENTITIES][WEX_USER_MAX];   /* user names allowed to authenticate */
} WEX_AGENT_EVENT;

/* Called on a wexhost thread for every event; copy what you need and hand work to your UI thread. */
typedef void (CALLBACK *WEX_AGENT_CALLBACK)( const WEX_AGENT_EVENT *event, void *context );

DWORD WINAPI WexAgentStart( WEX_AGENT_CALLBACK callback, void *context );
/* Answer a WEX_EVENT_BEGIN: authenticate as user with password, or cancel.  Blocks for PAM (about a second). */
DWORD WINAPI WexAgentRespond( const char *cookie, const char *user, const char *password, BOOL cancel );
void  WINAPI WexAgentStop( void );

#define WEX_DISPLAY_MAX_MODES 128
typedef struct { DWORD width, height, refresh; } WEX_DISPLAY_MODE; /* refresh in millihertz */
typedef struct {
    char output[64];
    WEX_DISPLAY_MODE current;
    DWORD count;
    WEX_DISPLAY_MODE modes[WEX_DISPLAY_MAX_MODES];
} WEX_DISPLAY;
DWORD WINAPI WexDisplayGet(WEX_DISPLAY *display);
DWORD WINAPI WexDisplaySet(const WEX_DISPLAY_MODE *mode, BOOL persist);

#ifdef __cplusplus
}
#endif

#endif
