/*
 * Display modes are supplied by the host, with a Wine-driver fallback.
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

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wexhost.h>
#include "resource.h"
#include "preview.h"

static WEX_DISPLAY host;
static BOOL native,dirty;
static DEVMODEW modes[WEX_DISPLAY_MAX_MODES];
static WEX_DISPLAY_MODE host_modes[WEX_DISPLAY_MAX_MODES];
static int count,selected;
typedef struct {
    DEVMODEW old_mode,new_mode;
    WEX_DISPLAY_MODE old_host,new_host;
    BOOL wine_applied;
    DWORD deadline;
    int stable;
} CHANGE;

static void label(HWND hwnd)
{
    WCHAR text[80];
    if(!count) { SetDlgItemTextW(hwnd,IDC_SET_RES_TEXT,L"No supported modes"); return; }
    swprintf(text,80,L"%lu by %lu pixels",modes[selected].dmPelsWidth,modes[selected].dmPelsHeight);
    SetDlgItemTextW(hwnd,IDC_SET_RES_TEXT,text);
}

static int compare_modes(const void *a,const void *b)
{
    const DEVMODEW *x=a,*y=b;
    if(x->dmPelsWidth!=y->dmPelsWidth) return x->dmPelsWidth<y->dmPelsWidth?-1:1;
    return x->dmPelsHeight<y->dmPelsHeight?-1:x->dmPelsHeight>y->dmPelsHeight;
}

static void refresh(HWND hwnd)
{
    DEVMODEW current={0},mode={0}; int i,j;
    WCHAR name[128]; PREVIEW_DATA preview;
    current.dmSize=mode.dmSize=sizeof(mode);
    EnumDisplaySettingsW(NULL,ENUM_CURRENT_SETTINGS,&current);
    count=selected=0; native=!WexDisplayGet(&host);
    if(native)
    {
        for(i=0;i<host.count && count<WEX_DISPLAY_MAX_MODES;i++)
        {
            for(j=0;j<count;j++)
                if(modes[j].dmPelsWidth==host.modes[i].width && modes[j].dmPelsHeight==host.modes[i].height) break;
            if(j<count) continue;
            mode=current; mode.dmPelsWidth=host.modes[i].width; mode.dmPelsHeight=host.modes[i].height;
            mode.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL;
            modes[count++]=mode;
        }
        swprintf(name,128,L"1. %hs",host.output);
    }
    else
    {
        for(i=0;EnumDisplaySettingsW(NULL,i,&mode);i++)
        {
            if(mode.dmBitsPerPel!=current.dmBitsPerPel || ChangeDisplaySettingsExW(NULL,&mode,NULL,CDS_TEST,NULL)) continue;
            for(j=0;j<count;j++)
                if(modes[j].dmPelsWidth==mode.dmPelsWidth && modes[j].dmPelsHeight==mode.dmPelsHeight) break;
            if(j==count && count<WEX_DISPLAY_MAX_MODES) modes[count++]=mode;
        }
        lstrcpyW(name,L"1. Wine display");
    }
    qsort(modes,count,sizeof(*modes),compare_modes);
    for(i=0;i<count;i++)
    {
        if(modes[i].dmPelsWidth==current.dmPelsWidth && modes[i].dmPelsHeight==current.dmPelsHeight) selected=i;
        if(native)
        {
            DWORD best=0xffffffff;
            for(j=0;j<host.count;j++)
            {
                DWORD distance=abs((int)host.modes[j].refresh-60000);
                if(host.modes[j].width!=modes[i].dmPelsWidth || host.modes[j].height!=modes[i].dmPelsHeight) continue;
                if(!memcmp(&host.current,&host.modes[j],sizeof(host.current))) distance=0;
                if(distance<best) { host_modes[i]=host.modes[j]; best=distance; }
            }
        }
    }
    SetDlgItemTextW(hwnd,IDC_SET_DISPLAY_LBL,name);
    SendDlgItemMessageW(hwnd,IDC_SET_RES_SLIDER,TBM_SETRANGE,TRUE,MAKELONG(0,max(0,count-1)));
    SendDlgItemMessageW(hwnd,IDC_SET_RES_SLIDER,TBM_SETPOS,TRUE,selected);
    EnableWindow(GetDlgItem(hwnd,IDC_SET_RES_SLIDER),count>1);
    SendDlgItemMessageW(hwnd,IDC_SET_BPP_COMBO,CB_RESETCONTENT,0,0);
    swprintf(name,128,L"%s (%lu bit)",current.dmBitsPerPel==32?L"Highest":L"Current",current.dmBitsPerPel);
    SendDlgItemMessageW(hwnd,IDC_SET_BPP_COMBO,CB_ADDSTRING,0,(LPARAM)name);
    SendDlgItemMessageW(hwnd,IDC_SET_BPP_COMBO,CB_SETCURSEL,0,0);
    EnableWindow(GetDlgItem(hwnd,IDC_SET_BPP_COMBO),FALSE);
    EnableWindow(GetDlgItem(hwnd,IDC_SET_ADVANCED),FALSE);
    preview_init_data(&preview); preview_update(GetDlgItem(hwnd,IDC_SET_MONITOR_IMG),&preview);
    label(hwnd);
}

/* Pump messages while Wine updates its virtual source and the shell redraws.
 * The confirmation timer does not exist until this phase completes. */
static INT_PTR CALLBACK wait_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    CHANGE *change=(CHANGE *)GetWindowLongPtrW(hwnd,DWLP_USER);
    if(msg==WM_INITDIALOG)
    {
        change=(CHANGE *)lp; SetWindowLongPtrW(hwnd,DWLP_USER,lp);
        change->deadline=GetTickCount()+10000; SetTimer(hwnd,1,100,NULL); return TRUE;
    }
    if(msg==WM_TIMER)
    {
        RECT desktop,tray; HWND bar=FindWindowW(L"Shell_TrayWnd",NULL); DWORD_PTR pong;
        if(!change->wine_applied)
            change->wine_applied=ChangeDisplaySettingsExW(NULL,&change->new_mode,NULL,0,NULL)==DISP_CHANGE_SUCCESSFUL;
        GetWindowRect(GetDesktopWindow(),&desktop); GetWindowRect(bar,&tray);
        if(change->wine_applied && GetSystemMetrics(SM_CXSCREEN)==change->new_mode.dmPelsWidth &&
           GetSystemMetrics(SM_CYSCREEN)==change->new_mode.dmPelsHeight &&
           desktop.right-desktop.left==change->new_mode.dmPelsWidth &&
           desktop.bottom-desktop.top==change->new_mode.dmPelsHeight &&
           (!bar || tray.right-tray.left==change->new_mode.dmPelsWidth) &&
           SendMessageTimeoutW(GetDesktopWindow(),WM_NULL,0,0,SMTO_ABORTIFHUNG,200,&pong))
        {
            if(++change->stable==2)
            {
                RedrawWindow(GetDesktopWindow(),NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
                if(bar) RedrawWindow(bar,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
                EndDialog(hwnd,IDYES);
            }
        }
        else change->stable=0;
        if((int)(GetTickCount()-change->deadline)>=0) EndDialog(hwnd,IDNO);
        return TRUE;
    }
    if(msg==WM_CLOSE || (msg==WM_COMMAND && LOWORD(wp)==IDCANCEL)) { EndDialog(hwnd,IDNO); return TRUE; }
    if(msg==WM_DESTROY) KillTimer(hwnd,1);
    return FALSE;
}

static INT_PTR CALLBACK confirm_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_INITDIALOG) { SetWindowLongPtrW(hwnd,DWLP_USER,GetTickCount()+15000); SetTimer(hwnd,1,200,NULL); return TRUE; }
    if(msg==WM_TIMER)
    {
        int remaining=(int)((DWORD)GetWindowLongPtrW(hwnd,DWLP_USER)-GetTickCount()); WCHAR text[256];
        if(remaining<=0) EndDialog(hwnd,IDNO);
        else { swprintf(text,256,L"Your desktop has been reconfigured. Do you want to keep these settings?\n\nReverting in %d seconds",(remaining+999)/1000); SetDlgItemTextW(hwnd,IDC_CONFIRM_TEXT,text); }
        return TRUE;
    }
    if(msg==WM_COMMAND && (LOWORD(wp)==IDYES || LOWORD(wp)==IDNO || LOWORD(wp)==IDCANCEL))
    { EndDialog(hwnd,LOWORD(wp)==IDYES?IDYES:IDNO); return TRUE; }
    if(msg==WM_CLOSE) { EndDialog(hwnd,IDNO); return TRUE; }
    if(msg==WM_DESTROY) KillTimer(hwnd,1);
    return FALSE;
}

static BOOL apply(HWND hwnd)
{
    CHANGE change={0}; WEX_DISPLAY before; INT_PTR ready,keep=IDNO; BOOL restored=TRUE;
    HINSTANCE instance=GetModuleHandleW(L"desk.cpl");
    if(!count) return FALSE;
    change.old_mode.dmSize=sizeof(DEVMODEW);
    if(!EnumDisplaySettingsW(NULL,ENUM_CURRENT_SETTINGS,&change.old_mode)) return FALSE;
    change.new_mode=modes[selected];
    if(change.old_mode.dmPelsWidth==change.new_mode.dmPelsWidth && change.old_mode.dmPelsHeight==change.new_mode.dmPelsHeight) return TRUE;
    if(native)
    {
        if(WexDisplayGet(&before)) return FALSE;
        change.old_host=before.current; change.new_host=host_modes[selected];
        if(WexDisplaySet(&change.new_host,FALSE))
        { WexDisplaySet(&change.old_host,FALSE); return FALSE; }
    }
    ready=DialogBoxParamW(instance,MAKEINTRESOURCEW(IDD_WAITSETTINGS),hwnd,wait_proc,(LPARAM)&change);
    if(ready==IDYES) keep=DialogBoxParamW(instance,MAKEINTRESOURCEW(IDD_CONFIRMSETTINGS),hwnd,confirm_proc,0);
    if(keep==IDYES)
    {
        if(ChangeDisplaySettingsExW(NULL,&change.new_mode,NULL,CDS_UPDATEREGISTRY|CDS_NORESET,NULL)==DISP_CHANGE_SUCCESSFUL &&
           (!native || !WexDisplaySet(&change.new_host,TRUE)))
        { refresh(hwnd); return TRUE; }
    }
    if(native && WexDisplaySet(&change.old_host,FALSE)) restored=FALSE;
    if(ChangeDisplaySettingsExW(NULL,&change.old_mode,NULL,keep==IDYES?CDS_UPDATEREGISTRY:0,NULL)!=DISP_CHANGE_SUCCESSFUL) restored=FALSE;
    refresh(hwnd);
    if(!restored) MessageBoxW(hwnd,L"The previous display mode could not be restored.",L"Display Properties",MB_OK|MB_ICONEXCLAMATION);
    return ready==IDYES && keep!=IDYES && restored;
}

INT_PTR CALLBACK settings_page_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_INITDIALOG: dirty=FALSE; refresh(hwnd); return TRUE;
    case WM_HSCROLL:
        if((HWND)lp==GetDlgItem(hwnd,IDC_SET_RES_SLIDER))
        { selected=SendDlgItemMessageW(hwnd,IDC_SET_RES_SLIDER,TBM_GETPOS,0,0); label(hwnd); dirty=TRUE; SendMessageW(GetParent(hwnd),PSM_CHANGED,(WPARAM)hwnd,0); }
        return TRUE;
    case WM_NOTIFY:
        if(((NMHDR *)lp)->code==PSN_SETACTIVE && !dirty) { refresh(hwnd); return TRUE; }
        if(((NMHDR *)lp)->code==PSN_APPLY && dirty)
        {
            if(apply(hwnd)) dirty=FALSE;
            else
            {
                MessageBoxW(hwnd,L"The selected display mode could not be applied.",L"Display Properties",MB_OK|MB_ICONEXCLAMATION);
                SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE);
            }
            return TRUE;
        }
    }
    return FALSE;
}
