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

/* power actions: login1 and the display manager */
#define WEX_POWER_SHUTDOWN   0
#define WEX_POWER_REBOOT     1
#define WEX_POWER_SUSPEND    2
#define WEX_POWER_HIBERNATE  3
#define WEX_POWER_LOGOFF     4   /* end this login session (login1 TerminateSession) */
#define WEX_POWER_SWITCHUSER 5   /* show the display manager's greeter, the session stays (LightDM or GDM) */
#define WEX_POWER_LOCK       6   /* lock the session: show the greeter, the session stays (LightDM) */

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
/* The accounts for the logon screen, one per line: "name\treal name\ticon file\tlogged on (0/1)". */
DWORD WINAPI WexUsersList( char *buffer, DWORD size, DWORD *needed );

/* The LightDM greeter protocol (logonui.exe).  WexGreeterRead returns one event as text; see dlls/wexhost/greeter.c. */
DWORD WINAPI WexGreeterConnect( void );
DWORD WINAPI WexGreeterAuthenticate( const char *user, DWORD *sequence );
DWORD WINAPI WexGreeterRespond( const char *secret );
DWORD WINAPI WexGreeterCancel( void );
DWORD WINAPI WexGreeterStartSession( const char *session );
DWORD WINAPI WexGreeterRead( char *buffer, DWORD size, DWORD timeout );

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

/* Linux windows: wexhost is the X11 window manager for every program that is not ours (native Linux programs,
 * Steam, Proton games).  The shell puts each of their windows into a Win32 window of its own (the "host"), so the
 * taskbar, Alt+Tab, the visual style and the z-order treat them like Windows programs.  See docs/LINUX_WINDOWS.md. */
#define WEX_WM_STOP       0
#define WEX_WM_MAP        1   /* a window wants to be shown: create its host and call WexWmManage */
#define WEX_WM_WITHDRAW   2   /* the program hid or destroyed it; it is released already: destroy the host */
#define WEX_WM_CONFIGURE  3   /* the program asks for another position or size (flags: WEX_WM_MOVE, WEX_WM_SIZE) */
#define WEX_WM_TITLE      4
#define WEX_WM_ICON       5
#define WEX_WM_STATE      6   /* states: WEX_WM_STATE_* to change, value: 0 remove, 1 add, 2 toggle */
#define WEX_WM_ACTIVATE   7   /* the program asks to be activated, or was clicked */
#define WEX_WM_MINIMIZE   8   /* the program asks to be minimized */
#define WEX_WM_KEY        9   /* a key of a shell combination (Windows key, Ctrl+Esc, Alt+Tab, Alt+F4) while a Linux
                               * window has the keyboard, to replay into Wine: value VK, flags WEX_WM_KEY_UP */
#define WEX_WM_HINTS     10   /* min_size or max_size changed */
#define WEX_WM_MOVERESIZE 11  /* the program's title bar or edge was dragged: value 0-7 edge (top-left clockwise to
                               * left, as _NET_WM_MOVERESIZE), 8 move; rect.left/top: pointer on the screen */
#define WEX_WM_DRAG      12   /* the pointer moved during it (rect.left/top), value 1 when the button is released */
#define WEX_WM_MENU      13   /* show the window menu at rect.left/top */
#define WEX_WM_OBSERVED  14   /* complete observed state, not a request */

#define WEX_WM_TYPE_NORMAL   0
#define WEX_WM_TYPE_DIALOG   1
#define WEX_WM_TYPE_UTILITY  2
#define WEX_WM_TYPE_SPLASH   3

#define WEX_WM_POSITION   0x01   /* MAP: the program chose the position (top-left of the frame, as X11 window managers do) */
#define WEX_WM_NO_BORDER  0x02   /* MAP: asked for no frame (Motif hints) */
#define WEX_WM_EXISTING   0x04   /* MAP: already on the screen when the window manager started */
#define WEX_WM_MOVE       0x08   /* CONFIGURE */
#define WEX_WM_SIZE       0x10   /* CONFIGURE */
#define WEX_WM_KEY_UP     0x20   /* KEY */

#define WEX_WM_STATE_FULLSCREEN  0x01
#define WEX_WM_STATE_MAXIMIZED   0x02
#define WEX_WM_STATE_MINIMIZED   0x04
#define WEX_WM_STATE_ABOVE       0x08
#define WEX_WM_STATE_FOCUSED     0x10

#define WEX_WM_BACKEND_AUTO     0
#define WEX_WM_BACKEND_X11      1
#define WEX_WM_BACKEND_WAYLAND  2
#define WEX_WM_CAP_X11_PARENT   0x01
#define WEX_WM_CAP_FOREIGN_FRAME 0x02
#define WEX_WM_CAP_NATIVE_ENV   0x04
#define WEX_WM_CAP_OBSERVE      0x08

typedef struct
{
    char token[33];             /* native view lifetime token */
    char app_id[256];           /* UTF-8 desktop application identifier */
    char wayland_display[108];  /* compositor's normal socket */
    char xwayland_display[128]; /* empty when unavailable */
} WEX_NATIVE_WINDOW;
DWORD WINAPI WexWmGetNativeInfo( UINT64 window, WEX_NATIVE_WINDOW *info );

typedef struct
{
    DWORD  type;          /* WEX_WM_* */
    UINT64 window;        /* backend-local native window identifier */
    UINT64 owner;         /* MAP: the window it belongs to (WM_TRANSIENT_FOR), 0 if none */
    RECT   rect;          /* MAP, CONFIGURE: client area in screen coordinates */
    DWORD  window_type;   /* MAP: WEX_WM_TYPE_* */
    DWORD  flags;         /* WEX_WM_POSITION ... WEX_WM_SIZE */
    DWORD  states;        /* MAP: initial states; STATE: the states to change */
    DWORD  value;
    SIZE   min_size;      /* MAP, HINTS: client size limits, 0 when none */
    SIZE   max_size;
} WEX_WM_EVENT;

/* Called on a wexhost thread for every event, like WEX_AGENT_CALLBACK. */
typedef void (CALLBACK *WEX_WM_CALLBACK)( const WEX_WM_EVENT *event, void *context );

/* desktop_window: the X window of the shell's virtual desktop, which is not managed.  ERROR_ALREADY_EXISTS: another
 * window manager runs on the display. */
DWORD WINAPI WexWmStart( UINT64 desktop_window, WEX_WM_CALLBACK callback, void *context );
/* AUTO selects X11 for an X desktop. Native Wayland adoption still requires
 * explicit selection until complete acceptance. Missing support returns ERROR_NOT_SUPPORTED. */
DWORD WINAPI WexWmStartEx( DWORD backend, UINT64 desktop_window, WEX_WM_CALLBACK callback, void *context );
DWORD WINAPI WexWmGetCapabilities( DWORD *backend, DWORD *capabilities );
/* Backend-neutral host binding; platform window identifiers stay in wexhost.
 * Explicit Wayland backend: bind before ShowWindow (especially owned HWNDs).
 * ERROR_IO_PENDING means the request is installed; repeat on the owning UI
 * thread until success after the driver's group acknowledgement. Binding,
 * configure, release and stop belong to that UI thread. X11 behavior is unchanged.
 */
DWORD WINAPI WexWmBindHost( UINT64 window, HWND host, const RECT *client, const RECT *screen );
/* Put window into the host's X window (parent) at client (the host's client area, relative to parent) and show it;
 * screen is the same area on the screen.  parent 0 shows it unmanaged instead.  ERROR_INVALID_WINDOW_HANDLE: one of
 * the windows does not exist (yet). */
DWORD WINAPI WexWmManage( UINT64 window, UINT64 parent, const RECT *client, const RECT *screen );
DWORD WINAPI WexWmConfigure( UINT64 window, const RECT *client, const RECT *screen );
DWORD WINAPI WexWmSetState( UINT64 window, DWORD states );
DWORD WINAPI WexWmClose( UINT64 window );
/* Give the window back to the root window (its host goes away while the program keeps it). */
DWORD WINAPI WexWmRelease( UINT64 window, const RECT *screen );
DWORD WINAPI WexWmGetTitle( UINT64 window, WCHAR *title, DWORD count );
/* ARGB pixels, top-down, of the icon closest to size; ERROR_MORE_DATA when max pixels are not enough. */
DWORD WINAPI WexWmGetIcon( UINT64 window, DWORD size, DWORD *pixels, DWORD max, DWORD *width, DWORD *height );
/* Release every window to the root window and stop managing the display. */
void  WINAPI WexWmStop( void );
/* Synchronize X keyboard focus after switching a Win32 foreground window. */
DWORD WINAPI WexWmFocus( UINT64 window );

/* Optional X11 composition, off by default. Requires WexWmStart's connection.
 * Set returns ERROR_NOT_SUPPORTED for missing extensions, ERROR_ALREADY_EXISTS
 * when another compositor owns the selection or manual redirection. */
DWORD WINAPI WexCompositionSetEnabled( BOOL enabled );
DWORD WINAPI WexCompositionGetEnabled( BOOL *enabled );
DWORD WINAPI WexCompositionSetShadow( UINT64 xwindow, BOOL shadow );

/* Setup (setup.exe, oobe.exe): locale1 locale and keyboard layout; root's password; a new account through
 * AccountsService (account_type 1: administrator), with its password when one is given; session and automatic logon. */
DWORD WINAPI WexLocaleSet( const char *locale, const char *layout );
DWORD WINAPI WexRootPasswordSet( const char *password );
DWORD WINAPI WexUserCreate( const char *name, const char *real_name, int account_type, const char *password );
DWORD WINAPI WexUserSetSession( const char *name, const char *session, BOOL autologin );

/* Visible freedesktop applications. UTF-8 rows: desktop ID, name, description,
 * categories, PNG path, separated by tabs and terminated by newline; final NUL.
 * ERROR_MORE_DATA returns the required byte count, including the final NUL. */
#define WEX_APP_ID_MAX 512
DWORD WINAPI WexAppsList( char *buffer, DWORD size, DWORD *needed );
/* Launch by desktop ID through GIO, without the shell's Wine environment. */
DWORD WINAPI WexAppsLaunch( const WCHAR *id );

/* The computer's hardware for Device Manager: a UTF-8 report of the devices (sysfs, the udev database) and their
 * kernel modules (libkmod), one item per line; the format is described in dlls/wexhost/hardware.c.
 * ERROR_MORE_DATA returns the required byte count, including the final NUL. */
DWORD WINAPI WexHardwareList( char *buffer, DWORD size, DWORD *needed );

/* Native notification area. Records contain UTF-8 strings and straight-alpha
 * ARGB pixels, with no process-local pointers in the Unix/WoW64 boundary. */
#define WEX_TRAY_PIXELS 4096
#define WEX_TRAY_MENU_MAX 128
#define WEX_TRAY_CHANGED 1
#define WEX_TRAY_REMOVED 2
#define WEX_TRAY_ACTIVATE 1
#define WEX_TRAY_SECONDARY 2
#define WEX_TRAY_CONTEXT 3
#define WEX_TRAY_SCROLL 4
#define WEX_TRAY_CLICK_MENU 5
#define WEX_TRAY_SCROLL_HORIZONTAL 6
#define WEX_TRAY_PASSIVE 1
#define WEX_TRAY_ATTENTION 2
#define WEX_TRAY_ITEM_MENU 4
typedef struct
{
    DWORD event, token, flags, width, height;
    char id[256], title[256], tooltip[512];
    DWORD pixels[WEX_TRAY_PIXELS];
} WEX_TRAY_ITEM;
typedef struct
{
    INT id, parent;
    DWORD enabled, visible, separator, toggle, checked;
    char label[256];
} WEX_TRAY_MENU_ENTRY;
typedef struct
{
    DWORD count;
    WEX_TRAY_MENU_ENTRY entries[WEX_TRAY_MENU_MAX];
} WEX_TRAY_MENU;
typedef void (CALLBACK *WEX_TRAY_CALLBACK)(const WEX_TRAY_ITEM *, void *);
DWORD WINAPI WexTrayStart(WEX_TRAY_CALLBACK callback, void *context);
DWORD WINAPI WexTrayStop(void);
/* x/y use native screen coordinates (compositor logical pixels on Wayland).
 * CLICK_MENU instead carries the event timestamp in x and the menu id in value. */
DWORD WINAPI WexTrayAction(DWORD token, DWORD action, INT x, INT y, INT value);
DWORD WINAPI WexTrayGetMenu(DWORD token, INT root, WEX_TRAY_MENU *menu);

/* OOBE Optional Components Coordinator */
DWORD WINAPI WexOobeGetComponents( char *buffer, DWORD size, DWORD *needed );
DWORD WINAPI WexOobeGetTerms( const char *verb, char *sha256, DWORD sha256_size, char *buffer, DWORD size, DWORD *needed );
DWORD WINAPI WexOobeInstall( const char *verb, const char *sha256, char *err_buf, DWORD err_size );
DWORD WINAPI WexOobeGetReceipts( char *buffer, DWORD size, DWORD *needed );

/* Review-only embedded terms (CoreFonts). These never install anything:
 * Info reports payload size/cache state, Start begins a bounded cancellable
 * download, Query is read-mostly and safe to retry, Cancel stops it. */
DWORD WINAPI WexOobeReviewInfo( const char *verb, ULONG64 *total, DWORD *packages, BOOL *cached );
DWORD WINAPI WexOobeReviewStart( const char *verb, char *op_id, DWORD op_id_size, char *err_buf, DWORD err_size );
DWORD WINAPI WexOobeReviewQuery( const char *op_id, char *status, DWORD status_size, ULONG64 *done,
                                 ULONG64 *total, char *fingerprint, DWORD fingerprint_size,
                                 char *err_buf, DWORD err_size );
DWORD WINAPI WexOobeReviewCancel( const char *op_id, char *err_buf, DWORD err_size );

#ifdef __cplusplus
}
#endif

#endif
