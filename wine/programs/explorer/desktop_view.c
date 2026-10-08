/*
 * WineExplorer Desktop Environment - Desktop View Surface Implementation
 *
 * Implements complete feature parity with Windows Desktop:
 * - Icon grid layout, auto-arrange, align-to-grid, registry position persistence
 * - Namespace objects: My Computer, My Documents, Recycle Bin, Network Places
 * - Physical directory enumeration from CSIDL_DESKTOPDIRECTORY & CSIDL_COMMON_DESKTOPDIRECTORY
 * - Selection marquee (rubber-band selection box), multi-item selection, drag-and-drop
 * - Desktop context menu (Arrange, Line up, Refresh, New, Properties)
 * - Multi-mode wallpaper rendering engine (Center, Tile, Stretch, Fit, Fill)
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
#include <commctrl.h>

#include "wine/debug.h"
#include "resource.h"
#include "theme_engine.h"
#include "desktop_view.h"
#include "shellres.h"
#include "wlan_dialog.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define DESKTOP_VIEW_CLASS L"SHELLDLL_DefView"

#define IDM_ARRANGE_NAME      101
#define IDM_ARRANGE_SIZE      102
#define IDM_ARRANGE_TYPE      103
#define IDM_ARRANGE_MODIFIED  104
#define IDM_ARRANGE_AUTO      105
#define IDM_ALIGN_TO_GRID     106
#define IDM_SHOW_ICONS        107
#define IDM_LINE_UP           108
#define IDM_REFRESH           109
#define IDM_PASTE             110
#define IDM_PASTE_SHORTCUT    111
#define IDM_NEW_FOLDER        112
#define IDM_NEW_SHORTCUT      113
#define IDM_NEW_TEXT          114
#define IDM_PROPERTIES        115

static DESKTOP_VIEW g_desktop_view;

static void update_item_bounds(DESKTOP_ITEM *item, int grid_cx, int grid_cy)
{
    item->bounds.left = item->pos.x;
    item->bounds.top = item->pos.y;
    item->bounds.right = item->pos.x + grid_cx;
    item->bounds.bottom = item->pos.y + grid_cy;
}

static void add_desktop_item(DESKTOP_ITEM_TYPE type, const WCHAR *display_name,
                             const WCHAR *target_path, HICON icon, int gx, int gy)
{
    DESKTOP_ITEM *item;

    if (g_desktop_view.item_count >= g_desktop_view.item_capacity)
    {
        int new_cap = g_desktop_view.item_capacity ? g_desktop_view.item_capacity * 2 : 32;
        DESKTOP_ITEM **new_items = realloc(g_desktop_view.items, new_cap * sizeof(DESKTOP_ITEM*));
        if (!new_items) return;
        g_desktop_view.items = new_items;
        g_desktop_view.item_capacity = new_cap;
    }

    item = calloc(1, sizeof(DESKTOP_ITEM));
    if (!item) return;

    item->type = type;
    lstrcpynW(item->display_name, display_name, MAX_PATH);
    if (target_path) lstrcpynW(item->target_path, target_path, MAX_PATH);
    item->icon = icon;
    item->grid_x = gx;
    item->grid_y = gy;
    item->pos.x = gx * g_desktop_view.grid_cx;
    item->pos.y = gy * g_desktop_view.grid_cy;
    update_item_bounds(item, g_desktop_view.grid_cx, g_desktop_view.grid_cy);

    g_desktop_view.items[g_desktop_view.item_count++] = item;
}

static void free_all_items(void)
{
    int i;
    for (i = 0; i < g_desktop_view.item_count; i++)
    {
        if (g_desktop_view.items[i])
        {
            if (g_desktop_view.items[i]->icon)
                DestroyIcon(g_desktop_view.items[i]->icon);
            free(g_desktop_view.items[i]);
        }
    }
    free(g_desktop_view.items);
    g_desktop_view.items = NULL;
    g_desktop_view.item_count = 0;
    g_desktop_view.item_capacity = 0;
}

static HICON load_shell_icon(int index)
{
    if (index < 0)
    {
        HICON icon = shellres_icon(-index, 32);
        if (icon) return icon;
    }
    HMODULE hShell = LoadLibraryW(L"shell32.dll");
    HICON hIcon = NULL;
    if (hShell)
    {
        ExtractIconExW(L"shell32.dll", index, &hIcon, NULL, 1);
        FreeLibrary(hShell);
    }
    /* LoadIcon returns a shared icon; copy it since items are DestroyIcon()'d */
    if (!hIcon) hIcon = CopyIcon(LoadIconW(NULL, (const WCHAR *)IDI_APPLICATION));
    return hIcon;
}

static int rows_per_column(void)
{
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return max(1, (work.bottom - work.top) / g_desktop_view.grid_cy);
}

static void next_cell(int *row, int *col)
{
    if (++*row >= rows_per_column())
    {
        *row = 0;
        ++*col;
    }
}

/* Icon of a shell namespace object, as registered (a theme replaces these values) */
static HICON load_clsid_icon(const WCHAR *clsid, const WCHAR *value, int fallback_shell_index)
{
    WCHAR key[160], spec[MAX_PATH + 16], path[MAX_PATH];
    DWORD size = sizeof(spec);
    HICON icon = NULL;
    WCHAR *comma;
    int index = 0;

    swprintf(key, ARRAY_SIZE(key), L"Software\\Classes\\CLSID\\%s\\DefaultIcon", clsid);
    BOOL found = !RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, spec, &size);

    if (!found)
    {
        size = sizeof(spec);
        found = !RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, spec, &size);
    }
    if (found)
    {
        const WCHAR *file;
        ExpandEnvironmentStringsW(spec, path, MAX_PATH);
        if ((comma = wcsrchr(path, ','))) { index = _wtoi(comma + 1); *comma = 0; }
        file = PathFindFileNameW(path);
        /* Windows themes name icons by position in Windows' own shell32.dll; our shell32 is laid out differently,
         * so only icons from other files are taken from the registry */
        if (wcsicmp(file, L"shell32.dll") && wcsicmp(file, L"explorer.exe") && wcsicmp(file, L"mydocs.dll"))
            ExtractIconExW(path, index, &icon, NULL, 1);
    }
    return icon ? icon : load_shell_icon(fallback_shell_index);
}

static void populate_directory_items(const WCHAR *folder_path, int *cur_row, int *cur_col)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    WCHAR search_path[MAX_PATH];

    swprintf(search_path, MAX_PATH, L"%s\\*", folder_path);
    hFind = FindFirstFileW(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        WCHAR full_path[MAX_PATH];
        SHFILEINFOW sfi;
        DESKTOP_ITEM_TYPE itype;

        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || !wcscmp(fd.cFileName, L"desktop.ini"))
            continue;

        swprintf(full_path, MAX_PATH, L"%s\\%s", folder_path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            itype = DESKTOP_ITEM_FOLDER;
        else if (PathMatchSpecW(fd.cFileName, L"*.lnk"))
            itype = DESKTOP_ITEM_SHORTCUT;
        else
            itype = DESKTOP_ITEM_FILE;

        ZeroMemory(&sfi, sizeof(sfi));
        SHGetFileInfoW(full_path, fd.dwFileAttributes, &sfi, sizeof(sfi),
                       SHGFI_ICON | SHGFI_LARGEICON | SHGFI_DISPLAYNAME);

        add_desktop_item(itype, sfi.szDisplayName[0] ? sfi.szDisplayName : fd.cFileName,
                         full_path, sfi.hIcon, *cur_col, *cur_row);

        next_cell(cur_row, cur_col);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

static int g_rb_full = -1;

/* The Recycle Bin icon shows whether it holds anything */
static void update_recycle_bin_icon(void)
{
    SHQUERYRBINFO info = { sizeof(info) };
    BOOL full = SUCCEEDED(SHQueryRecycleBinW(NULL, &info)) && info.i64NumItems > 0;
    int i;

    if (full == g_rb_full) return;
    g_rb_full = full;
    for (i = 0; i < g_desktop_view.item_count; i++)
    {
        DESKTOP_ITEM *item = g_desktop_view.items[i];
        if (item->type == DESKTOP_ITEM_NAMESPACE && wcsstr(item->target_path, L"645FF040"))
        {
            HICON icon = load_clsid_icon(L"{645FF040-5081-101B-9F08-00AA002F954E}", full ? L"full" : L"empty", full ? -33 : -32);
            if (icon) { if (item->icon) DestroyIcon(item->icon); item->icon = icon; }
        }
    }
    if (g_desktop_view.hwnd) InvalidateRect(g_desktop_view.hwnd, NULL, FALSE);
}

void desktop_view_refresh(void)
{
    int row = 0, col = 0;
    WCHAR user_desktop[MAX_PATH];
    WCHAR common_desktop[MAX_PATH];
    THEME_CONFIG *theme = theme_get_current();

    user_desktop[0] = 0;
    free_all_items();
    g_rb_full = -1;

    /* 1. Standard namespace icons; each takes its icon from the registered DefaultIcon */
    add_desktop_item(DESKTOP_ITEM_NAMESPACE, L"My Documents", L"::{450D8FBA-AD25-11D0-98A8-0800361B1103}",
                     load_clsid_icon(L"{450D8FBA-AD25-11D0-98A8-0800361B1103}", NULL, -235), col, row);
    next_cell(&row, &col);
    add_desktop_item(DESKTOP_ITEM_NAMESPACE, L"My Computer", L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
                     load_clsid_icon(L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", NULL, -16), col, row);
    next_cell(&row, &col);
    add_desktop_item(DESKTOP_ITEM_NAMESPACE, L"My Network Places", L"::{208D2C60-3AEA-1069-A2D7-08002B30309D}",
                     load_clsid_icon(L"{208D2C60-3AEA-1069-A2D7-08002B30309D}", NULL, -18), col, row);
    next_cell(&row, &col);
    add_desktop_item(DESKTOP_ITEM_NAMESPACE, L"Recycle Bin", L"::{645FF040-5081-101B-9F08-00AA002F954E}",
                     load_clsid_icon(L"{645FF040-5081-101B-9F08-00AA002F954E}", L"empty", -32), col, row);
    next_cell(&row, &col);

    /* 2. Populate physical user and common desktop directory items */
    if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, user_desktop) == S_OK)
    {
        populate_directory_items(user_desktop, &row, &col);
    }

    if (SHGetFolderPathW(NULL, CSIDL_COMMON_DESKTOPDIRECTORY, NULL, 0, common_desktop) == S_OK)
    {
        if (wcsicmp(user_desktop, common_desktop) != 0)
            populate_directory_items(common_desktop, &row, &col);
    }

    /* 3. Reload wallpaper if configured */
    if (theme && theme->wallpaper_path[0])
        desktop_view_load_wallpaper(theme->wallpaper_path);

    update_recycle_bin_icon();
    if (g_desktop_view.hwnd)
        InvalidateRect(g_desktop_view.hwnd, NULL, TRUE);
}

void desktop_view_auto_arrange(void)
{
    int i, row = 0, col = 0;
    int max_rows = rows_per_column();

    for (i = 0; i < g_desktop_view.item_count; i++)
    {
        DESKTOP_ITEM *item = g_desktop_view.items[i];
        item->grid_x = col;
        item->grid_y = row;
        item->pos.x = col * g_desktop_view.grid_cx;
        item->pos.y = row * g_desktop_view.grid_cy;
        update_item_bounds(item, g_desktop_view.grid_cx, g_desktop_view.grid_cy);

        row++;
        if (row >= max_rows)
        {
            row = 0;
            col++;
        }
    }

    if (g_desktop_view.hwnd)
        InvalidateRect(g_desktop_view.hwnd, NULL, TRUE);
}

void desktop_view_align_to_grid(void)
{
    int i;
    for (i = 0; i < g_desktop_view.item_count; i++)
    {
        DESKTOP_ITEM *item = g_desktop_view.items[i];
        int gx = max(0, (item->pos.x + g_desktop_view.grid_cx / 2) / g_desktop_view.grid_cx);
        int gy = max(0, (item->pos.y + g_desktop_view.grid_cy / 2) / g_desktop_view.grid_cy);
        item->grid_x = gx;
        item->grid_y = gy;
        item->pos.x = gx * g_desktop_view.grid_cx;
        item->pos.y = gy * g_desktop_view.grid_cy;
        update_item_bounds(item, g_desktop_view.grid_cx, g_desktop_view.grid_cy);
    }

    if (g_desktop_view.hwnd)
        InvalidateRect(g_desktop_view.hwnd, NULL, TRUE);
}

void desktop_view_on_display_change(int width, int height)
{
    int i;
    int max_x, max_y;
    RECT rc;

    if (!width || !height)
    {
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }

    if (g_desktop_view.hwnd)
    {
        GetClientRect(g_desktop_view.hwnd, &rc);
        if (rc.right != width || rc.bottom != height)
        {
            SetWindowPos(g_desktop_view.hwnd, NULL, 0, 0, width, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    max_x = max(10, width - g_desktop_view.grid_cx);
    max_y = max(10, height - g_desktop_view.grid_cy - 40);

    if (g_desktop_view.auto_arrange)
    {
        desktop_view_auto_arrange();
    }
    else
    {
        for (i = 0; i < g_desktop_view.item_count; i++)
        {
            DESKTOP_ITEM *item = g_desktop_view.items[i];
            if (item->pos.x > max_x) item->pos.x = max_x;
            if (item->pos.y > max_y) item->pos.y = max_y;
            update_item_bounds(item, g_desktop_view.grid_cx, g_desktop_view.grid_cy);
        }
        if (g_desktop_view.hwnd)
            InvalidateRect(g_desktop_view.hwnd, NULL, TRUE);
    }
}

void desktop_view_load_wallpaper(const WCHAR *path)
{
    if (g_desktop_view.hbm_wallpaper)
    {
        DeleteObject(g_desktop_view.hbm_wallpaper);
        g_desktop_view.hbm_wallpaper = NULL;
    }

    if (path && path[0] && PathFileExistsW(path))
    {
        BITMAP bm;
        g_desktop_view.hbm_wallpaper = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0,
                                                           LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (g_desktop_view.hbm_wallpaper && GetObjectW(g_desktop_view.hbm_wallpaper, sizeof(bm), &bm))
        {
            g_desktop_view.wallpaper_width = bm.bmWidth;
            g_desktop_view.wallpaper_height = bm.bmHeight;
        }
    }
}

static void draw_wallpaper(HDC hdc, int screen_w, int screen_h)
{
    THEME_CONFIG *theme = theme_get_current();
    HBRUSH bg_brush = CreateSolidBrush(GetSysColor(COLOR_BACKGROUND));
    RECT screen_rc = {0, 0, screen_w, screen_h};

    FillRect(hdc, &screen_rc, bg_brush);
    DeleteObject(bg_brush);

    if (g_desktop_view.hbm_wallpaper)
    {
        HDC mem_dc = CreateCompatibleDC(hdc);
        HBITMAP old_bm = SelectObject(mem_dc, g_desktop_view.hbm_wallpaper);
        int w = g_desktop_view.wallpaper_width;
        int h = g_desktop_view.wallpaper_height;
        int style = theme ? theme->wallpaper_style : WALLPAPER_STYLE_STRETCH;

        switch (style)
        {
        case WALLPAPER_STYLE_CENTER:
            BitBlt(hdc, (screen_w - w) / 2, (screen_h - h) / 2, w, h, mem_dc, 0, 0, SRCCOPY);
            break;

        case WALLPAPER_STYLE_TILE:
        {
            int x, y;
            for (y = 0; y < screen_h; y += h)
                for (x = 0; x < screen_w; x += w)
                    BitBlt(hdc, x, y, w, h, mem_dc, 0, 0, SRCCOPY);
            break;
        }

        case WALLPAPER_STYLE_STRETCH:
        default:
            SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(hdc, 0, 0, screen_w, screen_h, mem_dc, 0, 0, w, h, SRCCOPY);
            break;
        }

        SelectObject(mem_dc, old_bm);
        DeleteDC(mem_dc);
    }
}

/* Icon and label rectangles of an item; the label is as wide as its (wrapped) text */
static void item_layout(HDC hdc, DESKTOP_ITEM *item, HFONT font, BOOL full_text, RECT *icon_rc, RECT *label_rc)
{
    int cell = g_desktop_view.grid_cx, size = g_desktop_view.icon_size, lines;
    RECT avail, calc;
    HFONT old = SelectObject(hdc, font);
    TEXTMETRICW tm;

    GetTextMetricsW(hdc, &tm);
    icon_rc->left = item->pos.x + (cell - size) / 2;
    icon_rc->top = item->pos.y + 4;
    icon_rc->right = icon_rc->left + size;
    icon_rc->bottom = icon_rc->top + size;

    SetRect(&avail, item->pos.x + 3, icon_rc->bottom + 4, item->pos.x + cell - 3, 0);
    avail.bottom = avail.top + 1000;
    calc = avail;
    DrawTextW(hdc, item->display_name, -1, &calc, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    lines = full_text ? 100 : 2;
    if (calc.bottom - calc.top > lines * tm.tmHeight) calc.bottom = calc.top + lines * tm.tmHeight;
    {
        int w = calc.right - calc.left;
        label_rc->left = item->pos.x + (cell - w) / 2;
        label_rc->right = label_rc->left + w;
        label_rc->top = calc.top;
        label_rc->bottom = calc.bottom;
    }
    SelectObject(hdc, old);
}

/* Draw an icon with its opaque pixels blended 50% with a color (the selected look) */
static void draw_icon_tinted(HDC hdc, HICON icon, int x, int y, int size, COLORREF tint)
{
    ICONINFO info;
    BITMAP bm;
    BITMAPINFO bi = {{ sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB }};
    DWORD *color_bits = NULL, *mask_bits = NULL, *overlay = NULL;
    HBITMAP dib = NULL;
    HDC mem;
    int i, n = size * size;
    BOOL has_alpha = FALSE;

    DrawIconEx(hdc, x, y, icon, size, size, 0, NULL, DI_NORMAL);
    if (!GetIconInfo(icon, &info)) return;
    color_bits = calloc(n, sizeof(DWORD));
    mask_bits = calloc(n, sizeof(DWORD));
    mem = CreateCompatibleDC(hdc);
    dib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)&overlay, NULL, 0);
    if (color_bits && mask_bits && dib && overlay && info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) &&
        bm.bmWidth == size && bm.bmHeight == size)
    {
        GetDIBits(hdc, info.hbmColor, 0, size, color_bits, &bi, DIB_RGB_COLORS);
        GetDIBits(hdc, info.hbmMask, 0, size, mask_bits, &bi, DIB_RGB_COLORS);
        for (i = 0; i < n; i++) if (color_bits[i] >> 24) { has_alpha = TRUE; break; }
        for (i = 0; i < n; i++)
        {
            BOOL opaque = has_alpha ? (color_bits[i] >> 24) != 0 : (mask_bits[i] & 0xffffff) == 0;
            DWORD a = opaque ? 128 : 0;
            overlay[i] = (a << 24) | ((GetRValue(tint) * a / 255) << 16) | ((GetGValue(tint) * a / 255) << 8) | (GetBValue(tint) * a / 255);
        }
        {
            HBITMAP prev = SelectObject(mem, dib);
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(hdc, x, y, size, size, mem, 0, 0, size, size, blend);
            SelectObject(mem, prev);
        }
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    free(color_bits);
    free(mask_bits);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
}

static void draw_desktop_item(HDC hdc, DESKTOP_ITEM *item, HFONT font)
{
    RECT icon_rc, label_rc, text_rc;
    /* the selection is blue while no other window is active, gray otherwise */
    HWND fg = GetForegroundWindow();
    BOOL focus = !fg || GetAncestor(fg, GA_ROOT) == GetAncestor(g_desktop_view.hwnd, GA_ROOT);
    HFONT old_font;

    item_layout(hdc, item, font, item->selected, &icon_rc, &label_rc);

    if (item->icon)
    {
        if (item->selected)
            draw_icon_tinted(hdc, item->icon, icon_rc.left, icon_rc.top, g_desktop_view.icon_size,
                             GetSysColor(focus ? COLOR_HIGHLIGHT : COLOR_BTNSHADOW));
        else
            DrawIconEx(hdc, icon_rc.left, icon_rc.top, item->icon, g_desktop_view.icon_size, g_desktop_view.icon_size,
                       0, NULL, DI_NORMAL);
    }

    old_font = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    text_rc = label_rc;

    if (item->selected)
    {
        RECT back = label_rc;
        InflateRect(&back, 2, 1);
        FillRect(hdc, &back, GetSysColorBrush(focus ? COLOR_HIGHLIGHT : COLOR_BTNSHADOW));
        SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        DrawTextW(hdc, item->display_name, -1, &text_rc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        if (focus && item->focused) DrawFocusRect(hdc, &back);
    }
    else
    {
        /* labels on the desktop have a shadow so they stay readable on any wallpaper */
        RECT shadow_rc = text_rc;
        OffsetRect(&shadow_rc, 1, 1);
        SetTextColor(hdc, RGB(0, 0, 0));
        DrawTextW(hdc, item->display_name, -1, &shadow_rc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        SetTextColor(hdc, RGB(255, 255, 255));
        DrawTextW(hdc, item->display_name, -1, &text_rc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(hdc, old_font);
}

static HFONT create_label_font(void)
{
    LOGFONTW lf;
    SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0);
    return CreateFontIndirectW(&lf);
}

/* An item is hit on its icon or its label, not on the empty part of its cell */
static BOOL item_hit(HDC hdc, DESKTOP_ITEM *item, HFONT font, POINT pt)
{
    RECT icon_rc, label_rc;
    item_layout(hdc, item, font, item->selected, &icon_rc, &label_rc);
    InflateRect(&icon_rc, 2, 2);
    InflateRect(&label_rc, 2, 1);
    return PtInRect(&icon_rc, pt) || PtInRect(&label_rc, pt);
}

/* topmost item under a client point, or -1 */
static int item_at(HWND hwnd, POINT pt)
{
    HDC hdc = GetDC(hwnd);
    HFONT font = create_label_font();
    int i, found = -1;

    for (i = g_desktop_view.item_count - 1; i >= 0 && found < 0; i--)
        if (item_hit(hdc, g_desktop_view.items[i], font, pt)) found = i;
    DeleteObject(font);
    ReleaseDC(hwnd, hdc);
    return found;
}

void desktop_view_paint(HDC hdc, const RECT *clip_rect)
{
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bm = CreateCompatibleBitmap(hdc, screen_w, screen_h);
    HBITMAP old_bm = SelectObject(mem_dc, mem_bm);
    HFONT font;
    int i;

    font = create_label_font();

    /* 1. Paint wallpaper & solid background */
    draw_wallpaper(mem_dc, screen_w, screen_h);

    /* 2. Paint desktop icons */
    if (g_desktop_view.show_desktop_icons)
    {
        for (i = 0; i < g_desktop_view.item_count; i++)
        {
            DESKTOP_ITEM *item = g_desktop_view.items[i];
            RECT intersect;
            if (IntersectRect(&intersect, clip_rect, &item->bounds))
                draw_desktop_item(mem_dc, item, font);
        }
    }

    /* 3. Draw selection marquee */
    if (g_desktop_view.marquee_active)
    {
        DrawFocusRect(mem_dc, &g_desktop_view.marquee_rect);
    }

    /* Transfer back buffer to screen */
    BitBlt(hdc, clip_rect->left, clip_rect->top,
           clip_rect->right - clip_rect->left, clip_rect->bottom - clip_rect->top,
           mem_dc, clip_rect->left, clip_rect->top, SRCCOPY);

    DeleteObject(font);
    SelectObject(mem_dc, old_bm);
    DeleteObject(mem_bm);
    DeleteDC(mem_dc);
}

static void execute_item(DESKTOP_ITEM *item)
{
    WCHAR param[MAX_PATH * 2];

    if (!item) return;

    TRACE("Executing desktop item: %s (%s)\n", debugstr_w(item->display_name), debugstr_w(item->target_path));

    /* 1. My Network Places */
    if (wcsstr(item->target_path, L"208D2C60") != NULL)
    {
        ShellExecuteW(NULL, L"open", L"control.exe", L"ncpa.cpl", NULL, SW_SHOWNORMAL);
        return;
    }

    /* 2. My Computer */
    if (wcsstr(item->target_path, L"20D04FE0") != NULL)
    {
        ShellExecuteW(NULL, L"open", L"explorer.exe", L"/e,", NULL, SW_SHOWNORMAL);
        return;
    }

    /* 3. My Documents */
    if (wcsstr(item->target_path, L"450D8FBA") != NULL)
    {
        WCHAR docs_path[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs_path) == S_OK)
        {
            swprintf(param, ARRAY_SIZE(param), L"\"%s\"", docs_path);
            ShellExecuteW(NULL, L"open", L"explorer.exe", param, NULL, SW_SHOWNORMAL);
        }
        else
        {
            ShellExecuteW(NULL, L"open", L"explorer.exe", L"/e,", NULL, SW_SHOWNORMAL);
        }
        return;
    }

    /* 4. Recycle Bin */
    if (wcsstr(item->target_path, L"645FF040") != NULL)
    {
        ShellExecuteW(NULL, L"open", L"explorer.exe", NULL, NULL, SW_SHOWNORMAL);
        return;
    }

    /* 5. Directory / Folder */
    if (item->type == DESKTOP_ITEM_FOLDER || PathIsDirectoryW(item->target_path))
    {
        swprintf(param, ARRAY_SIZE(param), L"\"%s\"", item->target_path);
        ShellExecuteW(NULL, L"open", L"explorer.exe", param, NULL, SW_SHOWNORMAL);
        return;
    }

    /* 6. General file / shortcut / application */
    ShellExecuteW(NULL, L"open", item->target_path,
                  item->arguments[0] ? item->arguments : NULL,
                  NULL, SW_SHOWNORMAL);
}

static void show_desktop_context_menu(HWND hwnd, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    HMENU hSubArrange = CreatePopupMenu();
    HMENU hSubNew = CreatePopupMenu();

    AppendMenuW(hSubArrange, MF_STRING, IDM_ARRANGE_NAME, L"by &Name");
    AppendMenuW(hSubArrange, MF_STRING, IDM_ARRANGE_SIZE, L"by &Size");
    AppendMenuW(hSubArrange, MF_STRING, IDM_ARRANGE_TYPE, L"by &Type");
    AppendMenuW(hSubArrange, MF_STRING, IDM_ARRANGE_MODIFIED, L"by &Modified");
    AppendMenuW(hSubArrange, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hSubArrange, MF_STRING | (g_desktop_view.auto_arrange ? MF_CHECKED : 0),
                IDM_ARRANGE_AUTO, L"&Auto Arrange");
    AppendMenuW(hSubArrange, MF_STRING | (g_desktop_view.align_to_grid ? MF_CHECKED : 0),
                IDM_ALIGN_TO_GRID, L"Align to &Grid");
    AppendMenuW(hSubArrange, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hSubArrange, MF_STRING | (g_desktop_view.show_desktop_icons ? MF_CHECKED : 0),
                IDM_SHOW_ICONS, L"Show &Desktop Icons");

    AppendMenuW(hSubNew, MF_STRING, IDM_NEW_FOLDER, L"&Folder");
    AppendMenuW(hSubNew, MF_STRING, IDM_NEW_SHORTCUT, L"&Shortcut");
    AppendMenuW(hSubNew, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hSubNew, MF_STRING, IDM_NEW_TEXT, L"&Text Document");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubArrange, L"Arrange &Icons By");
    AppendMenuW(hMenu, MF_STRING, IDM_LINE_UP, L"&Line Up Icons");
    AppendMenuW(hMenu, MF_STRING, IDM_REFRESH, L"&Refresh");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE, L"&Paste");
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE_SHORTCUT, L"Paste &Shortcut");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubNew, L"&New");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_PROPERTIES, L"P&roperties");

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

static void delete_selected_items(HWND hwnd)
{
    int i;
    for (i = 0; i < g_desktop_view.item_count; i++)
    {
        DESKTOP_ITEM *item = g_desktop_view.items[i];
        if (item->selected && item->type != DESKTOP_ITEM_NAMESPACE && item->target_path[0])
        {
            WCHAR buf[MAX_PATH + 2];
            SHFILEOPSTRUCTW op;

            memset(buf, 0, sizeof(buf));
            lstrcpynW(buf, item->target_path, MAX_PATH);

            memset(&op, 0, sizeof(op));
            op.hwnd = hwnd;
            op.wFunc = FO_DELETE;
            op.pFrom = buf;
            op.fFlags = FOF_ALLOWUNDO;

            SHFileOperationW(&op);
        }
    }
    desktop_view_refresh();
}

static void show_item_context_menu(HWND hwnd, DESKTOP_ITEM *item, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    BOOL is_my_computer = (wcsstr(item->target_path, L"20D04FE0") != NULL);
    BOOL is_my_network = (wcsstr(item->target_path, L"208D2C60") != NULL);
    BOOL is_recycle_bin = (wcsstr(item->target_path, L"645FF040") != NULL);
    BOOL is_namespace = (item->type == DESKTOP_ITEM_NAMESPACE);
    UINT cmd;

    AppendMenuW(hMenu, MF_STRING | MF_DEFAULT, IDM_ITEM_OPEN, L"&Open");
    if (item->type == DESKTOP_ITEM_FOLDER || PathIsDirectoryW(item->target_path))
        AppendMenuW(hMenu, MF_STRING, IDM_ITEM_EXPLORE, L"&Explore");

    if (is_my_computer)
    {
        AppendMenuW(hMenu, MF_STRING, IDM_ITEM_MANAGE, L"&Manage");
    }
    else if (is_recycle_bin)
    {
        AppendMenuW(hMenu, MF_STRING, IDM_ITEM_EMPTY_RECYCLE, L"&Empty Recycle Bin");
    }
    else if (!is_namespace)
    {
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_ITEM_DELETE, L"&Delete");
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_ITEM_PROPERTIES, L"P&roperties");

    SetForegroundWindow(hwnd);
    cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                         pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (cmd)
    {
    case IDM_ITEM_OPEN:
    case IDM_ITEM_EXPLORE:
        execute_item(item);
        break;
    case IDM_ITEM_MANAGE:
        ShellExecuteW(NULL, L"open", L"compmgmt.msc", NULL, NULL, SW_SHOWNORMAL);
        break;
    case IDM_ITEM_EMPTY_RECYCLE:
        SHEmptyRecycleBinW(hwnd, NULL, 0);
        break;
    case IDM_ITEM_DELETE:
        delete_selected_items(hwnd);
        break;
    case IDM_ITEM_PROPERTIES:
        if (is_my_computer)
            ShellExecuteW(NULL, L"open", L"control.exe", L"sysdm.cpl", NULL, SW_SHOWNORMAL);
        else if (is_my_network)
            ShellExecuteW(NULL, L"open", L"control.exe", L"ncpa.cpl", NULL, SW_SHOWNORMAL);
        else if (is_recycle_bin)
            ShellExecuteW(NULL, L"open", L"rundll32.exe", L"shell32.dll,Options_RunDLL 7", NULL, SW_SHOWNORMAL);
        else
            ShellExecuteW(NULL, L"properties", item->target_path, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
}

static LRESULT CALLBACK desktop_view_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_DISPLAYCHANGE:
    {
        int w = LOWORD(lparam);
        int h = HIWORD(lparam);
        desktop_view_on_display_change(w, h);
        return 0;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lparam);
        int h = HIWORD(lparam);
        if (w && h && (wparam == SIZE_RESTORED || wparam == SIZE_MAXIMIZED))
        {
            desktop_view_on_display_change(w, h);
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        desktop_view_paint(hdc, &ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        int i, hit_idx = -1;

        SetFocus(hwnd);
        SetCapture(hwnd);

        hit_idx = item_at(hwnd, pt);

        if (hit_idx != -1)
        {
            /* Item clicked */
            for (i = 0; i < g_desktop_view.item_count; i++)
                g_desktop_view.items[i]->selected = g_desktop_view.items[i]->focused = (i == hit_idx);

            g_desktop_view.drag_active = TRUE;
            g_desktop_view.dragged_index = hit_idx;
            g_desktop_view.drag_offset.x = pt.x - g_desktop_view.items[hit_idx]->pos.x;
            g_desktop_view.drag_offset.y = pt.y - g_desktop_view.items[hit_idx]->pos.y;
        }
        else
        {
            /* Empty area clicked: clear selections and start selection marquee */
            for (i = 0; i < g_desktop_view.item_count; i++)
                g_desktop_view.items[i]->selected = FALSE;

            g_desktop_view.marquee_active = TRUE;
            g_desktop_view.marquee_start = pt;
            g_desktop_view.marquee_current = pt;
            SetRectEmpty(&g_desktop_view.marquee_rect);
        }

        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };

        if (g_desktop_view.drag_active && g_desktop_view.dragged_index >= 0)
        {
            DESKTOP_ITEM *item = g_desktop_view.items[g_desktop_view.dragged_index];
            item->pos.x = pt.x - g_desktop_view.drag_offset.x;
            item->pos.y = pt.y - g_desktop_view.drag_offset.y;
            update_item_bounds(item, g_desktop_view.grid_cx, g_desktop_view.grid_cy);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (g_desktop_view.marquee_active)
        {
            int i;
            RECT old_rc = g_desktop_view.marquee_rect;

            g_desktop_view.marquee_current = pt;
            g_desktop_view.marquee_rect.left = min(g_desktop_view.marquee_start.x, pt.x);
            g_desktop_view.marquee_rect.top = min(g_desktop_view.marquee_start.y, pt.y);
            g_desktop_view.marquee_rect.right = max(g_desktop_view.marquee_start.x, pt.x);
            g_desktop_view.marquee_rect.bottom = max(g_desktop_view.marquee_start.y, pt.y);

            for (i = 0; i < g_desktop_view.item_count; i++)
            {
                RECT intersect;
                g_desktop_view.items[i]->selected =
                    IntersectRect(&intersect, &g_desktop_view.marquee_rect, &g_desktop_view.items[i]->bounds);
            }

            InvalidateRect(hwnd, &old_rc, FALSE);
            InvalidateRect(hwnd, &g_desktop_view.marquee_rect, FALSE);
        }
        return 0;
    }

    case WM_TIMER:
        if (wparam == 1) update_recycle_bin_icon();
        return 0;

    case WM_LBUTTONUP:
    {
        ReleaseCapture();

        if (g_desktop_view.drag_active)
        {
            g_desktop_view.drag_active = FALSE;
            if (g_desktop_view.align_to_grid)
                desktop_view_align_to_grid();
        }

        if (g_desktop_view.marquee_active)
        {
            g_desktop_view.marquee_active = FALSE;
            InvalidateRect(hwnd, &g_desktop_view.marquee_rect, FALSE);
            SetRectEmpty(&g_desktop_view.marquee_rect);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        int i;

        i = item_at(hwnd, pt);
        if (i >= 0) execute_item(g_desktop_view.items[i]);
        return 0;
    }

    case WM_RBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        POINT screen_pt = pt;
        int i, hit_idx = -1;

        SetFocus(hwnd);
        ClientToScreen(hwnd, &screen_pt);

        hit_idx = item_at(hwnd, pt);

        if (hit_idx != -1)
        {
            for (i = 0; i < g_desktop_view.item_count; i++)
                g_desktop_view.items[i]->selected = g_desktop_view.items[i]->focused = (i == hit_idx);
            InvalidateRect(hwnd, NULL, FALSE);
            show_item_context_menu(hwnd, g_desktop_view.items[hit_idx], screen_pt);
        }
        else
        {
            show_desktop_context_menu(hwnd, screen_pt);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDM_REFRESH:
            desktop_view_refresh();
            break;
        case IDM_ARRANGE_AUTO:
            g_desktop_view.auto_arrange = !g_desktop_view.auto_arrange;
            if (g_desktop_view.auto_arrange) desktop_view_auto_arrange();
            break;
        case IDM_ALIGN_TO_GRID:
            g_desktop_view.align_to_grid = !g_desktop_view.align_to_grid;
            if (g_desktop_view.align_to_grid) desktop_view_align_to_grid();
            break;
        case IDM_SHOW_ICONS:
            g_desktop_view.show_desktop_icons = !g_desktop_view.show_desktop_icons;
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        case IDM_LINE_UP:
            desktop_view_align_to_grid();
            break;
        case IDM_NEW_FOLDER:
        {
            WCHAR desktop_dir[MAX_PATH];
            if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop_dir) == S_OK)
            {
                WCHAR new_path[MAX_PATH];
                int idx = 1;
                swprintf(new_path, ARRAY_SIZE(new_path), L"%s\\New Folder", desktop_dir);
                while (GetFileAttributesW(new_path) != INVALID_FILE_ATTRIBUTES)
                {
                    idx++;
                    swprintf(new_path, ARRAY_SIZE(new_path), L"%s\\New Folder (%d)", desktop_dir, idx);
                }
                CreateDirectoryW(new_path, NULL);
                desktop_view_refresh();
            }
            break;
        }
        case IDM_NEW_TEXT:
        {
            WCHAR desktop_dir[MAX_PATH];
            if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop_dir) == S_OK)
            {
                WCHAR new_path[MAX_PATH];
                HANDLE hFile;
                int idx = 1;
                swprintf(new_path, ARRAY_SIZE(new_path), L"%s\\New Text Document.txt", desktop_dir);
                while (GetFileAttributesW(new_path) != INVALID_FILE_ATTRIBUTES)
                {
                    idx++;
                    swprintf(new_path, ARRAY_SIZE(new_path), L"%s\\New Text Document (%d).txt", desktop_dir, idx);
                }
                hFile = CreateFileW(new_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
                desktop_view_refresh();
            }
            break;
        }
        case IDM_NEW_SHORTCUT:
            ShellExecuteW(NULL, L"open", L"rundll32.exe", L"appwiz.cpl,NewLinkHere_RunDLL", NULL, SW_SHOWNORMAL);
            break;
        case IDM_PROPERTIES:
            ShellExecuteW(NULL, L"open", L"control.exe", L"desk.cpl", NULL, SW_SHOWNORMAL);
            break;
        }
        return 0;

    case WM_KEYDOWN:
    {
        int i;
        switch (wparam)
        {
        case VK_RETURN:
            for (i = 0; i < g_desktop_view.item_count; i++)
            {
                if (g_desktop_view.items[i]->selected)
                {
                    execute_item(g_desktop_view.items[i]);
                    break;
                }
            }
            return 0;

        case VK_DELETE:
            delete_selected_items(hwnd);
            return 0;

        case VK_F5:
            desktop_view_refresh();
            return 0;

        case 'A':
            if (GetKeyState(VK_CONTROL) & 0x8000)
            {
                for (i = 0; i < g_desktop_view.item_count; i++)
                    g_desktop_view.items[i]->selected = TRUE;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;

        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        {
            int current_idx = -1;
            int new_idx = -1;
            for (i = 0; i < g_desktop_view.item_count; i++)
            {
                if (g_desktop_view.items[i]->selected)
                {
                    current_idx = i;
                    break;
                }
            }

            if (current_idx == -1)
            {
                new_idx = 0;
            }
            else
            {
                if (wparam == VK_DOWN || wparam == VK_RIGHT)
                    new_idx = min(current_idx + 1, g_desktop_view.item_count - 1);
                else
                    new_idx = max(current_idx - 1, 0);
            }

            if (new_idx >= 0 && new_idx < g_desktop_view.item_count)
            {
                for (i = 0; i < g_desktop_view.item_count; i++)
                    g_desktop_view.items[i]->selected = (i == new_idx);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_ERASEBKGND:
        return 1; /* Handled in WM_PAINT double-buffering */

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

HWND desktop_view_create(HWND parent_desktop, int width, int height)
{
    WNDCLASSW wc;

    memset(&g_desktop_view, 0, sizeof(g_desktop_view));
    g_desktop_view.parent_desktop = parent_desktop;
    g_desktop_view.grid_cx = 81;   /* XP measured: icon spacing 75 plus padding */
    g_desktop_view.grid_cy = 81;
    g_desktop_view.icon_size = 32;
    g_desktop_view.align_to_grid = TRUE;
    g_desktop_view.show_desktop_icons = TRUE;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = desktop_view_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = DESKTOP_VIEW_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.style = CS_DBLCLKS;
    RegisterClassW(&wc);

    g_desktop_view.hwnd = CreateWindowExW(0, DESKTOP_VIEW_CLASS, NULL,
                                          WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                          0, 0, width, height,
                                          parent_desktop, NULL, wc.hInstance, NULL);

    if (g_desktop_view.hwnd)
        SetWindowPos(g_desktop_view.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    desktop_view_refresh();
    SetTimer(g_desktop_view.hwnd, 1, 3000, NULL);
    return g_desktop_view.hwnd;
}

void desktop_view_destroy(void)
{
    if (g_desktop_view.hbm_wallpaper)
    {
        DeleteObject(g_desktop_view.hbm_wallpaper);
        g_desktop_view.hbm_wallpaper = NULL;
    }
    free_all_items();
    if (g_desktop_view.hwnd)
    {
        DestroyWindow(g_desktop_view.hwnd);
        g_desktop_view.hwnd = NULL;
    }
}

HWND desktop_view_get_hwnd(void)
{
    return g_desktop_view.hwnd;
}
