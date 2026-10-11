/*
 * Device Manager - device properties (General, Driver, Details, Resources) and Driver File Details
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
#include <stdio.h>
#include <stdlib.h>

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winnls.h>
#include <wingdi.h>
#include <commctrl.h>
#include <prsht.h>

#include "devmgr.h"
#include "resource.h"


struct page_data
{
    struct hardware *hw;
    struct device *dev;
    HICON icon;
};

/* the driver: the kernel module, or the kernel itself for the computer */
static struct module *driver_module( struct page_data *data, struct module *kernel )
{
    unsigned int i;

    if (data->dev->mod) return data->dev->mod;
    if (data->dev != data->hw->computer) return NULL;
    for (i = 0; i < data->hw->mod_count; i++)
        if (data->hw->mods[i].builtin && data->hw->mods[i].path)
        {
            *kernel = data->hw->mods[i];
            kernel->name = "vmlinuz";
            kernel->author = kernel->description = kernel->license = kernel->version = kernel->depends = NULL;
            kernel->vermagic = data->hw->release;
            return kernel;
        }
    return NULL;
}

static BOOL in_tree( const struct module *mod )
{
    return mod->builtin || (mod->path && strstr( mod->path, "/kernel/" ));
}

static void module_provider( const struct module *mod, WCHAR *buffer, DWORD size )
{
    char author[256], *p;

    if (in_tree( mod )) { lstrcpynW( buffer, load_string( IDS_PROVIDER_LINUX ), size ); return; }
    if (!mod->author) { lstrcpynW( buffer, load_string( IDS_PROVIDER_UNKNOWN ), size ); return; }
    lstrcpynA( author, mod->author, sizeof(author) );
    if ((p = strchr( author, '<' ))) *p = 0;
    while (p && p > author && p[-1] == ' ') *--p = 0;
    utf8_to_wide( author, buffer, size );
}

static void module_version( struct hardware *hw, const struct module *mod, WCHAR *buffer, DWORD size )
{
    char version[128], *p;

    lstrcpynA( version, mod->version ? mod->version : mod->vermagic ? mod->vermagic : hw->release ? hw->release : "",
               sizeof(version) );
    if (!mod->version && (p = strchr( version, ' ' ))) *p = 0;
    utf8_to_wide( version, buffer, size );
}

static void module_date( const struct module *mod, WCHAR *buffer, DWORD size )
{
    LONGLONG ticks = mod->mtime * 10000000 + 116444736000000000;
    FILETIME ft, local;
    SYSTEMTIME st;

    ft.dwLowDateTime = (DWORD)ticks;
    ft.dwHighDateTime = ticks >> 32;
    if (!mod->mtime || !FileTimeToLocalFileTime( &ft, &local ) || !FileTimeToSystemTime( &local, &st ) ||
        !GetDateFormatW( LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, buffer, size ))
        lstrcpynW( buffer, load_string( IDS_NOT_AVAILABLE ), size );
}

static void module_signer( const struct module *mod, WCHAR *buffer, DWORD size )
{
    if (mod->signer) utf8_to_wide( mod->signer, buffer, size );
    else lstrcpynW( buffer, load_string( mod->builtin ? IDS_BUILTIN_SIGNER : IDS_NOT_SIGNED ), size );
}

/* the unix path as Windows programs see it (Z:\lib\modules\...) */
static void dos_path( const char *path, WCHAR *buffer, DWORD size )
{
    static WCHAR *(CDECL *get_dos_name)( const char * );
    WCHAR *name;

    if (!get_dos_name) get_dos_name = (void *)GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "wine_get_dos_file_name" );
    if (get_dos_name && (name = get_dos_name( path )))
    {
        lstrcpynW( buffer, name, size );
        HeapFree( GetProcessHeap(), 0, name );
    }
    else utf8_to_wide( path, buffer, size );
}

/* the icon and name at the top of every page */
static void init_header( HWND hwnd, struct page_data *data )
{
    SendDlgItemMessageW( hwnd, IDC_DEVICE_ICON, STM_SETICON, (WPARAM)data->icon, 0 );
    SetDlgItemTextW( hwnd, IDC_NAME, data->dev->name );
}

static const WCHAR *class_name( int cls )
{
    return load_string( IDS_CLASS_BASE + cls );
}

static INT_PTR CALLBACK general_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_INITDIALOG)
    {
        struct page_data *data = (struct page_data *)((PROPSHEETPAGEW *)lparam)->lParam;
        WCHAR text[1024];

        init_header( hwnd, data );
        SetDlgItemTextW( hwnd, IDC_TYPE, class_name( data->dev->cls ) );
        dev_manufacturer( data->dev, text, ARRAY_SIZE(text) );
        SetDlgItemTextW( hwnd, IDC_MANUFACTURER, text );
        dev_location( data->dev, text, ARRAY_SIZE(text) );
        SetDlgItemTextW( hwnd, IDC_LOCATION, text );
        if (data->dev->problem)
        {
            swprintf( text, ARRAY_SIZE(text), L"%s\r\n\r\n%s", load_string( IDS_NO_DRIVER ), load_string( IDS_NO_DRIVER_HINT ) );
            SetDlgItemTextW( hwnd, IDC_TROUBLESHOOT, load_string( IDS_REINSTALL ) );
        }
        else swprintf( text, ARRAY_SIZE(text), L"%s\r\n\r\n%s", load_string( IDS_WORKING ), load_string( IDS_WORKING_HINT ) );
        SetDlgItemTextW( hwnd, IDC_STATUS, text );
        SendDlgItemMessageW( hwnd, IDC_USAGE, CB_ADDSTRING, 0, (LPARAM)load_string( IDS_USE_DEVICE ) );
        SendDlgItemMessageW( hwnd, IDC_USAGE, CB_SETCURSEL, 0, 0 );
        /* the troubleshooters are Windows Help; enabling and disabling devices is the kernel's (grayed) */
        EnableWindow( GetDlgItem( hwnd, IDC_TROUBLESHOOT ), FALSE );
        EnableWindow( GetDlgItem( hwnd, IDC_USAGE ), FALSE );
        EnableWindow( GetDlgItem( hwnd, IDC_USAGE_LABEL ), FALSE );
        return TRUE;
    }
    return FALSE;
}

static INT_PTR CALLBACK driver_files_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam );

static INT_PTR CALLBACK driver_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct page_data *data = (struct page_data *)GetWindowLongPtrW( hwnd, DWLP_USER );

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        struct module kernel, *mod;
        WCHAR text[512];

        data = (struct page_data *)((PROPSHEETPAGEW *)lparam)->lParam;
        SetWindowLongPtrW( hwnd, DWLP_USER, (LONG_PTR)data );
        init_header( hwnd, data );
        if ((mod = driver_module( data, &kernel )))
        {
            module_provider( mod, text, ARRAY_SIZE(text) );
            SetDlgItemTextW( hwnd, IDC_PROVIDER, text );
            module_date( mod, text, ARRAY_SIZE(text) );
            SetDlgItemTextW( hwnd, IDC_DATE, text );
            module_version( data->hw, mod, text, ARRAY_SIZE(text) );
            SetDlgItemTextW( hwnd, IDC_VERSION, text );
            module_signer( mod, text, ARRAY_SIZE(text) );
            SetDlgItemTextW( hwnd, IDC_SIGNER, text );
        }
        else
        {
            SetDlgItemTextW( hwnd, IDC_PROVIDER, load_string( IDS_PROVIDER_UNKNOWN ) );
            SetDlgItemTextW( hwnd, IDC_DATE, load_string( IDS_NOT_AVAILABLE ) );
            SetDlgItemTextW( hwnd, IDC_VERSION, load_string( IDS_NOT_AVAILABLE ) );
            SetDlgItemTextW( hwnd, IDC_SIGNER, load_string( IDS_NOT_SIGNED ) );
        }
        /* drivers come with the kernel and its packages */
        EnableWindow( GetDlgItem( hwnd, IDC_UPDATE_DRIVER ), FALSE );
        EnableWindow( GetDlgItem( hwnd, IDC_ROLLBACK ), FALSE );
        EnableWindow( GetDlgItem( hwnd, IDC_UNINSTALL ), FALSE );
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_DRIVER_DETAILS)
            DialogBoxParamW( devmgr_instance, MAKEINTRESOURCEW(IDD_DRIVER_FILES), hwnd, driver_files_proc, (LPARAM)data );
        return TRUE;
    }
    return FALSE;
}

static void show_file( HWND hwnd, struct page_data *data, struct module *mod )
{
    WCHAR text[512];

    module_provider( mod, text, ARRAY_SIZE(text) );
    SetDlgItemTextW( hwnd, IDC_FILE_PROVIDER, text );
    module_version( data->hw, mod, text, ARRAY_SIZE(text) );
    SetDlgItemTextW( hwnd, IDC_FILE_VERSION, text );
    utf8_to_wide( mod->license ? mod->license : "", text, ARRAY_SIZE(text) );
    SetDlgItemTextW( hwnd, IDC_FILE_COPYRIGHT, text[0] ? text : load_string( IDS_NOT_AVAILABLE ) );
    module_signer( mod, text, ARRAY_SIZE(text) );
    SetDlgItemTextW( hwnd, IDC_FILE_SIGNER, text );
}

static void add_file( HWND list, HIMAGELIST images, struct module *mod, struct module **files, unsigned int *count )
{
    LVITEMW item = {0};
    WCHAR path[MAX_PATH];
    unsigned int i;

    if (!mod || !mod->path || *count >= 32) return;
    for (i = 0; i < *count; i++) if (files[i]->path && !strcmp( files[i]->path, mod->path )) return;
    files[*count] = mod;
    dos_path( mod->path, path, ARRAY_SIZE(path) );
    item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
    item.iItem = *count;
    item.pszText = path;
    item.iImage = mod->signer || mod->builtin ? 0 : 1;
    item.lParam = (*count)++;
    SendMessageW( list, LVM_INSERTITEMW, 0, (LPARAM)&item );
}

static INT_PTR CALLBACK driver_files_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    static struct module *files[32], kernel;
    static unsigned int count;
    struct page_data *data = (struct page_data *)GetWindowLongPtrW( hwnd, DWLP_USER );

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND list = GetDlgItem( hwnd, IDC_FILES );
        HMODULE pack = xp_resources( L"devmgr" );
        HIMAGELIST images = ImageList_Create( 16, 16, ILC_COLOR32 | ILC_MASK, 2, 0 );
        LVCOLUMNW column = {0};
        struct module *mod;
        RECT rect;
        int i;

        data = (struct page_data *)lparam;
        SetWindowLongPtrW( hwnd, DWLP_USER, (LONG_PTR)data );
        init_header( hwnd, data );
        for (i = 0; i < 2; i++)
        {
            UINT id = i ? IDI_BLANK : IDI_SIGNED;
            HICON icon = pack ? LoadImageW( pack, MAKEINTRESOURCEW(id), IMAGE_ICON, 16, 16, 0 ) : NULL;
            if (!icon) icon = LoadImageW( devmgr_instance, MAKEINTRESOURCEW(id), IMAGE_ICON, 16, 16, 0 );
            ImageList_AddIcon( images, icon );
            DestroyIcon( icon );
        }
        SendMessageW( list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)images );
        GetClientRect( list, &rect );
        column.mask = LVCF_WIDTH;
        column.cx = rect.right - GetSystemMetrics( SM_CXVSCROLL );
        SendMessageW( list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column );

        count = 0;
        if ((mod = driver_module( data, &kernel )))
        {
            char depends[512], *name;
            add_file( list, images, mod, files, &count );
            /* the modules it needs, as Windows lists a miniport's port drivers */
            lstrcpynA( depends, mod->depends ? mod->depends : "", sizeof(depends) );
            for (name = strtok( depends, "," ); name; name = strtok( NULL, "," ))
                add_file( list, images, find_module( data->hw, name ), files, &count );
        }
        if (count)
        {
            LVITEMW item = {0};
            item.stateMask = item.state = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW( list, LVM_SETITEMSTATE, 0, (LPARAM)&item );
            show_file( hwnd, data, files[0] );
        }
        else
        {
            LVITEMW item = {0};
            item.mask = LVIF_TEXT | LVIF_IMAGE;
            item.pszText = load_string( IDS_NO_FILES );
            item.iImage = -1;
            SendMessageW( list, LVM_INSERTITEMW, 0, (LPARAM)&item );
        }
        return TRUE;
    }
    case WM_NOTIFY:
    {
        NMLISTVIEW *nm = (NMLISTVIEW *)lparam;
        if (nm->hdr.idFrom == IDC_FILES && nm->hdr.code == LVN_ITEMCHANGED && (nm->uNewState & LVIS_SELECTED) &&
            count && nm->lParam < count)
            show_file( hwnd, data, files[nm->lParam] );
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) EndDialog( hwnd, 0 );
        return TRUE;
    case WM_DESTROY:
        ImageList_Destroy( (HIMAGELIST)SendDlgItemMessageW( hwnd, IDC_FILES, LVM_GETIMAGELIST, LVSIL_SMALL, 0 ) );
        return FALSE;
    }
    return FALSE;
}

static void property_value( struct page_data *data, int property, WCHAR *buffer, DWORD size )
{
    const struct device *dev = data->dev;
    const char *value;
    unsigned int i, index = 0;

    buffer[0] = 0;
    switch (property)
    {
    case 0: dev_instance_id( dev, buffer, size ); break;
    case 1: dev_hardware_ids( dev, FALSE, buffer, size ); break;
    case 2: dev_hardware_ids( dev, TRUE, buffer, size ); break;
    case 3:
    {
        WCHAR *nl;
        dev_hardware_ids( dev, FALSE, buffer, size );
        if ((nl = wcschr( buffer, '\n' ))) *nl = 0;
        break;
    }
    case 4: utf8_to_wide( dev->driver, buffer, size ); break;
    case 5: lstrcpynW( buffer, dev_enumerator( dev ), size ); break;
    case 6: lstrcpynW( buffer, class_key( dev->cls ), size ); break;
    case 7: lstrcpynW( buffer, class_guid( dev->cls ), size ); break;
    case 8:
        for (i = 0; i < data->hw->count && &data->hw->devs[i] != dev; i++)
            if (data->hw->devs[i].cls == dev->cls && !data->hw->devs[i].owner) index++;
        swprintf( buffer, size, L"%s\\%04u", class_guid( dev->cls ), index );
        break;
    case 9:
        if ((value = dev_get( dev, 'A', "device/rev" )) || (value = dev_get( dev, 'A', "firmware_rev" )) ||
            (value = dev_get( dev, 'A', "bcdDevice" )))
            utf8_to_wide( value, buffer, size );
        break;
    case 10: if (dev->path[0]) swprintf( buffer, size, L"/sys/devices/%S", dev->path ); break;
    }
}

static void show_property( HWND hwnd, struct page_data *data )
{
    HWND list = GetDlgItem( hwnd, IDC_VALUES );
    int property = SendDlgItemMessageW( hwnd, IDC_PROPERTY, CB_GETCURSEL, 0, 0 );
    WCHAR text[2048], *line, *next;
    LVITEMW item = {0};

    SendMessageW( list, LVM_DELETEALLITEMS, 0, 0 );
    property_value( data, property, text, ARRAY_SIZE(text) );
    item.mask = LVIF_TEXT;
    for (line = text; line && *line; line = next)
    {
        if ((next = wcschr( line, '\n' ))) *next++ = 0;
        item.pszText = line;
        SendMessageW( list, LVM_INSERTITEMW, 0, (LPARAM)&item );
        item.iItem++;
    }
    SendMessageW( list, LVM_SETCOLUMNWIDTH, 0, LVSCW_AUTOSIZE_USEHEADER );
}

static INT_PTR CALLBACK details_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct page_data *data = (struct page_data *)GetWindowLongPtrW( hwnd, DWLP_USER );

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        LVCOLUMNW column = {0};
        int i;

        data = (struct page_data *)((PROPSHEETPAGEW *)lparam)->lParam;
        SetWindowLongPtrW( hwnd, DWLP_USER, (LONG_PTR)data );
        init_header( hwnd, data );
        for (i = 0; i < IDS_PROP_COUNT; i++)
            SendDlgItemMessageW( hwnd, IDC_PROPERTY, CB_ADDSTRING, 0, (LPARAM)load_string( IDS_PROP_INSTANCE + i ) );
        SendDlgItemMessageW( hwnd, IDC_PROPERTY, CB_SETCURSEL, 0, 0 );
        column.mask = LVCF_WIDTH;
        column.cx = 200;
        SendDlgItemMessageW( hwnd, IDC_VALUES, LVM_INSERTCOLUMNW, 0, (LPARAM)&column );
        SendDlgItemMessageW( hwnd, IDC_VALUES, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT );
        show_property( hwnd, data );
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_PROPERTY && HIWORD(wparam) == CBN_SELCHANGE) show_property( hwnd, data );
        return TRUE;
    }
    return FALSE;
}

static INT_PTR CALLBACK resources_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_INITDIALOG)
    {
        static const UINT names[RES_COUNT] = { IDS_RES_MEMORY, IDS_RES_IO, IDS_RES_IRQ, IDS_RES_DMA };
        struct page_data *data = (struct page_data *)((PROPSHEETPAGEW *)lparam)->lParam;
        HWND list = GetDlgItem( hwnd, IDC_RESOURCE_LIST );
        HMODULE pack = xp_resources( L"devmgr" );
        HIMAGELIST images = ImageList_Create( 16, 16, ILC_COLOR32 | ILC_MASK, 1, 0 );
        HICON icon = pack ? LoadImageW( pack, MAKEINTRESOURCEW(IDI_RESOURCES), IMAGE_ICON, 16, 16, 0 ) : NULL;
        LVCOLUMNW column = {0};
        unsigned int i;

        init_header( hwnd, data );
        if (!icon) icon = LoadImageW( devmgr_instance, MAKEINTRESOURCEW(IDI_RESOURCES), IMAGE_ICON, 16, 16, 0 );
        ImageList_AddIcon( images, icon );
        DestroyIcon( icon );
        SendMessageW( list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)images );
        SendMessageW( list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT );
        column.mask = LVCF_WIDTH | LVCF_TEXT;
        column.cx = 110;
        column.pszText = load_string( IDS_RES_TYPE );
        SendMessageW( list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column );
        column.cx = 230;
        column.pszText = load_string( IDS_RES_SETTING );
        SendMessageW( list, LVM_INSERTCOLUMNW, 1, (LPARAM)&column );
        for (i = 0; i < data->dev->res_count; i++)
        {
            const struct resource *res = &data->dev->res[i];
            LVITEMW item = {0};
            WCHAR text[64];

            item.mask = LVIF_TEXT | LVIF_IMAGE;
            item.iItem = i;
            item.pszText = load_string( names[res->type] );
            SendMessageW( list, LVM_INSERTITEMW, 0, (LPARAM)&item );
            if (res->type == RES_IO) swprintf( text, ARRAY_SIZE(text), L"%04I64X - %04I64X", res->start, res->end );
            else if (res->type == RES_MEMORY) swprintf( text, ARRAY_SIZE(text), L"%08I64X - %08I64X", res->start, res->end );
            else if (res->type == RES_DMA) swprintf( text, ARRAY_SIZE(text), L"%02I64u", res->start );
            else swprintf( text, ARRAY_SIZE(text), L"%I64u", res->start );
            item.mask = LVIF_TEXT;
            item.iSubItem = 1;
            item.pszText = text;
            SendMessageW( list, LVM_SETITEMTEXTW, i, (LPARAM)&item );
        }
        SendDlgItemMessageW( hwnd, IDC_BASED_ON, CB_ADDSTRING, 0, (LPARAM)load_string( IDS_BASIC_CONFIG ) );
        SendDlgItemMessageW( hwnd, IDC_BASED_ON, CB_SETCURSEL, 0, 0 );
        CheckDlgButton( hwnd, IDC_AUTOMATIC, BST_CHECKED );
        SetDlgItemTextW( hwnd, IDC_CONFLICTS, load_string( IDS_NO_CONFLICTS ) );
        /* the kernel assigns the resources */
        EnableWindow( GetDlgItem( hwnd, IDC_BASED_ON ), FALSE );
        EnableWindow( GetDlgItem( hwnd, IDC_BASED_ON_LABEL ), FALSE );
        EnableWindow( GetDlgItem( hwnd, IDC_AUTOMATIC ), FALSE );
        EnableWindow( GetDlgItem( hwnd, IDC_CHANGE_SETTING ), FALSE );
        return TRUE;
    }
    if (msg == WM_DESTROY)
        ImageList_Destroy( (HIMAGELIST)SendDlgItemMessageW( hwnd, IDC_RESOURCE_LIST, LVM_GETIMAGELIST, LVSIL_SMALL, 0 ) );
    return FALSE;
}

void device_properties( HWND owner, struct hardware *hw, struct device *dev )
{
    static const struct { UINT id; DLGPROC proc; } pages[] =
    {
        { IDD_GENERAL, general_proc }, { IDD_DRIVER, driver_proc }, { IDD_DETAILS, details_proc },
        { IDD_RESOURCES, resources_proc },
    };
    struct page_data data = { hw, dev };
    HPROPSHEETPAGE handles[ARRAY_SIZE(pages)];
    PROPSHEETHEADERW header = { sizeof(header) };
    unsigned int i, count = 0;

    data.icon = class_icon( dev->cls, 32 );
    for (i = 0; i < ARRAY_SIZE(pages); i++)
    {
        PROPSHEETPAGEW page = { sizeof(page) };
        if (pages[i].id == IDD_RESOURCES && !dev->res_count) continue;   /* as Windows: only with resources */
        page.hInstance = devmgr_instance;
        page.pszTemplate = MAKEINTRESOURCEW(pages[i].id);
        page.pfnDlgProc = pages[i].proc;
        page.lParam = (LPARAM)&data;
        handles[count++] = CreatePropertySheetPageW( &page );
    }
    header.dwFlags = PSH_PROPTITLE | PSH_NOAPPLYNOW;
    header.hwndParent = owner;
    header.hInstance = devmgr_instance;
    header.pszCaption = dev->name;
    header.nPages = count;
    header.phpage = handles;
    PropertySheetW( &header );
    DestroyIcon( data.icon );
}
