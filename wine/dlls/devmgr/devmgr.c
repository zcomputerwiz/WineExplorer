/*
 * Device Manager - the window (as the Microsoft Management Console shows the snap-in on Windows XP)
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

#include "devmgr.h"
#include "resource.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(devmgr);

enum { VIEW_BY_TYPE, VIEW_BY_CONNECTION, VIEW_RES_BY_TYPE, VIEW_RES_BY_CONNECTION };

/* tree item lParam: 0 the computer, a small number a class (1 + CLASS_*) or a resource type (100 + RES_*),
 * else the device */
#define ITEM_CLASS(cls)   ((LPARAM)(1 + (cls)))
#define ITEM_RESTYPE(t)   ((LPARAM)(100 + (t)))
#define IS_DEVICE(param)  ((param) >= 0x10000)

/* toolbar images: Back, Forward, Console Tree, Properties, Print, Help, Action Pane, then Scan, Enable, Uninstall,
 * Disable, Update Driver; where they are in the genuine strips (mmc.exe 202, devmgr.dll 358) and in ours */
enum { TB_BACK, TB_FORWARD, TB_TREE, TB_PROPERTIES, TB_PRINT, TB_HELP, TB_ACTION, TB_SCAN, TB_ENABLE, TB_UNINSTALL,
       TB_DISABLE, TB_UPDATE, TB_COUNT };
static const int genuine_images[TB_COUNT] = { 0, 1, 10, 8, 12, 15, 17, 19, 20, 21, 22, 23 };

static struct
{
    HWND hwnd, tree, toolbar, status;
    struct hardware *hw;
    int view;
    BOOL show_hidden;
    HIMAGELIST images;
    int image_root, image_resource;
} dm;

static const struct { int image; UINT command; } buttons[] =
{
    { TB_BACK, IDM_BACK }, { TB_FORWARD, IDM_FORWARD }, { -1, 0 }, { TB_TREE, IDM_CONSOLE_TREE }, { -1, 1 },
    { TB_PROPERTIES, IDM_PROPERTIES }, { TB_PRINT, IDM_PRINT }, { -1, 2 }, { TB_HELP, IDM_HELP },
    { TB_ACTION, IDM_ACTION_PANE }, { -1, 3 }, { TB_SCAN, IDM_SCAN }, { -1, 4 }, { TB_UPDATE, IDM_UPDATE },
    { TB_UNINSTALL, IDM_UNINSTALL }, { TB_DISABLE, IDM_DISABLE },
};

static BOOL visible( const struct device *dev )
{
    return dev->cls >= 0 && !dev->owner && (dm.show_hidden || !dev->hidden);
}

/* the device above dev in the tree: hidden ones are skipped while they are not shown */
static struct device *visible_parent( struct device *dev )
{
    struct device *parent = dev->parent;
    while (parent && parent != dm.hw->computer && !visible( parent )) parent = parent->parent;
    return parent ? parent : dm.hw->computer;
}

static HTREEITEM add_item( HTREEITEM parent, const WCHAR *text, int image, LPARAM param, BOOL problem )
{
    TVINSERTSTRUCTW insert = {0};

    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_STATE;
    insert.item.pszText = (WCHAR *)text;
    insert.item.iImage = insert.item.iSelectedImage = image;
    insert.item.lParam = param;
    insert.item.stateMask = TVIS_OVERLAYMASK;
    insert.item.state = problem ? INDEXTOOVERLAYMASK( 1 ) : 0;
    return (HTREEITEM)SendMessageW( dm.tree, TVM_INSERTITEMW, 0, (LPARAM)&insert );
}

static int __cdecl compare_names( const void *a, const void *b )
{
    const struct device *const *x = a, *const *y = b;
    return lstrcmpiW( (*x)->name, (*y)->name );
}

/* the visible devices of a class (cls >= 0) or below a device (parent), sorted by name */
static unsigned int collect( struct device **list, int cls, struct device *parent )
{
    unsigned int i, count = 0;

    for (i = 0; i < dm.hw->count; i++)
    {
        struct device *dev = &dm.hw->devs[i];
        if (!visible( dev ) || dev == parent) continue;
        if (cls >= 0 ? dev->cls != cls : visible_parent( dev ) != parent || dev == dm.hw->computer) continue;
        list[count++] = dev;
    }
    qsort( list, count, sizeof(*list), compare_names );
    return count;
}

static void add_children( HTREEITEM item, struct device *parent, struct device **list )
{
    unsigned int i, count = collect( list, -1, parent );
    struct device **mine = malloc( count * sizeof(*mine) );

    if (!mine) return;
    memcpy( mine, list, count * sizeof(*mine) );
    for (i = 0; i < count; i++)
    {
        HTREEITEM child = add_item( item, mine[i]->name, mine[i]->cls, (LPARAM)mine[i], mine[i]->problem != 0 );
        add_children( child, mine[i], list );
        SendMessageW( dm.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)child );
    }
    free( mine );
}

static const WCHAR *resource_text( const struct resource *res, const struct device *dev, WCHAR *buffer, DWORD size )
{
    switch (res->type)
    {
    case RES_IRQ: swprintf( buffer, size, L"(%s) %I64u  %s", res->start < 16 ? L"ISA" : L"PCI", res->start, dev->name ); break;
    case RES_DMA: swprintf( buffer, size, L"[%I64u]  %s", res->start, dev->name ); break;
    default: swprintf( buffer, size, L"[%08I64X - %08I64X]  %s", res->start, res->end, dev->name ); break;
    }
    return buffer;
}

struct res_entry { const struct resource *res; struct device *dev; };

static int __cdecl compare_resources( const void *a, const void *b )
{
    const struct res_entry *x = a, *y = b;
    if (x->res->start != y->res->start) return x->res->start < y->res->start ? -1 : 1;
    return lstrcmpiW( x->dev->name, y->dev->name );
}

/* ponytail: "Resources by connection" lists them like "by type"; nesting ranges in bridge windows needs the
 * windows, which sysfs does not give without privileges */
static void add_resources( HTREEITEM root )
{
    static const int order[] = { RES_DMA, RES_IO, RES_IRQ, RES_MEMORY };
    static const UINT names[] = { IDS_RESTYPE_DMA, IDS_RESTYPE_IO, IDS_RESTYPE_IRQ, IDS_RESTYPE_MEMORY };
    unsigned int i, j, k, count;
    struct res_entry *entries;
    WCHAR text[512];

    for (i = 0; i < ARRAY_SIZE(order); i++)
    {
        HTREEITEM type;

        for (j = count = 0; j < dm.hw->count; j++)
            if (visible( &dm.hw->devs[j] ))
                for (k = 0; k < dm.hw->devs[j].res_count; k++) count += dm.hw->devs[j].res[k].type == order[i];
        if (!count || !(entries = malloc( count * sizeof(*entries) ))) continue;
        for (j = count = 0; j < dm.hw->count; j++)
        {
            struct device *dev = &dm.hw->devs[j];
            if (!visible( dev )) continue;
            for (k = 0; k < dev->res_count; k++)
                if (dev->res[k].type == order[i]) { entries[count].res = &dev->res[k]; entries[count++].dev = dev; }
        }
        qsort( entries, count, sizeof(*entries), compare_resources );
        type = add_item( root, load_string( names[i] ), dm.image_resource, ITEM_RESTYPE( order[i] ), FALSE );
        for (j = 0; j < count; j++)
            add_item( type, resource_text( entries[j].res, entries[j].dev, text, ARRAY_SIZE(text) ),
                      entries[j].dev->cls, (LPARAM)entries[j].dev, entries[j].dev->problem != 0 );
        free( entries );
    }
}

static void fill_tree( void )
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = ARRAY_SIZE(name);
    struct device **list;
    HTREEITEM root;
    int cls;

    SendMessageW( dm.tree, WM_SETREDRAW, FALSE, 0 );
    SendMessageW( dm.tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT );
    if (!GetComputerNameW( name, &size )) name[0] = 0;
    root = add_item( TVI_ROOT, name, dm.image_root, 0, FALSE );
    if (!dm.hw || !(list = malloc( (dm.hw->count + 1) * sizeof(*list) ))) goto done;

    switch (dm.view)
    {
    case VIEW_BY_TYPE:
    {
        int order[CLASS_COUNT], i, j;
        WCHAR names[CLASS_COUNT][128];

        for (i = 0; i < CLASS_COUNT; i++)
        {
            order[i] = i;
            lstrcpynW( names[i], load_string( IDS_CLASS_BASE + i ), ARRAY_SIZE(names[i]) );
        }
        for (i = 1; i < CLASS_COUNT; i++)   /* by name */
            for (j = i; j > 0 && lstrcmpiW( names[order[j - 1]], names[order[j]] ) > 0; j--)
            { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
        for (i = 0; i < CLASS_COUNT; i++)
        {
            unsigned int k, count;
            HTREEITEM item;
            BOOL problem = FALSE;

            cls = order[i];
            if (!(count = collect( list, cls, NULL ))) continue;
            for (k = 0; k < count; k++) problem |= list[k]->problem != 0;
            item = add_item( root, names[cls], cls, ITEM_CLASS( cls ), FALSE );
            for (k = 0; k < count; k++) add_item( item, list[k]->name, cls, (LPARAM)list[k], list[k]->problem != 0 );
            if (problem) SendMessageW( dm.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)item );
        }
        break;
    }
    case VIEW_BY_CONNECTION:
    {
        HTREEITEM item = add_item( root, dm.hw->computer->name, CLASS_COMPUTER, (LPARAM)dm.hw->computer, FALSE );
        add_children( item, dm.hw->computer, list );
        SendMessageW( dm.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)item );
        break;
    }
    default:
        add_resources( root );
        break;
    }
    free( list );
done:
    SendMessageW( dm.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)root );
    SendMessageW( dm.tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)root );
    SendMessageW( dm.tree, WM_SETREDRAW, TRUE, 0 );
}

static LPARAM selection( void )
{
    TVITEMW item = {0};

    item.mask = TVIF_PARAM;
    if (!(item.hItem = (HTREEITEM)SendMessageW( dm.tree, TVM_GETNEXTITEM, TVGN_CARET, 0 ))) return 0;
    SendMessageW( dm.tree, TVM_GETITEMW, 0, (LPARAM)&item );
    return item.lParam;
}

/* what the selection allows: Windows XP shows the device commands only for devices, Properties for devices and
 * classes; Update Driver, Disable and Uninstall are the kernel's business on Linux (grayed) */
static void update_commands( void )
{
    LPARAM param = selection();
    BOOL device = IS_DEVICE( param ), cls = param > 0 && param < 100, any = param != 0;
    HMENU menu = GetSubMenu( GetMenu( dm.hwnd ), 1 );

    SendMessageW( dm.toolbar, TB_HIDEBUTTON, IDM_PROPERTIES, !(device || cls) );
    SendMessageW( dm.toolbar, TB_HIDEBUTTON, IDM_PRINT, !any );
    SendMessageW( dm.toolbar, TB_HIDEBUTTON, IDM_SCAN, FALSE );
    SendMessageW( dm.toolbar, TB_HIDEBUTTON, IDM_UPDATE, !device );
    SendMessageW( dm.toolbar, TB_HIDEBUTTON, IDM_UNINSTALL, !device );
    SendMessageW( dm.toolbar, TB_HIDEBUTTON, IDM_DISABLE, !device );
    SendMessageW( dm.toolbar, TB_HIDEBUTTON, 0xff02, !any );      /* after Properties, Print */
    SendMessageW( dm.toolbar, TB_HIDEBUTTON, 0xff04, !device );   /* before Update Driver */
    SendMessageW( dm.toolbar, TB_ENABLEBUTTON, IDM_PROPERTIES, device );
    SendMessageW( dm.toolbar, TB_ENABLEBUTTON, IDM_PRINT, FALSE );

    while (GetMenuItemCount( menu )) DeleteMenu( menu, 0, MF_BYPOSITION );
    if (device)
    {
        AppendMenuW( menu, MF_STRING | MF_GRAYED, IDM_UPDATE, load_string( IDS_LABEL_BASE + IDM_UPDATE - 1000 ) );
        AppendMenuW( menu, MF_STRING | MF_GRAYED, IDM_DISABLE, load_string( IDS_LABEL_BASE + IDM_DISABLE - 1000 ) );
        AppendMenuW( menu, MF_STRING | MF_GRAYED, IDM_UNINSTALL, load_string( IDS_LABEL_BASE + IDM_UNINSTALL - 1000 ) );
        AppendMenuW( menu, MF_SEPARATOR, 0, NULL );
    }
    AppendMenuW( menu, MF_STRING, IDM_SCAN, load_string( IDS_LABEL_BASE + IDM_SCAN - 1000 ) );
    if (!any)
        AppendMenuW( menu, MF_STRING | MF_GRAYED, IDM_ADD_LEGACY, load_string( IDS_LABEL_BASE + IDM_ADD_LEGACY - 1000 ) );
    AppendMenuW( menu, MF_SEPARATOR, 0, NULL );
    AppendMenuW( menu, MF_STRING | MF_GRAYED, IDM_PRINT, load_string( IDS_LABEL_BASE + IDM_PRINT - 1000 ) );
    if (device || cls)
    {
        AppendMenuW( menu, MF_SEPARATOR, 0, NULL );
        AppendMenuW( menu, MF_STRING | (device ? 0 : MF_GRAYED), IDM_PROPERTIES,
                     load_string( IDS_LABEL_BASE + IDM_PROPERTIES - 1000 ) );
        SetMenuDefaultItem( menu, device ? IDM_PROPERTIES : -1, FALSE );
    }
    AppendMenuW( menu, MF_SEPARATOR, 0, NULL );
    AppendMenuW( menu, MF_STRING | MF_GRAYED, IDM_HELP, load_string( IDS_LABEL_BASE + IDM_HELP - 1000 ) );
}

static void refresh( void )
{
    HCURSOR cursor = SetCursor( LoadCursorW( NULL, (LPCWSTR)IDC_WAIT ) );
    struct hardware *old = dm.hw;

    dm.hw = hardware_load();
    fill_tree();
    hardware_free( old );
    update_commands();
    SetCursor( cursor );
}

static void set_view( int view )
{
    dm.view = view;
    CheckMenuRadioItem( GetMenu( dm.hwnd ), IDM_BY_TYPE, IDM_RES_BY_CONNECTION, IDM_BY_TYPE + view, MF_BYCOMMAND );
    fill_tree();
    update_commands();
}

static void show_properties( void )
{
    LPARAM param = selection();
    if (IS_DEVICE( param )) device_properties( dm.hwnd, dm.hw, (struct device *)param );
}

static void create_images( void )
{
    HBITMAP bitmap;
    HMODULE pack = xp_resources( L"devmgr" );
    int i;

    dm.images = ImageList_Create( 16, 16, ILC_COLOR32 | ILC_MASK, CLASS_COUNT + 4, 4 );
    for (i = 0; i < CLASS_COUNT; i++)
    {
        HICON icon = class_icon( i, 16 );
        ImageList_AddIcon( dm.images, icon );
        DestroyIcon( icon );
    }
    bitmap = pack ? LoadImageW( pack, MAKEINTRESOURCEW(IDB_SYSDM16), IMAGE_BITMAP, 0, 0, 0 ) : NULL;
    if (!bitmap) bitmap = LoadImageW( devmgr_instance, MAKEINTRESOURCEW(IDB_SYSDM16), IMAGE_BITMAP, 0, 0, 0 );
    dm.image_root = ImageList_AddMasked( dm.images, bitmap, RGB( 255, 0, 255 ) );
    DeleteObject( bitmap );
    {
        HMODULE setupapi = xp_resources( L"setupapi" );
        HICON icon = NULL;
        if (pack) icon = LoadImageW( pack, MAKEINTRESOURCEW(IDI_RESOURCES), IMAGE_ICON, 16, 16, 0 );
        if (!icon) icon = LoadImageW( devmgr_instance, MAKEINTRESOURCEW(IDI_RESOURCES), IMAGE_ICON, 16, 16, 0 );
        dm.image_resource = ImageList_AddIcon( dm.images, icon );
        DestroyIcon( icon );
        /* the problem overlay: the yellow exclamation mark */
        icon = setupapi ? LoadImageW( setupapi, MAKEINTRESOURCEW(500), IMAGE_ICON, 16, 16, 0 ) : NULL;
        if (!icon) icon = LoadImageW( devmgr_instance, MAKEINTRESOURCEW(500), IMAGE_ICON, 16, 16, 0 );
        ImageList_SetOverlayImage( dm.images, ImageList_AddIcon( dm.images, icon ), 1 );
        DestroyIcon( icon );
    }
}

static void create_toolbar( HWND hwnd )
{
    HMODULE mmc = xp_resources( L"mmc" ), pack = xp_resources( L"devmgr" );
    HBITMAP frame = mmc ? LoadImageW( mmc, MAKEINTRESOURCEW(202), IMAGE_BITMAP, 0, 0, 0 ) : NULL;
    HBITMAP actions = pack ? LoadImageW( pack, MAKEINTRESOURCEW(IDB_TOOLBAR), IMAGE_BITMAP, 0, 0, 0 ) : NULL;
    HIMAGELIST list = ImageList_Create( 16, 16, ILC_COLOR24 | ILC_MASK, TB_COUNT + 19, 0 );
    TBBUTTON tb[ARRAY_SIZE(buttons)] = {{0}};
    BOOL genuine = frame && actions;
    unsigned int i;

    if (genuine)
    {
        ImageList_AddMasked( list, frame, RGB( 255, 0, 255 ) );
        ImageList_AddMasked( list, actions, RGB( 255, 0, 255 ) );
    }
    else
    {
        HBITMAP own = LoadImageW( devmgr_instance, MAKEINTRESOURCEW(IDB_TOOLBAR), IMAGE_BITMAP, 0, 0, 0 );
        ImageList_AddMasked( list, own, RGB( 255, 0, 255 ) );
        DeleteObject( own );
    }
    if (frame) DeleteObject( frame );
    if (actions) DeleteObject( actions );

    dm.toolbar = CreateWindowExW( 0, TOOLBARCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
                                  CCS_TOP | CCS_NODIVIDER, 0, 0, 0, 0, hwnd, NULL, devmgr_instance, NULL );
    SendMessageW( dm.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0 );
    SendMessageW( dm.toolbar, TB_SETIMAGELIST, 0, (LPARAM)list );
    for (i = 0; i < ARRAY_SIZE(buttons); i++)
    {
        if (buttons[i].image < 0)
        {
            tb[i].fsStyle = BTNS_SEP;
            tb[i].idCommand = 0xff00 + buttons[i].command;
            continue;
        }
        tb[i].iBitmap = genuine ? genuine_images[buttons[i].image] : buttons[i].image;
        tb[i].idCommand = buttons[i].command;
        tb[i].fsState = TBSTATE_ENABLED;
        tb[i].fsStyle = BTNS_BUTTON;
        tb[i].iString = (INT_PTR)load_string( IDS_LABEL_BASE + buttons[i].command - 1000 );
    }
    SendMessageW( dm.toolbar, TB_ADDBUTTONSW, ARRAY_SIZE(tb), (LPARAM)tb );
    SendMessageW( dm.toolbar, TB_SETMAXTEXTROWS, 0, 0 );
    for (i = 0; i < ARRAY_SIZE(buttons); i++)   /* what does nothing here */
        if (buttons[i].command == IDM_BACK || buttons[i].command == IDM_FORWARD || buttons[i].command == IDM_HELP ||
            buttons[i].command == IDM_ACTION_PANE || buttons[i].command == IDM_CONSOLE_TREE ||
            buttons[i].command == IDM_UPDATE || buttons[i].command == IDM_UNINSTALL || buttons[i].command == IDM_DISABLE)
            SendMessageW( dm.toolbar, TB_ENABLEBUTTON, buttons[i].command, FALSE );
    SendMessageW( dm.toolbar, TB_AUTOSIZE, 0, 0 );
}

static void layout( HWND hwnd )
{
    RECT client, bar, status;

    GetClientRect( hwnd, &client );
    SendMessageW( dm.toolbar, TB_AUTOSIZE, 0, 0 );
    SendMessageW( dm.status, WM_SIZE, 0, 0 );
    GetWindowRect( dm.toolbar, &bar );
    GetWindowRect( dm.status, &status );
    {
        int parts[3], width = client.right;
        parts[0] = width * 64 / 100;
        parts[1] = width * 84 / 100;
        parts[2] = -1;
        SendMessageW( dm.status, SB_SETPARTS, 3, (LPARAM)parts );
    }
    /* the etched line under the toolbar is the frame's */
    MoveWindow( dm.tree, 0, bar.bottom - bar.top + 2, client.right,
                client.bottom - (bar.bottom - bar.top + 2) - (status.bottom - status.top), TRUE );
}

static void context_menu( HWND hwnd, int x, int y )
{
    HMENU menu = GetSubMenu( GetMenu( hwnd ), 1 );

    if (x == -1 && y == -1)
    {
        RECT rect;
        HTREEITEM item = (HTREEITEM)SendMessageW( dm.tree, TVM_GETNEXTITEM, TVGN_CARET, 0 );
        *(HTREEITEM *)&rect = item;
        if (!item || !SendMessageW( dm.tree, TVM_GETITEMRECT, TRUE, (LPARAM)&rect )) return;
        MapWindowPoints( dm.tree, NULL, (POINT *)&rect, 2 );
        x = rect.left;
        y = rect.bottom;
    }
    TrackPopupMenu( menu, TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL );
}

static LRESULT CALLBACK window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_CREATE:
        dm.hwnd = hwnd;
        create_toolbar( hwnd );
        dm.status = CreateWindowExW( 0, STATUSCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                     hwnd, NULL, devmgr_instance, NULL );
        dm.tree = CreateWindowExW( WS_EX_CLIENTEDGE, WC_TREEVIEWW, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                   TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                                   0, 0, 0, 0, hwnd, NULL, devmgr_instance, NULL );
        create_images();
        SendMessageW( dm.tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)dm.images );
        CheckMenuRadioItem( GetMenu( hwnd ), IDM_BY_TYPE, IDM_RES_BY_CONNECTION, IDM_BY_TYPE, MF_BYCOMMAND );
        PostMessageW( hwnd, WM_COMMAND, IDM_SCAN, 0 );
        return 0;

    case WM_SIZE:
        layout( hwnd );
        return 0;

    case WM_SETFOCUS:
        SetFocus( dm.tree );
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT bar, rect;
        HDC hdc = BeginPaint( hwnd, &ps );
        GetWindowRect( dm.toolbar, &bar );
        GetClientRect( hwnd, &rect );
        rect.top = bar.bottom - bar.top;
        rect.bottom = rect.top + 2;
        DrawEdge( hdc, &rect, EDGE_ETCHED, BF_TOP );
        EndPaint( hwnd, &ps );
        return 0;
    }

    case WM_MENUSELECT:
    {
        UINT id = LOWORD(wparam);
        if (HIWORD(wparam) == 0xffff && !lparam) SendMessageW( dm.status, SB_SIMPLE, FALSE, 0 );
        else
        {
            SendMessageW( dm.status, SB_SIMPLE, TRUE, 0 );
            SendMessageW( dm.status, SB_SETTEXTW, 255 | SBT_NOBORDERS,
                          (LPARAM)(id >= 1000 && !(HIWORD(wparam) & MF_POPUP) ? load_string( IDS_HINT_BASE + id - 1000 ) : L"") );
        }
        return 0;
    }

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lparam;
        if (hdr->hwndFrom == dm.tree)
        {
            switch (hdr->code)
            {
            case TVN_SELCHANGEDW: update_commands(); break;
            case NM_DBLCLK:
            case NM_RETURN: show_properties(); return 1;
            case NM_RCLICK:
            {
                TVHITTESTINFO hit = {{0}};
                GetCursorPos( &hit.pt );
                ScreenToClient( dm.tree, &hit.pt );
                if (SendMessageW( dm.tree, TVM_HITTEST, 0, (LPARAM)&hit ))
                    SendMessageW( dm.tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)hit.hItem );
                GetCursorPos( &hit.pt );
                context_menu( hwnd, hit.pt.x, hit.pt.y );
                return 1;
            }
            }
        }
        else if (hdr->code == TTN_GETDISPINFOW)
        {
            NMTTDISPINFOW *info = (NMTTDISPINFOW *)lparam;
            lstrcpynW( info->szText, load_string( IDS_LABEL_BASE + hdr->idFrom - 1000 ), ARRAY_SIZE(info->szText) );
        }
        break;
    }

    case WM_CONTEXTMENU:
        if ((HWND)wparam == dm.tree && (short)LOWORD(lparam) == -1) context_menu( hwnd, -1, -1 );
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDM_EXIT: DestroyWindow( hwnd ); break;
        case IDM_SCAN: refresh(); break;
        case IDM_PROPERTIES: show_properties(); break;
        case IDM_BY_TYPE: case IDM_BY_CONNECTION: case IDM_RES_BY_TYPE: case IDM_RES_BY_CONNECTION:
            set_view( LOWORD(wparam) - IDM_BY_TYPE );
            break;
        case IDM_SHOW_HIDDEN:
            dm.show_hidden = !dm.show_hidden;
            CheckMenuItem( GetMenu( hwnd ), IDM_SHOW_HIDDEN, dm.show_hidden ? MF_CHECKED : MF_UNCHECKED );
            fill_tree();
            update_commands();
            break;
        case IDM_ABOUT:
        {
            HICON icon = LoadImageW( devmgr_instance, MAKEINTRESOURCEW(IDI_DEVMGR), IMAGE_ICON, 32, 32, 0 );
            ShellAboutW( hwnd, load_string( IDS_APPNAME ), NULL, icon );
            DestroyIcon( icon );
            break;
        }
        }
        return 0;

    case WM_DESTROY:
        hardware_free( dm.hw );
        dm.hw = NULL;
        ImageList_Destroy( dm.images );
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static HICON load_app_icon( int size )
{
    HMODULE pack = xp_resources( L"devmgr" );
    HICON icon = pack ? LoadImageW( pack, MAKEINTRESOURCEW(IDI_DEVMGR), IMAGE_ICON, size, size, 0 ) : NULL;
    return icon ? icon : LoadImageW( devmgr_instance, MAKEINTRESOURCEW(IDI_DEVMGR), IMAGE_ICON, size, size, 0 );
}

/***********************************************************************
 *              DeviceManager_ExecuteW (DEVMGR.@)
 */
BOOL WINAPI DeviceManager_ExecuteW( HWND parent, HINSTANCE instance, const WCHAR *machine, int show )
{
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES };
    WNDCLASSEXW wc = { sizeof(wc) };
    HACCEL accel;
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
    wc.hInstance = devmgr_instance;
    wc.hIcon = load_app_icon( 32 );
    wc.hIconSm = load_app_icon( 16 );
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDM_MAIN);
    wc.lpszClassName = L"DevMgrMMCMainFrame";
    RegisterClassExW( &wc );

    memset( &dm, 0, sizeof(dm) );
    hwnd = CreateWindowExW( 0, wc.lpszClassName, load_string( IDS_APPNAME ), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, 640, 450, parent, NULL, devmgr_instance, NULL );
    if (!hwnd) return FALSE;
    ShowWindow( hwnd, show ? show : SW_SHOWNORMAL );
    UpdateWindow( hwnd );
    accel = LoadAcceleratorsW( devmgr_instance, MAKEINTRESOURCEW(IDM_MAIN) );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        if (accel && TranslateAcceleratorW( hwnd, accel, &msg )) continue;
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return TRUE;
}

/***********************************************************************
 *              DeviceManager_ExecuteA (DEVMGR.@)
 */
BOOL WINAPI DeviceManager_ExecuteA( HWND parent, HINSTANCE instance, const char *machine, int show )
{
    WCHAR name[MAX_PATH];

    if (!machine) return DeviceManager_ExecuteW( parent, instance, NULL, show );
    MultiByteToWideChar( CP_ACP, 0, machine, -1, name, MAX_PATH );
    return DeviceManager_ExecuteW( parent, instance, name, show );
}

BOOL WINAPI DllMain( HINSTANCE instance, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        devmgr_instance = instance;
        DisableThreadLibraryCalls( instance );
    }
    return TRUE;
}
