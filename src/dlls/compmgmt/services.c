/*
 * Computer Management - the Services node
 *
 * A list of the services of this computer from the service manager (Name, Description, Status, Startup Type, Log On As),
 * the commands Start, Stop, Pause, Resume and Restart, and the Properties sheet (General, Log On, Recovery,
 * Dependencies). What the service manager gives each caller is measured in docs/SERVICES_SNAPIN.md: everyone may list
 * and read; changing, starting and stopping needs an administrator's rights, so a refused change offers to open the
 * console again as an administrator.
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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winsvc.h>
#include <winerror.h>
#include <wingdi.h>
#include <commctrl.h>

#include "compmgmt.h"
#include "resource.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(compmgmt);

#ifndef SC_GROUP_IDENTIFIER
#define SC_GROUP_IDENTIFIER L'+'   /* a dependency that starts with this names a load order group */
#endif

struct service
{
    WCHAR *name, *display;
};

enum { COL_NAME, COL_DESCRIPTION, COL_STATUS, COL_STARTUP, COL_LOGON };

#define WAIT_MS 30000

static WCHAR *copy_string( const WCHAR *s )
{
    WCHAR *r = malloc( (lstrlenW( s ) + 1) * sizeof(WCHAR) );
    if (r) lstrcpyW( r, s );
    return r;
}

static const WCHAR *startup_text( DWORD type )
{
    switch (type)
    {
    case SERVICE_AUTO_START: return cm_string( IDS_STARTUP_AUTO );
    case SERVICE_DEMAND_START: return cm_string( IDS_STARTUP_MANUAL );
    case SERVICE_DISABLED: return cm_string( IDS_STARTUP_DISABLED );
    case SERVICE_BOOT_START: return cm_string( IDS_STARTUP_BOOT );
    default: return cm_string( IDS_STARTUP_SYSTEM );
    }
}

/* the configuration of an open service in a buffer the caller frees */
static QUERY_SERVICE_CONFIGW *query_config( SC_HANDLE svc )
{
    DWORD size = 0;
    QUERY_SERVICE_CONFIGW *cfg;

    QueryServiceConfigW( svc, NULL, 0, &size );
    if (!size || !(cfg = malloc( size ))) return NULL;
    if (!QueryServiceConfigW( svc, cfg, size, &size ))
    {
        free( cfg );
        return NULL;
    }
    return cfg;
}

static WCHAR *query_description( SC_HANDLE svc )
{
    DWORD size = 0;
    SERVICE_DESCRIPTIONW *desc;
    WCHAR *text = NULL;

    QueryServiceConfig2W( svc, SERVICE_CONFIG_DESCRIPTION, NULL, 0, &size );
    if (!size || !(desc = malloc( size ))) return NULL;
    if (QueryServiceConfig2W( svc, SERVICE_CONFIG_DESCRIPTION, (BYTE *)desc, size, &size ) && desc->lpDescription)
        text = copy_string( desc->lpDescription );
    free( desc );
    return text;
}

static BOOL query_status( SC_HANDLE svc, SERVICE_STATUS_PROCESS *st )
{
    DWORD needed;
    return QueryServiceStatusEx( svc, SC_STATUS_PROCESS_INFO, (BYTE *)st, sizeof(*st), &needed );
}

static int CALLBACK compare_services( LPARAM a, LPARAM b, LPARAM unused )
{
    return lstrcmpiW( ((struct service *)a)->display, ((struct service *)b)->display );
}

/* ----------------------------------------------------------------------------------------------- the list */

void svc_free( HWND list )
{
    int i, count = (int)SendMessageW( list, LVM_GETITEMCOUNT, 0, 0 );
    LVITEMW item = {0};

    for (i = 0; i < count; i++)
    {
        item.mask = LVIF_PARAM;
        item.iItem = i;
        if (SendMessageW( list, LVM_GETITEMW, 0, (LPARAM)&item ) && item.lParam)
        {
            struct service *s = (struct service *)item.lParam;
            free( s->name );
            free( s->display );
            free( s );
        }
    }
    SendMessageW( list, LVM_DELETEALLITEMS, 0, 0 );
}

static void add_service( HWND list, SC_HANDLE scm, const ENUM_SERVICE_STATUS_PROCESSW *e )
{
    LVITEMW item = {0};
    struct service *s = calloc( 1, sizeof(*s) );
    SC_HANDLE svc;
    WCHAR *desc = NULL;
    const WCHAR *status = L"", *startup = L"", *logon = L"";
    QUERY_SERVICE_CONFIGW *cfg = NULL;
    int index;

    if (!s) return;
    s->name = copy_string( e->lpServiceName );
    s->display = copy_string( e->lpDisplayName && e->lpDisplayName[0] ? e->lpDisplayName : e->lpServiceName );
    if (e->ServiceStatusProcess.dwCurrentState == SERVICE_RUNNING) status = cm_string( IDS_STATUS_STARTED );
    else if (e->ServiceStatusProcess.dwCurrentState == SERVICE_PAUSED) status = cm_string( IDS_STATUS_PAUSED );
    if ((svc = OpenServiceW( scm, e->lpServiceName, SERVICE_QUERY_CONFIG )))
    {
        if ((cfg = query_config( svc )))
        {
            startup = startup_text( cfg->dwStartType );
            logon = cfg->lpServiceStartName ? cfg->lpServiceStartName : L"";
        }
        desc = query_description( svc );
        CloseServiceHandle( svc );
    }
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = 0x7fffffff;
    item.pszText = s->display;
    item.lParam = (LPARAM)s;
    index = (int)SendMessageW( list, LVM_INSERTITEMW, 0, (LPARAM)&item );
    if (index >= 0)
    {
        const WCHAR *cols[] = { NULL, desc ? desc : L"", status, startup, logon };
        int i;

        for (i = 1; i < 5; i++)
        {
            LVITEMW sub = {0};
            sub.iSubItem = i;
            sub.pszText = (WCHAR *)cols[i];
            SendMessageW( list, LVM_SETITEMTEXTW, index, (LPARAM)&sub );
        }
    }
    free( cfg );
    free( desc );
}

void svc_refresh( HWND list )
{
    SC_HANDLE scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE );
    HCURSOR cursor = SetCursor( LoadCursorW( NULL, (LPCWSTR)IDC_WAIT ) );
    BYTE *buffer = NULL;
    DWORD size = 16384, needed, count, resume = 0, i;
    BOOL ok;

    svc_free( list );
    if (!scm) goto done;
    SendMessageW( list, WM_SETREDRAW, FALSE, 0 );
    for (;;)
    {
        free( buffer );
        if (!(buffer = malloc( size ))) break;
        ok = EnumServicesStatusExW( scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buffer, size, &needed,
                                    &count, &resume, NULL );
        if (!ok && GetLastError() != ERROR_MORE_DATA) break;
        for (i = 0; i < count; i++) add_service( list, scm, (ENUM_SERVICE_STATUS_PROCESSW *)buffer + i );
        if (ok) break;
        if (needed > size) size = needed + 1024;
    }
    free( buffer );
    SendMessageW( list, LVM_SORTITEMS, 0, (LPARAM)compare_services );
    SendMessageW( list, WM_SETREDRAW, TRUE, 0 );
    CloseServiceHandle( scm );
done:
    SetCursor( cursor );
}

HWND svc_create_view( HWND parent )
{
    static const struct { UINT title; int width; } columns[] =
    {
        { IDS_COL_NAME, 170 }, { IDS_COL_DESCRIPTION, 230 }, { IDS_COL_STATUS, 60 }, { IDS_COL_STARTUP, 80 },
        { IDS_COL_LOGON, 110 },
    };
    HWND list = CreateWindowExW( WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL, WS_CHILD | WS_TABSTOP | LVS_REPORT |
                                 LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, 0, 0, parent, NULL, compmgmt_module, NULL );
    LVCOLUMNW col = {0};
    int i;

    SendMessageW( list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT );
    for (i = 0; i < (int)ARRAY_SIZE(columns); i++)
    {
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.cx = columns[i].width;
        col.iSubItem = i;
        col.pszText = (WCHAR *)cm_string( columns[i].title );
        SendMessageW( list, LVM_INSERTCOLUMNW, i, (LPARAM)&col );
    }
    return list;
}

static struct service *selected_service( HWND list )
{
    int index = (int)SendMessageW( list, LVM_GETNEXTITEM, -1, LVNI_SELECTED );
    LVITEMW item = {0};

    if (index < 0) return NULL;
    item.mask = LVIF_PARAM;
    item.iItem = index;
    return SendMessageW( list, LVM_GETITEMW, 0, (LPARAM)&item ) ? (struct service *)item.lParam : NULL;
}

/* ------------------------------------------------------------------------------------------ the commands */

DWORD svc_commands( HWND list )
{
    struct service *s = selected_service( list );
    SC_HANDLE scm, svc;
    SERVICE_STATUS_PROCESS st;
    QUERY_SERVICE_CONFIGW *cfg;
    DWORD flags = 0;

    if (!s) return 0;
    flags = SVC_SELECTED;
    if (!(scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ))) return flags;
    if ((svc = OpenServiceW( scm, s->name, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS )))
    {
        if (query_status( svc, &st ))
        {
            cfg = query_config( svc );
            if (st.dwCurrentState == SERVICE_STOPPED && (!cfg || cfg->dwStartType != SERVICE_DISABLED)) flags |= SVC_CAN_START;
            if (st.dwCurrentState == SERVICE_RUNNING && (st.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE))
                flags |= SVC_CAN_PAUSE;
            if (st.dwCurrentState == SERVICE_PAUSED) flags |= SVC_CAN_RESUME;
            if ((st.dwCurrentState == SERVICE_RUNNING || st.dwCurrentState == SERVICE_PAUSED) &&
                (st.dwControlsAccepted & SERVICE_ACCEPT_STOP))
                flags |= SVC_CAN_STOP | SVC_CAN_RESTART;
            free( cfg );
        }
        CloseServiceHandle( svc );
    }
    CloseServiceHandle( scm );
    return flags;
}

static void report_error( HWND owner, const WCHAR *display, DWORD err )
{
    WCHAR system[400] = L"", text[900];

    FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, system, ARRAY_SIZE(system), NULL );
    swprintf( text, ARRAY_SIZE(text), cm_string( IDS_SVC_ERROR ), display, system );
    MessageBoxW( owner, text, cm_string( IDS_APPNAME ), MB_OK | MB_ICONERROR );
}

/* waits for the service to reach a state; FALSE when it did not in time */
static BOOL wait_for( SC_HANDLE svc, DWORD state )
{
    SERVICE_STATUS_PROCESS st;
    DWORD waited;

    for (waited = 0; waited < WAIT_MS; waited += 250)
    {
        if (query_status( svc, &st ) && st.dwCurrentState == state) return TRUE;
        Sleep( 250 );
    }
    return FALSE;
}

/* one control on one service by name; owner is where messages go. The error is 0 when it worked. */
static DWORD control_service( HWND owner, const WCHAR *name, const WCHAR *display, UINT command )
{
    SC_HANDLE scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ), svc;
    SERVICE_STATUS status;
    DWORD err = 0, access = SERVICE_QUERY_STATUS;
    HCURSOR cursor;

    if (!scm) return GetLastError();
    switch (command)
    {
    case IDM_SVC_START: access |= SERVICE_START; break;
    case IDM_SVC_RESTART: access |= SERVICE_START | SERVICE_STOP; break;
    case IDM_SVC_STOP: access |= SERVICE_STOP; break;
    default: access |= SERVICE_PAUSE_CONTINUE; break;
    }
    if (!(svc = OpenServiceW( scm, name, access )))
    {
        err = GetLastError();
        CloseServiceHandle( scm );
        return err;
    }
    cursor = SetCursor( LoadCursorW( NULL, (LPCWSTR)IDC_WAIT ) );
    switch (command)
    {
    case IDM_SVC_START:
        if (!StartServiceW( svc, 0, NULL )) err = GetLastError();
        else if (!wait_for( svc, SERVICE_RUNNING )) err = ERROR_SERVICE_REQUEST_TIMEOUT;
        break;
    case IDM_SVC_STOP:
        if (!ControlService( svc, SERVICE_CONTROL_STOP, &status )) err = GetLastError();
        else if (!wait_for( svc, SERVICE_STOPPED )) err = ERROR_SERVICE_REQUEST_TIMEOUT;
        break;
    case IDM_SVC_PAUSE:
        if (!ControlService( svc, SERVICE_CONTROL_PAUSE, &status )) err = GetLastError();
        else if (!wait_for( svc, SERVICE_PAUSED )) err = ERROR_SERVICE_REQUEST_TIMEOUT;
        break;
    case IDM_SVC_RESUME:
        if (!ControlService( svc, SERVICE_CONTROL_CONTINUE, &status )) err = GetLastError();
        else if (!wait_for( svc, SERVICE_RUNNING )) err = ERROR_SERVICE_REQUEST_TIMEOUT;
        break;
    case IDM_SVC_RESTART:
        if (!ControlService( svc, SERVICE_CONTROL_STOP, &status )) err = GetLastError();
        else if (!wait_for( svc, SERVICE_STOPPED )) err = ERROR_SERVICE_REQUEST_TIMEOUT;
        else if (!StartServiceW( svc, 0, NULL )) err = GetLastError();
        else if (!wait_for( svc, SERVICE_RUNNING )) err = ERROR_SERVICE_REQUEST_TIMEOUT;
        break;
    }
    SetCursor( cursor );
    CloseServiceHandle( svc );
    CloseServiceHandle( scm );
    return err;
}

/* the common end of every command: nothing for success, the elevation offer for a refusal, a message otherwise */
static BOOL finish_command( HWND owner, const WCHAR *display, DWORD err )
{
    WCHAR text[300];

    if (!err) return TRUE;
    if (err == ERROR_ACCESS_DENIED) cm_offer_elevation( owner );
    else if (err == ERROR_SERVICE_REQUEST_TIMEOUT)
    {
        swprintf( text, ARRAY_SIZE(text), cm_string( IDS_SVC_TIMEOUT ), display );
        MessageBoxW( owner, text, cm_string( IDS_APPNAME ), MB_OK | MB_ICONWARNING );
    }
    else report_error( owner, display, err );
    return FALSE;
}

/* ------------------------------------------------------------------------------------------- properties */

struct props
{
    WCHAR *name, *display;
    HWND sheet;
    DWORD startup;
};

static void set_item_text( HWND dlg, int id, const WCHAR *text )
{
    SetDlgItemTextW( dlg, id, text ? text : L"" );
}

static void fill_status( HWND dlg, struct props *p )
{
    SC_HANDLE scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ), svc;
    SERVICE_STATUS_PROCESS st;
    DWORD state = SERVICE_STOPPED, accepted = 0;
    QUERY_SERVICE_CONFIGW *cfg = NULL;
    const WCHAR *text = L"";

    if (scm && (svc = OpenServiceW( scm, p->name, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS )))
    {
        if (query_status( svc, &st )) { state = st.dwCurrentState; accepted = st.dwControlsAccepted; }
        cfg = query_config( svc );
        CloseServiceHandle( svc );
    }
    if (scm) CloseServiceHandle( scm );
    switch (state)
    {
    case SERVICE_RUNNING: text = cm_string( IDS_STATUS_STARTED ); break;
    case SERVICE_PAUSED: text = cm_string( IDS_STATUS_PAUSED ); break;
    default: text = L""; break;
    }
    set_item_text( dlg, IDC_SVC_STATUS, text );
    EnableWindow( GetDlgItem( dlg, IDC_SVC_START ), state == SERVICE_STOPPED && SendDlgItemMessageW( dlg, IDC_SVC_STARTUP, CB_GETCURSEL, 0, 0 ) != 2 );
    EnableWindow( GetDlgItem( dlg, IDC_SVC_STOP ), (state == SERVICE_RUNNING || state == SERVICE_PAUSED) && (accepted & SERVICE_ACCEPT_STOP) );
    EnableWindow( GetDlgItem( dlg, IDC_SVC_PAUSE ), state == SERVICE_RUNNING && (accepted & SERVICE_ACCEPT_PAUSE_CONTINUE) );
    EnableWindow( GetDlgItem( dlg, IDC_SVC_RESUME ), state == SERVICE_PAUSED );
    free( cfg );
}

static int startup_to_index( DWORD type )
{
    return type == SERVICE_AUTO_START ? 0 : type == SERVICE_DISABLED ? 2 : 1;
}

static DWORD index_to_startup( int index )
{
    return index == 0 ? SERVICE_AUTO_START : index == 2 ? SERVICE_DISABLED : SERVICE_DEMAND_START;
}

static INT_PTR CALLBACK general_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct props *p;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        SC_HANDLE scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ), svc;
        QUERY_SERVICE_CONFIGW *cfg = NULL;
        WCHAR *desc = NULL;

        p = (struct props *)((PROPSHEETPAGEW *)lparam)->lParam;
        SetWindowLongPtrW( dlg, DWLP_USER, (LONG_PTR)p );
        set_item_text( dlg, IDC_SVC_NAME, p->name );
        set_item_text( dlg, IDC_SVC_DISPLAY, p->display );
        SendDlgItemMessageW( dlg, IDC_SVC_STARTUP, CB_ADDSTRING, 0, (LPARAM)cm_string( IDS_STARTUP_AUTO ) );
        SendDlgItemMessageW( dlg, IDC_SVC_STARTUP, CB_ADDSTRING, 0, (LPARAM)cm_string( IDS_STARTUP_MANUAL ) );
        SendDlgItemMessageW( dlg, IDC_SVC_STARTUP, CB_ADDSTRING, 0, (LPARAM)cm_string( IDS_STARTUP_DISABLED ) );
        if (scm && (svc = OpenServiceW( scm, p->name, SERVICE_QUERY_CONFIG )))
        {
            if ((cfg = query_config( svc )))
            {
                p->startup = cfg->dwStartType;
                set_item_text( dlg, IDC_SVC_PATH, cfg->lpBinaryPathName );
            }
            desc = query_description( svc );
            CloseServiceHandle( svc );
        }
        if (scm) CloseServiceHandle( scm );
        set_item_text( dlg, IDC_SVC_DESCRIPTION, desc ? desc : cm_string( IDS_SVC_NO_DESCRIPTION ) );
        SendDlgItemMessageW( dlg, IDC_SVC_STARTUP, CB_SETCURSEL, startup_to_index( p->startup ), 0 );
        fill_status( dlg, p );
        free( cfg );
        free( desc );
        return TRUE;
    }

    case WM_COMMAND:
        p = (struct props *)GetWindowLongPtrW( dlg, DWLP_USER );
        if (LOWORD(wparam) == IDC_SVC_STARTUP && HIWORD(wparam) == CBN_SELCHANGE)
        {
            SendMessageW( GetParent( dlg ), PSM_CHANGED, (WPARAM)dlg, 0 );
            return TRUE;
        }
        if (HIWORD(wparam) == BN_CLICKED && p)
        {
            UINT command = 0;
            DWORD err;

            switch (LOWORD(wparam))
            {
            case IDC_SVC_START: command = IDM_SVC_START; break;
            case IDC_SVC_STOP: command = IDM_SVC_STOP; break;
            case IDC_SVC_PAUSE: command = IDM_SVC_PAUSE; break;
            case IDC_SVC_RESUME: command = IDM_SVC_RESUME; break;
            }
            if (!command) break;
            err = control_service( dlg, p->name, p->display, command );
            finish_command( dlg, p->display, err );
            fill_status( dlg, p );
            return TRUE;
        }
        break;

    case WM_NOTIFY:
        if (((NMHDR *)lparam)->code == PSN_APPLY)
        {
            DWORD wanted;
            DWORD err = 0;

            p = (struct props *)GetWindowLongPtrW( dlg, DWLP_USER );
            wanted = index_to_startup( (int)SendDlgItemMessageW( dlg, IDC_SVC_STARTUP, CB_GETCURSEL, 0, 0 ) );
            if (p && wanted != p->startup)
            {
                SC_HANDLE scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ), svc = NULL;

                if (scm) svc = OpenServiceW( scm, p->name, SERVICE_CHANGE_CONFIG );
                if (!svc) err = GetLastError();
                else if (!ChangeServiceConfigW( svc, SERVICE_NO_CHANGE, wanted, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL ))
                    err = GetLastError();
                else p->startup = wanted;
                if (svc) CloseServiceHandle( svc );
                if (scm) CloseServiceHandle( scm );
                if (!finish_command( dlg, p->display, err ))
                {
                    SetWindowLongPtrW( dlg, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE );
                    return TRUE;
                }
                fill_status( dlg, p );
            }
            SetWindowLongPtrW( dlg, DWLP_MSGRESULT, PSNRET_NOERROR );
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR CALLBACK logon_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_INITDIALOG)
    {
        struct props *p = (struct props *)((PROPSHEETPAGEW *)lparam)->lParam;
        SC_HANDLE scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ), svc;
        QUERY_SERVICE_CONFIGW *cfg = NULL;

        if (scm && (svc = OpenServiceW( scm, p->name, SERVICE_QUERY_CONFIG )))
        {
            cfg = query_config( svc );
            CloseServiceHandle( svc );
        }
        if (scm) CloseServiceHandle( scm );
        set_item_text( dlg, IDC_SVC_ACCOUNT, cfg && cfg->lpServiceStartName ? cfg->lpServiceStartName : L"" );
        set_item_text( dlg, IDC_SVC_NOTE, cm_string( IDS_LOGON_NOTE ) );
        free( cfg );
        return TRUE;
    }
    return FALSE;
}

static INT_PTR CALLBACK recovery_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_INITDIALOG)
    {
        set_item_text( dlg, IDC_SVC_NOTE, cm_string( IDS_RECOVERY_NOTE ) );
        return TRUE;
    }
    return FALSE;
}

static INT_PTR CALLBACK dependencies_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_INITDIALOG)
    {
        struct props *p = (struct props *)((PROPSHEETPAGEW *)lparam)->lParam;
        SC_HANDLE scm = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE ), svc;
        QUERY_SERVICE_CONFIGW *cfg = NULL;

        if (scm && (svc = OpenServiceW( scm, p->name, SERVICE_QUERY_CONFIG | SERVICE_ENUMERATE_DEPENDENTS )))
        {
            const WCHAR *dep;
            BYTE *buffer = NULL;
            DWORD size = 0, count = 0, i;

            if ((cfg = query_config( svc )) && cfg->lpDependencies)
                for (dep = cfg->lpDependencies; *dep; dep += lstrlenW( dep ) + 1)
                {
                    WCHAR display[256];
                    DWORD chars = ARRAY_SIZE(display);

                    if (*dep == SC_GROUP_IDENTIFIER) SendDlgItemMessageW( dlg, IDC_SVC_DEPENDS_ON, LB_ADDSTRING, 0, (LPARAM)(dep + 1) );
                    else if (GetServiceDisplayNameW( scm, dep, display, &chars ))
                        SendDlgItemMessageW( dlg, IDC_SVC_DEPENDS_ON, LB_ADDSTRING, 0, (LPARAM)display );
                    else SendDlgItemMessageW( dlg, IDC_SVC_DEPENDS_ON, LB_ADDSTRING, 0, (LPARAM)dep );
                }
            EnumDependentServicesW( svc, SERVICE_STATE_ALL, NULL, 0, &size, &count );
            if (size && (buffer = malloc( size )) &&
                EnumDependentServicesW( svc, SERVICE_STATE_ALL, (ENUM_SERVICE_STATUSW *)buffer, size, &size, &count ))
                for (i = 0; i < count; i++)
                {
                    const ENUM_SERVICE_STATUSW *e = (const ENUM_SERVICE_STATUSW *)buffer + i;
                    SendDlgItemMessageW( dlg, IDC_SVC_DEPENDENTS, LB_ADDSTRING, 0,
                                         (LPARAM)(e->lpDisplayName ? e->lpDisplayName : e->lpServiceName) );
                }
            free( buffer );
            CloseServiceHandle( svc );
        }
        if (scm) CloseServiceHandle( scm );
        free( cfg );
        return TRUE;
    }
    return FALSE;
}

static void show_properties( HWND owner, const struct service *s )
{
    struct props p = { s->name, s->display, NULL, 0 };
    PROPSHEETPAGEW pages[4] = {{0}};
    PROPSHEETHEADERW header = { sizeof(header) };
    static const struct { UINT id; DLGPROC proc; } defs[] =
    {
        { IDD_SVC_GENERAL, general_proc }, { IDD_SVC_LOGON, logon_proc }, { IDD_SVC_RECOVERY, recovery_proc },
        { IDD_SVC_DEPENDENCIES, dependencies_proc },
    };
    WCHAR title[400];
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(pages); i++)
    {
        pages[i].dwSize = sizeof(pages[i]);
        pages[i].dwFlags = PSP_DEFAULT;
        pages[i].hInstance = compmgmt_module;
        pages[i].pszTemplate = MAKEINTRESOURCEW( defs[i].id );
        pages[i].pfnDlgProc = defs[i].proc;
        pages[i].lParam = (LPARAM)&p;
    }
    swprintf( title, ARRAY_SIZE(title), cm_string( IDS_SVC_PROPS_TITLE ), s->display );
    header.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;
    header.hwndParent = owner;
    header.hInstance = compmgmt_module;
    header.pszCaption = title;
    header.nPages = ARRAY_SIZE(pages);
    header.ppsp = pages;
    PropertySheetW( &header );
}

/* ----------------------------------------------------------------------------------------- the commands */

void svc_command( HWND list, HWND owner, UINT command )
{
    struct service *s = selected_service( list );
    DWORD err;

    if (!s) return;
    if (command == IDM_SVC_PROPERTIES)
    {
        show_properties( owner, s );
        svc_refresh( list );
        return;
    }
    err = control_service( owner, s->name, s->display, command );
    finish_command( owner, s->display, err );
    svc_refresh( list );
}

void svc_context_menu( HWND list, HWND owner, int x, int y )
{
    HMENU menu = CreatePopupMenu();
    DWORD flags = svc_commands( list );
    UINT cmd;

    if (x == -1 && y == -1)
    {
        RECT rect;
        POINT pt;
        int index = (int)SendMessageW( list, LVM_GETNEXTITEM, -1, LVNI_SELECTED );

        if (index < 0) { DestroyMenu( menu ); return; }
        rect.left = LVIR_LABEL;
        SendMessageW( list, LVM_GETITEMRECT, index, (LPARAM)&rect );
        pt.x = rect.left;
        pt.y = rect.bottom;
        ClientToScreen( list, &pt );
        x = pt.x;
        y = pt.y;
    }
    AppendMenuW( menu, MF_STRING | ((flags & SVC_CAN_START) ? 0 : MF_GRAYED), IDM_SVC_START, L"&Start" );
    AppendMenuW( menu, MF_STRING | ((flags & SVC_CAN_STOP) ? 0 : MF_GRAYED), IDM_SVC_STOP, L"S&top" );
    AppendMenuW( menu, MF_STRING | ((flags & SVC_CAN_PAUSE) ? 0 : MF_GRAYED), IDM_SVC_PAUSE, L"&Pause" );
    AppendMenuW( menu, MF_STRING | ((flags & SVC_CAN_RESUME) ? 0 : MF_GRAYED), IDM_SVC_RESUME, L"&Resume" );
    AppendMenuW( menu, MF_STRING | ((flags & SVC_CAN_RESTART) ? 0 : MF_GRAYED), IDM_SVC_RESTART, L"R&estart" );
    AppendMenuW( menu, MF_SEPARATOR, 0, NULL );
    AppendMenuW( menu, MF_STRING | ((flags & SVC_SELECTED) ? 0 : MF_GRAYED), IDM_SVC_PROPERTIES, L"P&roperties" );
    SetMenuDefaultItem( menu, IDM_SVC_PROPERTIES, FALSE );
    cmd = TrackPopupMenu( menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, owner, NULL );
    DestroyMenu( menu );
    if (cmd) svc_command( list, owner, cmd );
}
