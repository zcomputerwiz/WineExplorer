/*
 * Display Properties sample controls. Each control owns its pending preview.
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

#define COBJMACROS
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include "preview.h"
#include "resource.h"

#define PREVIEW_CLASS L"DeskCplPreview"
typedef HRESULT (WINAPI *OPEN_FILE)(LPCWSTR,LPCWSTR,LPCWSTR,HANDLE *,DWORD);
typedef HRESULT (WINAPI *CLOSE_FILE)(HANDLE);
typedef HTHEME (WINAPI *OPEN_DATA)(HANDLE,HWND,LPCWSTR,DWORD);
typedef struct {
    PREVIEW_DATA data;
    HBITMAP wallpaper;
    HANDLE file;
    HTHEME window, button, scrollbar;
} PREVIEW_STATE;

void preview_expand_path(const WCHAR *src, WCHAR *dst)
{
    WCHAR base[MAX_PATH], expanded[MAX_PATH];
    if (!wcsnicmp(src, L"%ResourceDir%", 13))
    {
        GetWindowsDirectoryW(base, MAX_PATH);
        swprintf(expanded, MAX_PATH, L"%s\\Resources%s", base, src + 13);
        ExpandEnvironmentStringsW(expanded, dst, MAX_PATH);
    }
    else ExpandEnvironmentStringsW(src, dst, MAX_PATH);
}

void preview_init_data(PREVIEW_DATA *pd)
{
    HKEY key;
    WCHAR text[16];
    DWORD size;
    memset(pd, 0, sizeof(*pd));
    pd->cr_desktop = GetSysColor(COLOR_BACKGROUND);
    pd->cr_active_caption = GetSysColor(COLOR_ACTIVECAPTION);
    pd->cr_active_text = GetSysColor(COLOR_CAPTIONTEXT);
    pd->cr_inactive_caption = GetSysColor(COLOR_INACTIVECAPTION);
    pd->cr_inactive_text = GetSysColor(COLOR_INACTIVECAPTIONTEXT);
    pd->cr_btn_face = GetSysColor(COLOR_BTNFACE);
    pd->cr_btn_text = GetSysColor(COLOR_BTNTEXT);
    pd->cr_window = GetSysColor(COLOR_WINDOW);
    pd->cr_window_text = GetSysColor(COLOR_WINDOWTEXT);
    pd->cr_menu = GetSysColor(COLOR_MENU);
    pd->cr_menu_text = GetSysColor(COLOR_MENUTEXT);
    if (IsThemeActive()) GetCurrentThemeName(pd->theme_path, MAX_PATH, pd->color_name, 64, pd->size_name, 64);
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, pd->wallpaper_path, 0);
    pd->wallpaper_style = 2;
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_QUERY_VALUE, &key))
    {
        size = sizeof(text);
        if (!RegQueryValueExW(key, L"WallpaperStyle", NULL, NULL, (BYTE *)text, &size)) pd->wallpaper_style = wcstol(text, NULL, 10);
        size = sizeof(text);
        if (!RegQueryValueExW(key, L"TileWallpaper", NULL, NULL, (BYTE *)text, &size) && wcstol(text, NULL, 10)) pd->wallpaper_style = 1;
        RegCloseKey(key);
    }
}

static void read_color(const WCHAR *path, const WCHAR *name, COLORREF *color)
{
    WCHAR text[64]; int r,g,b;
    GetPrivateProfileStringW(L"Control Panel\\Colors", name, L"", text, 64, path);
    if (swscanf(text,L"%d %d %d", &r,&g,&b) == 3) *color = RGB(r,g,b);
}

void preview_theme_data(PREVIEW_DATA *pd, const WCHAR *path)
{
    WCHAR text[MAX_PATH];
    preview_init_data(pd);
    GetPrivateProfileStringW(L"VisualStyles", L"Path", L"", text, MAX_PATH, path);
    preview_expand_path(text, pd->theme_path);
    GetPrivateProfileStringW(L"VisualStyles", L"ColorStyle", L"NormalColor", pd->color_name, 64, path);
    GetPrivateProfileStringW(L"VisualStyles", L"Size", L"NormalSize", pd->size_name, 64, path);
    GetPrivateProfileStringW(L"Control Panel\\Desktop", L"Wallpaper", L"", text, MAX_PATH, path);
    preview_expand_path(text, pd->wallpaper_path);
    pd->wallpaper_style = GetPrivateProfileIntW(L"Control Panel\\Desktop", L"TileWallpaper", 0, path) ? 1 :
                          GetPrivateProfileIntW(L"Control Panel\\Desktop", L"WallpaperStyle", 2, path);
    read_color(path,L"Background",&pd->cr_desktop);
    read_color(path,L"ActiveTitle",&pd->cr_active_caption);
    read_color(path,L"InactiveTitle",&pd->cr_inactive_caption);
    read_color(path,L"TitleText",&pd->cr_active_text);
    read_color(path,L"InactiveTitleText",&pd->cr_inactive_text);
    read_color(path,L"ButtonFace",&pd->cr_btn_face);
    read_color(path,L"ButtonText",&pd->cr_btn_text);
    read_color(path,L"Window",&pd->cr_window);
    read_color(path,L"WindowText",&pd->cr_window_text);
    read_color(path,L"Menu",&pd->cr_menu);
    read_color(path,L"MenuText",&pd->cr_menu_text);
}

static void release_state(PREVIEW_STATE *s)
{
    HMODULE module = GetModuleHandleW(L"uxtheme.dll");
    CLOSE_FILE close_file = (CLOSE_FILE)GetProcAddress(module, MAKEINTRESOURCEA(3));
    if (s->wallpaper) DeleteObject(s->wallpaper);
    if (s->window) CloseThemeData(s->window);
    if (s->button) CloseThemeData(s->button);
    if (s->scrollbar) CloseThemeData(s->scrollbar);
    if (s->file && close_file) close_file(s->file);
    s->wallpaper = NULL; s->window = s->button = s->scrollbar = NULL; s->file = NULL;
}

static HBITMAP load_picture(const WCHAR *path)
{
    static const GUID clsid_factory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
    static const GUID iid_factory = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
    static const GUID format_bgra = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f } };
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    HBITMAP bitmap = NULL;
    UINT w, h;

    if (FAILED(CoCreateInstance(&clsid_factory, NULL, CLSCTX_INPROC_SERVER, &iid_factory, (void **)&factory))) return NULL;
    if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) &&
        SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) &&
        SUCCEEDED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &format_bgra, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(IWICFormatConverter_GetSize(converter, &w, &h)))
    {
        BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), (LONG)w, -(LONG)h, 1, 32, BI_RGB } };
        void *bits = NULL;
        HDC dc = GetDC(NULL);
        bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        ReleaseDC(NULL, dc);
        if (bitmap && FAILED(IWICFormatConverter_CopyPixels(converter, NULL, w * 4, w * 4 * h, bits)))
        {
            DeleteObject(bitmap);
            bitmap = NULL;
        }
    }
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    IWICImagingFactory_Release(factory);
    return bitmap;
}

void preview_update(HWND hwnd, const PREVIEW_DATA *data)
{
    PREVIEW_STATE *s = (PREVIEW_STATE *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    HMODULE module = GetModuleHandleW(L"uxtheme.dll");
    OPEN_FILE open_file = (OPEN_FILE)GetProcAddress(module, MAKEINTRESOURCEA(2));
    OPEN_DATA open_data = (OPEN_DATA)GetProcAddress(module, MAKEINTRESOURCEA(16));
    if (!s) return;
    release_state(s);
    if (data) s->data = *data; else preview_init_data(&s->data);
    if (s->data.wallpaper_path[0]) s->wallpaper = load_picture(s->data.wallpaper_path);
    if (s->data.theme_path[0] && open_file && open_data &&
        SUCCEEDED(open_file(s->data.theme_path, s->data.color_name, s->data.size_name, &s->file, 0)))
    {
        s->window = open_data(s->file, hwnd, L"Window", 0);
        s->button = open_data(s->file, hwnd, L"Button", 0);
        s->scrollbar = open_data(s->file, hwnd, L"Scrollbar", 0);
        if(s->window && GetDlgCtrlID(hwnd)!=IDC_BG_PREVIEW)
        {
            s->data.cr_desktop=GetThemeSysColor(s->window,COLOR_BACKGROUND);
            s->data.cr_active_text=GetThemeSysColor(s->window,COLOR_CAPTIONTEXT);
            s->data.cr_inactive_text=GetThemeSysColor(s->window,COLOR_INACTIVECAPTIONTEXT);
            s->data.cr_btn_face=GetThemeSysColor(s->window,COLOR_BTNFACE);
            s->data.cr_btn_text=GetThemeSysColor(s->window,COLOR_BTNTEXT);
            s->data.cr_window=GetThemeSysColor(s->window,COLOR_WINDOW);
            s->data.cr_window_text=GetThemeSysColor(s->window,COLOR_WINDOWTEXT);
        }
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void fill_color(HDC dc, const RECT *rc, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, rc, brush); DeleteObject(brush);
}

static void draw_wallpaper(HDC dc, const RECT *rc, PREVIEW_STATE *s)
{
    BITMAP bm; HDC mem; HBITMAP old; int w,h,x,y,sw,sh;
    fill_color(dc, rc, s->data.cr_desktop);
    if (!s->wallpaper) return;
    GetObjectW(s->wallpaper, sizeof(bm), &bm);
    mem = CreateCompatibleDC(dc); old = SelectObject(mem,s->wallpaper);
    w = rc->right - rc->left; h = rc->bottom - rc->top;
    sw = bm.bmWidth; sh = bm.bmHeight;
    if (s->data.wallpaper_style == 2) { sw=w; sh=h; }
    else if (s->data.wallpaper_style == 6 || s->data.wallpaper_style == 10)
    {
        BOOL fit = s->data.wallpaper_style == 6;
        if ((w * bm.bmHeight < h * bm.bmWidth) == fit) { sw=w; sh=MulDiv(bm.bmHeight,w,bm.bmWidth); }
        else { sh=h; sw=MulDiv(bm.bmWidth,h,bm.bmHeight); }
    }
    else { sw=max(1,MulDiv(sw,w,GetSystemMetrics(SM_CXSCREEN))); sh=max(1,MulDiv(sh,h,GetSystemMetrics(SM_CYSCREEN))); }
    SaveDC(dc); IntersectClipRect(dc,rc->left,rc->top,rc->right,rc->bottom);
    SetStretchBltMode(dc,HALFTONE); SetBrushOrgEx(dc,0,0,NULL);
    if (s->data.wallpaper_style == 1)
        for (y=0;y<h;y+=sh) for(x=0;x<w;x+=sw)
            StretchBlt(dc,rc->left+x,rc->top+y,sw,sh,mem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
    else StretchBlt(dc,rc->left+(w-sw)/2,rc->top+(h-sh)/2,sw,sh,mem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
    RestoreDC(dc,-1); SelectObject(mem,old); DeleteDC(mem);
}

static void sample_window(HDC dc, PREVIEW_STATE *s, RECT rc, const WCHAR *title, BOOL active, BOOL dialog)
{
    RECT cap=rc, client=rc, part, text;
    int border=s->window ? 3 : 4, caption=26+s->data.font_size*3, state=active ? CS_ACTIVE : CS_INACTIVE;
    HFONT font, old; LOGFONTW lf={0};
    fill_color(dc,&rc,dialog ? s->data.cr_btn_face : s->data.cr_window);
    cap.bottom=rc.top+caption;
    if (s->window)
    {
        DrawThemeBackground(s->window,dc,WP_CAPTION,state,&cap,NULL);
        part=rc; part.top=cap.bottom; part.right=part.left+border; DrawThemeBackground(s->window,dc,WP_FRAMELEFT,state,&part,NULL);
        part=rc; part.top=cap.bottom; part.left=part.right-border; DrawThemeBackground(s->window,dc,WP_FRAMERIGHT,state,&part,NULL);
        part=rc; part.top=part.bottom-border; DrawThemeBackground(s->window,dc,WP_FRAMEBOTTOM,state,&part,NULL);
    }
    else
    {
        DrawEdge(dc,&rc,EDGE_RAISED,BF_RECT);
        InflateRect(&cap,-border,-border); cap.bottom=cap.top+caption-border;
        fill_color(dc,&cap,active ? s->data.cr_active_caption : s->data.cr_inactive_caption);
    }
    lf.lfHeight=-(s->window ? 13 : 11)-s->data.font_size*2; lf.lfWeight=FW_BOLD;
    lstrcpyW(lf.lfFaceName,s->window ? L"Trebuchet MS" : L"Tahoma");
    if(s->window) GetThemeSysFont(s->window,TMT_CAPTIONFONT,&lf);
    font=CreateFontIndirectW(&lf); old=SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,active ? s->data.cr_active_text : s->data.cr_inactive_text);
    text=cap; text.left+=7; text.right-=dialog ? 25 : 69;
    DrawTextW(dc,title,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    SelectObject(dc,old); DeleteObject(font);
    part=cap; part.top+=4; part.bottom=part.top+21; part.right-=4; part.left=part.right-21;
    if (s->window) DrawThemeBackground(s->window,dc,WP_CLOSEBUTTON,active ? CBS_NORMAL : CBS_DISABLED,&part,NULL);
    else DrawFrameControl(dc,&part,DFC_CAPTION,DFCS_CAPTIONCLOSE);
    if (!dialog)
    {
        OffsetRect(&part,-23,0);
        if(s->window) DrawThemeBackground(s->window,dc,WP_MAXBUTTON,active ? MAXBS_NORMAL : MAXBS_DISABLED,&part,NULL);
        else DrawFrameControl(dc,&part,DFC_CAPTION,DFCS_CAPTIONMAX);
        OffsetRect(&part,-23,0);
        if(s->window) DrawThemeBackground(s->window,dc,WP_MINBUTTON,active ? MINBS_NORMAL : MINBS_DISABLED,&part,NULL);
        else DrawFrameControl(dc,&part,DFC_CAPTION,DFCS_CAPTIONMIN);
    }
    client.left+=border; client.right-=border; client.top=cap.bottom; client.bottom-=border;
    lf.lfHeight=-11-s->data.font_size*2; lf.lfWeight=FW_NORMAL; lstrcpyW(lf.lfFaceName,L"Tahoma");
    if(s->window) GetThemeSysFont(s->window,TMT_MSGBOXFONT,&lf);
    font=CreateFontIndirectW(&lf); old=SelectObject(dc,font);
    SetTextColor(dc,s->data.cr_window_text);
    if (dialog)
    {
        part=client; part.left=client.left+(client.right-client.left-76)/2;
        part.right=part.left+76; part.top=client.top+(client.bottom-client.top-25)/2; part.bottom=part.top+25;
        if(s->button) DrawThemeBackground(s->button,dc,BP_PUSHBUTTON,PBS_NORMAL,&part,NULL);
        else { fill_color(dc,&part,s->data.cr_btn_face); DrawEdge(dc,&part,EDGE_RAISED,BF_RECT); }
        SetTextColor(dc,s->data.cr_btn_text); DrawTextW(dc,L"OK",-1,&part,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    else
    {
        text=client; text.left+=1;
        DrawTextW(dc,L"Window Text",-1,&text,DT_LEFT|DT_TOP);
        part=client; part.left=part.right-17; fill_color(dc,&part,s->data.cr_btn_face);
        if(s->scrollbar) DrawThemeBackground(s->scrollbar,dc,SBP_LOWERTRACKVERT,SCRBS_NORMAL,&part,NULL);
        text=part; text.bottom=text.top+17;
        if(s->scrollbar) DrawThemeBackground(s->scrollbar,dc,SBP_ARROWBTN,ABS_UPNORMAL,&text,NULL);
        else DrawFrameControl(dc,&text,DFC_SCROLL,DFCS_SCROLLUP);
        text.top=part.bottom-17; text.bottom=part.bottom;
        if(s->scrollbar) DrawThemeBackground(s->scrollbar,dc,SBP_ARROWBTN,ABS_DOWNNORMAL,&text,NULL);
        else DrawFrameControl(dc,&text,DFC_SCROLL,DFCS_SCROLLDOWN);
        text.top=part.top+17; text.bottom=text.top+17;
        if(s->scrollbar) DrawThemeBackground(s->scrollbar,dc,SBP_THUMBBTNVERT,SCRBS_NORMAL,&text,NULL);
        else DrawEdge(dc,&text,EDGE_RAISED,BF_RECT);
    }
    SelectObject(dc,old); DeleteObject(font);
}

static void draw_sample(HWND hwnd,HDC dc,const RECT *rc,PREVIEW_STATE *s)
{
    RECT r; int w=rc->right,h=rc->bottom,id=GetDlgCtrlID(hwnd);
    if(id==IDC_BG_PREVIEW || id==IDC_SET_MONITOR_IMG)
    {
        HBITMAP monitor=LoadBitmapW((HINSTANCE)GetWindowLongPtrW(hwnd,GWLP_HINSTANCE),MAKEINTRESOURCEW(126));
        BITMAP bm; HDC mem; HBITMAP old; int x,y;
        FillRect(dc,rc,GetSysColorBrush(COLOR_BTNFACE));
        if (!monitor) return;
        GetObjectW(monitor,sizeof(bm),&bm); x=(w-bm.bmWidth)/2; y=(h-bm.bmHeight)/2;
        mem=CreateCompatibleDC(dc); old=SelectObject(mem,monitor);
        /* the picture's background colour (its bottom-left pixel) is transparent */
        GdiTransparentBlt(dc,x,y,bm.bmWidth,bm.bmHeight,mem,0,0,bm.bmWidth,bm.bmHeight,GetPixel(mem,0,bm.bmHeight-1));
        /* the screen inside the monitor picture: Windows XP's (185 x 163), else ReactOS's (160 x 120) */
        if(bm.bmWidth==185) SetRect(&r,x+16,y+17,x+168,y+129);
        else SetRect(&r,x+19,y+7,x+141,y+93);
        draw_wallpaper(dc,&r,s);
        if(id==IDC_SET_MONITOR_IMG)
        {
            HFONT font=CreateFontW(-68,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Arial");
            HFONT previous=SelectObject(dc,font);
            fill_color(dc,&r,RGB(0,78,152)); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
            DrawTextW(dc,L"1",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,previous); DeleteObject(font);
        }
        SelectObject(mem,old); DeleteDC(mem); DeleteObject(monitor);
    }
    else if(id==IDC_THEME_PREVIEW)
    {
        draw_wallpaper(dc,rc,s);
        SetRect(&r,32,33,min(w-24,248),min(h-30,159));
        sample_window(dc,s,r,L"Active Window",TRUE,FALSE);
        {
            PIDLIST_ABSOLUTE pidl; SHFILEINFOW info;
            if(SUCCEEDED(SHParseDisplayName(L"::{645FF040-5081-101B-9F08-00AA002F954E}",NULL,&pidl,0,NULL)))
            {
                if(SHGetFileInfoW((LPCWSTR)pidl,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_ICON|SHGFI_LARGEICON))
                { DrawIconEx(dc,w-52,h-40,info.hIcon,32,32,0,NULL,DI_NORMAL); DestroyIcon(info.hIcon); }
                CoTaskMemFree(pidl);
            }
        }
    }
    else
    {
        fill_color(dc,rc,s->data.cr_desktop);
        SetRect(&r,8,10,w-33,h-41); sample_window(dc,s,r,L"Inactive Window",FALSE,FALSE);
        SetRect(&r,18,32,w-8,h-20); sample_window(dc,s,r,L"Active Window",TRUE,FALSE);
        SetRect(&r,(w-148)/2,60,(w+148)/2,min(h-26,158)); sample_window(dc,s,r,L"Message Box",TRUE,TRUE);
    }
}

static LRESULT CALLBACK preview_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PREVIEW_STATE *s=(PREVIEW_STATE *)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    switch(msg)
    {
    case WM_NCCREATE:
        s=calloc(1,sizeof(*s)); if(!s) return FALSE;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)s); return TRUE;
    case WM_NCDESTROY:
        release_state(s); free(s); SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps; RECT rc; HDC dc=BeginPaint(hwnd,&ps),mem=CreateCompatibleDC(dc);
        HBITMAP bm,old;
        GetClientRect(hwnd,&rc); bm=CreateCompatibleBitmap(dc,max(1,rc.right),max(1,rc.bottom)); old=SelectObject(mem,bm);
        draw_sample(hwnd,mem,&rc,s); BitBlt(dc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);
        SelectObject(mem,old); DeleteObject(bm); DeleteDC(mem); EndPaint(hwnd,&ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void preview_register_class(HINSTANCE instance)
{
    WNDCLASSW wc={0}; wc.lpfnWndProc=preview_wnd_proc; wc.hInstance=instance; wc.lpszClassName=PREVIEW_CLASS;
    wc.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW); RegisterClassW(&wc);
}
void preview_unregister_class(HINSTANCE instance) { UnregisterClassW(PREVIEW_CLASS,instance); }
/* Installed theme discovery, shared by Themes and Appearance. */
static int find_files(const WCHAR *folder,const WCHAR *extension,WCHAR paths[][MAX_PATH],int count,int limit,int depth)
{
    WCHAR mask[MAX_PATH],path[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE find;
    if(lstrlenW(folder)+3>=MAX_PATH) return count;
    swprintf(mask,MAX_PATH,L"%s\\*",folder); find=FindFirstFileW(mask,&fd);
    if(find==INVALID_HANDLE_VALUE) return count;
    do {
        if(fd.cFileName[0]=='.') continue;
        if(lstrlenW(folder)+lstrlenW(fd.cFileName)+2>=MAX_PATH) continue;
        swprintf(path,MAX_PATH,L"%s\\%s",folder,fd.cFileName);
        if((fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && depth<4 && !(fd.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))
            count=find_files(path,extension,paths,count,limit,depth+1);
        else if(count<limit && !wcsicmp(PathFindExtensionW(path),extension)) lstrcpyW(paths[count++],path);
    } while(count<limit && FindNextFileW(find,&fd));
    FindClose(find); return count;
}
int preview_find_files(const WCHAR *extension,WCHAR paths[][MAX_PATH],int limit)
{
    WCHAR folder[MAX_PATH],base[MAX_PATH]; int count;
    GetWindowsDirectoryW(base,MAX_PATH); swprintf(folder,MAX_PATH,L"%s\\Resources\\Themes",base);
    count=find_files(folder,extension,paths,0,limit,0);
    if(SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_APPDATA,NULL,SHGFP_TYPE_CURRENT,base)))
    {
        swprintf(folder,MAX_PATH,L"%s\\Microsoft\\Windows\\Themes",base);
        count=find_files(folder,extension,paths,count,limit,0);
    }
    return count;
}
