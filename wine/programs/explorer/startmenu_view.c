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

static HWND g_startbutton_hwnd = NULL;
static HWND g_startmenu_wnd = NULL;
static BOOL g_startmenu_open = FALSE;
static BOOL g_button_pressed = FALSE;
static HICON g_winlogo_icon = NULL;

extern void power_mgmt_show_shutdown_dialog(HWND parent);

#define MAX_PROGRAM_ENTRIES 256
static WCHAR g_program_paths[MAX_PROGRAM_ENTRIES][MAX_PATH];
static UINT g_program_count = 0;

/* Build Programs submenu by enumerating filesystem directory */
static void populate_programs_menu(HMENU hMenu, const WCHAR *path)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    WCHAR search_path[MAX_PATH];

    swprintf(search_path, MAX_PATH, L"%s\\*", path);
    hFind = FindFirstFileW(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        WCHAR item_path[MAX_PATH];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || !wcscmp(fd.cFileName, L"desktop.ini"))
            continue;

        swprintf(item_path, MAX_PATH, L"%s\\%s", path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            HMENU hSub = CreatePopupMenu();
            populate_programs_menu(hSub, item_path);
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSub, fd.cFileName);
        }
        else if (PathMatchSpecW(fd.cFileName, L"*.lnk") || PathMatchSpecW(fd.cFileName, L"*.exe"))
        {
            WCHAR title[MAX_PATH];
            WCHAR *ext;

            if (g_program_count < MAX_PROGRAM_ENTRIES)
            {
                lstrcpynW(g_program_paths[g_program_count], item_path, MAX_PATH);
                lstrcpynW(title, fd.cFileName, MAX_PATH);
                ext = wcsrchr(title, L'.');
                if (ext) *ext = L'\0';

                AppendMenuW(hMenu, MF_STRING, 2000 + g_program_count, title);
                g_program_count++;
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

#define STARTMENU_BANNER_CLASS L"WineStartMenuBanner"
static HWND g_banner_wnd = NULL;

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
            RECT line_rc = { rc.left, y, rc.right, y + 1 };
            COLORREF col;
            float t = (float)y / (float)(h > 0 ? h : 1);

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
        lf.lfHeight = -13;
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
        TextOutW(hdc, 3, h - 8, L"Windows XP", 10);

        SelectObject(hdc, font_norm);
        TextOutW(hdc, 3, h - 102, L"Professional", 12);

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

static HBITMAP get_shell32_icon_bitmap(int index)
{
    HICON hIcon = NULL;
    HDC screen_dc, hdc;
    HBITMAP hbm, old_bm;
    RECT rc = { 0, 0, 16, 16 };

    ExtractIconExW(L"shell32.dll", index, NULL, &hIcon, 1);
    if (!hIcon) return NULL;

    screen_dc = GetDC(NULL);
    hdc = CreateCompatibleDC(screen_dc);
    hbm = CreateCompatibleBitmap(screen_dc, 16, 16);
    old_bm = SelectObject(hdc, hbm);

    FillRect(hdc, &rc, GetSysColorBrush(COLOR_MENU));
    DrawIconEx(hdc, 0, 0, hIcon, 16, 16, 0, NULL, DI_NORMAL);

    SelectObject(hdc, old_bm);
    DeleteDC(hdc);
    ReleaseDC(NULL, screen_dc);
    DestroyIcon(hIcon);
    return hbm;
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
    }

    InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);
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
                int banner_w = 21;
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

static void show_classic_start_menu(HWND tray_hwnd, const RECT *btn_rect)
{
    HMENU hMenu = CreatePopupMenu();
    HMENU hPrograms = CreatePopupMenu();
    HMENU hFavorites = CreatePopupMenu();
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
    int banner_w = 21;
    int banner_overlap = 2;
    int estimated_h = 194;
    int banner_x = btn_rect->left;
    int banner_y = btn_rect->top - estimated_h;
    HBITMAP bm_prog, bm_fav, bm_doc, bm_set, bm_srch, bm_help, bm_run, bm_net, bm_logoff, bm_shut;

    g_program_count = 0;

    /* Get current username */
    if (!GetUserNameW(username, &ulen)) lstrcpyW(username, L"Administrator");
    swprintf(logoff_label, 256, L"&Log Off %s...", username);

    /* Enumerate Programs hierarchy */
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, user_programs) == S_OK)
        populate_programs_menu(hPrograms, user_programs);
    if (SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, common_programs) == S_OK)
        populate_programs_menu(hPrograms, common_programs);

    /* Documents Submenu */
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, my_docs) == S_OK)
        AppendMenuW(hDocuments, MF_STRING, 1050, L"My &Documents");

    /* Settings Submenu */
    AppendMenuW(hSettings, MF_STRING, STARTMENU_CMD_CONTROLPANEL, L"&Control Panel");
    AppendMenuW(hSettings, MF_STRING, STARTMENU_CMD_NETWORK, L"&Network Connections");
    AppendMenuW(hSettings, MF_STRING, STARTMENU_CMD_PRINTERS, L"&Printers and Faxes");
    AppendMenuW(hSettings, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hSettings, MF_STRING, 1020, L"&Taskbar and Start Menu");

    /* Search Submenu */
    AppendMenuW(hSearch, MF_STRING, STARTMENU_CMD_SEARCH, L"For &Files or Folders...");
    AppendMenuW(hSearch, MF_STRING, 1021, L"For &Computers...");

    /* Load authentic icon bitmaps from shell32 */
    bm_prog   = get_shell32_icon_bitmap(3);   /* Folder / Programs */
    bm_fav    = get_shell32_icon_bitmap(43);  /* Favorites */
    bm_doc    = get_shell32_icon_bitmap(1);   /* Documents */
    bm_set    = get_shell32_icon_bitmap(21);  /* Settings / Control Panel */
    bm_srch   = get_shell32_icon_bitmap(22);  /* Search */
    bm_help   = get_shell32_icon_bitmap(23);  /* Help */
    bm_run    = get_shell32_icon_bitmap(24);  /* Run */
    bm_net    = get_shell32_icon_bitmap(17);  /* Network / Connect To */
    bm_logoff = get_shell32_icon_bitmap(44);  /* Log Off key */
    bm_shut   = get_shell32_icon_bitmap(27);  /* Shut Down */

    /* Main Menu Assembly with Authentic Icons */
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hPrograms, L"&Programs", bm_prog);
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hFavorites, L"F&avorites", bm_fav);
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hDocuments, L"&Documents", bm_doc);
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hSettings, L"&Settings", bm_set);
    append_menu_item_icon(hMenu, MF_POPUP, (UINT_PTR)hSearch, L"Sea&rch", bm_srch);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_HELP, L"&Help and Support", bm_help);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_RUN, L"&Run...", bm_run);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_CONNECT_TO, L"Connect &To...", bm_net);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_LOGOFF, logoff_label, bm_logoff);
    append_menu_item_icon(hMenu, MF_STRING, STARTMENU_CMD_SHUTDOWN, L"Sh&ut Down...", bm_shut);

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
    InvalidateRect(g_startbutton_hwnd, NULL, FALSE);
    UpdateWindow(g_startbutton_hwnd);

    DestroyMenu(hMenu);

    if (bm_prog) DeleteObject(bm_prog);
    if (bm_fav) DeleteObject(bm_fav);
    if (bm_doc) DeleteObject(bm_doc);
    if (bm_set) DeleteObject(bm_set);
    if (bm_srch) DeleteObject(bm_srch);
    if (bm_help) DeleteObject(bm_help);
    if (bm_run) DeleteObject(bm_run);
    if (bm_net) DeleteObject(bm_net);
    if (bm_logoff) DeleteObject(bm_logoff);
    if (bm_shut) DeleteObject(bm_shut);

    switch (cmd)
    {
    case STARTMENU_CMD_RUN:
        ShellExecuteW(NULL, L"open", L"rundll32.exe", L"shell32.dll,#61", NULL, SW_SHOWNORMAL);
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
    case STARTMENU_CMD_CONNECT_TO:
        wlan_dialog_show(tray_hwnd);
        break;
    case STARTMENU_CMD_PRINTERS:
        ShellExecuteW(NULL, L"open", L"control.exe", L"printers", NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_HELP:
        ShellExecuteW(NULL, L"open", L"hh.exe", NULL, NULL, SW_SHOWNORMAL);
        break;
    case STARTMENU_CMD_SEARCH:
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"::{e17d4fc0-5564-11d1-83f2-00a0c90dc849}", NULL, SW_SHOWNORMAL);
        break;
    case 1020:
        taskbar_props_show(tray_hwnd);
        break;
    case 1050:
    {
        WCHAR doc_path[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, doc_path) == S_OK)
        {
            WCHAR param[MAX_PATH * 2];
            swprintf(param, ARRAY_SIZE(param), L"\"%s\"", doc_path);
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

/* Two-column Windows XP / 2003 Style Start Menu Window Procedure */
static LRESULT CALLBACK two_column_startmenu_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client_rc, header_rc, left_rc, right_rc, footer_rc;
        HBRUSH header_brush, right_brush, left_brush, footer_brush;
        WCHAR username[128];
        DWORD ulen = sizeof(username)/sizeof(WCHAR);
        HFONT bold_font;
        LOGFONTW lf;

        GetClientRect(hwnd, &client_rc);

        /* Layout zones */
        header_rc = client_rc; header_rc.bottom = 54;
        footer_rc = client_rc; footer_rc.top = client_rc.bottom - 44;
        left_rc.left = 0; left_rc.top = 54; left_rc.right = client_rc.right / 2; left_rc.bottom = footer_rc.top;
        right_rc.left = left_rc.right; right_rc.top = 54; right_rc.right = client_rc.right; right_rc.bottom = footer_rc.top;

        /* Brushes */
        header_brush = CreateSolidBrush(RGB(40, 100, 200));   /* XP Blue Header */
        left_brush = CreateSolidBrush(RGB(255, 255, 255));     /* White Left Pane */
        right_brush = CreateSolidBrush(RGB(214, 230, 252));    /* Tinted Right Pane */
        footer_brush = CreateSolidBrush(RGB(40, 100, 200));   /* XP Blue Footer */

        FillRect(hdc, &header_rc, header_brush);
        FillRect(hdc, &left_rc, left_brush);
        FillRect(hdc, &right_rc, right_brush);
        FillRect(hdc, &footer_rc, footer_brush);

        DeleteObject(header_brush);
        DeleteObject(left_brush);
        DeleteObject(right_brush);
        DeleteObject(footer_brush);

        /* 3D Border */
        theme_draw_classic_bevel(hdc, &client_rc, EDGE_RAISED, BF_RECT);

        /* Header Text: Username */
        if (!GetUserNameW(username, &ulen)) lstrcpyW(username, L"Administrator");

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -14;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        bold_font = CreateFontIndirectW(&lf);

        SelectObject(hdc, bold_font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        header_rc.left += 54;
        header_rc.top += 18;
        DrawTextW(hdc, username, -1, &header_rc, DT_LEFT | DT_TOP);

        /* Clean up fonts */
        DeleteObject(bold_font);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KILLFOCUS:
        startmenu_view_hide();
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

void startmenu_view_show(HWND tray_hwnd, const RECT *btn_rect)
{
    THEME_CONFIG *theme = theme_get_current();

    if (g_startmenu_open)
    {
        startmenu_view_hide();
        return;
    }

    if (!theme || theme->start_menu_style == THEME_STARTMENU_CLASSIC)
    {
        show_classic_start_menu(tray_hwnd, btn_rect);
    }
    else
    {
        /* Two-column / Modern Start Menu Window */
        int width = 380;
        int height = 480;
        int x = btn_rect->left;
        int y = btn_rect->top - height;

        if (!g_startmenu_wnd)
        {
            WNDCLASSW wc;
            memset(&wc, 0, sizeof(wc));
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

        SetWindowPos(g_startmenu_wnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
        SetForegroundWindow(g_startmenu_wnd);
        SetFocus(g_startmenu_wnd);

        g_startmenu_open = TRUE;
        g_button_pressed = TRUE;
        InvalidateRect(g_startbutton_hwnd, NULL, FALSE);
    }
}

void startmenu_view_hide(void)
{
    if (g_startmenu_wnd && IsWindowVisible(g_startmenu_wnd))
    {
        ShowWindow(g_startmenu_wnd, SW_HIDE);
    }
    g_startmenu_open = FALSE;
    g_button_pressed = FALSE;
    if (g_startbutton_hwnd)
        InvalidateRect(g_startbutton_hwnd, NULL, FALSE);
}

BOOL startmenu_view_is_open(void)
{
    return g_startmenu_open;
}

/* Start Button Implementation */

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
        int icon_size = 16;
        int icon_y, icon_x;
        RECT text_rc;

        GetClientRect(hwnd, &rc);

        /* 1. Draw Button Surface */
        if (theme_is_visual_styles_active())
        {
            /* Themed Start Button */
            HBRUSH face = GetSysColorBrush(g_button_pressed ? COLOR_BTNHIGHLIGHT : COLOR_BTNFACE);
            FillRect(hdc, &rc, face);
            theme_draw_classic_bevel(hdc, &rc, g_button_pressed ? EDGE_SUNKEN : EDGE_RAISED,
                                     BF_RECT | BF_SOFT | BF_ADJUST);
        }
        else
        {
            /* Authentic Windows Classic Start Button */
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
        }

        /* 2. Draw Windows Logo Icon */
        if (!g_winlogo_icon)
        {
            g_winlogo_icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_START));
            if (!g_winlogo_icon)
                g_winlogo_icon = LoadIconW(NULL, (const WCHAR *)IDI_WINLOGO);
        }

        icon_x = rc.left + 5 + (g_button_pressed ? 1 : 0);
        icon_y = rc.top + (rc.bottom - rc.top - icon_size) / 2 + (g_button_pressed ? 1 : 0);

        if (g_winlogo_icon)
            DrawIconEx(hdc, icon_x, icon_y, g_winlogo_icon, 16, 16, 0, NULL, DI_NORMAL);

        /* 3. Draw Bold "Start" Label */
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

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        RECT btn_screen_rc;
        GetWindowRect(hwnd, &btn_screen_rc);
        startmenu_view_show(GetParent(hwnd), &btn_screen_rc);
        return 0;
    }

    case WM_RBUTTONUP:
    {
        HMENU hMenu = CreatePopupMenu();
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        POINT screen_pt = pt;
        UINT cmd;

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
    if (g_startbutton_hwnd)
    {
        DestroyWindow(g_startbutton_hwnd);
        g_startbutton_hwnd = NULL;
    }
}
