/*
 * Computer Management - the console window (as the Microsoft Management Console shows it on Windows XP)
 *
 * The console tree on the left, the result pane on the right. A node is one of three kinds: a container (its result pane
 * lists its children), a hosted snap-in (Device Manager, a child window of this one) or a snap-in that is not there (the
 * node is shown grayed, and its result pane says why).
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

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winnls.h>
#include <wingdi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

#include "compmgmt.h"
#include "resource.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(compmgmt);

enum kind { CONTAINER, HOSTED_DEVMGR, SERVICES, USERS, GROUPS, MISSING };

/* the console tree, in the order of the string tables; parent is an index, -1 for the root */
static const struct { int parent; enum kind kind; } nodes[] =
{
    { -1, CONTAINER },       /*  0 Computer Management (Local) */
    {  0, CONTAINER },       /*  1 System Tools */
    {  1, MISSING },         /*  2 Event Viewer */
    {  1, MISSING },         /*  3 Shared Folders */
    {  1, CONTAINER },       /*  4 Local Users and Groups */
    {  1, MISSING },         /*  5 Performance Logs and Alerts */
    {  1, HOSTED_DEVMGR },   /*  6 Device Manager */
    {  0, CONTAINER },       /*  7 Storage */
    {  7, MISSING },         /*  8 Removable Storage */
    {  7, MISSING },         /*  9 Disk Defragmenter */
    {  7, MISSING },         /* 10 Disk Management */
    {  0, CONTAINER },       /* 11 Services and Applications */
    { 11, SERVICES },        /* 12 Services */
    { 11, MISSING },         /* 13 WMI Control */
    { 11, MISSING },         /* 14 Indexing Service */
    {  4, USERS },           /* 15 Users */
    {  4, GROUPS },          /* 16 Groups */
};
#define NODE_COUNT ARRAY_SIZE(nodes)

typedef HWND (WINAPI *CREATE_PANE)( HWND, int, int, int, int );
typedef BOOL (WINAPI *PANE_COMMAND)( HWND, int );

HINSTANCE compmgmt_module;

static struct
{
    HWND hwnd, tree, list, text, pane, svc, usr;
    HTREEITEM items[NODE_COUNT];
    int selected, split;
    BOOL dragging;
    HIMAGELIST images;
    int image_computer, image_folder, image_missing;
    HMODULE devmgr;
    CREATE_PANE create_pane;
    PANE_COMMAND pane_command;
} cm;

const WCHAR *cm_string( UINT id )
{
    static WCHAR buffer[8][1024];
    static int next;
    WCHAR *text = buffer[next++ % ARRAY_SIZE(buffer)];

    if (!LoadStringW( compmgmt_module, id, text, ARRAY_SIZE(buffer[0]) )) text[0] = 0;
    return text;
}

static int stock_image( HIMAGELIST list, SHSTOCKICONID id, int fallback )
{
    SHSTOCKICONINFO info = { sizeof(info) };
    int image = fallback;

    if (SUCCEEDED( SHGetStockIconInfo( id, SHGSI_ICON | SHGSI_SMALLICON, &info ) ) && info.hIcon)
    {
        image = ImageList_AddIcon( list, info.hIcon );
        DestroyIcon( info.hIcon );
    }
    return image;
}

static void create_images( void )
{
    cm.images = ImageList_Create( GetSystemMetrics( SM_CXSMICON ), GetSystemMetrics( SM_CYSMICON ),
                                  ILC_COLOR32 | ILC_MASK, 4, 4 );
    cm.image_computer = stock_image( cm.images, SIID_DESKTOPPC, 0 );
    cm.image_folder = stock_image( cm.images, SIID_FOLDER, 0 );
    cm.image_missing = stock_image( cm.images, SIID_FOLDER, 0 );
}

static int node_image( int node )
{
    if (node == 0) return cm.image_computer;
    return nodes[node].kind == MISSING ? cm.image_missing : cm.image_folder;
}

static HTREEITEM add_node( int node )
{
    TVINSERTSTRUCTW insert = {0};

    insert.hParent = nodes[node].parent < 0 ? TVI_ROOT : cm.items[nodes[node].parent];
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
    insert.item.pszText = (WCHAR *)cm_string( IDS_NODE_BASE + node );
    insert.item.iImage = insert.item.iSelectedImage = node_image( node );
    insert.item.lParam = node;
    return (HTREEITEM)SendMessageW( cm.tree, TVM_INSERTITEMW, 0, (LPARAM)&insert );
}

static void fill_tree( void )
{
    unsigned int i;

    for (i = 0; i < NODE_COUNT; i++) cm.items[i] = add_node( i );
    SendMessageW( cm.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)cm.items[0] );
    for (i = 1; i < NODE_COUNT; i++)
        if (nodes[i].kind == CONTAINER) SendMessageW( cm.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)cm.items[i] );
}

/* the Services commands at the top of the Action menu while the Services node is shown, as the selected service allows */
static void update_action_menu( HMENU action )
{
    static const struct { UINT id; const WCHAR *text; DWORD flag; } items[] =
    {
        { IDM_SVC_START, L"&Start", SVC_CAN_START }, { IDM_SVC_STOP, L"S&top", SVC_CAN_STOP },
        { IDM_SVC_PAUSE, L"&Pause", SVC_CAN_PAUSE }, { IDM_SVC_RESUME, L"&Resume", SVC_CAN_RESUME },
        { IDM_SVC_RESTART, L"R&estart", SVC_CAN_RESTART },
    };
    BOOL services = cm.selected >= 0 && nodes[cm.selected].kind == SERVICES;
    DWORD flags = services ? svc_commands( cm.svc ) : 0;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(items); i++) DeleteMenu( action, items[i].id, MF_BYCOMMAND );
    if (GetMenuState( action, 0, MF_BYPOSITION ) & MF_SEPARATOR) DeleteMenu( action, 0, MF_BYPOSITION );
    if (services)
    {
        for (i = 0; i < ARRAY_SIZE(items); i++)
            InsertMenuW( action, i, MF_BYPOSITION | MF_STRING | ((flags & items[i].flag) ? 0 : MF_GRAYED), items[i].id,
                         items[i].text );
        InsertMenuW( action, ARRAY_SIZE(items), MF_BYPOSITION | MF_SEPARATOR, 0, NULL );
        EnableMenuItem( action, IDM_PROPERTIES, MF_BYCOMMAND | ((flags & SVC_SELECTED) ? MF_ENABLED : MF_GRAYED) );
    }
}

/* the Users commands at the top of the Action menu while the Users node is shown */
static void update_users_menu( HMENU action )
{
    static const struct { UINT id; const WCHAR *text; DWORD flag; } items[] =
    {
        { IDM_USR_NEW, L"New &User...", USR_CAN_NEW }, { IDM_USR_PASSWORD, L"Set &Password...", USR_CAN_CHANGE },
        { IDM_USR_DELETE, L"&Delete", USR_CAN_CHANGE },
    };
    BOOL users = cm.selected >= 0 && nodes[cm.selected].kind == USERS;
    BOOL groups = cm.selected >= 0 && nodes[cm.selected].kind == GROUPS;
    DWORD flags = (users || groups) ? usr_commands( cm.usr ) : 0;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(items); i++) DeleteMenu( action, items[i].id, MF_BYCOMMAND );
    if (GetMenuState( action, 0, MF_BYPOSITION ) & MF_SEPARATOR) DeleteMenu( action, 0, MF_BYPOSITION );
    if (!users && !groups) return;
    if (users)
    {
        for (i = 0; i < ARRAY_SIZE(items); i++)
            InsertMenuW( action, i, MF_BYPOSITION | MF_STRING | ((flags & items[i].flag) ? 0 : MF_GRAYED), items[i].id,
                         items[i].text );
        InsertMenuW( action, ARRAY_SIZE(items), MF_BYPOSITION | MF_SEPARATOR, 0, NULL );
    }
    EnableMenuItem( action, IDM_PROPERTIES, MF_BYCOMMAND | ((flags & USR_SELECTED) ? MF_ENABLED : MF_GRAYED) );
}

/* the menus of a snap-in come with it: Device Manager's views and its scan, the list views otherwise */
static void update_menus( void )
{
    HMENU menu = GetMenu( cm.hwnd ), view = GetSubMenu( menu, 1 ), action = GetSubMenu( menu, 0 );
    BOOL devmgr = cm.selected >= 0 && nodes[cm.selected].kind == HOSTED_DEVMGR && cm.pane;
    BOOL container = cm.selected >= 0 && nodes[cm.selected].kind == CONTAINER;

    while (GetMenuItemCount( view ) > 0) DeleteMenu( view, 0, MF_BYPOSITION );
    if (devmgr)
    {
        AppendMenuW( view, MF_STRING, IDM_DM_BY_TYPE, L"Devices by &type" );
        AppendMenuW( view, MF_STRING, IDM_DM_BY_CONNECTION, L"Devices by &connection" );
        AppendMenuW( view, MF_STRING, IDM_DM_RES_BY_TYPE, L"Resources by t&ype" );
        AppendMenuW( view, MF_STRING, IDM_DM_RES_BY_CONNECTION, L"Resources by c&onnection" );
        AppendMenuW( view, MF_SEPARATOR, 0, NULL );
        AppendMenuW( view, MF_STRING, IDM_DM_SHOW_HIDDEN, L"Show &hidden devices" );
    }
    else
    {
        UINT flags = container ? MF_STRING : MF_STRING | MF_GRAYED;

        AppendMenuW( view, flags, IDM_VIEW_LARGE, L"Lar&ge Icons" );
        AppendMenuW( view, flags, IDM_VIEW_SMALL, L"S&mall Icons" );
        AppendMenuW( view, flags, IDM_VIEW_LIST, L"&List" );
        AppendMenuW( view, flags, IDM_VIEW_DETAIL, L"&Detail" );
    }
    EnableMenuItem( action, IDM_PROPERTIES, MF_BYCOMMAND | MF_GRAYED );
    update_action_menu( action );
    update_users_menu( action );
    EnableMenuItem( action, IDM_REFRESH, MF_BYCOMMAND | MF_ENABLED );
    DrawMenuBar( cm.hwnd );
}

/* a change was refused for want of rights: the console opens again as an administrator (the consent prompt) */
BOOL cm_offer_elevation( HWND owner )
{
    WCHAR program[MAX_PATH], console[MAX_PATH + 24];
    UINT n;

    if (MessageBoxW( owner, cm_string( IDS_ASK_ELEVATE ), cm_string( IDS_ELEVATE_TITLE ), MB_YESNO | MB_ICONQUESTION ) != IDYES)
        return FALSE;
    /* the programs of the system folder, not whatever the current directory has under those names */
    n = GetSystemDirectoryW( program, ARRAY_SIZE(program) - 12 );
    if (!n) return FALSE;
    swprintf( console, ARRAY_SIZE(console), L"\"%ls\\compmgmt.msc\"", program );
    wcscat( program, L"\\mmc.exe" );
    if ((INT_PTR)ShellExecuteW( owner, L"runas", program, console, NULL, SW_SHOWNORMAL ) <= 32) return FALSE;
    if (IsWindow( cm.hwnd )) DestroyWindow( cm.hwnd );
    return TRUE;
}

static void list_children( int node )
{
    LVITEMW item = {0};
    unsigned int i;
    int count = 0;

    SendMessageW( cm.list, LVM_DELETEALLITEMS, 0, 0 );
    for (i = 0; i < NODE_COUNT; i++)
    {
        if (nodes[i].parent != node) continue;
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = count++;
        item.pszText = (WCHAR *)cm_string( IDS_NODE_BASE + i );
        item.iImage = node_image( i );
        item.lParam = i;
        SendMessageW( cm.list, LVM_INSERTITEMW, 0, (LPARAM)&item );
        item.mask = LVIF_TEXT;
        item.iSubItem = 1;
        item.pszText = (WCHAR *)cm_string( IDS_DESC_BASE + i );
        SendMessageW( cm.list, LVM_SETITEMW, 0, (LPARAM)&item );
        item.iSubItem = 0;
    }
}

static void layout( void )
{
    RECT client;
    int split;

    GetClientRect( cm.hwnd, &client );
    split = cm.split;
    if (split > client.right - 40) split = client.right - 40;
    if (split < 40) split = 40;
    MoveWindow( cm.tree, 0, 0, split, client.bottom, TRUE );
    MoveWindow( cm.list, split + 4, 0, client.right - split - 4, client.bottom, TRUE );
    MoveWindow( cm.text, split + 4, 0, client.right - split - 4, client.bottom, TRUE );
    MoveWindow( cm.svc, split + 4, 0, client.right - split - 4, client.bottom, TRUE );
    MoveWindow( cm.usr, split + 4, 0, client.right - split - 4, client.bottom, TRUE );
    if (cm.pane) MoveWindow( cm.pane, split + 4, 0, client.right - split - 4, client.bottom, TRUE );
}

static void close_pane( void )
{
    if (cm.pane) DestroyWindow( cm.pane );
    cm.pane = NULL;
}

static BOOL open_pane( void )
{
    RECT client;

    if (!cm.devmgr && (cm.devmgr = LoadLibraryW( L"devmgr.dll" )))
    {
        cm.create_pane = (CREATE_PANE)GetProcAddress( cm.devmgr, "DeviceManager_CreatePane" );
        cm.pane_command = (PANE_COMMAND)GetProcAddress( cm.devmgr, "DeviceManager_PaneCommand" );
    }
    if (!cm.create_pane) return FALSE;
    GetClientRect( cm.hwnd, &client );
    cm.pane = cm.create_pane( cm.hwnd, cm.split + 4, 0, client.right - cm.split - 4, client.bottom );
    if (!cm.pane) return FALSE;
    ShowWindow( cm.pane, SW_SHOW );
    return TRUE;
}

static void select_node( int node )
{
    WCHAR message[900];
    cm.selected = node;
    close_pane();
    ShowWindow( cm.list, SW_HIDE );
    ShowWindow( cm.text, SW_HIDE );
    ShowWindow( cm.svc, SW_HIDE );
    ShowWindow( cm.usr, SW_HIDE );
    switch (nodes[node].kind)
    {
    case USERS:
    case GROUPS:
    {
        DWORD err = usr_show( cm.usr, nodes[node].kind == GROUPS );

        if (!err)
        {
            ShowWindow( cm.usr, SW_SHOW );
            break;
        }
        swprintf( message, ARRAY_SIZE(message), cm_string( IDS_USR_UNAVAILABLE ), usr_error_text( err ) );
        SetWindowTextW( cm.text, message );
        ShowWindow( cm.text, SW_SHOW );
        break;
    }
    case SERVICES:
        svc_refresh( cm.svc );
        ShowWindow( cm.svc, SW_SHOW );
        break;
    case CONTAINER:
        list_children( node );
        ShowWindow( cm.list, SW_SHOW );
        break;
    case HOSTED_DEVMGR:
        if (open_pane()) break;
        SetWindowTextW( cm.text, cm_string( IDS_NO_DEVMGR ) );
        ShowWindow( cm.text, SW_SHOW );
        break;
    default:
        SetWindowTextW( cm.text, cm_string( IDS_REASON_BASE + node ) );
        ShowWindow( cm.text, SW_SHOW );
        break;
    }
    update_menus();
}

static void refresh( void )
{
    if (cm.selected < 0) return;
    if (nodes[cm.selected].kind == SERVICES) svc_refresh( cm.svc );
    else if (nodes[cm.selected].kind == USERS || nodes[cm.selected].kind == GROUPS) select_node( cm.selected );
    else if (cm.pane && cm.pane_command) cm.pane_command( cm.pane, 1 );
    else if (nodes[cm.selected].kind == CONTAINER) list_children( cm.selected );
}

static void create_list_columns( void )
{
    LVCOLUMNW col = {0};

    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx = 200;
    col.pszText = (WCHAR *)cm_string( IDS_COL_NAME );
    SendMessageW( cm.list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col );
    col.cx = 300;
    col.iSubItem = 1;
    col.pszText = (WCHAR *)cm_string( IDS_COL_DESCRIPTION );
    SendMessageW( cm.list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col );
}

static void set_list_view( UINT command )
{
    static const DWORD styles[] = { LVS_ICON, LVS_SMALLICON, LVS_LIST, LVS_REPORT };
    DWORD style = GetWindowLongW( cm.list, GWL_STYLE ) & ~LVS_TYPEMASK;

    SetWindowLongW( cm.list, GWL_STYLE, style | styles[command - IDM_VIEW_LARGE] );
    CheckMenuRadioItem( GetSubMenu( GetMenu( cm.hwnd ), 1 ), IDM_VIEW_LARGE, IDM_VIEW_DETAIL, command, MF_BYCOMMAND );
}

static BOOL over_splitter( int x )
{
    return x >= cm.split && x < cm.split + 4;
}

static LRESULT CALLBACK window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_CREATE:
        cm.hwnd = hwnd;
        cm.selected = -1;
        cm.split = 220;
        create_images();
        cm.tree = CreateWindowExW( WS_EX_CLIENTEDGE, WC_TREEVIEWW, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                   TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0,
                                   hwnd, NULL, compmgmt_module, NULL );
        SendMessageW( cm.tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)cm.images );
        cm.list = CreateWindowExW( WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL, WS_CHILD | WS_TABSTOP | LVS_REPORT |
                                   LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, 0, 0, hwnd, NULL, compmgmt_module, NULL );
        SendMessageW( cm.list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)cm.images );
        create_list_columns();
        cm.svc = svc_create_view( hwnd );
        cm.usr = usr_create_view( hwnd );
        cm.text = CreateWindowExW( WS_EX_CLIENTEDGE, L"STATIC", NULL, WS_CHILD | SS_LEFT, 0, 0, 0, 0, hwnd, NULL,
                                   compmgmt_module, NULL );
        SendMessageW( cm.text, WM_SETFONT, (WPARAM)GetStockObject( DEFAULT_GUI_FONT ), 0 );
        fill_tree();
        CheckMenuRadioItem( GetSubMenu( GetMenu( hwnd ), 1 ), IDM_VIEW_LARGE, IDM_VIEW_DETAIL, IDM_VIEW_DETAIL,
                            MF_BYCOMMAND );
        SendMessageW( cm.tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)cm.items[0] );
        return 0;

    case WM_SIZE:
        layout();
        return 0;

    case WM_SETFOCUS:
        SetFocus( cm.tree );
        return 0;

    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos( &pt );
        ScreenToClient( hwnd, &pt );
        if (LOWORD(lparam) == HTCLIENT && over_splitter( pt.x ))
        {
            SetCursor( LoadCursorW( NULL, (LPCWSTR)IDC_SIZEWE ) );
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN:
        if (over_splitter( (short)LOWORD(lparam) ))
        {
            cm.dragging = TRUE;
            SetCapture( hwnd );
        }
        return 0;

    case WM_MOUSEMOVE:
        if (cm.dragging)
        {
            cm.split = (short)LOWORD(lparam) - 2;
            layout();
        }
        return 0;

    case WM_LBUTTONUP:
        if (cm.dragging)
        {
            cm.dragging = FALSE;
            ReleaseCapture();
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        if ((HWND)lparam == cm.text)
        {
            SetBkColor( (HDC)wparam, GetSysColor( COLOR_WINDOW ) );
            return (LRESULT)GetSysColorBrush( COLOR_WINDOW );
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lparam;

        if (hdr->hwndFrom == cm.tree)
        {
            if (hdr->code == TVN_SELCHANGEDW)
            {
                NMTREEVIEWW *nm = (NMTREEVIEWW *)lparam;
                select_node( (int)nm->itemNew.lParam );
            }
            else if (hdr->code == NM_CUSTOMDRAW)
            {
                NMTVCUSTOMDRAW *draw = (NMTVCUSTOMDRAW *)lparam;

                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    int node = (int)draw->nmcd.lItemlParam;
                    if (node >= 0 && node < (int)NODE_COUNT && nodes[node].kind == MISSING &&
                        !(draw->nmcd.uItemState & CDIS_SELECTED))
                        draw->clrText = GetSysColor( COLOR_GRAYTEXT );
                    return CDRF_NEWFONT;
                }
            }
        }
        else if (hdr->hwndFrom == cm.svc)
        {
            if (hdr->code == NM_DBLCLK || hdr->code == NM_RETURN) svc_command( cm.svc, hwnd, IDM_SVC_PROPERTIES );
            else if (hdr->code == NM_RCLICK)
            {
                POINT pt;
                GetCursorPos( &pt );
                svc_context_menu( cm.svc, hwnd, pt.x, pt.y );
            }
            return 0;
        }
        else if (hdr->hwndFrom == cm.usr)
        {
            if (hdr->code == NM_DBLCLK || hdr->code == NM_RETURN) SendMessageW( hwnd, WM_COMMAND, IDM_USR_PROPERTIES, 0 );
            else if (hdr->code == NM_RCLICK)
            {
                POINT pt;
                GetCursorPos( &pt );
                usr_context_menu( cm.usr, hwnd, pt.x, pt.y );
            }
            else if (hdr->code == LVN_ITEMCHANGED) update_menus();
            return 0;
        }
        else if (hdr->hwndFrom == cm.list && (hdr->code == NM_DBLCLK || hdr->code == NM_RETURN))
        {
            int index = (int)SendMessageW( cm.list, LVM_GETNEXTITEM, -1, LVNI_SELECTED );
            LVITEMW item = {0};

            if (index >= 0)
            {
                item.mask = LVIF_PARAM;
                item.iItem = index;
                SendMessageW( cm.list, LVM_GETITEMW, 0, (LPARAM)&item );
                SendMessageW( cm.tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)cm.items[item.lParam] );
            }
            return 0;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDM_REFRESH: refresh(); break;
        case IDM_SVC_START: case IDM_SVC_STOP: case IDM_SVC_PAUSE: case IDM_SVC_RESUME: case IDM_SVC_RESTART:
            svc_command( cm.svc, hwnd, LOWORD(wparam) );
            break;
        case IDM_PROPERTIES:
            if (cm.selected >= 0 && nodes[cm.selected].kind == SERVICES) svc_command( cm.svc, hwnd, IDM_SVC_PROPERTIES );
            else if (cm.selected >= 0 && (nodes[cm.selected].kind == USERS || nodes[cm.selected].kind == GROUPS))
                SendMessageW( hwnd, WM_COMMAND, IDM_USR_PROPERTIES, 0 );
            break;
        case IDM_USR_NEW: case IDM_USR_DELETE: case IDM_USR_PASSWORD: case IDM_USR_PROPERTIES:
            if (cm.selected >= 0 && (nodes[cm.selected].kind == USERS || nodes[cm.selected].kind == GROUPS) &&
                IsWindow( hwnd ) && usr_command( cm.usr, hwnd, LOWORD(wparam) ) && IsWindow( hwnd ))
                select_node( cm.selected );
            break;
        case IDM_EXIT: DestroyWindow( hwnd ); break;
        case IDM_VIEW_LARGE: case IDM_VIEW_SMALL: case IDM_VIEW_LIST: case IDM_VIEW_DETAIL:
            set_list_view( LOWORD(wparam) );
            break;
        case IDM_DM_BY_TYPE: case IDM_DM_BY_CONNECTION: case IDM_DM_RES_BY_TYPE: case IDM_DM_RES_BY_CONNECTION:
            if (cm.pane && cm.pane_command)
            {
                cm.pane_command( cm.pane, 4 + LOWORD(wparam) - IDM_DM_BY_TYPE );
                CheckMenuRadioItem( GetSubMenu( GetMenu( hwnd ), 1 ), IDM_DM_BY_TYPE, IDM_DM_RES_BY_CONNECTION,
                                    LOWORD(wparam), MF_BYCOMMAND );
            }
            break;
        case IDM_DM_SHOW_HIDDEN:
            if (cm.pane && cm.pane_command)
            {
                BOOL on = !(GetMenuState( GetSubMenu( GetMenu( hwnd ), 1 ), IDM_DM_SHOW_HIDDEN, MF_BYCOMMAND ) & MF_CHECKED);
                cm.pane_command( cm.pane, 3 );
                CheckMenuItem( GetSubMenu( GetMenu( hwnd ), 1 ), IDM_DM_SHOW_HIDDEN, on ? MF_CHECKED : MF_UNCHECKED );
            }
            break;
        case IDM_ABOUT:
            ShellAboutW( hwnd, cm_string( IDS_APPNAME ), NULL, NULL );
            break;
        }
        return 0;

    case WM_INITMENUPOPUP:
        if (!HIWORD(lparam) && (HMENU)wparam == GetSubMenu( GetMenu( hwnd ), 0 ))
        {
            update_action_menu( (HMENU)wparam );
            update_users_menu( (HMENU)wparam );
        }
        break;

    case WM_CONTEXTMENU:
        if ((HWND)wparam == cm.svc && (short)LOWORD(lparam) == -1) svc_context_menu( cm.svc, hwnd, -1, -1 );
        if ((HWND)wparam == cm.usr && (short)LOWORD(lparam) == -1) usr_context_menu( cm.usr, hwnd, -1, -1 );
        return 0;

    case WM_DESTROY:
        svc_free( cm.svc );
        usr_free( cm.usr );
        close_pane();
        ImageList_Destroy( cm.images );
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

/***********************************************************************
 *              CompMgmt_ExecuteW (COMPMGMT.@)
 */
BOOL WINAPI CompMgmt_ExecuteW( HWND parent, HINSTANCE instance, const WCHAR *machine, int show )
{
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    WNDCLASSEXW wc = { sizeof(wc) };
    HWND hwnd;
    MSG msg;

    TRACE( "%p %p %s %d\n", parent, instance, debugstr_w(machine), show );
    if (machine && machine[0])
    {
        FIXME( "remote computers are not supported\n" );
        SetLastError( ERROR_NOT_SUPPORTED );
        return FALSE;
    }
    InitCommonControlsEx( &icex );
    wc.lpfnWndProc = window_proc;
    wc.hInstance = compmgmt_module;
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDM_MAIN);
    wc.lpszClassName = L"MMCMainFrame";
    RegisterClassExW( &wc );

    memset( &cm, 0, sizeof(cm) );
    hwnd = CreateWindowExW( 0, wc.lpszClassName, cm_string( IDS_APPNAME ), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, 800, 560, parent, NULL, compmgmt_module, NULL );
    if (!hwnd) return FALSE;
    ShowWindow( hwnd, show ? show : SW_SHOWNORMAL );
    UpdateWindow( hwnd );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    if (cm.devmgr) FreeLibrary( cm.devmgr );
    return TRUE;
}

/***********************************************************************
 *              CompMgmt_ExecuteA (COMPMGMT.@)
 */
BOOL WINAPI CompMgmt_ExecuteA( HWND parent, HINSTANCE instance, const char *machine, int show )
{
    WCHAR name[MAX_PATH];

    if (!machine) return CompMgmt_ExecuteW( parent, instance, NULL, show );
    MultiByteToWideChar( CP_ACP, 0, machine, -1, name, MAX_PATH );
    return CompMgmt_ExecuteW( parent, instance, name, show );
}

BOOL WINAPI DllMain( HINSTANCE instance, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        compmgmt_module = instance;
        DisableThreadLibraryCalls( instance );
    }
    return TRUE;
}
