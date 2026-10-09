/* Appearance choices are reported by installed visual style files. */
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include "resource.h"
#include "preview.h"
#define MAX_STYLES 96
#define MAX_CHOICES 32
#define WEX_APPLY_THEME 0x57455448
typedef struct { WCHAR name[MAX_PATH+1], display[MAX_PATH+1], tip[MAX_PATH+1]; } THEME_NAMES;
typedef HRESULT (WINAPI *ENUM_CHOICE)(LPWSTR,LPWSTR,DWORD,THEME_NAMES *);
static WCHAR styles[MAX_STYLES][MAX_PATH], classic[MAX_CHOICES][MAX_PATH];
static int style_count,classic_count;
static THEME_NAMES colors[MAX_CHOICES],sizes[MAX_CHOICES];
static int color_count,size_count;
static PREVIEW_DATA pending;
static WCHAR classic_selected[MAX_PATH];
static BOOL dirty;

static void update_preview(HWND hwnd)
{
    int style=SendDlgItemMessageW(hwnd,IDC_APP_STYLE,CB_GETCURSEL,0,0);
    int color=SendDlgItemMessageW(hwnd,IDC_APP_COLORSCHEME,CB_GETCURSEL,0,0);
    int size=SendDlgItemMessageW(hwnd,IDC_APP_FONTSIZE,CB_GETCURSEL,0,0);
    if(style<0 || style>=style_count) return;
    if(!style && color>=0 && color<classic_count)
    { lstrcpyW(classic_selected,classic[color]); preview_theme_data(&pending,classic[color]); }
    lstrcpyW(pending.theme_path,styles[style]);
    if(style && color>=0 && color<color_count) lstrcpynW(pending.color_name,colors[color].name,64);
    if(style && size>=0 && size<size_count) lstrcpynW(pending.size_name,sizes[size].name,64);
    pending.font_size=max(0,size);
    preview_update(GetDlgItem(hwnd,IDC_APP_PREVIEW),&pending);
}

static void fill_choices(HWND hwnd, BOOL reset)
{
    HMODULE module=GetModuleHandleW(L"uxtheme.dll");
    ENUM_CHOICE enum_color=(ENUM_CHOICE)GetProcAddress(module,MAKEINTRESOURCEA(9));
    ENUM_CHOICE enum_size=(ENUM_CHOICE)GetProcAddress(module,MAKEINTRESOURCEA(10));
    int style=SendDlgItemMessageW(hwnd,IDC_APP_STYLE,CB_GETCURSEL,0,0), i,selection=0;
    WCHAR name[MAX_PATH];
    color_count=size_count=0;
    SendDlgItemMessageW(hwnd,IDC_APP_COLORSCHEME,CB_RESETCONTENT,0,0);
    SendDlgItemMessageW(hwnd,IDC_APP_FONTSIZE,CB_RESETCONTENT,0,0);
    if(style<0 || style>=style_count)
    {
        EnableWindow(GetDlgItem(hwnd,IDC_APP_COLORSCHEME),FALSE);
        EnableWindow(GetDlgItem(hwnd,IDC_APP_FONTSIZE),FALSE);
        return;
    }
    if(style)
    {
        for(i=0;i<MAX_CHOICES && enum_color && SUCCEEDED(enum_color(styles[style],NULL,i,&colors[i]));i++)
        {
            SendDlgItemMessageW(hwnd,IDC_APP_COLORSCHEME,CB_ADDSTRING,0,(LPARAM)(colors[i].display[0]?colors[i].display:colors[i].name));
            if(!reset && !wcsicmp(colors[i].name,pending.color_name)) selection=i;
        }
        color_count=i;
    }
    else
    {
        for(i=0;i<classic_count;i++)
        {
            GetPrivateProfileStringW(L"Theme",L"DisplayName",PathFindFileNameW(classic[i]),name,MAX_PATH,classic[i]);
            SendDlgItemMessageW(hwnd,IDC_APP_COLORSCHEME,CB_ADDSTRING,0,(LPARAM)name);
            if(!reset && !wcsicmp(classic[i],classic_selected)) selection=i;
        }
    }
    SendDlgItemMessageW(hwnd,IDC_APP_COLORSCHEME,CB_SETCURSEL,selection,0);
    selection=0;
    if(style)
    {
        WCHAR *color=color_count?colors[SendDlgItemMessageW(hwnd,IDC_APP_COLORSCHEME,CB_GETCURSEL,0,0)].name:NULL;
        for(i=0;i<MAX_CHOICES && enum_size && SUCCEEDED(enum_size(styles[style],color,i,&sizes[i]));i++)
        {
            SendDlgItemMessageW(hwnd,IDC_APP_FONTSIZE,CB_ADDSTRING,0,(LPARAM)(sizes[i].display[0]?sizes[i].display:sizes[i].name));
            if(!reset && !wcsicmp(sizes[i].name,pending.size_name)) selection=i;
        }
        size_count=i;
    }
    else
    {
        SendDlgItemMessageW(hwnd,IDC_APP_FONTSIZE,CB_ADDSTRING,0,(LPARAM)L"Normal");
        size_count=1;
    }
    SendDlgItemMessageW(hwnd,IDC_APP_FONTSIZE,CB_SETCURSEL,selection,0);
    EnableWindow(GetDlgItem(hwnd,IDC_APP_COLORSCHEME),style?color_count>0:classic_count>0);
    EnableWindow(GetDlgItem(hwnd,IDC_APP_FONTSIZE),size_count>1);
    update_preview(hwnd);
}

static void refresh_styles(HWND hwnd)
{
    WCHAR paths[MAX_STYLES][MAX_PATH],name[MAX_PATH],value[MAX_PATH]; int n,i,selected=pending.theme_path[0]?-1:0;
    styles[0][0]=0; style_count=1;
    SendDlgItemMessageW(hwnd,IDC_APP_STYLE,CB_RESETCONTENT,0,0);
    SendDlgItemMessageW(hwnd,IDC_APP_STYLE,CB_ADDSTRING,0,(LPARAM)L"Windows Classic style");
    n=preview_find_files(L".msstyles",paths,MAX_STYLES-1);
    for(i=0;i<n;i++)
    {
        THEME_NAMES check;
        ENUM_CHOICE enumerate=(ENUM_CHOICE)GetProcAddress(GetModuleHandleW(L"uxtheme.dll"),MAKEINTRESOURCEA(9));
        if(!enumerate || FAILED(enumerate(paths[i],NULL,0,&check))) continue;
        lstrcpyW(styles[style_count],paths[i]);
        lstrcpyW(name,PathFindFileNameW(paths[i])); PathRemoveExtensionW(name);
        if(!wcsicmp(name,L"Luna")) lstrcpyW(name,L"Windows XP style");
        SendDlgItemMessageW(hwnd,IDC_APP_STYLE,CB_ADDSTRING,0,(LPARAM)name);
        if(!wcsicmp(paths[i],pending.theme_path)) selected=style_count;
        style_count++;
    }
    classic_count=0; n=preview_find_files(L".theme",paths,MAX_STYLES);
    for(i=0;i<n && classic_count<MAX_CHOICES;i++)
    {
        GetPrivateProfileStringW(L"VisualStyles",L"Path",L"",value,MAX_PATH,paths[i]);
        if(!value[0]) lstrcpyW(classic[classic_count++],paths[i]);
    }
    SendDlgItemMessageW(hwnd,IDC_APP_STYLE,CB_SETCURSEL,selected,0);
    fill_choices(hwnd,FALSE);
}

static BOOL apply_appearance(HWND hwnd)
{
    WCHAR dir[MAX_PATH],path[MAX_PATH],current[MAX_PATH],section[4096];
    WCHAR wallpaper_style[16]; PREVIEW_DATA live;
    DWORD bytes=sizeof(current); int style,color; COPYDATASTRUCT data;
    if(pending.theme_path[0])
    {
        typedef HRESULT (WINAPI *OPEN)(LPCWSTR,LPCWSTR,LPCWSTR,HANDLE *,DWORD);
        typedef HRESULT (WINAPI *CLOSE)(HANDLE);
        HMODULE module=GetModuleHandleW(L"uxtheme.dll"); HANDLE file;
        OPEN open=(OPEN)GetProcAddress(module,MAKEINTRESOURCEA(2));
        CLOSE close=(CLOSE)GetProcAddress(module,MAKEINTRESOURCEA(3));
        if(!open || !close || FAILED(open(pending.theme_path,pending.color_name,pending.size_name,&file,0))) return FALSE;
        close(file);
    }
    if(FAILED(SHGetFolderPathW(NULL,CSIDL_APPDATA,NULL,SHGFP_TYPE_CURRENT,dir))) return FALSE;
    lstrcatW(dir,L"\\Microsoft\\Windows\\Themes"); SHCreateDirectoryExW(hwnd,dir,NULL);
    swprintf(path,MAX_PATH,L"%s\\WineExplorer Custom.theme",dir);
    current[0]=0; RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes",L"CurrentTheme",RRF_RT_REG_SZ,NULL,current,&bytes);
    if(current[0] && wcsicmp(current,path) && !CopyFileW(current,path,FALSE)) return FALSE;
    preview_init_data(&live);
    swprintf(wallpaper_style,16,L"%d",live.wallpaper_style==1?0:live.wallpaper_style);
    if(!WritePrivateProfileStringW(L"Theme",L"DisplayName",L"Custom",path) ||
       !WritePrivateProfileStringW(L"VisualStyles",L"Path",pending.theme_path,path) ||
       !WritePrivateProfileStringW(L"VisualStyles",L"ColorStyle",pending.color_name,path) ||
       !WritePrivateProfileStringW(L"VisualStyles",L"Size",pending.size_name,path) ||
       !WritePrivateProfileStringW(L"Control Panel\\Desktop",L"Wallpaper",live.wallpaper_path,path) ||
       !WritePrivateProfileStringW(L"Control Panel\\Desktop",L"WallpaperStyle",wallpaper_style,path) ||
       !WritePrivateProfileStringW(L"Control Panel\\Desktop",L"TileWallpaper",live.wallpaper_style==1?L"1":L"0",path)) return FALSE;
    style=SendDlgItemMessageW(hwnd,IDC_APP_STYLE,CB_GETCURSEL,0,0);
    color=SendDlgItemMessageW(hwnd,IDC_APP_COLORSCHEME,CB_GETCURSEL,0,0);
    if(!style && color>=0 && color<classic_count)
    {
        GetPrivateProfileSectionW(L"Control Panel\\Colors",section,4096,classic[color]);
        if(!WritePrivateProfileSectionW(L"Control Panel\\Colors",section,path)) return FALSE;
    }
    data.dwData=WEX_APPLY_THEME; data.cbData=(lstrlenW(path)+1)*sizeof(WCHAR); data.lpData=path;
    return SendMessageW(FindWindowW(L"Shell_TrayWnd",NULL),WM_COPYDATA,(WPARAM)hwnd,(LPARAM)&data)!=0;
}

INT_PTR CALLBACK appearance_page_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg)
    {
    case WM_INITDIALOG:
        dirty=FALSE;
        /* The first activation below discovers the current style and scheme. */
        return TRUE;
    case WM_COMMAND:
        if(HIWORD(wp)==CBN_DROPDOWN && LOWORD(wp)==IDC_APP_STYLE) { refresh_styles(hwnd); return TRUE; }
        if(HIWORD(wp)==CBN_SELCHANGE)
        {
            if(LOWORD(wp)==IDC_APP_STYLE) fill_choices(hwnd,TRUE);
            else if(LOWORD(wp)==IDC_APP_COLORSCHEME) { update_preview(hwnd); fill_choices(hwnd,FALSE); }
            else if(LOWORD(wp)==IDC_APP_FONTSIZE) update_preview(hwnd);
            else break;
            dirty=TRUE; SendMessageW(GetParent(hwnd),PSM_CHANGED,(WPARAM)hwnd,0); return TRUE;
        }
        break;
    case WM_NOTIFY:
        if(((NMHDR *)lp)->code==PSN_SETACTIVE && !dirty)
        {
            DWORD bytes=sizeof(classic_selected); classic_selected[0]=0;
            RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes",L"CurrentTheme",RRF_RT_REG_SZ,NULL,classic_selected,&bytes);
            preview_init_data(&pending); refresh_styles(hwnd); return TRUE;
        }
        if(((NMHDR *)lp)->code==PSN_APPLY && dirty)
        {
            if(apply_appearance(hwnd)) dirty=FALSE;
            else { MessageBoxW(hwnd,L"The selected appearance could not be applied.",L"Display Properties",MB_OK|MB_ICONEXCLAMATION); SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE); }
            return TRUE;
        }
        break;
    }
    return FALSE;
}
