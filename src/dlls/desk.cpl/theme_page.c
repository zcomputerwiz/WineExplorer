/*
 * Installed .theme files and their pending sample. Apply runs the shell theme engine.
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "wextheme.h"
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <stdio.h>
#include "resource.h"
#include "preview.h"

#define MAX_THEMES 96
#define WEX_APPLY_THEME 0x57455448
static WCHAR themes[MAX_THEMES][MAX_PATH];
static int count;
static WCHAR selected[MAX_PATH];
static BOOL dirty;
static WCHAR open_file[MAX_PATH];   /* a .theme file given on the command line (double-click on a .theme) */

void theme_page_open_file(const WCHAR *file)
{
    lstrcpynW(open_file, file, MAX_PATH);
}

static void update_preview(HWND hwnd)
{
    PREVIEW_DATA pd;
    if(selected[0] && GetFileAttributesW(selected)!=INVALID_FILE_ATTRIBUTES) preview_theme_data(&pd,selected);
    else preview_init_data(&pd);
    preview_update(GetDlgItem(hwnd,IDC_THEME_PREVIEW),&pd);
}

static void refresh_list(HWND hwnd)
{
    HWND combo=GetDlgItem(hwnd,IDC_THEME_LIST);
    WCHAR title[MAX_PATH],text[MAX_PATH]; int i,choice=-1;
    WCHAR resolved_selected[MAX_PATH];

    if (selected[0] && theme_resolve_path(selected, resolved_selected, MAX_PATH))
        lstrcpyW(selected, resolved_selected);

    count=preview_find_files(L".theme",themes,MAX_THEMES);
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    for(i=0;i<count;i++)
    {
        GetPrivateProfileStringW(L"Theme",L"DisplayName",L"",text,MAX_PATH,themes[i]);
        if(!text[0] || (text[0]=='@' && FAILED(SHLoadIndirectString(text,title,MAX_PATH,NULL))))
        { lstrcpyW(title,PathFindFileNameW(themes[i])); PathRemoveExtensionW(title); }
        else if(text[0]!='@') lstrcpyW(title,text);
        SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)title);
        if(!wcsicmp(themes[i],selected)) choice=i;
        else if(choice < 0)
        {
            WCHAR base_theme[MAX_PATH], base_sel[MAX_PATH];
            lstrcpyW(base_theme, PathFindFileNameW(themes[i]));
            PathRemoveExtensionW(base_theme);
            lstrcpyW(base_sel, PathFindFileNameW(selected));
            PathRemoveExtensionW(base_sel);
            if(!wcsicmp(base_theme, base_sel)) choice=i;
        }
    }
    /* a theme file from elsewhere (opened by its file) is added to the list, as XP does */
    if(choice<0 && open_file[0] && !wcsicmp(selected,open_file) && count<MAX_THEMES &&
       GetFileAttributesW(open_file)!=INVALID_FILE_ATTRIBUTES)
    {
        lstrcpyW(themes[count],open_file);
        lstrcpyW(title,PathFindFileNameW(open_file)); PathRemoveExtensionW(title);
        GetPrivateProfileStringW(L"Theme",L"DisplayName",L"",text,MAX_PATH,open_file);
        if(text[0] && text[0]!='@') lstrcpyW(title,text);
        SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)title);
        choice=count++;
    }
    SendMessageW(combo,CB_SETCURSEL,choice,0);
    EnableWindow(combo,count>0);
    update_preview(hwnd);
}

INT_PTR CALLBACK theme_page_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_INITDIALOG:
    {
        DWORD size=sizeof(selected); selected[0]=0;
        if(open_file[0])
        {
            /* opened with a theme file: that theme selected and pending, applied by OK or Apply */
            lstrcpyW(selected,open_file);
            dirty=TRUE; refresh_list(hwnd);
            SendMessageW(GetParent(hwnd),PSM_CHANGED,(WPARAM)hwnd,0);
            return TRUE;
        }
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes",L"CurrentTheme",RRF_RT_REG_SZ,NULL,selected,&size);
        dirty=FALSE; refresh_list(hwnd); return TRUE;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==IDC_THEME_LIST && HIWORD(wp)==CBN_DROPDOWN) { refresh_list(hwnd); return TRUE; }
        if(LOWORD(wp)==IDC_THEME_LIST && HIWORD(wp)==CBN_SELCHANGE)
        {
            int index=SendDlgItemMessageW(hwnd,IDC_THEME_LIST,CB_GETCURSEL,0,0);
            if(index>=0 && index<count) lstrcpyW(selected,themes[index]);
            dirty=TRUE; update_preview(hwnd); SendMessageW(GetParent(hwnd),PSM_CHANGED,(WPARAM)hwnd,0); return TRUE;
        }
        break;
    case WM_NOTIFY:
        if(((NMHDR *)lp)->code==PSN_SETACTIVE && !dirty)
        {
            DWORD size=sizeof(selected); selected[0]=0;
            RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes",L"CurrentTheme",RRF_RT_REG_SZ,NULL,selected,&size);
            refresh_list(hwnd); return TRUE;
        }
        if(((NMHDR *)lp)->code==PSN_APPLY && dirty)
        {
            if (!theme_apply_file(selected))
            {
                MessageBoxW(hwnd,L"The selected theme could not be applied.",L"Display Properties",MB_OK|MB_ICONEXCLAMATION);
                SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE);
            }
            else dirty=FALSE;
            return TRUE;
        }
        break;
    }
    return FALSE;
}
