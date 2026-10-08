/*
 * WineExplorer Desktop Environment - Flexible Start Menu Implementation
 *
 * Implements multi-style Start Menu engine:
 * 1. Classic Server 2003 / NT single-column cascading menu with vertical banner
 * 2. Windows XP / 2003 two-column layout (pinned/recent on left, shell folders on right)
 * 3. Open-Shell inspired modern hybrid layout with integrated search box
 * Authentic Start button with Windows flag icon and theme-aware rendering.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <wexhost.h>

#include "wine/debug.h"
#include "resource.h"
#include "theme_engine.h"
#include "startmenu_view.h"
#include "taskbar_props.h"
#include "wlan_dialog.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define STARTBUTTON_CLASS L"Start"
#define STARTMENU_WND_CLASS L"WineStartMenuWnd"
#define STARTMENU_BANNER_CLASS L"WineStartMenuBanner"

static HWND g_startbutton_hwnd = NULL;
static HWND g_startmenu_wnd = NULL;
static HWND g_banner_wnd = NULL;
static BOOL g_startmenu_open = FALSE;
static BOOL g_button_pressed = FALSE;
static BOOL g_button_hovered = FALSE;
static HICON g_winlogo_icon = NULL;
static DWORD g_last_close_tick = 0;
static HHOOK g_mouse_hook = NULL;

extern void power_mgmt_show_shutdown_dialog(HWND parent);

#define MAX_PROGRAM_ENTRIES 256
static WCHAR g_program_paths[MAX_PROGRAM_ENTRIES][MAX_PATH];
static UINT g_program_count = 0;

static LRESULT CALLBACK mouse_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && g_startmenu_open)
    {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_NCLBUTTONDOWN ||
            wParam == WM_RBUTTONDOWN || wParam == WM_NCRBUTTONDOWN)
        {
            MOUSEHOOKSTRUCT *mhs = (MOUSEHOOKSTRUCT *)lParam;
            if (mhs->hwnd != g_startmenu_wnd && !IsChild(g_startmenu_wnd, mhs->hwnd))
            {
                WCHAR cls[64];
                cls[0] = L'\0';
                GetClassNameW(mhs->hwnd, cls, ARRAY_SIZE(cls));
                if (lstrcmpW(cls, L"#32768") != 0)
                {
                    startmenu_view_hide();
                }
            }
        }
    }
    return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
}

/* The name shown for the user: the account's display name (AccountsService) when there is one, cached so
 * painting never talks to D-Bus. */
static void get_display_user_name(WCHAR *buf, DWORD count)
{
    static WCHAR cached[128];
    static BOOL done;

    if (!done)
    {
        WEX_USER u;
        DWORD len = ARRAY_SIZE(cached);

        done = TRUE;
        if (!WexUserGet(&u) && u.real_name[0])
            MultiByteToWideChar(CP_UTF8, 0, u.real_name, -1, cached, ARRAY_SIZE(cached));
        else if (!GetUserNameW(cached, &len))
            lstrcpyW(cached, L"User");
    }
    lstrcpynW(buf, cached, count);
}

/* --- Helpers for Loading Shell and System Icons --- */

static HICON load_shell_icon(int index, int size)
{
    HICON hLarge = NULL;
    HICON hSmall = NULL;
    UINT count = ExtractIconExW(L"shell32.dll", index, &hLarge, &hSmall, 1);
    if (count > 0)
    {
        if (size >= 32 && hLarge)
        {
            if (hSmall) DestroyIcon(hSmall);
            return hLarge;
        }
        else if (hSmall)
        {
            if (hLarge) DestroyIcon(hLarge);
            return hSmall;
        }
        else if (hLarge)
        {
            return hLarge;
        }
    }
    return NULL;
}

static HICON load_app_icon(const WCHAR *exe_name, int fallback_shell_idx, int size)
{
    HICON hLarge = NULL;
    HICON hSmall = NULL;
    UINT count = ExtractIconExW(exe_name, 0, &hLarge, &hSmall, 1);
    if (count > 0)
    {
        if (size >= 32 && hLarge)
        {
            if (hSmall) DestroyIcon(hSmall);
            return hLarge;
        }
        else if (hSmall)
        {
            if (hLarge) DestroyIcon(hLarge);
            return hSmall;
        }
        else if (hLarge)
        {
            return hLarge;
        }
    }
    return load_shell_icon(fallback_shell_idx, size);
}

#define MAX_MENU_BITMAPS 512
static HBITMAP g_menu_bitmaps[MAX_MENU_BITMAPS];
static UINT g_menu_bitmap_count = 0;

static void free_menu_bitmaps(void)
{
    UINT i;
    for (i = 0; i < g_menu_bitmap_count; i++)
    {
        if (g_menu_bitmaps[i])
            DeleteObject(g_menu_bitmaps[i]);
    }
    g_menu_bitmap_count = 0;
}

static HBITMAP icon_to_menu_bitmap(HICON hIcon, int size)
{
    HDC screen_dc, hdc;
    HBITMAP hbm, old_bm;
    RECT rc;

    if (!hIcon) return NULL;

    rc.left = 0;
    rc.top = 0;
    rc.right = size;
    rc.bottom = size;

    screen_dc = GetDC(NULL);
    hdc = CreateCompatibleDC(screen_dc);
    hbm = CreateCompatibleBitmap(screen_dc, size, size);
    old_bm = SelectObject(hdc, hbm);

    FillRect(hdc, &rc, GetSysColorBrush(COLOR_MENU));
    DrawIconEx(hdc, 0, 0, hIcon, size, size, 0, NULL, DI_NORMAL);

    SelectObject(hdc, old_bm);
    DeleteDC(hdc);
    ReleaseDC(NULL, screen_dc);
    DestroyIcon(hIcon);
    return hbm;
}

static HBITMAP get_shell32_icon_bitmap(int index, int size)
{
    HICON hIcon = load_shell_icon(index, size);
    return icon_to_menu_bitmap(hIcon, size);
}

static HBITMAP get_file_icon_bitmap(const WCHAR *file_path, int size)
{
    SHFILEINFOW sfi;
    HICON hIcon = NULL;

    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoW(file_path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon)
    {
        hIcon = sfi.hIcon;
    }
    else
    {
        WCHAR target[MAX_PATH];
        IShellLinkW *psl;
        IPersistFile *ppf;

        target[0] = L'\0';
        if (PathMatchSpecW(file_path, L"*.lnk") &&
            SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_IShellLinkW, (void **)&psl)))
        {
            if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void **)&ppf)))
            {
                if (SUCCEEDED(ppf->lpVtbl->Load(ppf, file_path, STGM_READ)))
                {
                    psl->lpVtbl->GetPath(psl, target, MAX_PATH, NULL, 0);
                }
                ppf->lpVtbl->Release(ppf);
            }
            psl->lpVtbl->Release(psl);
        }

        if (target[0])
            ExtractIconExW(target, 0, NULL, &hIcon, 1);
        if (!hIcon)
            hIcon = load_shell_icon(0, size);
    }

    return icon_to_menu_bitmap(hIcon, size);
}

static void append_menu_item_icon(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem,
                                  LPCWSTR lpNewItem, HBITMAP hbm)
{
    MENUITEMINFOW mii;
    memset(&mii, 0, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_ID;
    mii.fType = MFT_STRING;
    mii.wID = (UINT)uIDNewItem;
    mii.dwTypeData = (LPWSTR)lpNewItem;

    if (uFlags & MF_POPUP)
    {
        mii.fMask |= MIIM_SUBMENU;
        mii.hSubMenu = (HMENU)uIDNewItem;
    }

    if (hbm)
    {
        mii.fMask |= MIIM_BITMAP;
        mii.hbmpItem = hbm;
        if (g_menu_bitmap_count < MAX_MENU_BITMAPS)
            g_menu_bitmaps[g_menu_bitmap_count++] = hbm;
    }

    InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);
}

static HMENU find_existing_submenu(HMENU hMenu, const WCHAR *name)
{
    int count = GetMenuItemCount(hMenu);
    int i;
    for (i = 0; i < count; i++)
    {
        HMENU hSub = GetSubMenu(hMenu, i);
        if (hSub)
        {
            WCHAR buf[256];
            buf[0] = L'\0';
            if (GetMenuStringW(hMenu, i, buf, ARRAY_SIZE(buf), MF_BYPOSITION) > 0)
            {
                if (_wcsicmp(buf, name) == 0)
                    return hSub;
            }
        }
    }
    return NULL;
}

static BOOL menu_has_item(HMENU hMenu, const WCHAR *title)
{
    int count = GetMenuItemCount(hMenu);
    int i;
    for (i = 0; i < count; i++)
    {
        WCHAR buf[256];
        buf[0] = L'\0';
        if (GetMenuStringW(hMenu, i, buf, ARRAY_SIZE(buf), MF_BYPOSITION) > 0)
        {
            if (_wcsicmp(buf, title) == 0)
                return TRUE;
        }
    }
    return FALSE;
}

static void create_shortcut(const WCHAR *dir, const WCHAR *name, const WCHAR *target)
{
    WCHAR lnk_path[MAX_PATH];
    WCHAR full_target[MAX_PATH];
    IShellLinkW *psl;
    IPersistFile *ppf;

    _snwprintf(lnk_path, MAX_PATH, L"%s\\%s.lnk", dir, name);
    if (GetFileAttributesW(lnk_path) != INVALID_FILE_ATTRIBUTES)
        return;

    if (!SearchPathW(NULL, target, L".exe", MAX_PATH, full_target, NULL))
        lstrcpynW(full_target, target, MAX_PATH);

    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void **)&psl)))
    {
        psl->lpVtbl->SetPath(psl, full_target);
        psl->lpVtbl->SetIconLocation(psl, full_target, 0);
        if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void **)&ppf)))
        {
            ppf->lpVtbl->Save(ppf, lnk_path, TRUE);
            ppf->lpVtbl->Release(ppf);
        }
        psl->lpVtbl->Release(psl);
    }
}

static void ensure_default_shortcuts(void)
{
    static BOOL initialized = FALSE;
    WCHAR prog_dir[MAX_PATH];
    WCHAR acc_dir[MAX_PATH];
    WCHAR ent_dir[MAX_PATH];
    WCHAR sys_dir[MAX_PATH];
    WCHAR adm_dir[MAX_PATH];
    WCHAR gam_dir[MAX_PATH];
    WCHAR stu_dir[MAX_PATH];

    if (initialized) return;
    initialized = TRUE;

    CoInitialize(NULL);

    if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, prog_dir) != S_OK &&
        SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, prog_dir) != S_OK)
        return;

    _snwprintf(acc_dir, MAX_PATH, L"%s\\Accessories", prog_dir);
    CreateDirectoryW(acc_dir, NULL);
    _snwprintf(ent_dir, MAX_PATH, L"%s\\Entertainment", acc_dir);
    CreateDirectoryW(ent_dir, NULL);
    _snwprintf(sys_dir, MAX_PATH, L"%s\\System Tools", acc_dir);
    CreateDirectoryW(sys_dir, NULL);
    _snwprintf(adm_dir, MAX_PATH, L"%s\\Administrative Tools", prog_dir);
    CreateDirectoryW(adm_dir, NULL);
    _snwprintf(gam_dir, MAX_PATH, L"%s\\Games", prog_dir);
    CreateDirectoryW(gam_dir, NULL);
    _snwprintf(stu_dir, MAX_PATH, L"%s\\StartUp", prog_dir);
    CreateDirectoryW(stu_dir, NULL);

    /* Accessories items */
    create_shortcut(acc_dir, L"Calculator", L"calc.exe");
    create_shortcut(acc_dir, L"Paint", L"mspaint.exe");
    create_shortcut(acc_dir, L"Notepad", L"notepad.exe");
    create_shortcut(acc_dir, L"WordPad", L"wordpad.exe");
    create_shortcut(acc_dir, L"Command Prompt", L"cmd.exe");
    create_shortcut(acc_dir, L"Windows Explorer", L"explorer.exe");

    /* Entertainment items */
    create_shortcut(ent_dir, L"Volume Control", L"sndvol32.exe");
    create_shortcut(ent_dir, L"Windows Media Player", L"wmplayer.exe");

    /* System Tools items */
    create_shortcut(sys_dir, L"Task Manager", L"taskmgr.exe");
    create_shortcut(sys_dir, L"Registry Editor", L"regedit.exe");

    /* Administrative Tools items */
    create_shortcut(adm_dir, L"Wine Configuration", L"winecfg.exe");

    /* Games items */
    create_shortcut(gam_dir, L"Minesweeper", L"winemine.exe");

    /* Root Programs items (XP standard: direct shortcuts under folders) */
    create_shortcut(prog_dir, L"Internet Explorer", L"iexplore.exe");
    create_shortcut(prog_dir, L"Windows Media Player", L"wmplayer.exe");
    create_shortcut(prog_dir, L"Notepad", L"notepad.exe");
}

/* Build Programs submenu by enumerating filesystem directory */
static void populate_programs_menu(HMENU hMenu, const WCHAR *path)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    WCHAR search_path[MAX_PATH];

    ensure_default_shortcuts();

    _snwprintf(search_path, MAX_PATH, L"%s\\*", path);

    /* Pass 1: Directories / Submenus */
    hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            WCHAR item_path[MAX_PATH];
            HMENU hSub;

            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || !wcscmp(fd.cFileName, L"desktop.ini"))
                continue;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;

            _snwprintf(item_path, MAX_PATH, L"%s\\%s", path, fd.cFileName);
            hSub = find_existing_submenu(hMenu, fd.cFileName);
            if (!hSub)
            {
                HBITMAP hbm = get_shell32_icon_bitmap(3, 16);
                hSub = CreatePopupMenu();
                append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hSub, fd.cFileName, hbm);
            }
            populate_programs_menu(hSub, item_path);
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }

    /* Pass 2: Shortcuts / Executables */
    hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            WCHAR item_path[MAX_PATH];
            WCHAR title[MAX_PATH];
            WCHAR *ext;

            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || !wcscmp(fd.cFileName, L"desktop.ini"))
                continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            if (PathMatchSpecW(fd.cFileName, L"*.lnk") || PathMatchSpecW(fd.cFileName, L"*.exe"))
            {
                _snwprintf(item_path, MAX_PATH, L"%s\\%s", path, fd.cFileName);
                lstrcpynW(title, fd.cFileName, MAX_PATH);
                ext = wcsrchr(title, L'.');
                if (ext) *ext = L'\0';

                if (!menu_has_item(hMenu, title) && g_program_count < MAX_PROGRAM_ENTRIES)
                {
                    HBITMAP hbm = get_file_icon_bitmap(item_path, 16);
                    lstrcpynW(g_program_paths[g_program_count], item_path, MAX_PATH);
                    append_menu_item_icon(hMenu, MF_STRING, 2000 + g_program_count, title, hbm);
                    g_program_count++;
                }
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }
}

/* ========================================================================= */
/*                         CLASSIC START MENU BANNER                         */
/* ========================================================================= */

static LRESULT CALLBACK startmenu_banner_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HFONT font_bold, font_norm, old_font;
        LOGFONTW lf;
        int h, y;

        GetClientRect(hwnd, &rc);
        h = rc.bottom - rc.top;

        /* Paint authentic Windows XP Classic banner gradient:
         * Black at top, transitioning through dark navy to vibrant blue, back to navy at bottom */
        for (y = 0; y < h; y++)
        {
            RECT line_rc;
            COLORREF col;
            float t = (float)y / (float)(h > 0 ? h : 1);

            line_rc.left = rc.left;
            line_rc.top = y;
            line_rc.right = rc.right;
            line_rc.bottom = y + 1;

            if (t < 0.35f)
            {
                float local_t = t / 0.35f;
                col = RGB(0, 0, (int)(128.0f * local_t));
            }
            else if (t < 0.80f)
            {
                float local_t = (t - 0.35f) / 0.45f;
                int r = (int)(16.0f * local_t);
                int g = (int)(132.0f * local_t);
                int b = 128 + (int)(80.0f * local_t);
                col = RGB(r, g, b);
            }
            else
            {
                float local_t = (t - 0.80f) / 0.20f;
                int r = (int)(16.0f * (1.0f - local_t));
                int g = (int)(132.0f * (1.0f - local_t));
                int b = 208 - (int)(80.0f * local_t);
                col = RGB(r, g, b);
            }

            SetBkColor(hdc, col);
            ExtTextOutW(hdc, 0, y, ETO_OPAQUE, &line_rc, NULL, 0, NULL);
        }

        /* 3D Bevel Edge: Top, Left, Bottom (flush with menu on right) */
        DrawEdge(hdc, &rc, EDGE_RAISED, BF_TOP | BF_LEFT | BF_BOTTOM);

        /* Rotated 90-degree vertical typography: "Windows XP" (Bold) and "Professional" (Regular) */
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -14;
        lf.lfEscapement = 900;
        lf.lfOrientation = 900;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        font_bold = CreateFontIndirectW(&lf);

        lf.lfWeight = FW_NORMAL;
        font_norm = CreateFontIndirectW(&lf);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        old_font = SelectObject(hdc, font_bold);
        TextOutW(hdc, 4, h - 8, L"Windows XP", 10);

        SelectObject(hdc, font_norm);
        TextOutW(hdc, 4, h - 110, L"Professional", 12);

        SelectObject(hdc, old_font);
        DeleteObject(font_bold);
        DeleteObject(font_norm);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

static void ensure_banner_class(void)
{
    static BOOL registered = FALSE;
    if (!registered)
    {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = startmenu_banner_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = STARTMENU_BANNER_CLASS;
        wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
        RegisterClassW(&wc);
        registered = TRUE;
    }
}

void startmenu_on_enter_menu_loop(void)
{
    HWND hMenuWnd = FindWindowW(L"#32768", NULL);
    if (hMenuWnd && g_banner_wnd)
    {
        RECT rcMenu;
        if (GetWindowRect(hMenuWnd, &rcMenu))
        {
            int menu_h = rcMenu.bottom - rcMenu.top;
            if (menu_h > 50)
            {
                int banner_w = 22;
                int banner_overlap = 2;
                SetWindowPos(g_banner_wnd, HWND_TOPMOST,
                             rcMenu.left - banner_w + banner_overlap, rcMenu.top,
                             banner_w, menu_h,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);
                InvalidateRect(g_banner_wnd, NULL, TRUE);
            }
        }
    }
}

/* XP opens the Run box in the lower-left corner just above the taskbar when started from the Start menu.
 * shell32 centers it, so move it from a CBT hook the first time its window is activated. */
static HHOOK g_run_hook;
static HWND g_run_owner;

static LRESULT CALLBACK run_cbt_proc(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HCBT_ACTIVATE)
    {
        HWND dlg = (HWND)wparam;
        WCHAR cls[16];

        if (GetClassNameW(dlg, cls, ARRAY_SIZE(cls)) && !wcscmp(cls, L"#32770"))
        {
            RECT rc, tray;
            GetWindowRect(dlg, &rc);
            GetWindowRect(g_run_owner, &tray);
            SetWindowPos(dlg, NULL, tray.left + 14, tray.top - (rc.bottom - rc.top) - 14,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            UnhookWindowsHookEx(g_run_hook);
            g_run_hook = NULL;
        }
    }
    return CallNextHookEx(g_run_hook, code, wparam, lparam);
}

static void run_dialog_inproc(HWND owner)
{
    void (WINAPI *pRunFileDlg)(HWND owner, HICON icon, const WCHAR *dir,
                               const WCHAR *title, const WCHAR *desc, DWORD flags);
    HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
    if (hShell32)
    {
        pRunFileDlg = (void *)GetProcAddress(hShell32, (LPCSTR)61);
        if (pRunFileDlg)
        {
            g_run_owner = owner;
            g_run_hook = SetWindowsHookExW(WH_CBT, run_cbt_proc, NULL, GetCurrentThreadId());
            pRunFileDlg(owner, NULL, NULL, NULL, NULL, 0);
            if (g_run_hook) { UnhookWindowsHookEx(g_run_hook); g_run_hook = NULL; }
        }
        FreeLibrary(hShell32);
    }
}

/* ========================================================================= */
/*                         CLASSIC START MENU POPUP                          */
/* ========================================================================= */

static void show_classic_start_menu(HWND tray_hwnd, const RECT *btn_rect)
{
    HMENU hMenu = CreatePopupMenu();
    HMENU hPrograms = CreatePopupMenu();
    HMENU hDocuments = CreatePopupMenu();
    HMENU hSettings = CreatePopupMenu();
    HMENU hSearch = CreatePopupMenu();
    WCHAR user_programs[MAX_PATH];
    WCHAR common_programs[MAX_PATH];
    WCHAR my_docs[MAX_PATH];
    WCHAR username[128];
    WCHAR logoff_label[256];
    DWORD ulen = sizeof(username)/sizeof(WCHAR);
    UINT cmd;
    int banner_w = 22;
    int banner_overlap = 2;
    int estimated_h = 360;
    int banner_x = btn_rect->left;
    int banner_y = btn_rect->top - estimated_h;

    HBITMAP bm_progacc;
    HBITMAP bm_catalog;
    HBITMAP bm_update;
    HBITMAP bm_prog;
    HBITMAP bm_doc;
    HBITMAP bm_set;
    HBITMAP bm_srch;
    HBITMAP bm_help;
    HBITMAP bm_run;
    HBITMAP bm_logoff;
    HBITMAP bm_shut;

    g_program_count = 0;

    /* Current username */
    get_display_user_name(username, ARRAY_SIZE(username));
    _snwprintf(logoff_label, 256, L"&Log Off %s...", username);

    /* Enumerate Programs hierarchy */
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, user_programs) == S_OK)
        populate_programs_menu(hPrograms, user_programs);
    if (SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, common_programs) == S_OK)
        populate_programs_menu(hPrograms, common_programs);

    /* Documents Submenu */
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, my_docs) == S_OK)
        append_menu_item_icon(hDocuments, MF_STRING, STARTMENU_CMD_MYDOCS, L"My &Documents", get_shell32_icon_bitmap(1, 16));

    /* Settings Submenu */
    append_menu_item_icon(hSettings, MF_STRING, STARTMENU_CMD_CONTROLPANEL, L"&Control Panel", get_shell32_icon_bitmap(21, 16));
    append_menu_item_icon(hSettings, MF_STRING, STARTMENU_CMD_NETWORK, L"&Network Connections", get_shell32_icon_bitmap(17, 16));
    append_menu_item_icon(hSettings, MF_STRING, STARTMENU_CMD_PRINTERS, L"&Printers and Faxes", get_shell32_icon_bitmap(16, 16));
    AppendMenuW(hSettings, MF_SEPARATOR, 0, NULL);
    append_menu_item_icon(hSettings, MF_STRING, STARTMENU_CMD_TASKBAR_PROPS, L"&Taskbar and Start Menu", get_shell32_icon_bitmap(39, 16));

    /* Search Submenu */
    append_menu_item_icon(hSearch, MF_STRING, STARTMENU_CMD_SEARCH, L"For &Files or Folders...", get_shell32_icon_bitmap(22, 16));
    append_menu_item_icon(hSearch, MF_STRING, STARTMENU_CMD_SEARCH_COMPUTERS, L"For &Computers...", get_shell32_icon_bitmap(15, 16));

    /* Load authentic icon bitmaps from shell32 (24x24 for authentic Windows XP classic scale) */
    bm_progacc = get_shell32_icon_bitmap(271, 24); /* Set Program Access and Defaults */
    if (!bm_progacc) bm_progacc = get_shell32_icon_bitmap(21, 24);
    bm_catalog = get_shell32_icon_bitmap(14, 24);  /* Windows Catalog */
    bm_update  = get_shell32_icon_bitmap(46, 24);  /* Windows Update */
    bm_prog    = get_shell32_icon_bitmap(3, 24);   /* Programs */
    bm_doc     = get_shell32_icon_bitmap(1, 24);   /* Documents */
    bm_set     = get_shell32_icon_bitmap(21, 24);  /* Settings */
    bm_srch    = get_shell32_icon_bitmap(22, 24);  /* Search */
    bm_help    = get_shell32_icon_bitmap(23, 24);  /* Help */
    bm_run     = get_shell32_icon_bitmap(24, 24);  /* Run */
    bm_logoff  = get_shell32_icon_bitmap(44, 24);  /* Log Off key */
    bm_shut    = get_shell32_icon_bitmap(27, 24);  /* Turn Off red button */

    /* Top Pinned Items */
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_PROGACCESS, L"Set &Program Access and Defaults", bm_progacc);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_CATALOG, L"Windows &Catalog", bm_catalog);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_UPDATE, L"Windows &Update", bm_update);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* Cascading and Single Standard Items */
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hPrograms, L"&Programs", bm_prog);
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hDocuments, L"&Documents", bm_doc);
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hSettings, L"&Settings", bm_set);
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hSearch, L"Sea&rch", bm_srch);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_HELP, L"&Help and Support", bm_help);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_RUN, L"&Run...", bm_run);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_LOGOFF, logoff_label, bm_logoff);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_SHUTDOWN, L"T&urn Off Computer...", bm_shut);

    /* Show Companion Vertical Gradient Banner Window */
    ensure_banner_class();
    if (!g_banner_wnd)
    {
        g_banner_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                       STARTMENU_BANNER_CLASS, NULL,
                                       WS_POPUP | WS_CLIPSIBLINGS,
                                       banner_x, banner_y, banner_w, estimated_h,
                                       tray_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    }
    else
    {
        SetWindowPos(g_banner_wnd, HWND_TOPMOST, banner_x, banner_y, banner_w, estimated_h,
                     SWP_NOACTIVATE);
    }
    ShowWindow(g_banner_wnd, SW_SHOWNOACTIVATE);

    g_startmenu_open = TRUE;
    g_button_pressed = TRUE;
    InvalidateRect(g_startbutton_hwnd, NULL, FALSE);
    UpdateWindow(g_startbutton_hwnd);

    SetForegroundWindow(tray_hwnd);
    SetTimer(tray_hwnd, 9999, 15, NULL);
    cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                         btn_rect->left + banner_w - banner_overlap, btn_rect->top, 0, tray_hwnd, NULL);
    KillTimer(tray_hwnd, 9999);

    ShowWindow(g_banner_wnd, SW_HIDE);
    g_startmenu_open = FALSE;
    g_button_pressed = FALSE;
    g_last_close_tick = GetTickCount();
    InvalidateRect(g_startbutton_hwnd, NULL, FALSE);
    UpdateWindow(g_startbutton_hwnd);

    DestroyMenu(hMenu);
    free_menu_bitmaps();

    switch (cmd)
    {
    case STARTMENU_CMD_PROGACCESS:
        ShellExecuteW(NULL, L"open", L"control.exe", L"appwiz.cpl,,3", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_CATALOG:
        ShellExecuteW(NULL, L"open", L"iexplore.exe", L"http://go.microsoft.com/fwlink/?LinkId=374", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_UPDATE:
        ShellExecuteW(NULL, L"open", L"wupdmgr.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_RUN:
        run_dialog_inproc(tray_hwnd);
        break;
    case STARTMENU_CMD_SHUTDOWN:
        power_mgmt_show_shutdown_dialog(tray_hwnd);
        break;
    case STARTMENU_CMD_LOGOFF:
        if (MessageBoxW(tray_hwnd, L"Are you sure you want to log off?", L"Log Off Windows",
                        MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            ExitWindowsEx(EWX_LOGOFF, 0);
        }
        break;
    case STARTMENU_CMD_CONTROLPANEL:
        ShellExecuteW(NULL, L"open", L"control.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_NETWORK:
        ShellExecuteW(NULL, L"open", L"control.exe", L"ncpa.cpl", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_PRINTERS:
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{2227a280-3aea-1069-a2de-08002b30309d}", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_HELP:
        ShellExecuteW(NULL, L"open", L"hh.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_SEARCH:
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{e17d4fc0-5564-11d1-83f2-00a0c90dc849}", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_SEARCH_COMPUTERS:
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{e17d4fc0-5564-11d1-83f2-00a0c90dc849}", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_TASKBAR_PROPS:
        taskbar_props_show(tray_hwnd);
        break;
    case STARTMENU_CMD_MYDOCS:
    {
        WCHAR doc_path[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, doc_path) == S_OK)
        {
            WCHAR param[MAX_PATH * 2];
            _snwprintf(param, ARRAY_SIZE(param), L"\"%s\"", doc_path);
            ShellExecuteW(NULL, L"open", L"explorer.exe", param, NULL, SW_SHOWNORMAL);
        }
        break;
    }
    default:
        if (cmd >= 2000 && (cmd - 2000) < g_program_count)
        {
            ShellExecuteW(NULL, L"open", g_program_paths[cmd - 2000], NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    }
}

/* ========================================================================= */
/*                   TWO-COLUMN WINDOWS XP START MENU                        */
/* ========================================================================= */

static HWND g_tray_parent = NULL;

/* ---- Start panel: layout and drawing come from the visual style's StartPanel class ----
 * Sizes, margins, colors and images are read from the active style (DefaultPaneSize, ContentMargins, TextColor,
 * HotTracking ...).  Without a visual style the panel uses plain system colors with the same layout. */

enum { ZONE_USER, ZONE_PROG, ZONE_MORE, ZONE_PLACES, ZONE_LOGOFF, ZONE_COUNT };

#define CMD_ALL_PROGRAMS 100
#define PANEL_PINNED_H   38
#define PANEL_MFU_H      36
#define PANEL_PLACE_H    24
#define PANEL_SEP_H      6
#define PANEL_MAX_ITEMS  48

typedef struct _PANEL_ITEM
{
    RECT rc;
    const WCHAR *title;
    const WCHAR *subtitle;
    const WCHAR *exe;
    int icon_idx;
    int zone;
    UINT cmd;
    BOOL bold;
    BOOL sep;
} PANEL_ITEM;

typedef struct _LEFT_ITEM
{
    const WCHAR *title;
    const WCHAR *exe;
    int shell_icon_idx;
    UINT cmd;
} LEFT_ITEM;

static const LEFT_ITEM g_left_frequent[] =
{
    { L"Windows Media Player", L"wmplayer.exe", -227, 3001 },
    { L"Notepad",              L"notepad.exe",  -152, 3002 },
    { L"Calculator",           L"calc.exe",     -2, 3003 },
    { L"Paint",                L"mspaint.exe",  -226, 3004 },
    { L"Command Prompt",       L"cmd.exe",      -3,  3005 }
};
#define NUM_LEFT_FREQUENT (sizeof(g_left_frequent)/sizeof(g_left_frequent[0]))

typedef struct _RIGHT_ITEM
{
    const WCHAR *title;
    int icon_idx;
    BOOL is_bold;
    BOOL is_separator;
    UINT cmd;
} RIGHT_ITEM;

static const RIGHT_ITEM g_right_items[] =
{
    { L"My Documents",                 -235, TRUE,  FALSE, STARTMENU_CMD_MYDOCS },
    { L"My Recent Documents",          -21, FALSE, FALSE, STARTMENU_CMD_RECENT_DOCS },
    { L"My Pictures",                  -236, FALSE, FALSE, STARTMENU_CMD_PICTURES },
    { L"My Music",                     -237, FALSE, FALSE, STARTMENU_CMD_MUSIC },
    { L"My Computer",                  -16, TRUE,  FALSE, STARTMENU_CMD_MYCOMP },
    { L"My Network Places",            -18, FALSE, FALSE, STARTMENU_CMD_MYNET },
    { NULL,                            0,   FALSE, TRUE,  0 },
    { L"Control Panel",                -137, FALSE, FALSE, STARTMENU_CMD_CONTROLPANEL },
    { L"Set Program Access and Defaults", -37, FALSE, FALSE, STARTMENU_CMD_PROGACCESS },
    { L"Connect To",                   -175, FALSE, FALSE, STARTMENU_CMD_CONNECT_TO },
    { L"Printers and Faxes",           -138, FALSE, FALSE, STARTMENU_CMD_PRINTERS },
    { NULL,                            0,   FALSE, TRUE,  0 },
    { L"Help and Support",             -24, FALSE, FALSE, STARTMENU_CMD_HELP },
    { L"Search",                       -23, FALSE, FALSE, STARTMENU_CMD_SEARCH },
    { L"Run...",                       -25, FALSE, FALSE, STARTMENU_CMD_RUN }
};
#define NUM_RIGHT_ITEMS (sizeof(g_right_items)/sizeof(g_right_items[0]))

static PANEL_ITEM g_panel[PANEL_MAX_ITEMS];
static int g_panel_n;
static int g_panel_hot = -1;
static RECT g_zone[ZONE_COUNT];
static SIZE g_panel_size = { 380, 440 };

static BOOL pane_size(HTHEME theme, int part, int *w, int *h)
{
    RECT r;
    if (!theme || FAILED(GetThemeRect(theme, part, 0, TMT_DEFAULTPANESIZE, &r))) return FALSE;
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return FALSE;
    *w = r.right - r.left;
    *h = r.bottom - r.top;
    return TRUE;
}

static MARGINS content_margins(HTHEME theme, int part)
{
    MARGINS m = {0};
    if (theme) GetThemeMargins(theme, NULL, part, 0, TMT_CONTENTMARGINS, NULL, &m);
    return m;
}

static HFONT panel_font(BOOL bold)
{
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

static int text_width(HDC hdc, HFONT font, const WCHAR *s)
{
    SIZE size;
    HFONT old = SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, s, lstrlenW(s), &size);
    SelectObject(hdc, old);
    return size.cx;
}

static PANEL_ITEM *panel_add(int zone, const RECT *rc, UINT cmd)
{
    PANEL_ITEM *item;
    if (g_panel_n >= PANEL_MAX_ITEMS) return &g_panel[PANEL_MAX_ITEMS - 1];
    item = &g_panel[g_panel_n++];
    memset(item, 0, sizeof(*item));
    item->zone = zone;
    item->rc = *rc;
    item->cmd = cmd;
    return item;
}

/* Build zones and item rectangles from the style's pane sizes */
static void panel_layout(void)
{
    HTHEME theme = theme_open(NULL, L"StartPanel");
    int W = 380, hu = 64, hp = 306, hm = 30, hpl = 336, hl = 40, wl = 190, w, h;
    MARGINS mp, mm, mpl;
    RECT r, c;
    HDC hdc = GetDC(NULL);
    HFONT bold = panel_font(TRUE);
    unsigned int i;
    int y;

    if (pane_size(theme, SPP_USERPANE, &w, &h)) { W = w; hu = h; }
    if (pane_size(theme, SPP_PROGLIST, &w, &h)) { wl = w; hp = h; }
    if (pane_size(theme, SPP_MOREPROGRAMS, &w, &h)) hm = h;
    if (pane_size(theme, SPP_PLACESLIST, &w, &h)) hpl = h;
    if (pane_size(theme, SPP_LOGOFF, &w, &h)) hl = h;
    mp = content_margins(theme, SPP_PROGLIST);
    mm = content_margins(theme, SPP_MOREPROGRAMS);
    mpl = content_margins(theme, SPP_PLACESLIST);
    if (theme) CloseThemeData(theme);

    SetRect(&g_zone[ZONE_USER], 0, 0, W, hu);
    SetRect(&g_zone[ZONE_PROG], 0, hu, wl, hu + hp);
    SetRect(&g_zone[ZONE_MORE], 0, hu + hp, wl, hu + hp + hm);
    SetRect(&g_zone[ZONE_PLACES], wl, hu, W, hu + hpl);
    SetRect(&g_zone[ZONE_LOGOFF], 0, hu + hpl, W, hu + hpl + hl);
    g_panel_size.cx = W;
    g_panel_size.cy = hu + hpl + hl;
    g_panel_n = 0;

    /* programs: pinned browser and mail, separator, most frequently used */
    c = g_zone[ZONE_PROG];
    c.left += mp.cxLeftWidth; c.right -= mp.cxRightWidth; c.top += mp.cyTopHeight; c.bottom -= mp.cyBottomHeight;
    y = c.top;
    SetRect(&r, c.left, y, c.right, y + PANEL_PINNED_H);
    { PANEL_ITEM *it = panel_add(ZONE_PROG, &r, STARTMENU_CMD_INTERNET); it->title = L"Internet"; it->subtitle = L"Web Browser"; it->exe = L"winebrowser.exe"; it->icon_idx = -178; it->bold = TRUE; }
    y += PANEL_PINNED_H;
    SetRect(&r, c.left, y, c.right, y + PANEL_PINNED_H);
    { PANEL_ITEM *it = panel_add(ZONE_PROG, &r, STARTMENU_CMD_EMAIL); it->title = L"E-mail"; it->subtitle = L"Mail Client"; it->exe = L"winebrowser.exe"; it->icon_idx = -178; it->bold = TRUE; }
    y += PANEL_PINNED_H;
    SetRect(&r, c.left, y, c.right, y + PANEL_SEP_H);
    panel_add(ZONE_PROG, &r, 0)->sep = TRUE;
    y += PANEL_SEP_H;
    for (i = 0; i < NUM_LEFT_FREQUENT && y + PANEL_MFU_H <= c.bottom; i++, y += PANEL_MFU_H)
    {
        PANEL_ITEM *it;
        SetRect(&r, c.left, y, c.right, y + PANEL_MFU_H);
        it = panel_add(ZONE_PROG, &r, g_left_frequent[i].cmd);
        it->title = g_left_frequent[i].title;
        it->exe = g_left_frequent[i].exe;
        it->icon_idx = g_left_frequent[i].shell_icon_idx;
        it->bold = TRUE;
    }

    /* "All Programs" */
    c = g_zone[ZONE_MORE];
    c.left += mm.cxLeftWidth; c.right -= mm.cxRightWidth; c.top += mm.cyTopHeight; c.bottom -= mm.cyBottomHeight;
    { PANEL_ITEM *it = panel_add(ZONE_MORE, &c, CMD_ALL_PROGRAMS); it->title = L"All Programs"; it->bold = TRUE; }

    /* places */
    c = g_zone[ZONE_PLACES];
    c.left += mpl.cxLeftWidth; c.right -= mpl.cxRightWidth; c.top += mpl.cyTopHeight; c.bottom -= mpl.cyBottomHeight;
    y = c.top;
    for (i = 0; i < NUM_RIGHT_ITEMS; i++)
    {
        PANEL_ITEM *it;
        int ih = g_right_items[i].is_separator ? PANEL_SEP_H : PANEL_PLACE_H;
        SetRect(&r, c.left, y, c.right, y + ih);
        it = panel_add(ZONE_PLACES, &r, g_right_items[i].cmd);
        it->title = g_right_items[i].title;
        it->icon_idx = g_right_items[i].icon_idx;
        it->bold = g_right_items[i].is_bold;
        it->sep = g_right_items[i].is_separator;
        y += ih;
    }

    /* log off / turn off, right aligned in the footer */
    {
        int bh = 28, by = g_zone[ZONE_LOGOFF].top + (hl - bh) / 2;
        int shut_w = 8 + 20 + 4 + text_width(hdc, bold, L"Turn Off Computer") + 8;
        int off_w = 8 + 20 + 4 + text_width(hdc, bold, L"Log Off") + 8;
        int right = W - 8;
        PANEL_ITEM *it;

        SetRect(&r, right - shut_w, by, right, by + bh);
        it = panel_add(ZONE_LOGOFF, &r, STARTMENU_CMD_SHUTDOWN); it->title = L"Turn Off Computer"; it->bold = TRUE;
        SetRect(&r, right - shut_w - 6 - off_w, by, right - shut_w - 6, by + bh);
        it = panel_add(ZONE_LOGOFF, &r, STARTMENU_CMD_LOGOFF); it->title = L"Log Off"; it->bold = TRUE;
    }
    DeleteObject(bold);
    ReleaseDC(NULL, hdc);
}

static int panel_hit_test(int x, int y)
{
    POINT pt = { x, y };
    int i;
    for (i = 0; i < g_panel_n; i++)
        if (!g_panel[i].sep && PtInRect(&g_panel[i].rc, pt)) return i;
    return -1;
}

static COLORREF theme_color(HTHEME theme, int part, int prop, int fallback)
{
    COLORREF c;
    if (theme && SUCCEEDED(GetThemeColor(theme, part, 0, prop, &c))) return c;
    return GetSysColor(fallback);
}

static void panel_text(HDC hdc, RECT rc, const WCHAR *s, HFONT font, COLORREF color)
{
    HFONT old = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, s, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | DT_LEFT);
    SelectObject(hdc, old);
}

static void panel_draw_icon(HDC hdc, const PANEL_ITEM *item, int x, int y, int size)
{
    HICON icon = item->exe ? load_app_icon(item->exe, item->icon_idx, size) : load_shell_icon(item->icon_idx, size);
    if (!icon) return;
    DrawIconEx(hdc, x, y, icon, size, size, 0, NULL, DI_NORMAL);
    DestroyIcon(icon);
}

static void panel_draw_user(HDC hdc, HTHEME theme)
{
    RECT zone = g_zone[ZONE_USER], tile, content;
    WCHAR name[128];
    HFONT font;
    LOGFONTW lf;
    COLORREF color = RGB(255, 255, 255), shadow = 0;
    POINT offset = { 2, 2 };
    int shadow_type = 0;
    MARGINS m;

    if (theme) DrawThemeBackground(theme, hdc, SPP_USERPANE, 0, &zone, NULL);
    else
    {
        FillRect(hdc, &zone, GetSysColorBrush(COLOR_ACTIVECAPTION));
        color = GetSysColor(COLOR_CAPTIONTEXT);
    }

    SetRect(&tile, 6, 4, 66, zone.bottom - 4);
    m = content_margins(theme, SPP_USERPICTURE);
    if (theme) DrawThemeBackground(theme, hdc, SPP_USERPICTURE, 0, &tile, NULL);
    content = tile;
    content.left += m.cxLeftWidth; content.right -= m.cxRightWidth; content.top += m.cyTopHeight; content.bottom -= m.cyBottomHeight;
    {
        HBITMAP avatar = LoadBitmapW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDB_AVATAR));
        BITMAP bm;
        if (avatar && GetObjectW(avatar, sizeof(bm), &bm))
        {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP prev = SelectObject(mem, avatar);
            int old_mode = SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(hdc, content.left, content.top, content.right - content.left, content.bottom - content.top,
                       mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            SetStretchBltMode(hdc, old_mode);
            SelectObject(mem, prev);
            DeleteDC(mem);
        }
        if (avatar) DeleteObject(avatar);
    }

    if (theme)
    {
        GetThemeColor(theme, SPP_USERPANE, 0, TMT_TEXTCOLOR, &color);
        GetThemeEnumValue(theme, SPP_USERPANE, 0, TMT_TEXTSHADOWTYPE, &shadow_type);
        GetThemeColor(theme, SPP_USERPANE, 0, TMT_TEXTSHADOWCOLOR, &shadow);
        GetThemePosition(theme, SPP_USERPANE, 0, TMT_TEXTSHADOWOFFSET, &offset);
    }
    if (theme && SUCCEEDED(GetThemeFont(theme, hdc, SPP_USERPANE, 0, TMT_FONT, &lf))) font = CreateFontIndirectW(&lf);
    else font = panel_font(TRUE);

    get_display_user_name(name, ARRAY_SIZE(name));
    SetRect(&content, tile.right + 8, zone.top, zone.right - 8, zone.bottom);
    if (shadow_type)
    {
        RECT sr = content;
        OffsetRect(&sr, offset.x, offset.y);
        panel_text(hdc, sr, name, font, shadow);
    }
    panel_text(hdc, content, name, font, color);
    DeleteObject(font);
}

static void panel_paint(HDC hdc)
{
    HTHEME theme = theme_open(NULL, L"StartPanel");
    HFONT font = panel_font(FALSE), bold = panel_font(TRUE);
    int i;

    /* zone backgrounds */
    panel_draw_user(hdc, theme);
    if (theme)
    {
        DrawThemeBackground(theme, hdc, SPP_PROGLIST, 0, &g_zone[ZONE_PROG], NULL);
        DrawThemeBackground(theme, hdc, SPP_MOREPROGRAMS, 0, &g_zone[ZONE_MORE], NULL);
        DrawThemeBackground(theme, hdc, SPP_PLACESLIST, 0, &g_zone[ZONE_PLACES], NULL);
        DrawThemeBackground(theme, hdc, SPP_LOGOFF, 0, &g_zone[ZONE_LOGOFF], NULL);
    }
    else
    {
        FillRect(hdc, &g_zone[ZONE_PROG], GetSysColorBrush(COLOR_WINDOW));
        FillRect(hdc, &g_zone[ZONE_MORE], GetSysColorBrush(COLOR_WINDOW));
        FillRect(hdc, &g_zone[ZONE_PLACES], GetSysColorBrush(COLOR_MENU));
        FillRect(hdc, &g_zone[ZONE_LOGOFF], GetSysColorBrush(COLOR_ACTIVECAPTION));
    }

    for (i = 0; i < g_panel_n; i++)
    {
        const PANEL_ITEM *item = &g_panel[i];
        BOOL hot = (i == g_panel_hot);
        int part = item->zone == ZONE_PROG ? SPP_PROGLIST : item->zone == ZONE_MORE ? SPP_MOREPROGRAMS :
                   item->zone == ZONE_PLACES ? SPP_PLACESLIST : SPP_LOGOFF;
        COLORREF text = theme_color(theme, part, TMT_TEXTCOLOR, item->zone == ZONE_PLACES ? COLOR_MENUTEXT :
                                    item->zone == ZONE_LOGOFF ? COLOR_CAPTIONTEXT : COLOR_WINDOWTEXT);
        COLORREF sub = theme_color(theme, part, TMT_CAPTIONTEXT, COLOR_GRAYTEXT);
        COLORREF hot_bg = theme_color(theme, part, TMT_HOTTRACKING, COLOR_HIGHLIGHT);
        RECT rc = item->rc, tr;

        if (item->sep)
        {
            SIZE sz = {0, 2};
            RECT line = rc;
            HTHEME sep = theme ? theme : NULL;
            int sep_part = item->zone == ZONE_PROG ? SPP_PROGLISTSEPARATOR : SPP_PLACESLISTSEPARATOR;
            if (sep)
            {
                GetThemePartSize(sep, hdc, sep_part, 0, NULL, TS_TRUE, &sz);
                line.top = rc.top + (rc.bottom - rc.top - sz.cy) / 2;
                line.bottom = line.top + sz.cy;
                DrawThemeBackground(sep, hdc, sep_part, 0, &line, NULL);
            }
            else
            {
                line.top = rc.top + (rc.bottom - rc.top) / 2;
                line.bottom = line.top + 1;
                FillRect(hdc, &line, GetSysColorBrush(COLOR_BTNSHADOW));
            }
            continue;
        }

        if (item->zone == ZONE_LOGOFF)
        {
            HBITMAP bmp = LoadBitmapW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(item->cmd == STARTMENU_CMD_LOGOFF ? IDB_LOGOFF : IDB_SHUTDOWN));
            BITMAP bm;
            if (hot && theme) DrawThemeBackground(theme, hdc, SPP_LOGOFFBUTTONS, SPLS_HOT, &rc, NULL);
            if (bmp && GetObjectW(bmp, sizeof(bm), &bm))
            {
                HDC mem = CreateCompatibleDC(hdc);
                HBITMAP prev = SelectObject(mem, bmp);
                BitBlt(hdc, rc.left + 8, rc.top + (rc.bottom - rc.top - bm.bmHeight) / 2, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
                SelectObject(mem, prev);
                DeleteDC(mem);
            }
            if (bmp) DeleteObject(bmp);
            tr = rc; tr.left += 8 + 20 + 4;
            panel_text(hdc, tr, item->title, bold, text);
            continue;
        }

        if (hot)
        {
            HBRUSH br = CreateSolidBrush(hot_bg);
            FillRect(hdc, &rc, br);
            DeleteObject(br);
            text = sub = GetSysColor(COLOR_HIGHLIGHTTEXT);
        }

        if (item->zone == ZONE_MORE)
        {
            SIZE sz = {0};
            RECT arrow;
            tr = rc; tr.left += 4;
            panel_text(hdc, tr, item->title, bold, text);
            if (theme && SUCCEEDED(GetThemePartSize(theme, hdc, SPP_MOREPROGRAMSARROW, hot ? SPS_HOT : SPS_NORMAL, NULL, TS_TRUE, &sz)))
            {
                SetRect(&arrow, rc.right - sz.cx - 4, rc.top + (rc.bottom - rc.top - sz.cy) / 2, rc.right - 4, 0);
                arrow.bottom = arrow.top + sz.cy;
                DrawThemeBackground(theme, hdc, SPP_MOREPROGRAMSARROW, hot ? SPS_HOT : SPS_NORMAL, &arrow, NULL);
            }
            continue;
        }

        if (item->zone == ZONE_PROG)
        {
            panel_draw_icon(hdc, item, rc.left + 4, rc.top + (rc.bottom - rc.top - 32) / 2, 32);
            tr = rc; tr.left += 4 + 32 + 6;
            if (item->subtitle)
            {
                RECT t1 = tr, t2 = tr;
                t1.bottom = rc.top + (rc.bottom - rc.top) / 2 + 1;
                t2.top = t1.bottom - 1;
                panel_text(hdc, t1, item->title, bold, text);
                panel_text(hdc, t2, item->subtitle, font, sub);
            }
            else
                panel_text(hdc, tr, item->title, item->bold ? bold : font, text);
        }
        else
        {
            panel_draw_icon(hdc, item, rc.left + 4, rc.top + (rc.bottom - rc.top - 16) / 2, 16);
            tr = rc; tr.left += 4 + 16 + 6;
            panel_text(hdc, tr, item->title, item->bold ? bold : font, text);
        }
    }
    DeleteObject(font);
    DeleteObject(bold);
    if (theme) CloseThemeData(theme);
}

static void show_programs_popup_menu(HWND parent, int x, int y)
{
    HMENU hPrograms = CreatePopupMenu();
    WCHAR user_programs[MAX_PATH];
    WCHAR common_programs[MAX_PATH];
    UINT cmd;

    g_program_count = 0;

    if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, user_programs) == S_OK)
        populate_programs_menu(hPrograms, user_programs);
    if (SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, common_programs) == S_OK)
        populate_programs_menu(hPrograms, common_programs);

    cmd = TrackPopupMenu(hPrograms, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                         x, y, 0, parent, NULL);

    DestroyMenu(hPrograms);
    free_menu_bitmaps();

    if (cmd >= 2000 && (cmd - 2000) < g_program_count)
    {
        ShellExecuteW(NULL, L"open", g_program_paths[cmd - 2000], NULL, NULL, SW_SHOWNORMAL);
        startmenu_view_hide();
    }
}

static void execute_startmenu_command(UINT cmd)
{
    HWND tray = g_tray_parent;
    if (!g_startmenu_open) return;
    startmenu_view_hide();

    switch (cmd)
    {
    case STARTMENU_CMD_INTERNET:
        ShellExecuteW(NULL, L"open", L"iexplore.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_EMAIL:
        ShellExecuteW(NULL, L"open", L"mailto:", NULL, NULL, SW_SHOWNORMAL);
        break;
    case 3001: /* Windows Media Player */
        ShellExecuteW(NULL, L"open", L"wmplayer.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case 3002: /* Notepad */
        ShellExecuteW(NULL, L"open", L"notepad.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case 3003: /* Calculator */
        ShellExecuteW(NULL, L"open", L"calc.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case 3004: /* Paint */
        ShellExecuteW(NULL, L"open", L"mspaint.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case 3005: /* Command Prompt */
        ShellExecuteW(NULL, L"open", L"cmd.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_MYDOCS:
    case STARTMENU_CMD_RECENT_DOCS:
    {
        WCHAR path[MAX_PATH];
        if (SHGetFolderPathW(NULL, (cmd == STARTMENU_CMD_RECENT_DOCS) ? CSIDL_RECENT : CSIDL_PERSONAL,
                             NULL, 0, path) == S_OK)
        {
            WCHAR param[MAX_PATH * 2];
            _snwprintf(param, ARRAY_SIZE(param), L"\"%s\"", path);
            ShellExecuteW(NULL, L"open", L"explorer.exe", param, NULL, SW_SHOWNORMAL);
        }
        break;
    }
    case STARTMENU_CMD_PICTURES:
    {
        WCHAR path[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, path) == S_OK)
            ShellExecuteW(NULL, L"open", L"explorer.exe", path, NULL, SW_SHOWNORMAL);
        break;
    }
    case STARTMENU_CMD_MUSIC:
    {
        WCHAR path[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_MYMUSIC, NULL, 0, path) == S_OK)
            ShellExecuteW(NULL, L"open", L"explorer.exe", path, NULL, SW_SHOWNORMAL);
        break;
    }
    case STARTMENU_CMD_MYCOMP:
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_MYNET:
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{208D2C60-3AEA-1069-A2D7-08002B30309D}", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_CONTROLPANEL:
        ShellExecuteW(NULL, L"open", L"control.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_PROGACCESS:
        ShellExecuteW(NULL, L"open", L"control.exe", L"appwiz.cpl,,3", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_CONNECT_TO:
        wlan_dialog_show(tray);
        break;
    case STARTMENU_CMD_PRINTERS:
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{2227a280-3aea-1069-a2de-08002b30309d}", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_HELP:
        ShellExecuteW(NULL, L"open", L"hh.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_SEARCH:
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{e17d4fc0-5564-11d1-83f2-00a0c90dc849}", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_RUN:
        run_dialog_inproc(tray);
        break;
    case STARTMENU_CMD_LOGOFF:
        if (MessageBoxW(tray, L"Are you sure you want to log off?", L"Log Off Windows",
                        MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            ExitWindowsEx(EWX_LOGOFF, 0);
        }
        break;
    case STARTMENU_CMD_SHUTDOWN:
        power_mgmt_show_shutdown_dialog(tray);
        break;
    }
}

/* Two-column Start panel window procedure */
static LRESULT CALLBACK two_column_startmenu_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, g_panel_size.cx, g_panel_size.cy), prev = SelectObject(mem, bmp);

        panel_paint(mem);
        BitBlt(hdc, 0, 0, g_panel_size.cx, g_panel_size.cy, mem, 0, 0, SRCCOPY);
        SelectObject(mem, prev);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
    {
        int hot = panel_hit_test((short)LOWORD(lparam), (short)HIWORD(lparam));
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };

        if (hot != g_panel_hot)
        {
            g_panel_hot = hot;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_panel_hot = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDBLCLK:
        return 0;

    case WM_LBUTTONDOWN:
    {
        int hit;

        if (!g_startmenu_open) return 0;
        hit = panel_hit_test((short)LOWORD(lparam), (short)HIWORD(lparam));
        if (hit < 0) return 0;
        if (g_panel[hit].cmd == CMD_ALL_PROGRAMS)
        {
            RECT wnd_rc;
            GetWindowRect(hwnd, &wnd_rc);
            show_programs_popup_menu(hwnd, wnd_rc.left + g_zone[ZONE_MORE].right, wnd_rc.top + g_zone[ZONE_MORE].bottom);
        }
        else
            execute_startmenu_command(g_panel[hit].cmd);
        return 0;
    }

    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
        {
            startmenu_view_hide();
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE)
        {
            startmenu_view_hide();
            return 0;
        }
        break;

    case WM_ACTIVATEAPP:
        if (!wparam)
        {
            startmenu_view_hide();
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        startmenu_view_hide();
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return 0;
}

void startmenu_view_show(HWND tray_hwnd, const RECT *btn_rect)
{
    THEME_CONFIG *theme = theme_get_current();
    TASKBAR_SETTINGS *settings;
    int style = THEME_STARTMENU_CLASSIC;

    taskbar_settings_load();
    settings = taskbar_settings_get();

    g_tray_parent = tray_hwnd;

    if (g_startmenu_open)
    {
        startmenu_view_hide();
        return;
    }

    if (GetTickCount() - g_last_close_tick < 350)
    {
        return;
    }

    if (settings)
        style = settings->start_menu_style;
    else if (theme)
        style = theme->start_menu_style;

    if (style == THEME_STARTMENU_CLASSIC)
    {
        show_classic_start_menu(tray_hwnd, btn_rect);
    }
    else
    {
        /* Two-column Windows XP Start Menu Window */
        int width, height;
        panel_layout();
        g_panel_hot = -1;
        width = g_panel_size.cx;
        height = g_panel_size.cy;
        int x = btn_rect->left;
        int y = btn_rect->top - height;

        if (!g_startmenu_wnd)
        {
            WNDCLASSW wc;
            memset(&wc, 0, sizeof(wc));
            wc.style = CS_DBLCLKS;
            wc.lpfnWndProc = two_column_startmenu_proc;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.lpszClassName = STARTMENU_WND_CLASS;
            wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
            RegisterClassW(&wc);

            g_startmenu_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                              STARTMENU_WND_CLASS, NULL,
                                              WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                              x, y, width, height,
                                              tray_hwnd, NULL, wc.hInstance, NULL);
        }

        {
            HRGN rgn_top = CreateRoundRectRgn(0, 0, width + 1, 40, 12, 12);
            HRGN rgn_body = CreateRectRgn(0, 12, width, height);
            CombineRgn(rgn_body, rgn_body, rgn_top, RGN_OR);
            SetWindowRgn(g_startmenu_wnd, rgn_body, TRUE);
            DeleteObject(rgn_top);
        }

        SetWindowPos(g_startmenu_wnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
        SetForegroundWindow(g_startmenu_wnd);
        SetFocus(g_startmenu_wnd);

        if (!g_mouse_hook)
            g_mouse_hook = SetWindowsHookExW(WH_MOUSE, mouse_hook_proc, NULL, GetCurrentThreadId());

        g_startmenu_open = TRUE;
        g_button_pressed = TRUE;
        InvalidateRect(g_startbutton_hwnd, NULL, FALSE);
    }
}

void startmenu_view_hide(void)
{
    if (g_mouse_hook)
    {
        UnhookWindowsHookEx(g_mouse_hook);
        g_mouse_hook = NULL;
    }
    if (g_startmenu_wnd && IsWindowVisible(g_startmenu_wnd))
    {
        ShowWindow(g_startmenu_wnd, SW_HIDE);
    }
    g_startmenu_open = FALSE;
    g_button_pressed = FALSE;
    g_button_hovered = FALSE;
    g_last_close_tick = GetTickCount();
    if (g_startbutton_hwnd)
        InvalidateRect(g_startbutton_hwnd, NULL, FALSE);
}

BOOL startmenu_view_is_open(void)
{
    return g_startmenu_open;
}

/* ========================================================================= */
/*                         START BUTTON IMPLEMENTATION                       */
/* ========================================================================= */

int startbutton_get_desired_width(void)
{
    HTHEME theme = theme_open(NULL, L"Start::Button");
    int width = 58;

    if (theme)
    {
        SIZE size = {0};
        HDC hdc = GetDC(NULL);
        if (SUCCEEDED(GetThemePartSize(theme, hdc, 0, 1, NULL, TS_TRUE, &size)) && size.cx > 0) width = size.cx;
        ReleaseDC(NULL, hdc);
        CloseThemeData(theme);
    }
    return width;
}

static LRESULT CALLBACK startbutton_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HFONT font, old_font;
        LOGFONTW lf;
        BOOL is_themed = theme_is_visual_styles_active();

        GetClientRect(hwnd, &rc);

        /* 1. Draw Windows Logo Icon */
        if (!g_winlogo_icon)
        {
            g_winlogo_icon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_START),
                                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
            if (!g_winlogo_icon)
                g_winlogo_icon = (HICON)LoadImageW(NULL, (const WCHAR *)IDI_WINLOGO,
                                                   IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR | LR_SHARED);
        }

        if (is_themed)
        {
            /* Start::Button from the visual style: three images (normal, hot, pressed) and the style's font */
            HTHEME theme = OpenThemeData(hwnd, L"Start::Button");
            if (theme)
            {
                int state = g_button_pressed ? 3 : (g_button_hovered ? 2 : 1);
                RECT text_rc;
                LOGFONTW tf;
                COLORREF color = RGB(255, 255, 255), shadow = 0;
                POINT offset = {2, 2};

                DrawThemeBackground(theme, hdc, 0, state, &rc, NULL);
                GetThemeBackgroundContentRect(theme, hdc, 0, state, &rc, &text_rc);
                {
                    /* the flag is a 32 bit bitmap with alpha, drawn left aligned like a button image list */
                    HBITMAP flag = LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDB_STARTFLAG), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
                    BITMAP bm;
                    if (flag && GetObjectW(flag, sizeof(bm), &bm))
                    {
                        HDC mem = CreateCompatibleDC(hdc);
                        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                        int off = g_button_pressed ? 1 : 0;
                        HBITMAP prev = SelectObject(mem, flag);
                        AlphaBlend(hdc, text_rc.left + off, (rc.bottom - rc.top - bm.bmHeight) / 2 + off, bm.bmWidth, bm.bmHeight,
                                   mem, 0, 0, bm.bmWidth, bm.bmHeight, blend);
                        SelectObject(mem, prev);
                        DeleteDC(mem);
                        text_rc.left += bm.bmWidth + 3;
                    text_rc.right = max(text_rc.right, rc.right - 6);
                    }
                    if (flag) DeleteObject(flag);
                }
                GetThemeColor(theme, 0, state, TMT_TEXTCOLOR, &color);
                GetThemeColor(theme, 0, state, TMT_TEXTSHADOWCOLOR, &shadow);
                GetThemePosition(theme, 0, state, TMT_TEXTSHADOWOFFSET, &offset);
                if (SUCCEEDED(GetThemeFont(theme, hdc, 0, state, TMT_FONT, &tf)))
                {
                    HFONT tfont = CreateFontIndirectW(&tf), tprev = SelectObject(hdc, tfont);
                    RECT sr = text_rc;
                    SetBkMode(hdc, TRANSPARENT);
                    OffsetRect(&sr, offset.x, offset.y);
                    SetTextColor(hdc, shadow);
                    DrawTextW(hdc, L"start", -1, &sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SetTextColor(hdc, color);
                    DrawTextW(hdc, L"start", -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(hdc, tprev);
                    DeleteObject(tfont);
                }
                CloseThemeData(theme);
            }
        }
        else
        {
            /* --- Authentic Windows Classic 3D Start Button --- */
            int icon_size = 16;
            int icon_x, icon_y;
            RECT text_rc;

            if (g_button_pressed)
            {
                DrawEdge(hdc, &rc, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
                FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
            }
            else
            {
                DrawEdge(hdc, &rc, EDGE_RAISED, BF_RECT | BF_ADJUST);
                FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
            }

            icon_x = rc.left + 4 + (g_button_pressed ? 1 : 0);
            icon_y = rc.top + (rc.bottom - rc.top - icon_size) / 2 + (g_button_pressed ? 1 : 0);

            if (g_winlogo_icon)
                DrawIconEx(hdc, icon_x, icon_y, g_winlogo_icon, 16, 16, 0, NULL, DI_NORMAL);

            memset(&lf, 0, sizeof(lf));
            lf.lfHeight = -11;
            lf.lfWeight = FW_BOLD;
            lstrcpyW(lf.lfFaceName, L"Tahoma");
            font = CreateFontIndirectW(&lf);

            text_rc = rc;
            text_rc.left = icon_x + icon_size + 4;
            text_rc.top = rc.top + (g_button_pressed ? 1 : 0);
            text_rc.bottom = rc.bottom + (g_button_pressed ? 1 : 0);

            old_font = SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));

            DrawTextW(hdc, L"Start", -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, old_font);
            DeleteObject(font);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        TRACKMOUSEEVENT tme;
        if (!g_button_hovered)
        {
            g_button_hovered = TRUE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        memset(&tme, 0, sizeof(tme));
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (g_button_hovered)
        {
            g_button_hovered = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        SetCapture(hwnd);
        g_button_pressed = TRUE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        RECT rc, btn_screen_rc;
        POINT pt;
        BOOL was_captured = (GetCapture() == hwnd);

        if (was_captured)
            ReleaseCapture();

        pt.x = (short)LOWORD(lparam);
        pt.y = (short)HIWORD(lparam);
        GetClientRect(hwnd, &rc);

        if (PtInRect(&rc, pt) && was_captured)
        {
            if (g_startmenu_open)
            {
                startmenu_view_hide();
            }
            else if (GetTickCount() - g_last_close_tick >= 350)
            {
                GetWindowRect(hwnd, &btn_screen_rc);
                startmenu_view_show(GetParent(hwnd), &btn_screen_rc);
            }
            else
            {
                g_button_pressed = FALSE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        else
        {
            g_button_pressed = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
    {
        if (!g_startmenu_open)
        {
            g_button_pressed = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONUP:
    {
        HMENU hMenu = CreatePopupMenu();
        POINT pt;
        POINT screen_pt;
        UINT cmd;

        pt.x = (short)LOWORD(lparam);
        pt.y = (short)HIWORD(lparam);
        screen_pt = pt;

        ClientToScreen(hwnd, &screen_pt);

        AppendMenuW(hMenu, MF_STRING, IDM_STARTBTN_OPEN, L"&Open");
        AppendMenuW(hMenu, MF_STRING, IDM_STARTBTN_EXPLORE, L"&Explore");
        AppendMenuW(hMenu, MF_STRING, IDM_STARTBTN_SEARCH, L"&Search...");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_STARTBTN_PROPERTIES, L"&Properties");

        SetForegroundWindow(hwnd);
        cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                             screen_pt.x, screen_pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);

        switch (cmd)
        {
        case IDM_STARTBTN_OPEN:
        {
            WCHAR prog_dir[MAX_PATH];
            if (SHGetFolderPathW(NULL, CSIDL_STARTMENU, NULL, 0, prog_dir) == S_OK)
                ShellExecuteW(NULL, L"open", prog_dir, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case IDM_STARTBTN_EXPLORE:
        {
            WCHAR prog_dir[MAX_PATH];
            if (SHGetFolderPathW(NULL, CSIDL_STARTMENU, NULL, 0, prog_dir) == S_OK)
                ShellExecuteW(NULL, L"explore", prog_dir, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case IDM_STARTBTN_SEARCH:
            ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{e17d4fc0-5564-11d1-83f2-00a0c90dc849}", NULL, SW_SHOWNORMAL);
            break;
        case IDM_STARTBTN_PROPERTIES:
            taskbar_props_show(hwnd);
            break;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

HWND startbutton_create(HWND parent_tray, int x, int y, int width, int height)
{
    WNDCLASSW wc;

    if (g_startbutton_hwnd)
    {
        startbutton_resize(x, y, width, height);
        return g_startbutton_hwnd;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = startbutton_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = STARTBUTTON_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);

    g_startbutton_hwnd = CreateWindowExW(0, STARTBUTTON_CLASS, L"Start",
                                         WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                         x, y, width, height,
                                         parent_tray, NULL, wc.hInstance, NULL);

    return g_startbutton_hwnd;
}

void startbutton_resize(int x, int y, int width, int height)
{
    if (g_startbutton_hwnd)
        SetWindowPos(g_startbutton_hwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void startbutton_destroy(void)
{
    if (g_startmenu_wnd)
    {
        DestroyWindow(g_startmenu_wnd);
        g_startmenu_wnd = NULL;
    }
    if (g_banner_wnd)
    {
        DestroyWindow(g_banner_wnd);
        g_banner_wnd = NULL;
    }
    if (g_startbutton_hwnd)
    {
        DestroyWindow(g_startbutton_hwnd);
        g_startbutton_hwnd = NULL;
    }
}
