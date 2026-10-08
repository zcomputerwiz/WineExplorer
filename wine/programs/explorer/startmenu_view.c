/*
 * WineExplorer Desktop Environment - Flexible Start Menu Implementation
 *
 * Implements multi-style Start Menu engine:
 * 1. Classic Server 2003 / NT single-column cascading menu with vertical banner
 * 2. Windows XP / 2003 two-column layout (pinned/recent on left, shell folders on right)
 * 3. Open-Shell inspired modern hybrid layout with integrated search box
 * Authentic Start button with Windows flag icon and theme-aware rendering.
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

static void run_dialog_inproc(HWND owner)
{
    void (WINAPI *pRunFileDlg)(HWND owner, HICON icon, const WCHAR *dir,
                               const WCHAR *title, const WCHAR *desc, DWORD flags);
    HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
    if (hShell32)
    {
        pRunFileDlg = (void *)GetProcAddress(hShell32, (LPCSTR)61);
        if (pRunFileDlg)
            pRunFileDlg(owner, NULL, NULL, NULL, NULL, 0);
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
    if (!GetUserNameW(username, &ulen)) lstrcpyW(username, L"Administrator");
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

#define TWOCOL_WIDTH  380
#define TWOCOL_HEIGHT 490

#define COL_NONE   0
#define COL_LEFT   1
#define COL_RIGHT  2
#define COL_FOOTER 3

static int g_hover_column = COL_NONE;
static int g_hover_index  = -1;
static HWND g_tray_parent = NULL;

/* Left Column Items */
typedef struct _LEFT_ITEM
{
    const WCHAR *title;
    const WCHAR *subtitle;
    const WCHAR *exe;
    int shell_icon_idx;
    UINT cmd;
} LEFT_ITEM;

static const LEFT_ITEM g_left_frequent[] =
{
    { L"Windows Media Player", NULL, L"wmplayer.exe", 227, 3001 },
    { L"Notepad",              NULL, L"notepad.exe",  223, 3002 },
    { L"Calculator",           NULL, L"calc.exe",     221, 3003 },
    { L"Paint",                NULL, L"mspaint.exe",  226, 3004 },
    { L"Command Prompt",       NULL, L"cmd.exe",      24,  3005 }
};
#define NUM_LEFT_FREQUENT (sizeof(g_left_frequent)/sizeof(g_left_frequent[0]))

/* Right Column Items */
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
    { L"My Documents",                 235, TRUE,  FALSE, STARTMENU_CMD_MYDOCS },
    { L"My Recent Documents",          1,   FALSE, FALSE, STARTMENU_CMD_RECENT_DOCS },
    { L"My Pictures",                  236, FALSE, FALSE, STARTMENU_CMD_PICTURES },
    { L"My Music",                     237, FALSE, FALSE, STARTMENU_CMD_MUSIC },
    { L"My Computer",                  15,  TRUE,  FALSE, STARTMENU_CMD_MYCOMP },
    { L"My Network Places",            17,  FALSE, FALSE, STARTMENU_CMD_MYNET },
    { NULL,                            0,   FALSE, TRUE,  0 },
    { L"Control Panel",                21,  FALSE, FALSE, STARTMENU_CMD_CONTROLPANEL },
    { L"Set Program Access and Defaults", 271, FALSE, FALSE, STARTMENU_CMD_PROGACCESS },
    { L"Connect To",                   17,  FALSE, FALSE, STARTMENU_CMD_CONNECT_TO },
    { L"Printers and Faxes",           16,  FALSE, FALSE, STARTMENU_CMD_PRINTERS },
    { NULL,                            0,   FALSE, TRUE,  0 },
    { L"Help and Support",             23,  FALSE, FALSE, STARTMENU_CMD_HELP },
    { L"Search",                       22,  FALSE, FALSE, STARTMENU_CMD_SEARCH },
    { L"Run...",                       24,  FALSE, FALSE, STARTMENU_CMD_RUN }
};
#define NUM_RIGHT_ITEMS (sizeof(g_right_items)/sizeof(g_right_items[0]))

/* Hit testing for Two-Column Menu */
static void two_col_hit_test(int x, int y, int *out_col, int *out_idx)
{
    *out_col = COL_NONE;
    *out_idx = -1;

    /* Footer Buttons */
    if (y >= 444 && y < 482)
    {
        if (x >= 186 && x < 274)
        {
            *out_col = COL_FOOTER;
            *out_idx = 0; /* Log Off */
            return;
        }
        else if (x >= 276 && x < 376)
        {
            *out_col = COL_FOOTER;
            *out_idx = 1; /* Turn Off Computer */
            return;
        }
    }

    /* Left Column */
    if (x >= 4 && x < 188 && y >= 60 && y < 440)
    {
        /* Pinned Internet */
        if (y >= 62 && y < 102)
        {
            *out_col = COL_LEFT;
            *out_idx = 0;
            return;
        }
        /* Pinned E-mail */
        if (y >= 104 && y < 144)
        {
            *out_col = COL_LEFT;
            *out_idx = 1;
            return;
        }
        /* Frequent Apps */
        if (y >= 150 && y < 150 + (int)(NUM_LEFT_FREQUENT * 36))
        {
            int idx = (y - 150) / 36;
            if (idx >= 0 && idx < (int)NUM_LEFT_FREQUENT)
            {
                *out_col = COL_LEFT;
                *out_idx = 2 + idx;
                return;
            }
        }
        /* All Programs */
        if (y >= 398 && y < 436)
        {
            *out_col = COL_LEFT;
            *out_idx = 100; /* All Programs anchor */
            return;
        }
    }

    /* Right Column */
    if (x >= 190 && x < 376 && y >= 60 && y < 440)
    {
        int cur_y = 62;
        int i;
        for (i = 0; i < (int)NUM_RIGHT_ITEMS; i++)
        {
            if (g_right_items[i].is_separator)
            {
                cur_y += 6;
                continue;
            }
            if (y >= cur_y && y < cur_y + 24)
            {
                *out_col = COL_RIGHT;
                *out_idx = i;
                return;
            }
            cur_y += 24;
        }
    }
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

/* Two-column Windows XP Start Menu Window Procedure */
static LRESULT CALLBACK two_column_startmenu_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client_rc, left_rc, right_rc, footer_rc;
        HDC mem_dc;
        HBITMAP mem_bm, old_mem_bm;
        HFONT font_bold, font_norm, font_head, old_font;
        LOGFONTW lf;
        WCHAR username[128];
        DWORD ulen = sizeof(username)/sizeof(WCHAR);
        int y, i, cur_y;
        HICON icon_user = NULL;
        HICON icon_ie = NULL;
        HICON icon_mail = NULL;
        HICON icon_logoff = NULL;
        HICON icon_shut = NULL;

        GetClientRect(hwnd, &client_rc);

        /* Double Buffering */
        mem_dc = CreateCompatibleDC(hdc);
        mem_bm = CreateCompatibleBitmap(hdc, client_rc.right, client_rc.bottom);
        old_mem_bm = SelectObject(mem_dc, mem_bm);

        /* Layout zones */
        footer_rc = client_rc; footer_rc.top = 442;
        left_rc.left = 0; left_rc.top = 58; left_rc.right = 190; left_rc.bottom = footer_rc.top;
        right_rc.left = 190; right_rc.top = 58; right_rc.right = client_rc.right; right_rc.bottom = footer_rc.top;

        /* Fonts */
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -12;
        lf.lfWeight = FW_NORMAL;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        font_norm = CreateFontIndirectW(&lf);

        lf.lfWeight = FW_BOLD;
        font_bold = CreateFontIndirectW(&lf);

        lf.lfHeight = -16;
        lf.lfWeight = FW_BOLD;
        font_head = CreateFontIndirectW(&lf);

        /* 1. Header Rendering (Blue gradient with user profile) */
        for (y = 0; y < 56; y++)
        {
            RECT line_rc;
            float t = (float)y / 56.0f;
            int r = 32 + (int)(18.0f * t);
            int g = 92 + (int)(32.0f * t);
            int b = 198 + (int)(24.0f * t);

            line_rc.left = 0;
            line_rc.top = y;
            line_rc.right = client_rc.right;
            line_rc.bottom = y + 1;

            SetBkColor(mem_dc, RGB(r, g, b));
            ExtTextOutW(mem_dc, 0, y, ETO_OPAQUE, &line_rc, NULL, 0, NULL);
        }

        /* Avatar Box at x=8, y=3 */
        {
            HBITMAP hbm_avatar = LoadBitmapW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDB_AVATAR));
            if (hbm_avatar)
            {
                HDC bm_dc = CreateCompatibleDC(mem_dc);
                HBITMAP old_bm = SelectObject(bm_dc, hbm_avatar);
                BitBlt(mem_dc, 8, 3, 52, 50, bm_dc, 0, 0, SRCCOPY);
                SelectObject(bm_dc, old_bm);
                DeleteDC(bm_dc);
                DeleteObject(hbm_avatar);
            }
            else
            {
                RECT avatar_rc;
                HBRUSH white_br = CreateSolidBrush(RGB(255, 255, 255));
                HBRUSH shadow_br = CreateSolidBrush(RGB(20, 60, 130));

                avatar_rc.left = 11; avatar_rc.top = 8;
                avatar_rc.right = 53; avatar_rc.bottom = 50;
                FillRect(mem_dc, &avatar_rc, shadow_br);

                avatar_rc.left = 10; avatar_rc.top = 7;
                avatar_rc.right = 52; avatar_rc.bottom = 49;
                FillRect(mem_dc, &avatar_rc, white_br);

                DeleteObject(white_br);
                DeleteObject(shadow_br);

                icon_user = load_shell_icon(269, 32);
                if (!icon_user) icon_user = load_shell_icon(44, 32);
                if (icon_user)
                {
                    DrawIconEx(mem_dc, 15, 12, icon_user, 32, 32, 0, NULL, DI_NORMAL);
                    DestroyIcon(icon_user);
                }
            }
        }

        /* Username Text */
        if (!GetUserNameW(username, &ulen)) lstrcpyW(username, L"Administrator");
        SetBkMode(mem_dc, TRANSPARENT);
        old_font = SelectObject(mem_dc, font_head);

        /* Drop shadow */
        SetTextColor(mem_dc, RGB(16, 50, 110));
        TextOutW(mem_dc, 69, 21, username, lstrlenW(username));

        /* Main text */
        SetTextColor(mem_dc, RGB(255, 255, 255));
        TextOutW(mem_dc, 68, 20, username, lstrlenW(username));

        /* 2px Orange divider line below header */
        {
            RECT div_rc1 = { 0, 56, client_rc.right, 57 };
            RECT div_rc2 = { 0, 57, client_rc.right, 58 };
            SetBkColor(mem_dc, RGB(229, 145, 36));
            ExtTextOutW(mem_dc, 0, 56, ETO_OPAQUE, &div_rc1, NULL, 0, NULL);
            SetBkColor(mem_dc, RGB(235, 180, 91));
            ExtTextOutW(mem_dc, 0, 57, ETO_OPAQUE, &div_rc2, NULL, 0, NULL);
        }

        /* 2. Left Column Rendering (White Pane) */
        {
            HBRUSH left_br = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(mem_dc, &left_rc, left_br);
            DeleteObject(left_br);
        }

        /* Pinned Internet Item */
        {
            RECT itm_rc = { 6, 62, 186, 102 };
            if (g_hover_column == COL_LEFT && g_hover_index == 0)
            {
                HBRUSH hov_br = CreateSolidBrush(RGB(190, 215, 250));
                HPEN hov_pen = CreatePen(PS_SOLID, 1, RGB(49, 106, 197));
                HPEN old_pen = SelectObject(mem_dc, hov_pen);
                HBRUSH old_br = SelectObject(mem_dc, hov_br);
                RoundRect(mem_dc, itm_rc.left, itm_rc.top, itm_rc.right, itm_rc.bottom, 4, 4);
                SelectObject(mem_dc, old_pen);
                SelectObject(mem_dc, old_br);
                DeleteObject(hov_pen);
                DeleteObject(hov_br);
            }

            icon_ie = load_shell_icon(14, 32);
            if (!icon_ie) icon_ie = load_app_icon(L"iexplore.exe", 14, 32);
            if (icon_ie)
            {
                DrawIconEx(mem_dc, 10, 66, icon_ie, 32, 32, 0, NULL, DI_NORMAL);
                DestroyIcon(icon_ie);
            }

            SelectObject(mem_dc, font_bold);
            SetTextColor(mem_dc, RGB(0, 0, 0));
            TextOutW(mem_dc, 46, 66, L"Internet", 8);

            SelectObject(mem_dc, font_norm);
            SetTextColor(mem_dc, RGB(120, 120, 120));
            TextOutW(mem_dc, 46, 82, L"Internet Explorer", 17);
        }

        /* Pinned E-mail Item */
        {
            RECT itm_rc = { 6, 104, 186, 144 };
            if (g_hover_column == COL_LEFT && g_hover_index == 1)
            {
                HBRUSH hov_br = CreateSolidBrush(RGB(190, 215, 250));
                HPEN hov_pen = CreatePen(PS_SOLID, 1, RGB(49, 106, 197));
                HPEN old_pen = SelectObject(mem_dc, hov_pen);
                HBRUSH old_br = SelectObject(mem_dc, hov_br);
                RoundRect(mem_dc, itm_rc.left, itm_rc.top, itm_rc.right, itm_rc.bottom, 4, 4);
                SelectObject(mem_dc, old_pen);
                SelectObject(mem_dc, old_br);
                DeleteObject(hov_pen);
                DeleteObject(hov_br);
            }

            icon_mail = load_shell_icon(254, 32);
            if (!icon_mail) icon_mail = load_shell_icon(1, 32);
            if (icon_mail)
            {
                DrawIconEx(mem_dc, 10, 108, icon_mail, 32, 32, 0, NULL, DI_NORMAL);
                DestroyIcon(icon_mail);
            }

            SelectObject(mem_dc, font_bold);
            SetTextColor(mem_dc, RGB(0, 0, 0));
            TextOutW(mem_dc, 46, 108, L"E-mail", 6);

            SelectObject(mem_dc, font_norm);
            SetTextColor(mem_dc, RGB(120, 120, 120));
            TextOutW(mem_dc, 46, 124, L"Outlook Express", 15);
        }

        /* Subtle separator below pinned */
        {
            RECT sep_rc = { 12, 146, 178, 147 };
            SetBkColor(mem_dc, RGB(220, 220, 220));
            ExtTextOutW(mem_dc, 0, 146, ETO_OPAQUE, &sep_rc, NULL, 0, NULL);
        }

        /* Frequent Apps */
        for (i = 0; i < (int)NUM_LEFT_FREQUENT; i++)
        {
            int itm_y = 150 + i * 36;
            RECT itm_rc;
            HICON hAppIco;

            itm_rc.left = 6;
            itm_rc.top = itm_y;
            itm_rc.right = 186;
            itm_rc.bottom = itm_y + 34;

            if (g_hover_column == COL_LEFT && g_hover_index == (2 + i))
            {
                HBRUSH hov_br = CreateSolidBrush(RGB(190, 215, 250));
                HPEN hov_pen = CreatePen(PS_SOLID, 1, RGB(49, 106, 197));
                HPEN old_pen = SelectObject(mem_dc, hov_pen);
                HBRUSH old_br = SelectObject(mem_dc, hov_br);
                RoundRect(mem_dc, itm_rc.left, itm_rc.top, itm_rc.right, itm_rc.bottom, 4, 4);
                SelectObject(mem_dc, old_pen);
                SelectObject(mem_dc, old_br);
                DeleteObject(hov_pen);
                DeleteObject(hov_br);
            }

            hAppIco = load_app_icon(g_left_frequent[i].exe, g_left_frequent[i].shell_icon_idx, 24);
            if (hAppIco)
            {
                DrawIconEx(mem_dc, 12, itm_y + 5, hAppIco, 24, 24, 0, NULL, DI_NORMAL);
                DestroyIcon(hAppIco);
            }

            SelectObject(mem_dc, font_bold);
            SetTextColor(mem_dc, RGB(0, 0, 0));
            TextOutW(mem_dc, 44, itm_y + 10, g_left_frequent[i].title, lstrlenW(g_left_frequent[i].title));
        }

        /* Subtle separator above All Programs */
        {
            RECT sep_rc = { 12, 394, 178, 395 };
            SetBkColor(mem_dc, RGB(220, 220, 220));
            ExtTextOutW(mem_dc, 0, 394, ETO_OPAQUE, &sep_rc, NULL, 0, NULL);
        }

        /* All Programs Button */
        {
            RECT prog_rc = { 6, 398, 186, 436 };
            if (g_hover_column == COL_LEFT && g_hover_index == 100)
            {
                HBRUSH hov_br = CreateSolidBrush(RGB(49, 106, 197));
                FillRect(mem_dc, &prog_rc, hov_br);
                DeleteObject(hov_br);
                SetTextColor(mem_dc, RGB(255, 255, 255));
            }
            else
            {
                SetTextColor(mem_dc, RGB(0, 0, 0));
            }

            SelectObject(mem_dc, font_bold);
            TextOutW(mem_dc, 38, 410, L"All Programs", 12);

            /* Green right arrow triangle */
            {
                POINT pts[3];
                HBRUSH green_br = (g_hover_column == COL_LEFT && g_hover_index == 100) ?
                                  CreateSolidBrush(RGB(255, 255, 255)) : CreateSolidBrush(RGB(40, 160, 40));
                HPEN green_pen = CreatePen(PS_SOLID, 1, RGB(40, 160, 40));
                HPEN old_pen = SelectObject(mem_dc, green_pen);
                HBRUSH old_br = SelectObject(mem_dc, green_br);

                pts[0].x = 154; pts[0].y = 412;
                pts[1].x = 154; pts[1].y = 422;
                pts[2].x = 160; pts[2].y = 417;
                Polygon(mem_dc, pts, 3);

                SelectObject(mem_dc, old_pen);
                SelectObject(mem_dc, old_br);
                DeleteObject(green_pen);
                DeleteObject(green_br);
            }
        }

        /* Vertical separator between left and right columns */
        {
            RECT col_div = { 190, 58, 191, 442 };
            SetBkColor(mem_dc, RGB(165, 195, 235));
            ExtTextOutW(mem_dc, 0, 58, ETO_OPAQUE, &col_div, NULL, 0, NULL);
        }

        /* 3. Right Column Rendering (Tinted Light Blue Pane) */
        {
            HBRUSH right_br = CreateSolidBrush(RGB(214, 230, 252));
            FillRect(mem_dc, &right_rc, right_br);
            DeleteObject(right_br);
        }

        cur_y = 62;
        for (i = 0; i < (int)NUM_RIGHT_ITEMS; i++)
        {
            if (g_right_items[i].is_separator)
            {
                RECT sep_rc;
                sep_rc.left = 198;
                sep_rc.top = cur_y + 2;
                sep_rc.right = 368;
                sep_rc.bottom = cur_y + 3;
                SetBkColor(mem_dc, RGB(180, 205, 240));
                ExtTextOutW(mem_dc, 0, cur_y + 2, ETO_OPAQUE, &sep_rc, NULL, 0, NULL);
                cur_y += 6;
                continue;
            }

            /* Item rect */
            {
                RECT itm_rc;
                HICON hIcon;

                itm_rc.left = 192;
                itm_rc.top = cur_y;
                itm_rc.right = 374;
                itm_rc.bottom = cur_y + 24;

                if (g_hover_column == COL_RIGHT && g_hover_index == i)
                {
                    HBRUSH hov_br = CreateSolidBrush(RGB(49, 106, 197));
                    FillRect(mem_dc, &itm_rc, hov_br);
                    DeleteObject(hov_br);
                    SetTextColor(mem_dc, RGB(255, 255, 255));
                }
                else
                {
                    SetTextColor(mem_dc, RGB(0, 0, 0));
                }

                hIcon = load_shell_icon(g_right_items[i].icon_idx, 16);
                if (hIcon)
                {
                    DrawIconEx(mem_dc, 198, cur_y + 4, hIcon, 16, 16, 0, NULL, DI_NORMAL);
                    DestroyIcon(hIcon);
                }

                SelectObject(mem_dc, g_right_items[i].is_bold ? font_bold : font_norm);
                TextOutW(mem_dc, 222, cur_y + 4, g_right_items[i].title, lstrlenW(g_right_items[i].title));
            }

            cur_y += 24;
        }

        /* 4. Footer Rendering (Darker Blue with Log Off / Turn Off) */
        for (y = 442; y < client_rc.bottom; y++)
        {
            RECT line_rc;
            float t = (float)(y - 442) / (float)(client_rc.bottom - 442);
            int r = 25 - (int)(10.0f * t);
            int g = 80 - (int)(25.0f * t);
            int b = 175 - (int)(45.0f * t);

            line_rc.left = 0;
            line_rc.top = y;
            line_rc.right = client_rc.right;
            line_rc.bottom = y + 1;

            SetBkColor(mem_dc, RGB(r, g, b));
            ExtTextOutW(mem_dc, 0, y, ETO_OPAQUE, &line_rc, NULL, 0, NULL);
        }

        /* Log Off Button */
        {
            RECT logoff_rc = { 186, 448, 272, 478 };
            HBITMAP hbm_logoff;

            if (g_hover_column == COL_FOOTER && g_hover_index == 0)
            {
                HBRUSH hov_br = CreateSolidBrush(RGB(40, 100, 200));
                HPEN hov_pen = CreatePen(PS_SOLID, 1, RGB(70, 140, 240));
                HPEN old_pen = SelectObject(mem_dc, hov_pen);
                HBRUSH old_br = SelectObject(mem_dc, hov_br);
                RoundRect(mem_dc, logoff_rc.left, logoff_rc.top, logoff_rc.right, logoff_rc.bottom, 4, 4);
                SelectObject(mem_dc, old_pen);
                SelectObject(mem_dc, old_br);
                DeleteObject(hov_pen);
                DeleteObject(hov_br);
            }

            hbm_logoff = LoadBitmapW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDB_LOGOFF));
            if (hbm_logoff)
            {
                HDC bm_dc = CreateCompatibleDC(mem_dc);
                HBITMAP old_bm = SelectObject(bm_dc, hbm_logoff);
                BitBlt(mem_dc, 192, 453, 18, 20, bm_dc, 0, 0, SRCCOPY);
                SelectObject(bm_dc, old_bm);
                DeleteDC(bm_dc);
                DeleteObject(hbm_logoff);
            }
            else
            {
                icon_logoff = load_shell_icon(44, 16);
                if (icon_logoff)
                {
                    DrawIconEx(mem_dc, 192, 455, icon_logoff, 16, 16, 0, NULL, DI_NORMAL);
                    DestroyIcon(icon_logoff);
                }
            }

            SelectObject(mem_dc, font_bold);
            SetTextColor(mem_dc, RGB(255, 255, 255));
            TextOutW(mem_dc, 214, 455, L"Log Off", 7);
        }

        /* Turn Off Computer Button */
        {
            RECT shut_rc = { 276, 448, 374, 478 };
            HBITMAP hbm_shut;

            if (g_hover_column == COL_FOOTER && g_hover_index == 1)
            {
                HBRUSH hov_br = CreateSolidBrush(RGB(40, 100, 200));
                HPEN hov_pen = CreatePen(PS_SOLID, 1, RGB(70, 140, 240));
                HPEN old_pen = SelectObject(mem_dc, hov_pen);
                HBRUSH old_br = SelectObject(mem_dc, hov_br);
                RoundRect(mem_dc, shut_rc.left, shut_rc.top, shut_rc.right, shut_rc.bottom, 4, 4);
                SelectObject(mem_dc, old_pen);
                SelectObject(mem_dc, old_br);
                DeleteObject(hov_pen);
                DeleteObject(hov_br);
            }

            hbm_shut = LoadBitmapW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDB_SHUTDOWN));
            if (hbm_shut)
            {
                HDC bm_dc = CreateCompatibleDC(mem_dc);
                HBITMAP old_bm = SelectObject(bm_dc, hbm_shut);
                BitBlt(mem_dc, 280, 453, 20, 20, bm_dc, 0, 0, SRCCOPY);
                SelectObject(bm_dc, old_bm);
                DeleteDC(bm_dc);
                DeleteObject(hbm_shut);
            }
            else
            {
                icon_shut = load_shell_icon(27, 16);
                if (icon_shut)
                {
                    DrawIconEx(mem_dc, 280, 455, icon_shut, 16, 16, 0, NULL, DI_NORMAL);
                    DestroyIcon(icon_shut);
                }
            }

            SelectObject(mem_dc, font_bold);
            SetTextColor(mem_dc, RGB(255, 255, 255));
            TextOutW(mem_dc, 303, 455, L"Turn Off Computer", 17);
        }

        /* Outer 2px Window Border */
        {
            HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(0, 36, 104));
            HPEN old_pen = SelectObject(mem_dc, border_pen);
            HBRUSH old_br = SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, 0, 0, client_rc.right, client_rc.bottom);
            Rectangle(mem_dc, 1, 1, client_rc.right - 1, client_rc.bottom - 1);
            SelectObject(mem_dc, old_pen);
            SelectObject(mem_dc, old_br);
            DeleteObject(border_pen);
        }

        /* Blit to screen */
        BitBlt(hdc, 0, 0, client_rc.right, client_rc.bottom, mem_dc, 0, 0, SRCCOPY);

        /* Cleanup GDI objects */
        SelectObject(mem_dc, old_font);
        SelectObject(mem_dc, old_mem_bm);
        DeleteDC(mem_dc);
        DeleteObject(mem_bm);
        DeleteObject(font_norm);
        DeleteObject(font_bold);
        DeleteObject(font_head);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);
        int col, idx;
        TRACKMOUSEEVENT tme;

        two_col_hit_test(x, y, &col, &idx);

        if (col != g_hover_column || idx != g_hover_index)
        {
            g_hover_column = col;
            g_hover_index = idx;
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
        g_hover_column = COL_NONE;
        g_hover_index = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
        return 0;

    case WM_LBUTTONDOWN:
    {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);
        int col, idx;

        if (!g_startmenu_open) return 0;

        two_col_hit_test(x, y, &col, &idx);

        if (col == COL_LEFT)
        {
            if (idx == 0)
                execute_startmenu_command(STARTMENU_CMD_INTERNET);
            else if (idx == 1)
                execute_startmenu_command(STARTMENU_CMD_EMAIL);
            else if (idx >= 2 && idx < 2 + (int)NUM_LEFT_FREQUENT)
                execute_startmenu_command(g_left_frequent[idx - 2].cmd);
            else if (idx == 100)
            {
                /* All Programs popup anchored at right of All Programs button */
                RECT wnd_rc;
                GetWindowRect(hwnd, &wnd_rc);
                show_programs_popup_menu(hwnd, wnd_rc.left + 190, wnd_rc.top + 436);
            }
        }
        else if (col == COL_RIGHT)
        {
            if (idx >= 0 && idx < (int)NUM_RIGHT_ITEMS && !g_right_items[idx].is_separator)
                execute_startmenu_command(g_right_items[idx].cmd);
        }
        else if (col == COL_FOOTER)
        {
            if (idx == 0)
                execute_startmenu_command(STARTMENU_CMD_LOGOFF);
            else if (idx == 1)
                execute_startmenu_command(STARTMENU_CMD_SHUTDOWN);
        }
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
        int width = TWOCOL_WIDTH;
        int height = TWOCOL_HEIGHT;
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
            HRGN rgn_top = CreateRoundRectRgn(0, 0, width + 1, 40, 16, 16);
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
    BOOL is_themed = theme_is_visual_styles_active();
    return is_themed ? 98 : 58;
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
            HBITMAP hbm_start = LoadBitmapW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDB_STARTBTN));
            if (hbm_start)
            {
                HDC bm_dc = CreateCompatibleDC(hdc);
                HBITMAP old_bm = SelectObject(bm_dc, hbm_start);
                BitBlt(hdc, g_button_pressed ? 1 : 0, g_button_pressed ? 1 : 0,
                       98, 30, bm_dc, 0, 0, SRCCOPY);
                SelectObject(bm_dc, old_bm);
                DeleteDC(bm_dc);
                DeleteObject(hbm_start);
            }
            else
            {
            /* --- Themed Windows XP Luna Green Pill Button --- */
            int h = rc.bottom - rc.top;
            int y;
            RECT text_rc;
            HPEN border_pen;
            HPEN old_pen;
            HBRUSH null_br;
            HBRUSH old_br;

            /* Gradient Fill */
            for (y = 0; y < h; y++)
            {
                RECT line_rc;
                float t = (float)y / (float)(h > 0 ? h : 1);
                int r, g, b;

                line_rc.left = rc.left + 2;
                line_rc.top = y;
                line_rc.right = rc.right - 2;
                line_rc.bottom = y + 1;

                if (g_button_pressed)
                {
                    /* Sunken Pressed: darker olive/forest green */
                    r = 35 + (int)(25.0f * t);
                    g = 95 + (int)(30.0f * t);
                    b = 30 + (int)(20.0f * t);
                }
                else if (g_button_hovered)
                {
                    /* Hovered: bright glowing lime green */
                    if (t < 0.45f)
                    {
                        float lt = t / 0.45f;
                        r = 75 + (int)(25.0f * lt);
                        g = 195 + (int)(25.0f * lt);
                        b = 70 + (int)(20.0f * lt);
                    }
                    else
                    {
                        float lt = (t - 0.45f) / 0.55f;
                        r = 100 - (int)(35.0f * lt);
                        g = 220 - (int)(45.0f * lt);
                        b = 90 - (int)(30.0f * lt);
                    }
                }
                else
                {
                    /* Normal Luna Green */
                    if (t < 0.45f)
                    {
                        float lt = t / 0.45f;
                        r = 55 + (int)(30.0f * lt);
                        g = 140 + (int)(35.0f * lt);
                        b = 50 + (int)(15.0f * lt);
                    }
                    else
                    {
                        float lt = (t - 0.45f) / 0.55f;
                        r = 85 - (int)(35.0f * lt);
                        g = 175 - (int)(50.0f * lt);
                        b = 65 - (int)(20.0f * lt);
                    }
                }

                SetBkColor(hdc, RGB(r, g, b));
                ExtTextOutW(hdc, 0, y, ETO_OPAQUE, &line_rc, NULL, 0, NULL);
            }

            /* Rounded Pill Outline */
            border_pen = CreatePen(PS_SOLID, 1, g_button_pressed ? RGB(20, 60, 20) : RGB(30, 85, 25));
            old_pen = SelectObject(hdc, border_pen);
            null_br = GetStockObject(NULL_BRUSH);
            old_br = SelectObject(hdc, null_br);
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 7, 7);
            SelectObject(hdc, old_pen);
            SelectObject(hdc, old_br);
            DeleteObject(border_pen);

            /* Subtle top highlight arc */
            if (!g_button_pressed)
            {
                HPEN hilite_pen = CreatePen(PS_SOLID, 1, RGB(140, 225, 130));
                old_pen = SelectObject(hdc, hilite_pen);
                MoveToEx(hdc, rc.left + 4, rc.top + 1, NULL);
                LineTo(hdc, rc.right - 4, rc.top + 1);
                SelectObject(hdc, old_pen);
                DeleteObject(hilite_pen);
            }

            /* Draw Windows XP Flag */
            if (g_winlogo_icon)
            {
                int icon_y = rc.top + (rc.bottom - rc.top - 16) / 2 + (g_button_pressed ? 1 : 0);
                DrawIconEx(hdc, rc.left + 7 + (g_button_pressed ? 1 : 0), icon_y,
                           g_winlogo_icon, 16, 16, 0, NULL, DI_NORMAL);
            }

            /* Authentic bold italic lowercase "start" text with drop shadow */
            memset(&lf, 0, sizeof(lf));
            lf.lfHeight = -13;
            lf.lfWeight = FW_BOLD;
            lf.lfItalic = TRUE;
            lstrcpyW(lf.lfFaceName, L"Tahoma");
            font = CreateFontIndirectW(&lf);

            text_rc = rc;
            text_rc.left = rc.left + 28 + (g_button_pressed ? 1 : 0);
            text_rc.top = rc.top + (g_button_pressed ? 1 : 0);
            text_rc.bottom = rc.bottom + (g_button_pressed ? 1 : 0);

            old_font = SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);

            /* Shadow */
            SetTextColor(hdc, RGB(20, 60, 20));
            text_rc.left += 1; text_rc.top += 1;
            DrawTextW(hdc, L"start", -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            /* White Text */
            SetTextColor(hdc, RGB(255, 255, 255));
            text_rc.left -= 1; text_rc.top -= 1;
            DrawTextW(hdc, L"start", -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, old_font);
            DeleteObject(font);
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
