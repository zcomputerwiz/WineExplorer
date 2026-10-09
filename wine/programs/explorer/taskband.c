/*
 * WineExplorer Desktop Environment - TaskBand Implementation
 *
 * Implements running application window tracking, taskbar buttons, active/flashing states,
 * window activation/minimization, grouping, and authentic system menus.
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
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "theme_engine.h"
#include "taskband.h"
#include "taskbar_props.h"

#define TASKBAND_CLASS L"MSTaskListWClass"

#define MAX_TASK_BUTTON_WIDTH 160
#define MIN_TASK_BUTTON_WIDTH  40

#define IDM_TASK_RESTORE  1
#define IDM_TASK_MOVE     2
#define IDM_TASK_SIZE     3
#define IDM_TASK_MINIMIZE 4
#define IDM_TASK_MAXIMIZE 5
#define IDM_TASK_CLOSE    6

static TASKBAND g_taskband;

static BOOL is_window_taskbar_candidate(HWND hwnd);

BOOL taskband_is_task_window(HWND hwnd)
{
    return is_window_taskbar_candidate(hwnd);
}

static BOOL is_window_taskbar_candidate(HWND hwnd)
{
    DWORD style, exstyle;
    DWORD process_id;
    HWND root_desktop;

    if (!hwnd || !IsWindow(hwnd)) return FALSE;
    if (!IsWindowVisible(hwnd)) return FALSE;

    /* Tray window or desktop itself cannot be a task */
    root_desktop = g_taskband.parent_tray ? GetAncestor(g_taskband.parent_tray, GA_ROOT) : NULL;
    if (hwnd == g_taskband.parent_tray || hwnd == g_taskband.hwnd || hwnd == GetDesktopWindow() || hwnd == root_desktop)
        return FALSE;

    /* Ignore our own explorer process windows EXCEPT cabinet/explore folder windows and the hosts of Linux
     * programs' windows (linux_windows.c) */
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == GetCurrentProcessId())
    {
        WCHAR cls[64];
        if (GetClassNameW(hwnd, cls, ARRAY_SIZE(cls)))
        {
            if (wcscmp(cls, L"CabinetWClass") != 0 && wcscmp(cls, L"ExploreWClass") != 0 &&
                wcscmp(cls, L"WineExplorerLinuxWindow") != 0)
                return FALSE;
        }
        else return FALSE;
    }

    style = GetWindowLongW(hwnd, GWL_STYLE);
    exstyle = GetWindowLongW(hwnd, GWL_EXSTYLE);

    /* Child windows are not shown on the taskbar */
    if (style & WS_CHILD) return FALSE;

    /* Tool windows are not shown unless explicitly marked with WS_EX_APPWINDOW */
    if ((exstyle & WS_EX_TOOLWINDOW) && !(exstyle & WS_EX_APPWINDOW)) return FALSE;

    /* Owned windows without WS_EX_APPWINDOW are not shown */
    if (GetWindow(hwnd, GW_OWNER) != NULL && !(exstyle & WS_EX_APPWINDOW)) return FALSE;

    return TRUE;
}

/* Icon handles belong to the process that created them and Wine cannot draw another process's icon, so a
 * handle from WM_GETICON is only usable when GetIconInfo accepts it. */
static BOOL icon_is_usable(HICON icon)
{
    ICONINFO info;

    if (!icon || !GetIconInfo(icon, &info)) return FALSE;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return TRUE;
}

/* The small icon of the window's program, extracted from its executable (cached per process). */
static HICON get_process_icon(HWND hwnd)
{
    static struct { DWORD pid; HICON icon; } cache[64];
    static unsigned int next;
    WCHAR path[MAX_PATH];
    DWORD pid, len = MAX_PATH;
    HICON small_icon = NULL;
    HANDLE process;
    unsigned int i;

    GetWindowThreadProcessId(hwnd, &pid);
    for (i = 0; i < ARRAY_SIZE(cache); i++)
        if (cache[i].pid == pid && cache[i].icon) return cache[i].icon;

    if (!(process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))) return NULL;
    if (QueryFullProcessImageNameW(process, 0, path, &len))
        ExtractIconExW(path, 0, NULL, &small_icon, 1);
    CloseHandle(process);
    if (!small_icon) return NULL;

    i = next++ % ARRAY_SIZE(cache);
    if (cache[i].icon) DestroyIcon(cache[i].icon);
    cache[i].pid = pid;
    cache[i].icon = small_icon;
    return small_icon;
}

static HICON get_window_task_icon(HWND hwnd)
{
    static const WPARAM kinds[] = { 2 /* ICON_SMALL2 */, ICON_SMALL, ICON_BIG };
    HICON icon = NULL;
    DWORD_PTR res;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(kinds) && !icon; i++)
        if (SendMessageTimeoutW(hwnd, WM_GETICON, kinds[i], 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &res) &&
            icon_is_usable((HICON)res))
            icon = (HICON)res;
    if (!icon && icon_is_usable((HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM)))
        icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
    if (!icon) icon = get_process_icon(hwnd);
    if (!icon) icon = LoadIconW(NULL, (const WCHAR *)IDI_APPLICATION);
    return icon;
}

static TASK_ITEM *find_task_by_hwnd(HWND hwnd)
{
    TASK_ITEM *curr = g_taskband.tasks;
    while (curr)
    {
        if (curr->target_hwnd == hwnd) return curr;
        curr = curr->next;
    }
    return NULL;
}

void taskband_relayout(void)
{
    int count = g_taskband.task_count;
    int available_width = g_taskband.max_width;
    int button_width = MAX_TASK_BUTTON_WIDTH;
    BOOL themed = theme_is_visual_styles_active();
    int first = theme_taskbar_first_row(), pitch = theme_taskbar_row_pitch();
    int rows = max(1, (g_taskband.button_height - first) / pitch + 1);
    int per_row = max(1, available_width / MAX_TASK_BUTTON_WIDTH), i = 0;
    TASK_ITEM *curr;

    if (count <= 0)
    {
        if (g_taskband.hwnd) InvalidateRect(g_taskband.hwnd, NULL, TRUE);
        return;
    }

    /* the buttons fill the first row at their full width and only use further rows (narrower) when they do not fit */
    if (count > per_row)
    {
        per_row = (count + rows - 1) / rows;
        button_width = max(MIN_TASK_BUTTON_WIDTH, available_width / per_row);
    }
    g_taskband.button_width = button_width;

    for (curr = g_taskband.tasks; curr; curr = curr->next, i++)
    {
        int row = i / per_row, col = i % per_row;

        curr->rect.left = col * button_width;
        curr->rect.right = curr->rect.left + button_width - 2;
        /* the picture of a styled button has its own margins and covers the whole row */
        curr->rect.top = row * pitch + (themed ? 0 : 3);
        curr->rect.bottom = curr->rect.top + (themed ? 30 : 22);
    }

    if (g_taskband.hwnd)
        InvalidateRect(g_taskband.hwnd, NULL, TRUE);
}

void taskband_add_window(HWND hwnd)
{
    TASK_ITEM *item, *tail;

    if (!is_window_taskbar_candidate(hwnd)) return;
    if (find_task_by_hwnd(hwnd)) return;

    item = calloc(1, sizeof(TASK_ITEM));
    if (!item) return;

    item->target_hwnd = hwnd;
    GetWindowThreadProcessId(hwnd, &item->process_id);
    GetWindowTextW(hwnd, item->title, sizeof(item->title)/sizeof(WCHAR));
    item->icon = get_window_task_icon(hwnd);
    item->minimized = IsIconic(hwnd);
    item->active = (hwnd == GetAncestor(GetForegroundWindow(), GA_ROOTOWNER));
    item->seen = TRUE;
    item->next = NULL;

    if (!g_taskband.tasks)
    {
        g_taskband.tasks = item;
    }
    else
    {
        tail = g_taskband.tasks;
        while (tail->next) tail = tail->next;
        tail->next = item;
    }
    g_taskband.task_count++;

    taskband_relayout();
}

static BOOL CALLBACK sync_enum_windows_proc(HWND hwnd, LPARAM lparam)
{
    HWND foreground = (HWND)lparam;
    TASK_ITEM *item;

    if (!is_window_taskbar_candidate(hwnd)) return TRUE;

    item = find_task_by_hwnd(hwnd);
    if (!item)
    {
        taskband_add_window(hwnd);
        item = find_task_by_hwnd(hwnd);
    }

    if (item)
    {
        WCHAR title[256];
        BOOL active = (hwnd == foreground);
        BOOL minimized = IsIconic(hwnd);
        GetWindowTextW(hwnd, title, ARRAY_SIZE(title));

        if (wcscmp(item->title, title) != 0 || item->active != active || item->minimized != minimized)
        {
            lstrcpynW(item->title, title, ARRAY_SIZE(item->title));
            item->active = active;
            item->minimized = minimized;
            if (g_taskband.hwnd) InvalidateRect(g_taskband.hwnd, &item->rect, FALSE);
        }
        item->seen = TRUE;
    }
    return TRUE;
}

void taskband_sync_windows(void)
{
    HWND foreground = GetAncestor(GetForegroundWindow(), GA_ROOTOWNER);
    TASK_ITEM **curr;
    TASK_ITEM *it;
    BOOL changed = FALSE;

    /* Mark existing tasks as unseen */
    it = g_taskband.tasks;
    while (it) { it->seen = FALSE; it = it->next; }

    /* Enumerate top-level desktop windows */
    EnumWindows(sync_enum_windows_proc, (LPARAM)foreground);

    /* Purge tasks that are no longer valid, visible, or seen */
    curr = &g_taskband.tasks;
    while (*curr)
    {
        if (!(*curr)->seen || !IsWindow((*curr)->target_hwnd) || !IsWindowVisible((*curr)->target_hwnd))
        {
            TASK_ITEM *to_free = *curr;
            *curr = (*curr)->next;
            free(to_free);
            g_taskband.task_count--;
            changed = TRUE;
        }
        else
        {
            curr = &(*curr)->next;
        }
    }

    if (changed) taskband_relayout();
}

void taskband_remove_window(HWND hwnd)
{
    TASK_ITEM **curr = &g_taskband.tasks;

    while (*curr)
    {
        if ((*curr)->target_hwnd == hwnd)
        {
            TASK_ITEM *to_free = *curr;
            *curr = (*curr)->next;
            free(to_free);
            g_taskband.task_count--;
            taskband_relayout();
            return;
        }
        curr = &(*curr)->next;
    }
}

void taskband_activate_window(HWND hwnd)
{
    TASK_ITEM *curr = g_taskband.tasks;
    while (curr)
    {
        curr->active = (curr->target_hwnd == hwnd);
        curr->minimized = IsIconic(curr->target_hwnd);
        if (curr->active) curr->flashing = FALSE;
        curr = curr->next;
    }
    if (g_taskband.hwnd)
        InvalidateRect(g_taskband.hwnd, NULL, FALSE);
}

void taskband_update_window(HWND hwnd)
{
    TASK_ITEM *item = find_task_by_hwnd(hwnd);
    if (!item)
    {
        if (is_window_taskbar_candidate(hwnd))
            taskband_add_window(hwnd);
        return;
    }

    GetWindowTextW(hwnd, item->title, sizeof(item->title)/sizeof(WCHAR));
    item->icon = get_window_task_icon(hwnd);
    item->minimized = IsIconic(hwnd);

    if (g_taskband.hwnd)
        InvalidateRect(g_taskband.hwnd, &item->rect, FALSE);
}

void taskband_flash_window(HWND hwnd)
{
    TASK_ITEM *item = find_task_by_hwnd(hwnd);
    if (item && !item->active)
    {
        item->flashing = !item->flashing;
        if (g_taskband.hwnd)
            InvalidateRect(g_taskband.hwnd, &item->rect, FALSE);
    }
}

static HBRUSH get_dither_brush(void)
{
    static HBRUSH hDitherBrush = NULL;
    if (!hDitherBrush)
    {
        static const WORD dither_bits[8] = {
            0x5555, 0xAAAA, 0x5555, 0xAAAA,
            0x5555, 0xAAAA, 0x5555, 0xAAAA
        };
        HBITMAP hbm = CreateBitmap(8, 8, 1, 1, dither_bits);
        if (hbm)
        {
            hDitherBrush = CreatePatternBrush(hbm);
            DeleteObject(hbm);
        }
    }
    return hDitherBrush;
}

static void draw_task_button(HDC hdc, TASK_ITEM *item, HFONT font_normal, HFONT font_bold)
{
    RECT rc = item->rect;
    HFONT old_font;
    int icon_size = 16;
    int icon_y, icon_x;
    RECT text_rc;
    COLORREF text_color = GetSysColor(COLOR_BTNTEXT);

    /* 1. Draw Button Edge & Background */
    HTHEME theme = theme_open(NULL, item->flashing ? L"TaskBand" : L"TaskBand::Toolbar");
    if (theme)
    {
        /* TaskBand::Toolbar button states: 1 normal, 5 checked (the active window); TaskBand flash button when flashing */
        /* button images: 1 normal, 2 hot, 3 pressed, 5 checked (the active window), 6 hot and checked */
        int state = item->pressed ? 3 : (item->active ? (item->hot ? 6 : 5) : (item->hot ? 2 : 1));
        DrawThemeBackground(theme, hdc, item->flashing ? 2 : 1, state, &rc, NULL);
        GetThemeColor(theme, item->flashing ? 2 : 1, state, TMT_TEXTCOLOR, &text_color);
        CloseThemeData(theme);
    }
    else
    {
        /* Authentic Windows Classic 3D button rendering */
        if (item->flashing)
        {
            HBRUSH flash_brush = CreateSolidBrush(RGB(255, 170, 0)); /* Highlight flash color */
            FillRect(hdc, &rc, flash_brush);
            DeleteObject(flash_brush);
            theme_draw_classic_bevel(hdc, &rc, EDGE_RAISED, BF_RECT | BF_ADJUST);
        }
        else if (item->active)
        {
            /* Active button has sunken 3D bevel filled with 50% checkerboard dither pattern */
            COLORREF old_txt, old_bk;
            DrawEdge(hdc, &rc, EDGE_SUNKEN, BF_RECT | BF_ADJUST);

            old_txt = SetTextColor(hdc, GetSysColor(COLOR_BTNHIGHLIGHT));
            old_bk = SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
            FillRect(hdc, &rc, get_dither_brush());
            SetTextColor(hdc, old_txt);
            SetBkColor(hdc, old_bk);
        }
        else
        {
            /* Inactive button has raised 3D bevel and solid button face */
            DrawEdge(hdc, &rc, EDGE_RAISED, BF_RECT | BF_ADJUST);
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        }
    }

    /* 2. Position icon and text - shifted by (+1, +1) when active (authentic pressed look) */
    icon_x = item->rect.left + 4 + (item->active ? 1 : 0);
    icon_y = item->rect.top + (item->rect.bottom - item->rect.top - icon_size) / 2 + (item->active ? 1 : 0);

    if (item->icon)
    {
        DrawIconEx(hdc, icon_x, icon_y, item->icon, icon_size, icon_size, 0, NULL, DI_NORMAL);
    }

    /* 3. Draw Window Title Text (Bold for active, Normal for inactive) */
    text_rc.left = icon_x + icon_size + 4;
    text_rc.top = item->rect.top + (item->active ? 1 : 0);
    text_rc.right = item->rect.right - 4;
    text_rc.bottom = item->rect.bottom + (item->active ? 1 : 0);

    old_font = SelectObject(hdc, item->active ? font_bold : font_normal);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);

    DrawTextW(hdc, item->title, -1, &text_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(hdc, old_font);
}

static void show_task_context_menu(HWND hwnd, TASK_ITEM *item, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    BOOL iconic = IsIconic(item->target_hwnd);
    BOOL zoomed = IsZoomed(item->target_hwnd);
    DWORD style = GetWindowLongW(item->target_hwnd, GWL_STYLE);
    UINT cmd;

    AppendMenuW(hMenu, (iconic || zoomed) ? MF_STRING : (MF_STRING | MF_GRAYED),
                IDM_TASK_RESTORE, L"&Restore");
    AppendMenuW(hMenu, (!iconic) ? MF_STRING : (MF_STRING | MF_GRAYED),
                IDM_TASK_MOVE, L"&Move");
    AppendMenuW(hMenu, (!iconic && !zoomed && (style & WS_THICKFRAME)) ? MF_STRING : (MF_STRING | MF_GRAYED),
                IDM_TASK_SIZE, L"&Size");
    AppendMenuW(hMenu, (!iconic) ? MF_STRING : (MF_STRING | MF_GRAYED),
                IDM_TASK_MINIMIZE, L"Mi&nimize");
    AppendMenuW(hMenu, (!zoomed && (style & WS_MAXIMIZEBOX)) ? MF_STRING : (MF_STRING | MF_GRAYED),
                IDM_TASK_MAXIMIZE, L"Ma&ximize");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_TASK_CLOSE, L"&Close\tAlt+F4");

    SetForegroundWindow(g_taskband.parent_tray ? g_taskband.parent_tray : hwnd);
    cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                         pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (cmd)
    {
    case IDM_TASK_RESTORE:
        ShowWindow(item->target_hwnd, SW_RESTORE);
        SetForegroundWindow(item->target_hwnd);
        break;
    case IDM_TASK_MOVE:
        PostMessageW(item->target_hwnd, WM_SYSCOMMAND, SC_MOVE, 0);
        break;
    case IDM_TASK_SIZE:
        PostMessageW(item->target_hwnd, WM_SYSCOMMAND, SC_SIZE, 0);
        break;
    case IDM_TASK_MINIMIZE:
        ShowWindow(item->target_hwnd, SW_MINIMIZE);
        break;
    case IDM_TASK_MAXIMIZE:
        ShowWindow(item->target_hwnd, SW_MAXIMIZE);
        SetForegroundWindow(item->target_hwnd);
        break;
    case IDM_TASK_CLOSE:
        if (item->target_hwnd != g_taskband.parent_tray &&
            item->target_hwnd != g_taskband.hwnd &&
            item->target_hwnd != GetDesktopWindow() &&
            item->target_hwnd != (g_taskband.parent_tray ? GetAncestor(g_taskband.parent_tray, GA_ROOT) : NULL))
        {
            PostMessageW(item->target_hwnd, WM_CLOSE, 0, 0);
        }
        break;
    }
    taskband_sync_windows();
}

static LRESULT CALLBACK taskband_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg && msg == g_taskband.shell_hook_msg)
        return taskband_handle_shell_hook(wparam, lparam);

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC real = BeginPaint(hwnd, &ps), hdc;
        HPAINTBUFFER buffer = BeginBufferedPaint(real, &ps.rcPaint, BPBF_COMPATIBLEBITMAP, NULL, &hdc);
        HFONT font_normal, font_bold;
        LOGFONTW lf;
        TASK_ITEM *curr;

        if (!buffer) hdc = real;
        DrawThemeParentBackground(hwnd, hdc, NULL);   /* the taskbar under the buttons */

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -11;
        lf.lfWeight = FW_NORMAL;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        font_normal = CreateFontIndirectW(&lf);

        lf.lfWeight = FW_BOLD;
        font_bold = CreateFontIndirectW(&lf);

        curr = g_taskband.tasks;
        while (curr)
        {
            RECT intersect;
            if (IntersectRect(&intersect, &ps.rcPaint, &curr->rect))
                draw_task_button(hdc, curr, font_normal, font_bold);
            curr = curr->next;
        }

        DeleteObject(font_normal);
        DeleteObject(font_bold);
        if (buffer) EndBufferedPaint(buffer, TRUE);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        TASK_ITEM *curr = g_taskband.tasks;
        BOOL changed = FALSE;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };

        TrackMouseEvent(&tme);
        while (curr)
        {
            BOOL hot = PtInRect(&curr->rect, pt);
            if (hot != curr->hot) { curr->hot = hot; changed = TRUE; }
            curr = curr->next;
        }
        if (changed) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        TASK_ITEM *curr = g_taskband.tasks;
        while (curr) { curr->hot = curr->pressed = FALSE; curr = curr->next; }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        TASK_ITEM *curr = g_taskband.tasks;

        while (curr)
        {
            curr->pressed = PtInRect(&curr->rect, pt);
            curr = curr->next;
        }
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        TASK_ITEM *curr = g_taskband.tasks;

        if (GetCapture() == hwnd) ReleaseCapture();
        while (curr)
        {
            BOOL was_pressed = curr->pressed;
            curr->pressed = FALSE;
            if (was_pressed && PtInRect(&curr->rect, pt))
            {
                /* the button acts when it is released, like on Windows */
                HWND fg = GetAncestor(GetForegroundWindow(), GA_ROOTOWNER);
                if ((curr->target_hwnd == fg || curr->active) && !IsIconic(curr->target_hwnd))
                    ShowWindow(curr->target_hwnd, SW_MINIMIZE);
                else
                {
                    if (IsIconic(curr->target_hwnd)) ShowWindow(curr->target_hwnd, SW_RESTORE);
                    else ShowWindow(curr->target_hwnd, SW_SHOWNORMAL);
                    SetForegroundWindow(curr->target_hwnd);
                }
                taskband_sync_windows();
                break;
            }
            curr = curr->next;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_RBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        TASK_ITEM *curr = g_taskband.tasks;
        POINT screen_pt = pt;
        ClientToScreen(hwnd, &screen_pt);

        while (curr)
        {
            if (PtInRect(&curr->rect, pt))
            {
                POINT menu_pt = { curr->rect.left, curr->rect.top };
                ClientToScreen(hwnd, &menu_pt);
                show_task_context_menu(hwnd, curr, menu_pt);
                return 0;
            }
            curr = curr->next;
        }

        /* Forward to taskbar context menu */
        SendMessageW(GetParent(hwnd), WM_CONTEXTMENU, (WPARAM)GetParent(hwnd),
                     MAKELPARAM(screen_pt.x, screen_pt.y));
        return 0;
    }

    case WM_TIMER:
        if (wparam == 1)
        {
            taskband_sync_windows();
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;

    case WM_NCHITTEST:
    {
        /* Windows Classic: the top edge of an unlocked taskbar belongs to the taskbar, which changes its height from there */
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < 3 && !taskbar_settings_get()->lock_taskbar && !theme_is_visual_styles_active()) return HTTRANSPARENT;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return 0;
}

LRESULT taskband_handle_shell_hook(WPARAM wparam, LPARAM lparam)
{
    HWND hwnd = (HWND)lparam;

    switch (wparam)
    {
    case HSHELL_WINDOWCREATED:
        taskband_add_window(hwnd);
        break;

    case HSHELL_WINDOWDESTROYED:
        taskband_remove_window(hwnd);
        break;

    case HSHELL_WINDOWACTIVATED:
    case HSHELL_RUDEAPPACTIVATED:
        if (hwnd && !find_task_by_hwnd(hwnd)) taskband_add_window(hwnd);
        taskband_activate_window(hwnd);
        break;

    case HSHELL_REDRAW:
        taskband_update_window(hwnd);
        break;

    case HSHELL_FLASH:
        taskband_flash_window(hwnd);
        break;
    }

    return 0;
}

HWND taskband_create(HWND parent_tray, int x, int y, int width, int height)
{
    WNDCLASSW wc;

    if (g_taskband.hwnd)
    {
        taskband_resize(x, y, width, height);
        return g_taskband.hwnd;
    }

    memset(&g_taskband, 0, sizeof(g_taskband));
    g_taskband.parent_tray = parent_tray;
    g_taskband.max_width = width;
    g_taskband.button_height = height;
    g_taskband.button_width = MAX_TASK_BUTTON_WIDTH;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = taskband_proc;
    wc.style = CS_HREDRAW | CS_VREDRAW;   /* everything is laid out from the size: repaint all of it */
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = TASKBAND_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);

    g_taskband.hwnd = CreateWindowExW(0, TASKBAND_CLASS, NULL,
                                      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      x, y, width, height,
                                      parent_tray, NULL, wc.hInstance, NULL);

    /* Top-level window notifications arrive as the "SHELLHOOK" message; the slow timer is only a safety net
     * for windows whose events we could not see (e.g. created before we registered). */
    g_taskband.shell_hook_msg = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(g_taskband.hwnd);

    SetTimer(g_taskband.hwnd, 1, 2000, NULL);
    taskband_sync_windows();

    return g_taskband.hwnd;
}

void taskband_resize(int x, int y, int width, int height)
{
    g_taskband.max_width = width;
    g_taskband.button_height = height;
    if (g_taskband.hwnd)
    {
        SetWindowPos(g_taskband.hwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        taskband_relayout();
    }
}

void taskband_destroy(void)
{
    TASK_ITEM *curr = g_taskband.tasks;
    while (curr)
    {
        TASK_ITEM *next = curr->next;
        free(curr);
        curr = next;
    }
    g_taskband.tasks = NULL;
    g_taskband.task_count = 0;

    if (g_taskband.hwnd)
    {
        KillTimer(g_taskband.hwnd, 1);
        DeregisterShellHookWindow(g_taskband.hwnd);
        DestroyWindow(g_taskband.hwnd);
        g_taskband.hwnd = NULL;
    }
}
