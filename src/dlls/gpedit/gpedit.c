/*
 * gpedit: the Local Group Policy editor (gpedit.msc), hosted by mmc.exe
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * docs/GROUP_POLICY.md, "Local policy". A two-pane window, as the Device Manager's (devmgr): the tree of Computer
 * Configuration and User Configuration with their Administrative Templates, and the settings of the chosen folder
 * with their state; a setting's Properties are three choices (Not Configured, Enabled, Disabled), a value for a
 * number, and an Explain page. The settings are the table of policies.c; the files are the local object's
 * Registry.pol of the computer and of the user, read from session 0's root by anyone and written through gpsvc by
 * a process of the system group (wexgpclient.h): without administrator rights the window shows the settings and
 * offers to open again elevated when a change is made.
 */

#define SNDMSG SendMessageW   /* Wine's headers want the W form named; the list and tree macros send through this */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commctrl.h>
#include <prsht.h>
#include <shellapi.h>

#include "resource.h"
#include "wexgpclient.h"
#include "model.h"
#include "policies.h"

#define MAX_NODES 64

struct node
{
    enum policy_scope scope;
    WCHAR category[256];
};

static HINSTANCE instance;
static HWND frame, tree, list, status;
static struct model *models[2];
static struct node nodes[MAX_NODES];
static unsigned int node_count;
static int split = 230;
static int icon_folder, icon_open;   /* the shell's folder icons in the system image list */

static void to_wide( const char *text, WCHAR *out, int count )
{
    if (!MultiByteToWideChar( CP_UTF8, 0, text, -1, out, count )) out[0] = 0;
}

static const WCHAR *scope_name( enum policy_scope scope )
{
    return scope == POLICY_COMPUTER ? L"Computer Configuration" : L"User Configuration";
}

/* ---------------------------------------------------------------------------------------------------- the files */

static BOOL store_path( enum policy_scope scope, WCHAR *path, DWORD chars )
{
    UINT n = GetSystemDirectoryW( path, chars );

    if (!n || n + 40 > chars) return FALSE;
    wcscat( path, scope == POLICY_COMPUTER ? L"\\GroupPolicy\\Machine\\Registry.pol" : L"\\GroupPolicy\\User\\Registry.pol" );
    return TRUE;
}

/* the local object's file of a scope into its model; no file is an empty model, a file that is not a policy file is
 * reported and also leaves an empty one */
static void load_scope( enum policy_scope scope )
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD size, done;
    BYTE *data;
    int ret = 0;

    if (!models[scope]) models[scope] = model_new();
    model_load( models[scope], NULL, 0 );
    if (!store_path( scope, path, ARRAY_SIZE(path) )) return;
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return;
    size = GetFileSize( file, NULL );
    if (size != INVALID_FILE_SIZE && size && size <= WEX_GP_REQUEST_LIMIT && (data = malloc( size )))
    {
        if (ReadFile( file, data, size, &done, NULL ) && done == size) ret = model_load( models[scope], data, size );
        else ret = -1;
        free( data );
    }
    CloseHandle( file );
    if (ret)
        MessageBoxW( frame, L"The local policy file is not a valid policy file and is ignored.", L"Group Policy", MB_OK | MB_ICONWARNING );
}

/* ---------------------------------------------------------------------------------------------------- the states */

static void state_text( const struct policy *policy, WCHAR *text, int chars )
{
    enum setting_state state;
    uint32_t number;

    if (policy->status == POLICY_NOT_APPLICABLE)
    {
        lstrcpynW( text, L"Not applicable", chars );
        return;
    }
    model_get( models[policy->scope], policy, &state, &number );
    if (state == SETTING_NOT_CONFIGURED) lstrcpynW( text, L"Not configured", chars );
    else if (state == SETTING_DISABLED) lstrcpynW( text, L"Disabled", chars );
    else if (policy->kind == POLICY_NUMBER) swprintf( text, chars, L"Enabled (%lu)", (unsigned long)number );
    else lstrcpynW( text, L"Enabled", chars );
}

static void support_text( const struct policy *policy, WCHAR *text, int chars )
{
    WCHAR reason[200];

    switch (policy->status)
    {
    case POLICY_APPLIES:
        lstrcpynW( text, L"This setting is acted on by this system.", chars );
        break;
    case POLICY_STORED:
        lstrcpynW( text, L"This setting is stored. Programs that ask the shell for restrictions see it; the shell of "
                         L"this system does not carry it out itself.", chars );
        break;
    default:
        to_wide( policy->reason ? policy->reason : "", reason, ARRAY_SIZE(reason) );
        swprintf( text, chars, L"Not applicable: %s.", reason );
    }
}

/* ---------------------------------------------------------------------------------------------------- saving */

/* a change needs the system group: open the same console again with an administrator's rights */
static void offer_elevation( HWND owner )
{
    if (MessageBoxW( owner, L"Changing the local policy needs the rights of an administrator.\n\n"
                            L"Open Group Policy again as an administrator?", L"Group Policy", MB_YESNO | MB_ICONQUESTION ) != IDYES)
        return;
    if ((INT_PTR)ShellExecuteW( owner, L"runas", L"mmc.exe", L"gpedit.msc", NULL, SW_SHOWNORMAL ) > 32) DestroyWindow( frame );
}

static void report_error( HWND owner, DWORD err, const WCHAR *message )
{
    WCHAR text[600], system[300];

    if (err == ERROR_ACCESS_DENIED) { offer_elevation( owner ); return; }
    if (err == ERROR_INVALID_DATA && message[0]) swprintf( text, ARRAY_SIZE(text), L"The change was not accepted.\n\n%s", message );
    else if (err == ERROR_TIMEOUT)
        lstrcpynW( text, L"The change was not taken, or it did not take effect in time: the policy service is not "
                         L"running, or the local policy store is not set up for it.", ARRAY_SIZE(text) );
    else
    {
        if (!FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, system, ARRAY_SIZE(system), NULL ))
            swprintf( system, ARRAY_SIZE(system), L"Error %lu.", (unsigned long)err );
        swprintf( text, ARRAY_SIZE(text), L"The change could not be saved.\n\n%s", system );
    }
    MessageBoxW( owner, text, L"Group Policy", MB_OK | MB_ICONERROR );
}

/* the model of a scope goes to gpsvc; the model is read again afterwards either way (a refused change is dropped) */
static BOOL save_scope( HWND owner, enum policy_scope scope )
{
    uint8_t *data;
    size_t size;
    WCHAR message[260];
    DWORD err;
    HCURSOR old;

    if (model_save( models[scope], &data, &size ))
    {
        report_error( owner, ERROR_NOT_ENOUGH_MEMORY, L"" );
        load_scope( scope );
        return FALSE;
    }
    old = SetCursor( LoadCursorW( NULL, (LPCWSTR)IDC_WAIT ) );
    err = wex_gp_store_write( scope == POLICY_COMPUTER ? WEX_GP_MACHINE_POL : WEX_GP_USER_POL, data, (DWORD)size, 120,
                              message, ARRAY_SIZE(message) );
    SetCursor( old );
    free( data );
    load_scope( scope );
    if (err) report_error( owner, err, message );
    return !err;
}

/* ---------------------------------------------------------------------------------------------------- Properties */

struct edit
{
    const struct policy *policy;
    enum setting_state state;
    uint32_t number;
    BOOL changed;
};

/* a number is allowed when the policy has no list, or the number is in it ("2,5") */
static BOOL number_allowed( const struct policy *policy, uint32_t number )
{
    const char *p = policy->allowed;

    if (!p) return TRUE;
    while (*p)
    {
        char *end;
        unsigned long value = strtoul( p, &end, 10 );

        if (end == p) break;
        if (value == number) return TRUE;
        p = *end == ',' ? end + 1 : end;
    }
    return FALSE;
}

static void update_number_controls( HWND dlg, const struct edit *edit )
{
    BOOL enabled = edit->policy->status != POLICY_NOT_APPLICABLE && edit->policy->kind == POLICY_NUMBER &&
                   IsDlgButtonChecked( dlg, IDC_ENABLED ) == BST_CHECKED;

    EnableWindow( GetDlgItem( dlg, IDC_NUMBER ), enabled );
    EnableWindow( GetDlgItem( dlg, IDC_NUMBER_LABEL ), enabled );
}

static void init_setting_page( HWND dlg, struct edit *edit )
{
    const struct policy *p = edit->policy;
    WCHAR text[600], wide[300];
    BOOL usable = p->status != POLICY_NOT_APPLICABLE, number = p->kind == POLICY_NUMBER;

    to_wide( p->name, wide, ARRAY_SIZE(wide) );
    SetDlgItemTextW( dlg, IDC_POLICY_NAME, wide );
    CheckRadioButton( dlg, IDC_NOTCONFIGURED, IDC_DISABLED,
                      edit->state == SETTING_ENABLED ? IDC_ENABLED : edit->state == SETTING_DISABLED ? IDC_DISABLED : IDC_NOTCONFIGURED );
    swprintf( text, ARRAY_SIZE(text), L"%lu", (unsigned long)edit->number );
    SetDlgItemTextW( dlg, IDC_NUMBER, text );
    if (!number)
    {
        ShowWindow( GetDlgItem( dlg, IDC_NUMBER ), SW_HIDE );
        ShowWindow( GetDlgItem( dlg, IDC_NUMBER_LABEL ), SW_HIDE );
    }
    else
    {
        /* a number has no Disabled: it is Not Configured or Enabled with its value */
        EnableWindow( GetDlgItem( dlg, IDC_DISABLED ), FALSE );
        if (p->allowed) { swprintf( text, ARRAY_SIZE(text), L"Accepted values: %S.", p->allowed ); SetDlgItemTextW( dlg, IDC_HINT, text ); }
        else if (!strcmp( p->value, "NoDrives" ))
            SetDlgItemTextW( dlg, IDC_HINT, L"A bit mask of the drive letters to hide: A is 1, B is 2, C is 4, D is 8, and so on "
                                            L"(add them up, or type 0x for hexadecimal)." );
    }
    support_text( p, text, ARRAY_SIZE(text) );
    SetDlgItemTextW( dlg, IDC_SUPPORT, text );
    if (!usable)
    {
        EnableWindow( GetDlgItem( dlg, IDC_NOTCONFIGURED ), FALSE );
        EnableWindow( GetDlgItem( dlg, IDC_ENABLED ), FALSE );
        EnableWindow( GetDlgItem( dlg, IDC_DISABLED ), FALSE );
    }
    update_number_controls( dlg, edit );
}

/* the page's choices into the edit; FALSE (with a message) for a number that is not accepted */
static BOOL apply_setting_page( HWND dlg, struct edit *edit )
{
    WCHAR text[64], message[200];
    const struct policy *p = edit->policy;

    if (p->status == POLICY_NOT_APPLICABLE) return TRUE;
    if (IsDlgButtonChecked( dlg, IDC_ENABLED ) == BST_CHECKED) edit->state = SETTING_ENABLED;
    else if (IsDlgButtonChecked( dlg, IDC_DISABLED ) == BST_CHECKED) edit->state = SETTING_DISABLED;
    else edit->state = SETTING_NOT_CONFIGURED;
    if (p->kind == POLICY_NUMBER && edit->state == SETTING_ENABLED)
    {
        WCHAR *end;
        unsigned long value;

        GetDlgItemTextW( dlg, IDC_NUMBER, text, ARRAY_SIZE(text) );
        value = wcstoul( text, &end, 0 );
        if (!text[0] || *end || value > 0xffffffffUL || !number_allowed( p, (uint32_t)value ))
        {
            swprintf( message, ARRAY_SIZE(message), L"The value is not accepted%s%S.", p->allowed ? L". Accepted values: " : L"", p->allowed ? p->allowed : "" );
            MessageBoxW( dlg, message, L"Group Policy", MB_OK | MB_ICONWARNING );
            return FALSE;
        }
        edit->number = (uint32_t)value;
    }
    edit->changed = TRUE;
    return TRUE;
}

static INT_PTR CALLBACK setting_proc( HWND dlg, UINT msg, WPARAM wp, LPARAM lp )
{
    struct edit *edit = (struct edit *)GetWindowLongPtrW( dlg, DWLP_USER );

    switch (msg)
    {
    case WM_INITDIALOG:
        edit = (struct edit *)((PROPSHEETPAGEW *)lp)->lParam;
        SetWindowLongPtrW( dlg, DWLP_USER, (LONG_PTR)edit );
        init_setting_page( dlg, edit );
        return TRUE;
    case WM_COMMAND:
        if (!edit) break;
        if (LOWORD(wp) >= IDC_NOTCONFIGURED && LOWORD(wp) <= IDC_DISABLED)
        {
            update_number_controls( dlg, edit );
            SendMessageW( GetParent( dlg ), PSM_CHANGED, (WPARAM)dlg, 0 );
        }
        else if (LOWORD(wp) == IDC_NUMBER && HIWORD(wp) == EN_CHANGE) SendMessageW( GetParent( dlg ), PSM_CHANGED, (WPARAM)dlg, 0 );
        break;
    case WM_NOTIFY:
        if (edit && ((NMHDR *)lp)->code == PSN_APPLY)
        {
            SetWindowLongPtrW( dlg, DWLP_MSGRESULT, apply_setting_page( dlg, edit ) ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE );
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR CALLBACK explain_proc( HWND dlg, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_INITDIALOG)
    {
        const struct policy *p = ((struct edit *)((PROPSHEETPAGEW *)lp)->lParam)->policy;
        WCHAR text[1200], explain[700], support[400];

        to_wide( p->explain, explain, ARRAY_SIZE(explain) );
        support_text( p, support, ARRAY_SIZE(support) );
        swprintf( text, ARRAY_SIZE(text), L"%s\r\n\r\n%s", explain, support );
        SetDlgItemTextW( dlg, IDC_EXPLAIN, text );
        return TRUE;
    }
    return FALSE;
}

static void refresh_list(void);

static void show_properties( HWND owner, const struct policy *policy )
{
    struct edit edit = { policy, SETTING_NOT_CONFIGURED, 0, FALSE };
    PROPSHEETPAGEW pages[2];
    PROPSHEETHEADERW header;
    WCHAR title[300];

    model_get( models[policy->scope], policy, &edit.state, &edit.number );
    if (policy->kind == POLICY_NUMBER && !edit.number && policy->allowed) edit.number = (uint32_t)strtoul( policy->allowed, NULL, 10 );
    memset( pages, 0, sizeof(pages) );
    pages[0].dwSize = sizeof(pages[0]);
    pages[0].hInstance = instance;
    pages[0].pszTemplate = MAKEINTRESOURCEW( IDD_SETTING );
    pages[0].pfnDlgProc = setting_proc;
    pages[0].lParam = (LPARAM)&edit;
    pages[1] = pages[0];
    pages[1].pszTemplate = MAKEINTRESOURCEW( IDD_EXPLAIN );
    pages[1].pfnDlgProc = explain_proc;
    to_wide( policy->name, title, ARRAY_SIZE(title) );
    memset( &header, 0, sizeof(header) );
    header.dwSize = sizeof(header);
    header.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_PROPTITLE;
    header.hwndParent = owner;
    header.hInstance = instance;
    header.pszCaption = title;
    header.nPages = 2;
    header.ppsp = pages;
    if (PropertySheetW( &header ) > 0 && edit.changed && policy->status != POLICY_NOT_APPLICABLE)
    {
        if (!model_set( models[policy->scope], policy, edit.state, edit.number )) save_scope( owner, policy->scope );
        refresh_list();
    }
}

/* ---------------------------------------------------------------------------------------------------- the panes */

static struct node *selected_node(void)
{
    TVITEMW item;

    item.mask = TVIF_PARAM;
    item.hItem = TreeView_GetSelection( tree );
    if (!item.hItem || !TreeView_GetItemW( tree, &item )) return NULL;
    return (struct node *)item.lParam;
}

static void refresh_list(void)
{
    struct node *node = selected_node();
    LVITEMW item;
    WCHAR name[300], state[80], text[80];
    unsigned int i, count = 0;
    int row = 0;

    ListView_DeleteAllItems( list );
    if (node && node->category[0])
    {
        for (i = 0; i < policy_count; i++)
        {
            WCHAR category[256];

            if (policies[i].scope != node->scope) continue;
            to_wide( policies[i].category, category, ARRAY_SIZE(category) );
            if (wcscmp( category, node->category )) continue;
            to_wide( policies[i].name, name, ARRAY_SIZE(name) );
            state_text( &policies[i], state, ARRAY_SIZE(state) );
            memset( &item, 0, sizeof(item) );
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = row;
            item.pszText = name;
            item.lParam = (LPARAM)&policies[i];
            ListView_InsertItemW( list, &item );
            ListView_SetItemTextW( list, row, 1, state );
            row++;
            count++;
        }
    }
    swprintf( text, ARRAY_SIZE(text), L"%u setting%s", count, count == 1 ? L"" : L"s" );
    SendMessageW( status, SB_SETTEXTW, 0, (LPARAM)text );
}

static HTREEITEM add_node( HTREEITEM parent, const WCHAR *text, enum policy_scope scope, const WCHAR *category )
{
    TVINSERTSTRUCTW insert;
    struct node *node = NULL;

    if (node_count < MAX_NODES)
    {
        node = &nodes[node_count++];
        node->scope = scope;
        lstrcpynW( node->category, category, ARRAY_SIZE(node->category) );
    }
    memset( &insert, 0, sizeof(insert) );
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    insert.item.iImage = icon_folder;
    insert.item.iSelectedImage = icon_open;
    insert.item.pszText = (WCHAR *)text;
    insert.item.lParam = (LPARAM)node;
    return TreeView_InsertItemW( tree, &insert );
}

static HTREEITEM find_child( HTREEITEM parent, const WCHAR *text )
{
    HTREEITEM child;
    WCHAR buffer[256];
    TVITEMW item;

    for (child = TreeView_GetChild( tree, parent ); child; child = TreeView_GetNextSibling( tree, child ))
    {
        item.mask = TVIF_TEXT;
        item.hItem = child;
        item.pszText = buffer;
        item.cchTextMax = ARRAY_SIZE(buffer);
        if (TreeView_GetItemW( tree, &item ) && !wcscmp( buffer, text )) return child;
    }
    return NULL;
}

/* "Windows Components\Windows Explorer" below the Administrative Templates of a scope: a folder for each part */
static void add_category( HTREEITEM base, enum policy_scope scope, const char *category )
{
    WCHAR full[256], path[256], part[256];
    const WCHAR *start = full, *end;
    HTREEITEM parent = base, child;

    to_wide( category, full, ARRAY_SIZE(full) );
    path[0] = 0;
    while (*start)
    {
        end = wcschr( start, '\\' );
        if (!end) end = start + wcslen( start );
        lstrcpynW( part, start, (int)(end - start) + 1 );
        if (path[0]) wcscat( path, L"\\" );
        wcscat( path, part );
        if (!(child = find_child( parent, part ))) child = add_node( parent, part, scope, path );
        parent = child;
        start = *end ? end + 1 : end;
    }
}

static void expand_all( HTREEITEM item )
{
    HTREEITEM child;

    TreeView_Expand( tree, item, TVE_EXPAND );
    for (child = TreeView_GetChild( tree, item ); child; child = TreeView_GetNextSibling( tree, child )) expand_all( child );
}

static void build_tree(void)
{
    static const enum policy_scope scopes[] = { POLICY_COMPUTER, POLICY_USER };
    HTREEITEM root, scope_item, templates;
    unsigned int s, i;

    node_count = 0;
    TreeView_DeleteAllItems( tree );
    root = add_node( TVI_ROOT, L"Local Computer Policy", POLICY_COMPUTER, L"" );
    for (s = 0; s < 2; s++)
    {
        scope_item = add_node( root, scope_name( scopes[s] ), scopes[s], L"" );
        templates = add_node( scope_item, L"Administrative Templates", scopes[s], L"" );
        for (i = 0; i < policy_count; i++)
            if (policies[i].scope == scopes[s]) add_category( templates, scopes[s], policies[i].category );
    }
    expand_all( root );
    TreeView_SelectItem( tree, root );
}

static void layout(void)
{
    RECT rect;
    int status_height, width, height;

    GetClientRect( frame, &rect );
    SendMessageW( status, WM_SIZE, 0, 0 );
    GetWindowRect( status, &rect );
    status_height = rect.bottom - rect.top;
    GetClientRect( frame, &rect );
    width = rect.right;
    height = rect.bottom - status_height;
    if (split > width - 100) split = width - 100;
    if (split < 80) split = 80;
    MoveWindow( tree, 0, 0, split, height, TRUE );
    MoveWindow( list, split + 3, 0, width - split - 3, height, TRUE );
}

static const struct policy *selected_policy(void)
{
    LVITEMW item;
    int index = ListView_GetNextItem( list, -1, LVNI_SELECTED );

    if (index < 0) return NULL;
    memset( &item, 0, sizeof(item) );
    item.mask = LVIF_PARAM;
    item.iItem = index;
    return ListView_GetItemW( list, &item ) ? (const struct policy *)item.lParam : NULL;
}

static LRESULT notify( NMHDR *hdr )
{
    if (hdr->hwndFrom == tree && hdr->code == TVN_SELCHANGEDW) refresh_list();
    else if (hdr->hwndFrom == list && hdr->code == NM_DBLCLK)
    {
        const struct policy *policy = selected_policy();
        if (policy) show_properties( frame, policy );
    }
    else if (hdr->hwndFrom == list && hdr->code == NM_CUSTOMDRAW)
    {
        NMLVCUSTOMDRAW *draw = (NMLVCUSTOMDRAW *)hdr;

        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && draw->nmcd.lItemlParam &&
            ((const struct policy *)draw->nmcd.lItemlParam)->status == POLICY_NOT_APPLICABLE)
        {
            draw->clrText = GetSysColor( COLOR_GRAYTEXT );
            return CDRF_NEWFONT;
        }
    }
    return 0;
}

static LRESULT CALLBACK frame_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_CREATE:
    {
        SHFILEINFOW info;
        HIMAGELIST images;
        LVCOLUMNW column;

        frame = hwnd;
        tree = CreateWindowExW( WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES |
                                TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd, NULL, instance, NULL );
        list = CreateWindowExW( WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                                LVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd, NULL, instance, NULL );
        status = CreateWindowExW( 0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, instance, NULL );
        ListView_SetExtendedListViewStyle( list, LVS_EX_FULLROWSELECT );
        memset( &column, 0, sizeof(column) );
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = (WCHAR *)L"Setting";
        column.cx = 330;
        ListView_InsertColumnW( list, 0, &column );
        column.pszText = (WCHAR *)L"State";
        column.cx = 130;
        ListView_InsertColumnW( list, 1, &column );
        images = (HIMAGELIST)SHGetFileInfoW( L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
                                             SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES );
        if (images)
        {
            icon_folder = info.iIcon;
            SHGetFileInfoW( L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
                            SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_OPENICON | SHGFI_USEFILEATTRIBUTES );
            icon_open = info.iIcon;
            TreeView_SetImageList( tree, images, TVSIL_NORMAL );
        }
        load_scope( POLICY_COMPUTER );
        load_scope( POLICY_USER );
        build_tree();
        return 0;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_NOTIFY:
        return notify( (NMHDR *)lp );
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDM_EXIT: DestroyWindow( hwnd ); return 0;
        case IDM_PROPERTIES:
        {
            const struct policy *policy = selected_policy();
            if (policy) show_properties( hwnd, policy );
            return 0;
        }
        case IDM_REFRESH:
            load_scope( POLICY_COMPUTER );
            load_scope( POLICY_USER );
            refresh_list();
            return 0;
        case IDM_ABOUT:
            ShellAboutW( hwnd, L"Group Policy", L"Local Group Policy editor", NULL );
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

/* the entry point that mmc.exe calls for the console gpedit.msc: runs until the window closes */
BOOL WINAPI GpEdit_ExecuteW( HWND parent, HINSTANCE inst, const WCHAR *machine, int show )
{
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    WNDCLASSW wc;
    HACCEL accelerators;
    MSG msg;

    if (machine && machine[0])
    {
        MessageBoxW( parent, L"Only the local computer's policy can be edited.", L"Group Policy", MB_OK | MB_ICONINFORMATION );
        return FALSE;
    }
    InitCommonControlsEx( &controls );
    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GpEditFrame";
    wc.lpszMenuName = MAKEINTRESOURCEW( IDM_MAIN );
    RegisterClassW( &wc );
    frame = CreateWindowExW( 0, L"GpEditFrame", L"Group Policy", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 520,
                             parent, NULL, instance, NULL );
    if (!frame) return FALSE;
    accelerators = LoadAcceleratorsW( instance, MAKEINTRESOURCEW( IDA_MAIN ) );
    ShowWindow( frame, show ? show : SW_SHOWNORMAL );
    UpdateWindow( frame );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        if (!TranslateAcceleratorW( frame, accelerators, &msg )) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    }
    UnregisterClassW( L"GpEditFrame", instance );
    model_free( models[0] );
    model_free( models[1] );
    models[0] = models[1] = NULL;
    return TRUE;
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( inst );
        instance = inst;
    }
    return TRUE;
}
