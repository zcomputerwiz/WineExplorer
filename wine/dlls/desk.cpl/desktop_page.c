/*
 * WineExplorer Desktop Environment - Display Properties Desktop (Background) Page
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

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <stdio.h>

#include "resource.h"
#include "preview.h"

static COLORREF g_desktop_color;
static WCHAR g_current_wallpaper[MAX_PATH];
static int g_wallpaper_style = 2; /* Stretch */

static const WCHAR *position_names[] = {
    L"Center",
    L"Tile",
    L"Stretch",
    L"Fit",
    L"Fill"
};

static INT_PTR CALLBACK custdesktop_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        /* Check current desktop icon settings */
        CheckDlgButton(hwnd, IDC_CUST_MYDOCS, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_CUST_MYCOMP, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_CUST_MYNET, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_CUST_RECYCLE, BST_CHECKED);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)
        {
            EndDialog(hwnd, LOWORD(wparam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void update_desktop_preview(HWND hwnd)
{
    PREVIEW_DATA pd;
    static const int styles[] = {0,1,2,6,10};
    preview_init_data(&pd);
    pd.cr_desktop = g_desktop_color;
    lstrcpyW(pd.wallpaper_path,g_current_wallpaper);
    pd.wallpaper_style = styles[g_wallpaper_style];
    preview_update(GetDlgItem(hwnd,IDC_BG_PREVIEW),&pd);
}

static WCHAR wallpapers[256][MAX_PATH];
static int wallpaper_count;
static BOOL dirty;

static void add_wallpapers(HWND list,const WCHAR *directory)
{
    WCHAR pattern[MAX_PATH],title[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE find;
    swprintf(pattern,MAX_PATH,L"%s\\*",directory);
    find=FindFirstFileW(pattern,&fd); if(find==INVALID_HANDLE_VALUE) return;
    do {
        if(wallpaper_count>=256) break;
        if((fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
            (!PathMatchSpecW(fd.cFileName,L"*.bmp") && !PathMatchSpecW(fd.cFileName,L"*.jpg") && !PathMatchSpecW(fd.cFileName,L"*.png"))) continue;
        swprintf(wallpapers[wallpaper_count],MAX_PATH,L"%s\\%s",directory,fd.cFileName);
        lstrcpyW(title,fd.cFileName); PathRemoveExtensionW(title);
        SendMessageW(list,LB_ADDSTRING,0,(LPARAM)title); wallpaper_count++;
    } while(FindNextFileW(find,&fd));
    FindClose(find);
}

static void populate_wallpapers(HWND hwnd)
{
    HWND list=GetDlgItem(hwnd,IDC_BG_LIST); WCHAR windir[MAX_PATH],folder[MAX_PATH]; int i,selected=0;
    wallpaper_count=1; wallpapers[0][0]=0;
    SendMessageW(list,LB_RESETCONTENT,0,0); SendMessageW(list,LB_ADDSTRING,0,(LPARAM)L"(None)");
    GetWindowsDirectoryW(windir,MAX_PATH); swprintf(folder,MAX_PATH,L"%s\\Web\\Wallpaper",windir);
    add_wallpapers(list,folder); add_wallpapers(list,windir);
    for(i=1;i<wallpaper_count;i++) if(!wcsicmp(wallpapers[i],g_current_wallpaper)) selected=i;
    if(!selected && g_current_wallpaper[0] && wallpaper_count<256)
    {
        selected=wallpaper_count; lstrcpyW(wallpapers[wallpaper_count++],g_current_wallpaper);
        SendMessageW(list,LB_ADDSTRING,0,(LPARAM)PathFindFileNameW(g_current_wallpaper));
    }
    SendMessageW(list,LB_SETCURSEL,selected,0);
}

static void refresh_background(HWND hwnd)
{
    PREVIEW_DATA pd;
    preview_init_data(&pd);
    g_desktop_color=pd.cr_desktop;
    lstrcpyW(g_current_wallpaper,pd.wallpaper_path);
    g_wallpaper_style=pd.wallpaper_style==1?1:pd.wallpaper_style==2?2:pd.wallpaper_style==6?3:pd.wallpaper_style==10?4:0;
    SendDlgItemMessageW(hwnd,IDC_BG_POSITION,CB_SETCURSEL,g_wallpaper_style,0);
    populate_wallpapers(hwnd);
    update_desktop_preview(hwnd);
}

INT_PTR CALLBACK desktop_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hPos = GetDlgItem(hwnd, IDC_BG_POSITION);
        unsigned int i;

        dirty=FALSE;

        for (i = 0; i < 5; i++)
            SendMessageW(hPos, CB_ADDSTRING, 0, (LPARAM)position_names[i]);
        refresh_background(hwnd);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_BG_LIST:
            if (HIWORD(wparam) == LBN_SELCHANGE)
            {
                int index=SendMessageW((HWND)lparam,LB_GETCURSEL,0,0);
                if(index>=0 && index<wallpaper_count) lstrcpyW(g_current_wallpaper,wallpapers[index]);
                dirty=TRUE;
                update_desktop_preview(hwnd);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;

        case IDC_BG_POSITION:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                g_wallpaper_style = SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0);
                dirty=TRUE;
                update_desktop_preview(hwnd);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;

        case IDC_BG_COLOR:
        {
            CHOOSECOLORW cc;
            static COLORREF custom_colors[16];
            memset(&cc, 0, sizeof(cc));
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = hwnd;
            cc.rgbResult = g_desktop_color;
            cc.lpCustColors = custom_colors;
            cc.Flags = CC_RGBINIT | CC_FULLOPEN;

            if (ChooseColorW(&cc))
            {
                g_desktop_color = cc.rgbResult;
                dirty=TRUE;
                update_desktop_preview(hwnd);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;
        }

        case IDC_BG_BROWSE:
        {
            OPENFILENAMEW ofn;
            WCHAR file_buf[MAX_PATH] = L"";
            memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Pictures (*.bmp;*.jpg;*.png)\0*.bmp;*.jpg;*.png\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file_buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameW(&ofn))
            {
                HWND hList = GetDlgItem(hwnd, IDC_BG_LIST);
                int idx = SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)PathFindFileNameW(file_buf));
                SendMessageW(hList, LB_SETCURSEL, idx, 0);
                lstrcpynW(g_current_wallpaper, file_buf, MAX_PATH);
                if(wallpaper_count<256) lstrcpyW(wallpapers[wallpaper_count++],file_buf);
                dirty=TRUE;
                update_desktop_preview(hwnd);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;
        }

        case IDC_BG_CUSTDESK:
            DialogBoxW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_CUSTDESKTOP), hwnd, custdesktop_dlg_proc);
            break;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if(nmhdr->code==PSN_SETACTIVE && !dirty) { refresh_background(hwnd); return TRUE; }
        if (nmhdr->code == PSN_APPLY && dirty)
        {
            int idx = COLOR_BACKGROUND;
            COLORREF clr = g_desktop_color;
            HKEY key;
            WCHAR text[16];
            static const int styles[] = {0,0,2,6,10};
            SetSysColors(1, &idx, &clr);
            if(!RegCreateKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Desktop",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL))
            {
                swprintf(text,16,L"%d",styles[g_wallpaper_style]);
                RegSetValueExW(key,L"WallpaperStyle",0,REG_SZ,(BYTE *)text,(lstrlenW(text)+1)*sizeof(WCHAR));
                RegSetValueExW(key,L"TileWallpaper",0,REG_SZ,(BYTE *)(g_wallpaper_style==1?L"1":L"0"),2*sizeof(WCHAR));
                RegCloseKey(key);
            }
            SystemParametersInfoW(SPI_SETDESKWALLPAPER,0,g_current_wallpaper,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE);
            dirty=FALSE;

            PostMessageW(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
