/*
 * WineExplorer Desktop Environment - Explorer task pane
 *
 * The left pane of an Explorer window: "File and Folder Tasks" (or "System Tasks"), "Other Places" and "Details",
 * drawn with the visual style's ExplorerBar class.  Links act on the current folder and selection.
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

#define COBJMACROS

#include <stdio.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>

#include "wine/debug.h"
#include "theme_engine.h"
#include "browser.h"
#include "shellres.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define TASKPANE_CLASS L"BrowserTaskPane"
#define MAX_LINES  40
#define LINK_ICON  16

extern HINSTANCE explorer_hInstance;

typedef struct
{
    WCHAR text[256];
    UINT command;        /* browser command, or place id */
    int place;           /* CSIDL of a place to browse to, or -1 */
    int icon_id;         /* shell32 icon resource id, 0 = none */
    BOOL detail_head;    /* bold first line of the Details group */
    BOOL plain;          /* detail text, not a link */
    RECT rc;
} LINE;

typedef struct
{
    WCHAR title[64];
    BOOL special;        /* dark blue head, like "System Tasks" */
    BOOL collapsed;
    int first, count;
    RECT head;
} GROUP;

typedef struct
{
    explorer_info *info;
    LINE lines[MAX_LINES];
    int line_count;
    GROUP groups[3];
    int group_count;
    int hot_line, hot_group;
    BOOL tracking;
    WCHAR signature[512];
} TASKPANE;

static TASKPANE g_pane;

static void add_group(const WCHAR *title, BOOL special)
{
    GROUP *g = &g_pane.groups[g_pane.group_count++];
    memset(g, 0, sizeof(*g));
    lstrcpynW(g->title, title, ARRAY_SIZE(g->title));
    g->special = special;
    g->first = g_pane.line_count;
}

static LINE *add_line(const WCHAR *text, UINT command, int place, int icon)
{
    LINE *l;
    if (g_pane.line_count >= MAX_LINES) return &g_pane.lines[MAX_LINES - 1];
    l = &g_pane.lines[g_pane.line_count++];
    memset(l, 0, sizeof(*l));
    lstrcpynW(l->text, text, ARRAY_SIZE(l->text));
    l->command = command;
    l->place = place;
    l->icon_id = icon;
    g_pane.groups[g_pane.group_count - 1].count++;
    return l;
}

static HWND current_list(explorer_info *info)
{
    IShellView *sv = NULL;
    HWND view = NULL, list = NULL;

    IExplorerBrowser_GetCurrentView(info->browser, &IID_IShellView, (void **)&sv);
    if (sv)
    {
        if (SUCCEEDED(IShellView_GetWindow(sv, &view))) list = FindWindowExW(view, NULL, L"SysListView32", NULL);
        IShellView_Release(sv);
    }
    return list;
}

/* text of a column of the first selected item, found by the column's header */
static BOOL selected_column(HWND list, const WCHAR *header, WCHAR *out, int count)
{
    HWND head = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
    int item = (int)SendMessageW(list, LVM_GETNEXTITEM, -1, LVNI_SELECTED), col, cols;

    out[0] = 0;
    if (item < 0 || !head) return FALSE;
    cols = (int)SendMessageW(head, HDM_GETITEMCOUNT, 0, 0);
    for (col = 0; col < cols; col++)
    {
        WCHAR name[64];
        LVCOLUMNW c = { LVCF_TEXT, 0, 0, name, ARRAY_SIZE(name) };
        if (SendMessageW(list, LVM_GETCOLUMNW, col, (LPARAM)&c) && !wcsicmp(name, header))
        {
            LVITEMW it = { .iSubItem = col, .pszText = out, .cchTextMax = count };
            return SendMessageW(list, LVM_GETITEMTEXTW, item, (LPARAM)&it) > 0;
        }
    }
    return FALSE;
}

static BOOL is_my_computer(explorer_info *info)
{
    LPITEMIDLIST drives;
    BOOL ret = FALSE;

    if (info->pidl && SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &drives)))
    {
        ret = ILIsEqual(info->pidl, drives);
        ILFree(drives);
    }
    return ret;
}

static void rebuild(explorer_info *info)
{
    HWND list = current_list(info);
    int selected = list ? (int)SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0) : 0;
    WCHAR name[MAX_PATH], type[80], date[80], size[80], folder_name[MAX_PATH] = {0}, signature[512];
    BOOL computer = is_my_computer(info), has_path;
    WCHAR path[MAX_PATH];
    SHFILEINFOW sfi = {0};
    LINE *l;

    has_path = info->pidl && SHGetPathFromIDListW(info->pidl, path);
    if (info->pidl && SHGetFileInfoW((const WCHAR *)info->pidl, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_DISPLAYNAME | SHGFI_TYPENAME))
        lstrcpynW(folder_name, sfi.szDisplayName, MAX_PATH);

    name[0] = type[0] = date[0] = size[0] = 0;
    if (selected == 1 && list)
    {
        selected_column(list, L"Name", name, ARRAY_SIZE(name));
        selected_column(list, L"Type", type, ARRAY_SIZE(type));
        selected_column(list, L"Date Modified", date, ARRAY_SIZE(date));
        selected_column(list, L"Size", size, ARRAY_SIZE(size));
    }
    swprintf(signature, ARRAY_SIZE(signature), L"%p|%d|%d|%s|%s|%s|%s", info->pidl, selected, computer, name, type, date, size);
    if (!wcscmp(signature, g_pane.signature)) return;
    lstrcpynW(g_pane.signature, signature, ARRAY_SIZE(g_pane.signature));

    g_pane.line_count = g_pane.group_count = 0;
    if (computer)
    {
        add_group(L"System Tasks", TRUE);
        add_line(L"View system information", 1000, -1, 0);
        add_line(L"Add or remove programs", 1001, -1, 0);
        add_line(L"Change a setting", 1002, -1, 0);
    }
    else if (has_path || selected)
    {
        add_group(L"File and Folder Tasks", TRUE);
        if (!selected)
            add_line(L"Make a new folder", IDM_FILE_NEWFOLDER, -1, 0);
        else
        {
            add_line(selected == 1 ? L"Rename this item" : L"Rename the selected items", IDM_FILE_RENAME, -1, 0);
            add_line(selected == 1 ? L"Copy this item" : L"Copy the selected items", IDM_EDIT_COPY, -1, 0);
            add_line(selected == 1 ? L"Delete this item" : L"Delete the selected items", IDM_FILE_DELETE, -1, 0);
            add_line(L"Make a new folder", IDM_FILE_NEWFOLDER, -1, 0);
        }
    }

    add_group(L"Other Places", FALSE);
    if (!computer) { l = add_line(L"My Computer", 0, CSIDL_DRIVES, 16); (void)l; }
    add_line(L"My Documents", 0, CSIDL_PERSONAL, 235);
    add_line(L"My Network Places", 0, CSIDL_NETWORK, 18);
    add_line(L"Desktop", 0, CSIDL_DESKTOP, 35);

    add_group(L"Details", FALSE);
    if (selected == 1 && name[0])
    {
        l = add_line(name, 0, -1, 0); l->detail_head = TRUE; l->plain = TRUE;
        if (type[0]) { l = add_line(type, 0, -1, 0); l->plain = TRUE; }
        if (date[0])
        {
            WCHAR text[160];
            swprintf(text, ARRAY_SIZE(text), L"Date Modified: %s", date);
            l = add_line(text, 0, -1, 0); l->plain = TRUE;
        }
        if (size[0])
        {
            WCHAR text[160];
            swprintf(text, ARRAY_SIZE(text), L"Size: %s", size);
            l = add_line(text, 0, -1, 0); l->plain = TRUE;
        }
    }
    else if (selected > 1)
    {
        WCHAR text[80];
        swprintf(text, ARRAY_SIZE(text), L"%d objects selected", selected);
        l = add_line(text, 0, -1, 0); l->detail_head = TRUE; l->plain = TRUE;
    }
    else if (folder_name[0])
    {
        l = add_line(folder_name, 0, -1, 0); l->detail_head = TRUE; l->plain = TRUE;
        if (sfi.szTypeName[0]) { l = add_line(sfi.szTypeName, 0, -1, 0); l->plain = TRUE; }
    }
    g_pane.hot_line = g_pane.hot_group = -1;
}

/* ---- drawing and layout ---- */

static HFONT get_font(HTHEME theme, HDC hdc, int part, BOOL bold)
{
    LOGFONTW lf;
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };

    if (theme && SUCCEEDED(GetThemeFont(theme, hdc, part, 0, TMT_FONT, &lf))) return CreateFontIndirectW(&lf);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

/* Lay the groups out for a width; returns the total height */
static int layout(HDC hdc, int width, HTHEME theme)
{
    HFONT font = get_font(theme, hdc, EBP_NORMALGROUPBACKGROUND, FALSE), old = SelectObject(hdc, font);
    int y = 12, g, i, text_x = 8 + 4;
    MARGINS m = {0};

    if (theme) GetThemeMargins(theme, hdc, EBP_NORMALGROUPBACKGROUND, 0, TMT_CONTENTMARGINS, NULL, &m);
    (void)m;
    for (g = 0; g < g_pane.group_count; g++)
    {
        GROUP *grp = &g_pane.groups[g];
        int body_y;

        SetRect(&grp->head, 12, y, width - 12, y + 25);
        y += 25;
        body_y = y;
        if (!grp->collapsed)
        {
            y += 8;
            for (i = grp->first; i < grp->first + grp->count; i++)
            {
                LINE *l = &g_pane.lines[i];
                RECT calc = { 0, 0, width - 24 - 16 - text_x - (l->icon_id ? LINK_ICON + 4 : 0), 0 };
                int left = 12 + 8 + (l->icon_id ? LINK_ICON + 4 : 0);

                DrawTextW(hdc, l->text, -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                SetRect(&l->rc, left, y, width - 12 - 8, y + max(calc.bottom, LINK_ICON) + 2);
                y = l->rc.bottom + 4;
            }
            y += 4;
        }
        (void)body_y;
        y += 12;
    }
    SelectObject(hdc, old);
    DeleteObject(font);
    return y;
}

static void paint(HWND hwnd, HDC hdc)
{
    HTHEME theme = theme_open(hwnd, L"ExplorerBar");
    RECT rc;
    HFONT font, bold;
    int g, i;

    GetClientRect(hwnd, &rc);
    if (theme) DrawThemeBackground(theme, hdc, 0, 0, &rc, NULL);
    else FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

    layout(hdc, rc.right, theme);
    font = get_font(theme, hdc, EBP_NORMALGROUPBACKGROUND, FALSE);
    bold = get_font(theme, hdc, EBP_NORMALGROUPHEAD, TRUE);
    SetBkMode(hdc, TRANSPARENT);

    for (g = 0; g < g_pane.group_count; g++)
    {
        GROUP *grp = &g_pane.groups[g];
        int head_part = grp->special ? EBP_SPECIALGROUPHEAD : EBP_NORMALGROUPHEAD;
        int body_part = grp->special ? EBP_SPECIALGROUPBACKGROUND : EBP_NORMALGROUPBACKGROUND;
        int arrow_part = grp->collapsed ? (grp->special ? EBP_SPECIALGROUPEXPAND : EBP_NORMALGROUPEXPAND)
                                        : (grp->special ? EBP_SPECIALGROUPCOLLAPSE : EBP_NORMALGROUPCOLLAPSE);
        COLORREF head_color = grp->special ? RGB(255, 255, 255) : RGB(33, 93, 198), link_color = RGB(33, 93, 198);
        COLORREF hot_color = RGB(66, 142, 255), text_color = RGB(0, 0, 0);
        RECT body = grp->head, text;

        if (theme)
        {
            GetThemeColor(theme, head_part, 0, TMT_TEXTCOLOR, &head_color);
            GetThemeColor(theme, body_part, 0, TMT_TEXTCOLOR, &link_color);
        }

        if (!grp->collapsed && grp->count)
        {
            body.top = grp->head.bottom;
            body.bottom = g_pane.lines[grp->first + grp->count - 1].rc.bottom + 8;
            if (theme) DrawThemeBackground(theme, hdc, body_part, 0, &body, NULL);
            else FillRect(hdc, &body, GetSysColorBrush(COLOR_WINDOW));
        }
        if (theme) DrawThemeBackground(theme, hdc, head_part, 0, &grp->head, NULL);
        else FillRect(hdc, &grp->head, GetSysColorBrush(COLOR_ACTIVECAPTION));
        text = grp->head;
        text.left += 10; text.right -= 28;
        SelectObject(hdc, bold);
        SetTextColor(hdc, head_color);
        DrawTextW(hdc, grp->title, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

        if (theme)
        {
            SIZE sz = {0};
            RECT arrow;
            int state = (g == g_pane.hot_group) ? 2 : 1;
            if (SUCCEEDED(GetThemePartSize(theme, hdc, arrow_part, state, NULL, TS_TRUE, &sz)))
            {
                SetRect(&arrow, grp->head.right - sz.cx - 4, grp->head.top + (25 - sz.cy) / 2, grp->head.right - 4, 0);
                arrow.bottom = arrow.top + sz.cy;
                DrawThemeBackground(theme, hdc, arrow_part, state, &arrow, NULL);
            }
        }

        if (grp->collapsed) continue;
        for (i = grp->first; i < grp->first + grp->count; i++)
        {
            LINE *l = &g_pane.lines[i];
            BOOL hot = !l->plain && i == g_pane.hot_line;

            if (l->icon_id)
            {
                HICON icon = shellres_icon(l->icon_id, LINK_ICON);
                if (icon)
                {
                    DrawIconEx(hdc, l->rc.left - LINK_ICON - 4, l->rc.top, icon, LINK_ICON, LINK_ICON, 0, NULL, DI_NORMAL);
                    DestroyIcon(icon);
                }
            }
            SelectObject(hdc, l->detail_head ? bold : font);
            SetTextColor(hdc, l->plain ? (l->detail_head ? RGB(0, 0, 0) : text_color) : (hot ? hot_color : link_color));
            {
                RECT t = l->rc;
                DrawTextW(hdc, l->text, -1, &t, DT_WORDBREAK | DT_NOPREFIX | DT_LEFT);
                if (hot)
                {
                    /* links underline when the mouse is over them */
                    HPEN pen = CreatePen(PS_SOLID, 1, hot_color), prev = SelectObject(hdc, pen);
                    SIZE sz;
                    GetTextExtentPoint32W(hdc, l->text, lstrlenW(l->text), &sz);
                    MoveToEx(hdc, l->rc.left, l->rc.top + sz.cy, NULL);
                    LineTo(hdc, l->rc.left + min(sz.cx, l->rc.right - l->rc.left), l->rc.top + sz.cy);
                    SelectObject(hdc, prev);
                    DeleteObject(pen);
                }
            }
        }
    }
    DeleteObject(font);
    DeleteObject(bold);
    if (theme) CloseThemeData(theme);
}

static void hit_test(int x, int y, int *line, int *group)
{
    POINT pt = { x, y };
    int g, i;

    *line = *group = -1;
    for (g = 0; g < g_pane.group_count; g++)
    {
        GROUP *grp = &g_pane.groups[g];
        if (PtInRect(&grp->head, pt)) { *group = g; return; }
        if (grp->collapsed) continue;
        for (i = grp->first; i < grp->first + grp->count; i++)
            if (!g_pane.lines[i].plain && PtInRect(&g_pane.lines[i].rc, pt)) { *line = i; return; }
    }
}

static void activate(LINE *l)
{
    explorer_info *info = g_pane.info;

    if (l->place >= 0)
    {
        LPITEMIDLIST pidl;
        if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, l->place, &pidl)))
        {
            IExplorerBrowser_BrowseToIDList(info->browser, pidl, SBSP_ABSOLUTE);
            ILFree(pidl);
        }
        return;
    }
    switch (l->command)
    {
    case 1000: ShellExecuteW(NULL, L"open", L"control.exe", L"sysdm.cpl", NULL, SW_SHOWNORMAL); break;
    case 1001: ShellExecuteW(NULL, L"open", L"control.exe", L"appwiz.cpl", NULL, SW_SHOWNORMAL); break;
    case 1002: ShellExecuteW(NULL, L"open", L"control.exe", NULL, NULL, SW_SHOWNORMAL); break;
    default: if (l->command) browser_command(info, l->command); break;
    }
}

static LRESULT CALLBACK pane_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HDC mem;
        HBITMAP bmp, prev;

        GetClientRect(hwnd, &rc);
        mem = CreateCompatibleDC(hdc);
        bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        prev = SelectObject(mem, bmp);
        paint(hwnd, mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
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
        int line, group;
        hit_test((short)LOWORD(lparam), (short)HIWORD(lparam), &line, &group);
        if (!g_pane.tracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_pane.tracking = TRUE;
        }
        if (line != g_pane.hot_line || group != g_pane.hot_group)
        {
            g_pane.hot_line = line; g_pane.hot_group = group;
            SetCursor(LoadCursorW(NULL, (line >= 0 || group >= 0) ? (const WCHAR *)IDC_HAND : (const WCHAR *)IDC_ARROW));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_pane.tracking = FALSE;
        g_pane.hot_line = g_pane.hot_group = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (g_pane.hot_line >= 0 || g_pane.hot_group >= 0) { SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONUP:
    {
        int line, group;
        hit_test((short)LOWORD(lparam), (short)HIWORD(lparam), &line, &group);
        if (group >= 0)
        {
            g_pane.groups[group].collapsed = !g_pane.groups[group].collapsed;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (line >= 0) activate(&g_pane.lines[line]);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ---- API ---- */

HWND taskpane_create(explorer_info *info)
{
    WNDCLASSW wc = {0};

    wc.lpfnWndProc = pane_proc;
    wc.hInstance = explorer_hInstance;
    wc.lpszClassName = TASKPANE_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);
    memset(&g_pane, 0, sizeof(g_pane));
    g_pane.info = info;
    g_pane.hot_line = g_pane.hot_group = -1;
    return CreateWindowExW(0, TASKPANE_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, 100,
                           info->main_window, NULL, explorer_hInstance, NULL);
}

void taskpane_update(explorer_info *info)
{
    if (!info->taskpane || !IsWindowVisible(info->taskpane)) return;
    g_pane.info = info;
    rebuild(info);
    InvalidateRect(info->taskpane, NULL, FALSE);
}
