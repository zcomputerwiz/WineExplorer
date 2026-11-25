/*
 * Computer Management - the Users and Groups nodes of Local Users and Groups
 *
 * Both lists and every change go through netapi32 (NetUser*, NetLocalGroup*), which asks the accounts service of
 * session 0 (docs/LOCAL_USERS_DESIGN.md). The list shows the accounts; a change needs the rights of an elevated
 * administrator, and a refusal offers to open the console again as one.
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
#include <winerror.h>
#include <wingdi.h>
#include <commctrl.h>
#include <lm.h>

#include "compmgmt.h"
#include "resource.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(compmgmt);

#define PASSWORD_MAX 127

/* not in Wine's lmaccess.h (netapi32 exports NetUserSetInfo since the accounts service) */
#ifndef FILTER_NORMAL_ACCOUNT
#define FILTER_NORMAL_ACCOUNT 0x0002
#endif
NET_API_STATUS WINAPI NetUserSetInfo( LPCWSTR servername, LPCWSTR username, DWORD level, LPBYTE buf, LPDWORD parm_err );

static WCHAR *copy_string( const WCHAR *s )
{
    WCHAR *r = malloc( (lstrlenW( s ? s : L"" ) + 1) * sizeof(WCHAR) );
    if (r) lstrcpyW( r, s ? s : L"" );
    return r;
}

static void computer_name( WCHAR *name, DWORD size )
{
    if (!GetComputerNameW( name, &size )) name[0] = 0;
}

/* the text of an error: ours for the account errors, the system's for the rest */
const WCHAR *usr_error_text( DWORD err )
{
    static WCHAR system[400];
    UINT id = 0;

    switch (err)
    {
    case NERR_UserExists: id = IDS_ERR_EXISTS; break;
    case NERR_PasswordTooShort: id = IDS_ERR_PWD_SHORT; break;
    case NERR_UserNotFound: id = IDS_ERR_NOT_FOUND; break;
    case NERR_LastAdmin: id = IDS_ERR_LAST_ADMIN; break;
    case ERROR_SHARING_VIOLATION: id = IDS_ERR_LOGGED_IN; break;
    case RPC_S_SERVER_UNAVAILABLE: id = IDS_ERR_UNREACHABLE; break;
    }
    if (id) return cm_string( id );
    system[0] = 0;
    FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, system, ARRAY_SIZE(system), NULL );
    if (!system[0]) swprintf( system, ARRAY_SIZE(system), L"%lu", err );
    return system;
}

/* the end of every change: nothing for success, the elevation offer for a refusal, a message otherwise */
static BOOL finish( HWND owner, DWORD err )
{
    WCHAR text[900];

    if (!err) return TRUE;
    if (err == ERROR_ACCESS_DENIED) cm_offer_elevation( owner );
    else
    {
        swprintf( text, ARRAY_SIZE(text), cm_string( IDS_USR_ERROR ), usr_error_text( err ) );
        MessageBoxW( owner, text, cm_string( IDS_APPNAME ), MB_OK | MB_ICONERROR );
    }
    return FALSE;
}

/* ----------------------------------------------------------------------------------------------- the list */

static BOOL groups_shown;

static WCHAR *item_name( HWND list, int index )
{
    LVITEMW item = {0};

    if (index < 0) return NULL;
    item.mask = LVIF_PARAM;
    item.iItem = index;
    return SendMessageW( list, LVM_GETITEMW, 0, (LPARAM)&item ) ? (WCHAR *)item.lParam : NULL;
}

static WCHAR *selected_name( HWND list )
{
    return item_name( list, (int)SendMessageW( list, LVM_GETNEXTITEM, -1, LVNI_SELECTED ) );
}

void usr_free( HWND list )
{
    int i, count = (int)SendMessageW( list, LVM_GETITEMCOUNT, 0, 0 );

    for (i = 0; i < count; i++) free( item_name( list, i ) );
    SendMessageW( list, LVM_DELETEALLITEMS, 0, 0 );
}

HWND usr_create_view( HWND parent )
{
    HWND list = CreateWindowExW( WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL, WS_CHILD | WS_TABSTOP | LVS_REPORT |
                                 LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, 0, 0, parent, NULL, compmgmt_module, NULL );

    SendMessageW( list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT );
    return list;
}

static void set_columns( HWND list, BOOL groups )
{
    static const struct { UINT title; int width; } users[] = { { IDS_COL_NAME, 150 }, { IDS_USR_COL_FULLNAME, 200 }, { IDS_COL_DESCRIPTION, 250 } };
    static const struct { UINT title; int width; } grps[] = { { IDS_COL_NAME, 170 }, { IDS_COL_DESCRIPTION, 400 } };
    LVCOLUMNW col = {0};
    int i, n = groups ? ARRAY_SIZE(grps) : ARRAY_SIZE(users);

    while (SendMessageW( list, LVM_DELETECOLUMN, 0, 0 )) {}
    for (i = 0; i < n; i++)
    {
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.cx = groups ? grps[i].width : users[i].width;
        col.iSubItem = i;
        col.pszText = (WCHAR *)cm_string( groups ? grps[i].title : users[i].title );
        SendMessageW( list, LVM_INSERTCOLUMNW, i, (LPARAM)&col );
    }
}

static void add_row( HWND list, int index, const WCHAR *name, const WCHAR *c1, const WCHAR *c2 )
{
    LVITEMW item = {0};

    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = index;
    item.pszText = (WCHAR *)name;
    item.lParam = (LPARAM)copy_string( name );
    SendMessageW( list, LVM_INSERTITEMW, 0, (LPARAM)&item );
    item.mask = LVIF_TEXT;
    item.iSubItem = 1;
    item.pszText = (WCHAR *)c1;
    SendMessageW( list, LVM_SETITEMW, 0, (LPARAM)&item );
    if (c2)
    {
        item.iSubItem = 2;
        item.pszText = (WCHAR *)c2;
        SendMessageW( list, LVM_SETITEMW, 0, (LPARAM)&item );
    }
}

/* fills the list with the accounts or the groups; the error of the call when it failed (the list is then empty) */
DWORD usr_show( HWND list, BOOL groups )
{
    NET_API_STATUS st;
    BYTE *buf = NULL;
    DWORD read = 0, total = 0, i;

    usr_free( list );
    set_columns( list, groups );
    groups_shown = groups;
    SendMessageW( list, WM_SETREDRAW, FALSE, 0 );
    if (groups)
    {
        st = NetLocalGroupEnum( NULL, 1, &buf, MAX_PREFERRED_LENGTH, &read, &total, NULL );
        for (i = 0; !st && i < read; i++)
        {
            LOCALGROUP_INFO_1 *g = (LOCALGROUP_INFO_1 *)buf + i;
            add_row( list, i, g->lgrpi1_name, g->lgrpi1_comment ? g->lgrpi1_comment : L"", NULL );
        }
    }
    else
    {
        st = NetUserEnum( NULL, 20, FILTER_NORMAL_ACCOUNT, &buf, MAX_PREFERRED_LENGTH, &read, &total, NULL );
        for (i = 0; !st && i < read; i++)
        {
            USER_INFO_20 *u = (USER_INFO_20 *)buf + i;
            add_row( list, i, u->usri20_name, u->usri20_full_name ? u->usri20_full_name : L"",
                     u->usri20_comment ? u->usri20_comment : L"" );
        }
    }
    if (buf) NetApiBufferFree( buf );
    SendMessageW( list, WM_SETREDRAW, TRUE, 0 );
    return st;
}

DWORD usr_commands( HWND list )
{
    WCHAR *name = selected_name( list );
    DWORD flags = groups_shown ? 0 : USR_CAN_NEW;

    if (name) flags |= USR_SELECTED | (groups_shown ? 0 : USR_CAN_CHANGE);
    return flags;
}

/* ------------------------------------------------------------------------------------------------ dialogs */

struct new_user_data { WCHAR name[UNLEN + 1], full[257], pw1[PASSWORD_MAX + 1], pw2[PASSWORD_MAX + 1]; BOOL disabled, created; HWND owner; };

static DWORD create_user( HWND owner, const struct new_user_data *d )
{
    USER_INFO_1 u = {0};
    DWORD err, parm = 0;

    u.usri1_name = (WCHAR *)d->name;
    u.usri1_password = (WCHAR *)d->pw1;
    u.usri1_priv = USER_PRIV_USER;
    u.usri1_flags = UF_SCRIPT | UF_NORMAL_ACCOUNT;
    if ((err = NetUserAdd( NULL, 1, (BYTE *)&u, &parm ))) return err;
    if (d->full[0])
    {
        USER_INFO_1011 f = { (WCHAR *)d->full };
        if ((err = NetUserSetInfo( NULL, d->name, 1011, (BYTE *)&f, &parm ))) return err;
    }
    if (d->disabled)
    {
        USER_INFO_1008 f = { UF_SCRIPT | UF_NORMAL_ACCOUNT | UF_ACCOUNTDISABLE };
        if ((err = NetUserSetInfo( NULL, d->name, 1008, (BYTE *)&f, &parm ))) return err;
    }
    return 0;
}

/* a password is in memory only as long as it is needed: the edit controls and the copies are cleared when it is used,
 * refused, or the dialog is left */
static void wipe_edits( HWND dlg )
{
    SetDlgItemTextW( dlg, IDC_USR_PW1, L"" );
    SetDlgItemTextW( dlg, IDC_USR_PW2, L"" );
}

static INT_PTR CALLBACK new_user_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct new_user_data *d;

    switch (msg)
    {
    case WM_INITDIALOG:
        SetWindowLongPtrW( dlg, DWLP_USER, lparam );
        SendDlgItemMessageW( dlg, IDC_USR_NAME, EM_LIMITTEXT, UNLEN, 0 );
        SendDlgItemMessageW( dlg, IDC_USR_FULLNAME, EM_LIMITTEXT, 256, 0 );
        SendDlgItemMessageW( dlg, IDC_USR_PW1, EM_LIMITTEXT, PASSWORD_MAX, 0 );
        SendDlgItemMessageW( dlg, IDC_USR_PW2, EM_LIMITTEXT, PASSWORD_MAX, 0 );
        return TRUE;

    case WM_COMMAND:
        d = (struct new_user_data *)GetWindowLongPtrW( dlg, DWLP_USER );
        switch (LOWORD(wparam))
        {
        case IDC_USR_CREATE:
        {
            DWORD err;

            GetDlgItemTextW( dlg, IDC_USR_NAME, d->name, ARRAY_SIZE(d->name) );
            GetDlgItemTextW( dlg, IDC_USR_FULLNAME, d->full, ARRAY_SIZE(d->full) );
            GetDlgItemTextW( dlg, IDC_USR_PW1, d->pw1, ARRAY_SIZE(d->pw1) );
            GetDlgItemTextW( dlg, IDC_USR_PW2, d->pw2, ARRAY_SIZE(d->pw2) );
            d->disabled = IsDlgButtonChecked( dlg, IDC_USR_DISABLED ) == BST_CHECKED;
            if (!d->name[0])
            {
                MessageBoxW( dlg, cm_string( IDS_USR_NAME_EMPTY ), cm_string( IDS_APPNAME ), MB_OK | MB_ICONWARNING );
                break;
            }
            if (lstrcmpW( d->pw1, d->pw2 ))
            {
                SecureZeroMemory( d->pw1, sizeof(d->pw1) );
                SecureZeroMemory( d->pw2, sizeof(d->pw2) );
                MessageBoxW( dlg, cm_string( IDS_USR_PW_MISMATCH ), cm_string( IDS_APPNAME ), MB_OK | MB_ICONWARNING );
                wipe_edits( dlg );
                SetFocus( GetDlgItem( dlg, IDC_USR_PW1 ) );
                break;
            }
            err = create_user( dlg, d );
            SecureZeroMemory( d->pw1, sizeof(d->pw1) );
            SecureZeroMemory( d->pw2, sizeof(d->pw2) );
            if (err == ERROR_ACCESS_DENIED)
            {
                /* an elevated console takes over, and this one closes with its dialog */
                if (cm_offer_elevation( dlg )) EndDialog( dlg, IDCANCEL );
                break;
            }
            if (err)
            {
                finish( dlg, err );
                break;
            }
            d->created = TRUE;
            /* the dialog stays for the next user, as on Windows */
            SetDlgItemTextW( dlg, IDC_USR_NAME, L"" );
            SetDlgItemTextW( dlg, IDC_USR_FULLNAME, L"" );
            SetDlgItemTextW( dlg, IDC_USR_PW1, L"" );
            SetDlgItemTextW( dlg, IDC_USR_PW2, L"" );
            CheckDlgButton( dlg, IDC_USR_DISABLED, BST_UNCHECKED );
            SetFocus( GetDlgItem( dlg, IDC_USR_NAME ) );
            break;
        }
        case IDCANCEL:
            wipe_edits( dlg );
            SecureZeroMemory( d->pw1, sizeof(d->pw1) );
            SecureZeroMemory( d->pw2, sizeof(d->pw2) );
            EndDialog( dlg, d->created ? IDOK : IDCANCEL );
            break;
        }
        return TRUE;
    }
    return FALSE;
}

struct password_data { const WCHAR *name; WCHAR pw1[PASSWORD_MAX + 1], pw2[PASSWORD_MAX + 1]; };

static INT_PTR CALLBACK password_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct password_data *d;
    WCHAR title[400];

    switch (msg)
    {
    case WM_INITDIALOG:
        d = (struct password_data *)lparam;
        SetWindowLongPtrW( dlg, DWLP_USER, lparam );
        swprintf( title, ARRAY_SIZE(title), cm_string( IDS_USR_PW_TITLE ), d->name );
        SetWindowTextW( dlg, title );
        SetDlgItemTextW( dlg, IDC_USR_NOTE, cm_string( IDS_USR_PW_WARN ) );
        SendDlgItemMessageW( dlg, IDC_USR_PW1, EM_LIMITTEXT, PASSWORD_MAX, 0 );
        SendDlgItemMessageW( dlg, IDC_USR_PW2, EM_LIMITTEXT, PASSWORD_MAX, 0 );
        return TRUE;

    case WM_COMMAND:
        d = (struct password_data *)GetWindowLongPtrW( dlg, DWLP_USER );
        switch (LOWORD(wparam))
        {
        case IDOK:
            GetDlgItemTextW( dlg, IDC_USR_PW1, d->pw1, ARRAY_SIZE(d->pw1) );
            GetDlgItemTextW( dlg, IDC_USR_PW2, d->pw2, ARRAY_SIZE(d->pw2) );
            if (lstrcmpW( d->pw1, d->pw2 ))
            {
                SecureZeroMemory( d->pw1, sizeof(d->pw1) );
                SecureZeroMemory( d->pw2, sizeof(d->pw2) );
                MessageBoxW( dlg, cm_string( IDS_USR_PW_MISMATCH ), cm_string( IDS_APPNAME ), MB_OK | MB_ICONWARNING );
                wipe_edits( dlg );
                SetFocus( GetDlgItem( dlg, IDC_USR_PW1 ) );
                break;
            }
            wipe_edits( dlg );
            EndDialog( dlg, IDOK );   /* the caller uses d->pw1 and clears it */
            break;
        case IDCANCEL:
            wipe_edits( dlg );
            SecureZeroMemory( d->pw1, sizeof(d->pw1) );
            SecureZeroMemory( d->pw2, sizeof(d->pw2) );
            EndDialog( dlg, IDCANCEL );
            break;
        }
        return TRUE;
    }
    return FALSE;
}

/* ----------------------------------------------------------------------------------------- the properties */

struct user_props
{
    WCHAR name[UNLEN + 1], full[257];
    DWORD flags;
    BOOL full_changed, member_of_admins;
    HWND owner;
};

static BOOL is_administrators( const WCHAR *group )
{
    return !lstrcmpiW( group, L"Administrators" );
}

static void fill_member_of( HWND dlg, struct user_props *p )
{
    LOCALGROUP_USERS_INFO_0 *groups = NULL;
    DWORD read = 0, total = 0, i;

    SendDlgItemMessageW( dlg, IDC_USR_GROUPS, LB_RESETCONTENT, 0, 0 );
    p->member_of_admins = FALSE;
    if (!NetUserGetLocalGroups( NULL, p->name, 0, 0, (BYTE **)&groups, MAX_PREFERRED_LENGTH, &read, &total ))
    {
        for (i = 0; i < read; i++)
        {
            SendDlgItemMessageW( dlg, IDC_USR_GROUPS, LB_ADDSTRING, 0, (LPARAM)groups[i].lgrui0_name );
            if (is_administrators( groups[i].lgrui0_name )) p->member_of_admins = TRUE;
        }
        NetApiBufferFree( groups );
    }
    EnableWindow( GetDlgItem( dlg, IDC_USR_ADD ), !p->member_of_admins );
    EnableWindow( GetDlgItem( dlg, IDC_USR_REMOVE ), FALSE );
}

static INT_PTR CALLBACK general_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct user_props *p;

    switch (msg)
    {
    case WM_INITDIALOG:
        p = (struct user_props *)((PROPSHEETPAGEW *)lparam)->lParam;
        SetWindowLongPtrW( dlg, DWLP_USER, (LONG_PTR)p );
        SetDlgItemTextW( dlg, IDC_USR_NAME, p->name );
        SetDlgItemTextW( dlg, IDC_USR_FULLNAME, p->full );
        SendDlgItemMessageW( dlg, IDC_USR_FULLNAME, EM_LIMITTEXT, 256, 0 );
        CheckDlgButton( dlg, IDC_USR_DISABLED, (p->flags & UF_ACCOUNTDISABLE) ? BST_CHECKED : BST_UNCHECKED );
        return TRUE;

    case WM_COMMAND:
        if (HIWORD(wparam) == EN_CHANGE || HIWORD(wparam) == BN_CLICKED) SendMessageW( GetParent( dlg ), PSM_CHANGED, (WPARAM)dlg, 0 );
        return TRUE;

    case WM_NOTIFY:
        if (((NMHDR *)lparam)->code == PSN_APPLY)
        {
            DWORD err = 0, parm = 0;
            WCHAR full[257];
            BOOL disabled = IsDlgButtonChecked( dlg, IDC_USR_DISABLED ) == BST_CHECKED;

            p = (struct user_props *)GetWindowLongPtrW( dlg, DWLP_USER );
            GetDlgItemTextW( dlg, IDC_USR_FULLNAME, full, ARRAY_SIZE(full) );
            if (lstrcmpW( full, p->full ))
            {
                USER_INFO_1011 f = { full };
                if ((err = NetUserSetInfo( NULL, p->name, 1011, (BYTE *)&f, &parm ))) goto failed;
                lstrcpyW( p->full, full );
            }
            if (disabled != !!(p->flags & UF_ACCOUNTDISABLE))
            {
                USER_INFO_1008 f = { UF_SCRIPT | UF_NORMAL_ACCOUNT | (disabled ? UF_ACCOUNTDISABLE : 0) };
                if ((err = NetUserSetInfo( NULL, p->name, 1008, (BYTE *)&f, &parm ))) goto failed;
                p->flags = f.usri1008_flags;
            }
            SetWindowLongPtrW( dlg, DWLP_MSGRESULT, PSNRET_NOERROR );
            return TRUE;
        failed:
            finish( dlg, err );
            SetWindowLongPtrW( dlg, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE );
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

static INT_PTR CALLBACK member_of_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct user_props *p;
    WCHAR host[MAX_COMPUTERNAME_LENGTH + 1], member[MAX_COMPUTERNAME_LENGTH + UNLEN + 3];

    switch (msg)
    {
    case WM_INITDIALOG:
        p = (struct user_props *)((PROPSHEETPAGEW *)lparam)->lParam;
        SetWindowLongPtrW( dlg, DWLP_USER, (LONG_PTR)p );
        fill_member_of( dlg, p );
        return TRUE;

    case WM_COMMAND:
        p = (struct user_props *)GetWindowLongPtrW( dlg, DWLP_USER );
        switch (LOWORD(wparam))
        {
        case IDC_USR_GROUPS:
            if (HIWORD(wparam) == LBN_SELCHANGE)
            {
                int i = (int)SendDlgItemMessageW( dlg, IDC_USR_GROUPS, LB_GETCURSEL, 0, 0 );
                WCHAR g[100] = L"";
                if (i >= 0) SendDlgItemMessageW( dlg, IDC_USR_GROUPS, LB_GETTEXT, i, (LPARAM)g );
                EnableWindow( GetDlgItem( dlg, IDC_USR_REMOVE ), is_administrators( g ) );
            }
            break;
        case IDC_USR_ADD:
        case IDC_USR_REMOVE:
        {
            LOCALGROUP_MEMBERS_INFO_3 m;
            DWORD err;

            computer_name( host, ARRAY_SIZE(host) );
            swprintf( member, ARRAY_SIZE(member), L"%s\\%s", host, p->name );
            m.lgrmi3_domainandname = member;
            err = LOWORD(wparam) == IDC_USR_ADD ? NetLocalGroupAddMembers( NULL, L"Administrators", 3, (BYTE *)&m, 1 )
                                                 : NetLocalGroupDelMembers( NULL, L"Administrators", 3, (BYTE *)&m, 1 );
            if (finish( dlg, err )) fill_member_of( dlg, p );
            break;
        }
        }
        return TRUE;
    }
    return FALSE;
}

static void user_properties( HWND owner, const WCHAR *name )
{
    struct user_props p = {{0}};
    USER_INFO_20 *info = NULL;
    PROPSHEETPAGEW pages[2] = {{0}};
    PROPSHEETHEADERW header = { sizeof(header) };
    static const struct { UINT id; DLGPROC proc; } defs[] = { { IDD_USR_GENERAL, general_proc }, { IDD_USR_MEMBEROF, member_of_proc } };
    WCHAR title[400];
    DWORD err;
    unsigned int i;

    if ((err = NetUserGetInfo( NULL, name, 20, (BYTE **)&info )))
    {
        finish( owner, err );
        return;
    }
    lstrcpynW( p.name, info->usri20_name, ARRAY_SIZE(p.name) );
    lstrcpynW( p.full, info->usri20_full_name ? info->usri20_full_name : L"", ARRAY_SIZE(p.full) );
    p.flags = info->usri20_flags;
    NetApiBufferFree( info );
    for (i = 0; i < ARRAY_SIZE(pages); i++)
    {
        pages[i].dwSize = sizeof(pages[i]);
        pages[i].dwFlags = PSP_DEFAULT;
        pages[i].hInstance = compmgmt_module;
        pages[i].pszTemplate = MAKEINTRESOURCEW( defs[i].id );
        pages[i].pfnDlgProc = defs[i].proc;
        pages[i].lParam = (LPARAM)&p;
    }
    swprintf( title, ARRAY_SIZE(title), cm_string( IDS_USR_PROPS_TITLE ), p.name );
    header.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;
    header.hwndParent = owner;
    header.hInstance = compmgmt_module;
    header.pszCaption = title;
    header.nPages = ARRAY_SIZE(pages);
    header.ppsp = pages;
    PropertySheetW( &header );
}

struct group_data { WCHAR name[257]; };

static INT_PTR CALLBACK group_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        struct group_data *d = (struct group_data *)lparam;
        LOCALGROUP_INFO_1 *info = NULL;
        LOCALGROUP_MEMBERS_INFO_3 *members = NULL;
        DWORD read = 0, total = 0, i;
        WCHAR title[400];

        swprintf( title, ARRAY_SIZE(title), cm_string( IDS_USR_PROPS_TITLE ), d->name );
        SetWindowTextW( dlg, title );
        SetDlgItemTextW( dlg, IDC_USR_NAME, d->name );
        if (!NetLocalGroupGetInfo( NULL, d->name, 1, (BYTE **)&info ))
        {
            SetDlgItemTextW( dlg, IDC_USR_FULLNAME, info->lgrpi1_comment ? info->lgrpi1_comment : L"" );
            NetApiBufferFree( info );
        }
        if (!NetLocalGroupGetMembers( NULL, d->name, 3, (BYTE **)&members, MAX_PREFERRED_LENGTH, &read, &total, NULL ))
        {
            for (i = 0; i < read; i++) SendDlgItemMessageW( dlg, IDC_USR_GROUPS, LB_ADDSTRING, 0, (LPARAM)members[i].lgrmi3_domainandname );
            NetApiBufferFree( members );
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) EndDialog( dlg, IDOK );
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------------------------- the commands */

/* TRUE when the list should be filled again */
BOOL usr_command( HWND list, HWND owner, UINT command )
{
    WCHAR *name = selected_name( list );
    BOOL changed = FALSE;

    if (command == IDM_USR_NEW && !groups_shown)
    {
        struct new_user_data *d = calloc( 1, sizeof(*d) );

        if (!d) return FALSE;
        d->owner = owner;
        DialogBoxParamW( compmgmt_module, MAKEINTRESOURCEW( IDD_USR_NEW ), owner, new_user_proc, (LPARAM)d );
        changed = d->created;
        SecureZeroMemory( d, sizeof(*d) );
        free( d );
        return changed;
    }
    if (!name) return FALSE;
    if (command == IDM_USR_PROPERTIES)
    {
        if (groups_shown)
        {
            struct group_data d;
            lstrcpynW( d.name, name, ARRAY_SIZE(d.name) );
            DialogBoxParamW( compmgmt_module, MAKEINTRESOURCEW( IDD_GRP_PROPS ), owner, group_proc, (LPARAM)&d );
        }
        else
        {
            WCHAR copy[UNLEN + 1];
            lstrcpynW( copy, name, ARRAY_SIZE(copy) );
            user_properties( owner, copy );
        }
        return !groups_shown;
    }
    if (groups_shown) return FALSE;
    if (command == IDM_USR_DELETE)
    {
        WCHAR text[600];

        swprintf( text, ARRAY_SIZE(text), cm_string( IDS_USR_DELETE_ASK ), name );
        if (MessageBoxW( owner, text, cm_string( IDS_USR_DELETE_TITLE ), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 ) != IDYES) return FALSE;
        return finish( owner, NetUserDel( NULL, name ) );
    }
    if (command == IDM_USR_PASSWORD)
    {
        struct password_data d = { name };
        USER_INFO_1003 f;

        if (DialogBoxParamW( compmgmt_module, MAKEINTRESOURCEW( IDD_USR_PASSWORD ), owner, password_proc, (LPARAM)&d ) != IDOK)
        {
            SecureZeroMemory( &d, sizeof(d) );
            return FALSE;
        }
        f.usri1003_password = d.pw1;
        finish( owner, NetUserSetInfo( NULL, name, 1003, (BYTE *)&f, NULL ) );
        SecureZeroMemory( &d, sizeof(d) );
        SecureZeroMemory( &f, sizeof(f) );
    }
    return FALSE;
}

void usr_context_menu( HWND list, HWND owner, int x, int y )
{
    HMENU menu = CreatePopupMenu();
    DWORD flags = usr_commands( list );
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
    if (!groups_shown)
    {
        AppendMenuW( menu, MF_STRING | ((flags & USR_CAN_NEW) ? 0 : MF_GRAYED), IDM_USR_NEW, L"New &User..." );
        AppendMenuW( menu, MF_SEPARATOR, 0, NULL );
        AppendMenuW( menu, MF_STRING | ((flags & USR_CAN_CHANGE) ? 0 : MF_GRAYED), IDM_USR_PASSWORD, L"Set &Password..." );
        AppendMenuW( menu, MF_STRING | ((flags & USR_CAN_CHANGE) ? 0 : MF_GRAYED), IDM_USR_DELETE, L"&Delete" );
        AppendMenuW( menu, MF_SEPARATOR, 0, NULL );
    }
    AppendMenuW( menu, MF_STRING | ((flags & USR_SELECTED) ? 0 : MF_GRAYED), IDM_USR_PROPERTIES, L"P&roperties" );
    SetMenuDefaultItem( menu, IDM_USR_PROPERTIES, FALSE );
    cmd = TrackPopupMenu( menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, owner, NULL );
    DestroyMenu( menu );
    if (cmd) SendMessageW( owner, WM_COMMAND, cmd, 0 );
}
