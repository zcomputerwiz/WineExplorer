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

#ifdef __cplusplus
}
#endif

#endif
