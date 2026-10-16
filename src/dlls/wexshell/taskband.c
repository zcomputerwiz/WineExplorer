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
#include <shlwapi.h>

#include "wextheme.h"
#include "taskband.h"
#include "taskbar_props.h"
#include "linux_windows.h"
#include "module.h"


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
            res)
            icon = CopyIcon((HICON)res);
    if (!icon && (res = GetClassLongPtrW(hwnd, GCLP_HICONSM))) icon = CopyIcon((HICON)res);
    /* Wine icon handles are process-local. Use the actual window's published
     * pixels before falling back to its executable, which loses folder icons. */
    if (!icon) icon = linux_windows_copy_icon(hwnd, 16);
    if (!icon && (res = (DWORD_PTR)get_process_icon(hwnd))) icon = CopyIcon((HICON)res);
    if (!icon) icon = CopyIcon(LoadIconW(NULL, (const WCHAR *)IDI_APPLICATION));
    return icon;
}

/* Windows XP groups the buttons of one program: its path is the key, its description the group's name */
static void fill_group_info(TASK_ITEM *item)
{
    struct { WORD lang, code; } *translation;
    WCHAR cls[64], query[64], description_path[MAX_PATH], *value;
    const WCHAR *description_file;
    DWORD len = MAX_PATH, size;
    HANDLE process;
    void *data;
    UINT n;

    item->group_key[0] = item->group_name[0] = 0;
    /* the hosts of Linux programs' windows all belong to Explorer */
    if (GetClassNameW(item->target_hwnd, cls, ARRAY_SIZE(cls)) && !wcscmp(cls, L"WineExplorerLinuxWindow")) return;
    if (!(process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item->process_id))) return;
    if (!QueryFullProcessImageNameW(process, 0, item->group_key, &len)) item->group_key[0] = 0;
    CloseHandle(process);
    if (!item->group_key[0]) return;

    lstrcpynW(item->group_name, PathFindFileNameW(item->group_key), ARRAY_SIZE(item->group_name));
    PathRemoveExtensionW(item->group_name);
    description_file = item->group_key;
    /* The shell's version resources moved into the plugin; the executable is stock Wine. */
    if (!lstrcmpiW(PathFindFileNameW(item->group_key), L"explorer.exe") &&
        GetModuleFileNameW(explorer_hInstance, description_path, ARRAY_SIZE(description_path)))
        description_file = description_path;
    if (!(size = GetFileVersionInfoSizeW(description_file, NULL)) || !(data = malloc(size))) return;
    if (GetFileVersionInfoW(description_file, 0, size, data) &&
        VerQueryValueW(data, L"\\VarFileInfo\\Translation", (void **)&translation, &n) && n >= sizeof(*translation))
    {
        swprintf(query, ARRAY_SIZE(query), L"\\StringFileInfo\\%04x%04x\\FileDescription", translation->lang, translation->code);
        if (VerQueryValueW(data, query, (void **)&value, &n) && n > 1)
            lstrcpynW(item->group_name, value, ARRAY_SIZE(item->group_name));
    }
    free(data);
}

/* the rectangle of the button that shows the window: its own or its group's */
static RECT *button_rect(TASK_ITEM *item)
{
    return item->group ? &item->group->rect : &item->rect;
}

/* Group the buttons of the program with the most windows, then the next, until the buttons fit at full width
 * (Windows XP does it only when the taskbar is crowded).  Returns the number of buttons. */
static int group_buttons(int capacity)
{
    TASK_ITEM *a, *b, *best;
    int buttons = g_taskband.task_count, count, best_count;

    for (a = g_taskband.tasks; a; a = a->next) { a->group = NULL; a->group_count = 0; }
    if (!taskbar_settings_get()->group_buttons) return buttons;

    while (buttons > capacity)
    {
        best = NULL;
        best_count = 1;
        for (a = g_taskband.tasks; a; a = a->next)
        {
            if (a->group || !a->group_key[0]) continue;
            for (count = 0, b = a; b; b = b->next)
                if (!b->group && !wcsicmp(a->group_key, b->group_key)) count++;
            if (count > best_count) { best = a; best_count = count; }
        }
        if (!best) break;
        for (b = best; b; b = b->next)
            if (!b->group && !wcsicmp(best->group_key, b->group_key)) { b->group = best; best->group_count++; }
        buttons -= best_count - 1;
    }
    return buttons;
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
    int count;
    int available_width = g_taskband.max_width;
    int button_width = MAX_TASK_BUTTON_WIDTH;
    BOOL themed = theme_is_visual_styles_active();
    int first = theme_taskbar_first_row(), pitch = theme_taskbar_row_pitch();
    int rows = max(1, (g_taskband.button_height - first) / pitch + 1);
    int per_row = max(1, available_width / MAX_TASK_BUTTON_WIDTH), i = 0;
    TASK_ITEM *curr;

    count = group_buttons(rows * per_row);
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

    for (curr = g_taskband.tasks; curr; curr = curr->next)
    {
        int row = i / per_row, col = i % per_row;

        if (curr->group && curr->group != curr)
        {
            SetRectEmpty(&curr->rect);   /* shown by its group's button */
            continue;
        }
        i++;

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
    fill_group_info(item);
    GetWindowTextW(hwnd, item->title, sizeof(item->title)/sizeof(WCHAR));
    item->icon = get_window_task_icon(hwnd);
    item->minimized = IsIconic(hwnd);
    item->active = (hwnd == GetAncestor(GetForegroundWindow(), GA_ROOTOWNER));
    item->seen = TRUE;
    item->icon_dirty = TRUE;
    SetTimer(g_taskband.hwnd, 2, 100, NULL);
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
            if (g_taskband.hwnd) InvalidateRect(g_taskband.hwnd, button_rect(item), FALSE);
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
            if (to_free->icon) DestroyIcon(to_free->icon);
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
            if (to_free->icon) DestroyIcon(to_free->icon);
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
    if (item->icon) DestroyIcon(item->icon);
    item->icon = get_window_task_icon(hwnd);
    /* HSHELL_REDRAW can arrive before the other process's X11 property update.
     * Refresh once after the notification has settled, rather than caching the
     * previous folder's pixels until another shell event happens. */
    item->icon_dirty = TRUE;
    SetTimer(g_taskband.hwnd, 2, 100, NULL);
    item->minimized = IsIconic(hwnd);

    if (g_taskband.hwnd)
        InvalidateRect(g_taskband.hwnd, button_rect(item), FALSE);
}

void taskband_flash_window(HWND hwnd)
{
    TASK_ITEM *item = find_task_by_hwnd(hwnd);
    if (item && !item->active)
    {
        item->flashing = !item->flashing;
        if (g_taskband.hwnd)
            InvalidateRect(g_taskband.hwnd, button_rect(item), FALSE);
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
    BOOL group = item->group == item && item->group_count > 1, active = item->active, flashing = item->flashing;
    WCHAR text[300];
    TASK_ITEM *member;
    HTHEME theme;

    /* a group button is pressed while one of its windows is active */
    if (group)
        for (member = g_taskband.tasks; member; member = member->next)
            if (member->group == item) { active |= member->active; flashing |= member->flashing && !member->active; }

    /* 1. Draw Button Edge & Background */
    theme = theme_open(NULL, flashing ? L"TaskBand" : L"TaskBand::Toolbar");
    if (theme)
    {
        /* TaskBand::Toolbar button states: 1 normal, 5 checked (the active window); TaskBand flash button when flashing */
        /* button images: 1 normal, 2 hot, 3 pressed, 5 checked (the active window), 6 hot and checked */
        int state = item->pressed ? 3 : (active ? (item->hot ? 6 : 5) : (item->hot ? 2 : 1));
        DrawThemeBackground(theme, hdc, flashing ? 2 : 1, state, &rc, NULL);
        GetThemeColor(theme, flashing ? 2 : 1, state, TMT_TEXTCOLOR, &text_color);
        CloseThemeData(theme);
    }
    else
    {
        /* Authentic Windows Classic 3D button rendering */
        if (flashing)
        {
            HBRUSH flash_brush = CreateSolidBrush(RGB(255, 170, 0)); /* Highlight flash color */
            FillRect(hdc, &rc, flash_brush);
            DeleteObject(flash_brush);
            theme_draw_classic_bevel(hdc, &rc, EDGE_RAISED, BF_RECT | BF_ADJUST);
        }
        else if (active)
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
    icon_x = item->rect.left + 4 + (active ? 1 : 0);
    icon_y = item->rect.top + (item->rect.bottom - item->rect.top - icon_size) / 2 + (active ? 1 : 0);

    if (item->icon)
    {
        DrawIconEx(hdc, icon_x, icon_y, item->icon, icon_size, icon_size, 0, NULL, DI_NORMAL);
    }

    /* 3. Draw Window Title Text (Bold for active, Normal for inactive) */
    text_rc.left = icon_x + icon_size + 4;
    text_rc.top = item->rect.top + (active ? 1 : 0);
    text_rc.right = item->rect.right - (group ? 16 : 4);
    text_rc.bottom = item->rect.bottom + (active ? 1 : 0);

    old_font = SelectObject(hdc, active ? font_bold : font_normal);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);

    if (group) swprintf(text, ARRAY_SIZE(text), L"%d %s", item->group_count, item->group_name);
    else lstrcpynW(text, item->title, ARRAY_SIZE(text));
    DrawTextW(hdc, text, -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (group)
    {
        /* the arrow of the group's window list */
        int x = item->rect.right - 12 + (active ? 1 : 0), y = (item->rect.top + item->rect.bottom) / 2 - 1 + (active ? 1 : 0);
        POINT arrow[3] = { { x, y }, { x + 6, y }, { x + 3, y + 3 } };
        HBRUSH brush = CreateSolidBrush(text_color), old_brush = SelectObject(hdc, brush);
        HPEN pen = CreatePen(PS_SOLID, 1, text_color), old_pen = SelectObject(hdc, pen);

        Polygon(hdc, arrow, 3);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(brush);
        DeleteObject(pen);
    }

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
        SetForegroundWindow(item->target_hwnd);
        PostMessageW(item->target_hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        break;
    case IDM_TASK_MOVE:
        SetForegroundWindow(item->target_hwnd);
        PostMessageW(item->target_hwnd, WM_SYSCOMMAND, SC_MOVE, 0);
        break;
    case IDM_TASK_SIZE:
        SetForegroundWindow(item->target_hwnd);
        PostMessageW(item->target_hwnd, WM_SYSCOMMAND, SC_SIZE, 0);
        break;
    case IDM_TASK_MINIMIZE:
        ShowWindow(item->target_hwnd, SW_MINIMIZE);
        break;
    case IDM_TASK_MAXIMIZE:
        SetForegroundWindow(item->target_hwnd);
        PostMessageW(item->target_hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
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

/* a click on a window's button: minimize it when it is the active one, else bring it up */
static void click_task(TASK_ITEM *item)
{
    HWND fg = GetAncestor(GetForegroundWindow(), GA_ROOTOWNER);

    if ((item->target_hwnd == fg || item->active) && !IsIconic(item->target_hwnd))
        ShowWindow(item->target_hwnd, SW_MINIMIZE);
    else
    {
        HWND target = GetLastActivePopup(item->target_hwnd);
        if (!IsWindowVisible(target)) target = item->target_hwnd;
        if (IsIconic(item->target_hwnd)) ShowWindow(item->target_hwnd, SW_RESTORE);
        /* Preserve maximized placement and the active owned dialog. Showing
         * an already visible parent normally activates it first, hiding real
         * QTerminal's Exit confirmation behind its own main window. */
        SetForegroundWindow(target);
    }
}

/* a menu picture of a window's icon */
static HBITMAP icon_bitmap(HICON icon)
{
    BITMAPINFO info = { { sizeof(BITMAPINFOHEADER), 22, -16, 1, 32, BI_RGB } };   /* margins around the icon */
    HDC hdc = CreateCompatibleDC(NULL);
    void *bits;
    HBITMAP bitmap = CreateDIBSection(hdc, &info, DIB_RGB_COLORS, &bits, NULL, 0), old;

    if (bitmap)
    {
        RECT rc = { 0, 0, 22, 16 };

        old = SelectObject(hdc, bitmap);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_MENU));   /* menus draw the bitmap without its alpha */
        DrawIconEx(hdc, 2, 0, icon, 16, 16, 0, NULL, DI_NORMAL);
        SelectObject(hdc, old);
    }
    DeleteDC(hdc);
    return bitmap;
}

/* the list of a group's windows, under or above its button */
static void show_group_list(HWND hwnd, TASK_ITEM *group)
{
    HMENU menu = CreatePopupMenu();
    HBITMAP bitmaps[64];
    TASK_ITEM *member, *members[64];
    int n = 0, i;
    POINT pt = { group->rect.left, group->rect.top };
    UINT cmd;

    for (member = g_taskband.tasks; member && n < ARRAY_SIZE(members); member = member->next)
    {
        MENUITEMINFOW mi = { sizeof(mi), MIIM_ID | MIIM_STRING | MIIM_BITMAP };

        if (member->group != group) continue;
        bitmaps[n] = member->icon ? icon_bitmap(member->icon) : NULL;
        mi.wID = n + 1;
        mi.dwTypeData = member->title;
        mi.hbmpItem = bitmaps[n];
        InsertMenuItemW(menu, n, TRUE, &mi);
        members[n++] = member;
    }
    ClientToScreen(hwnd, &pt);
    SetForegroundWindow(g_taskband.parent_tray ? g_taskband.parent_tray : hwnd);
    cmd = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    for (i = 0; i < n; i++) if (bitmaps[i]) DeleteObject(bitmaps[i]);
    if (cmd && cmd <= n)
    {
        member = members[cmd - 1];
        if (IsIconic(member->target_hwnd)) ShowWindow(member->target_hwnd, SW_RESTORE);
        SetForegroundWindow(member->target_hwnd);
    }
}

#define IDM_GROUP_CASCADE    10
#define IDM_GROUP_TILE_H     11
#define IDM_GROUP_TILE_V     12
#define IDM_GROUP_MINIMIZE   13
#define IDM_GROUP_CLOSE      14

/* the menu of a group's button: arrange, minimize or close all its windows */
static void show_group_menu(HWND hwnd, TASK_ITEM *group, POINT pt)
{
    HMENU menu = CreatePopupMenu();
    HWND windows[64];
    TASK_ITEM *member;
    UINT cmd, n = 0, i;

    for (member = g_taskband.tasks; member && n < ARRAY_SIZE(windows); member = member->next)
        if (member->group == group) windows[n++] = member->target_hwnd;

    AppendMenuW(menu, MF_STRING, IDM_GROUP_CASCADE, L"&Cascade");
    AppendMenuW(menu, MF_STRING, IDM_GROUP_TILE_H, L"Tile &Horizontally");
    AppendMenuW(menu, MF_STRING, IDM_GROUP_TILE_V, L"Tile &Vertically");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_GROUP_MINIMIZE, L"Mi&nimize Group");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_GROUP_CLOSE, L"&Close Group");

    SetForegroundWindow(g_taskband.parent_tray ? g_taskband.parent_tray : hwnd);
    cmd = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);

    switch (cmd)
    {
    case IDM_GROUP_CASCADE:
        for (i = 0; i < n; i++) if (IsIconic(windows[i])) ShowWindow(windows[i], SW_RESTORE);
        CascadeWindows(NULL, 0, NULL, n, windows);
        break;
    case IDM_GROUP_TILE_H:
    case IDM_GROUP_TILE_V:
        for (i = 0; i < n; i++) if (IsIconic(windows[i])) ShowWindow(windows[i], SW_RESTORE);
        TileWindows(NULL, cmd == IDM_GROUP_TILE_H ? MDITILE_HORIZONTAL : MDITILE_VERTICAL, NULL, n, windows);
        break;
    case IDM_GROUP_MINIMIZE:
        for (i = 0; i < n; i++) ShowWindow(windows[i], SW_MINIMIZE);
        break;
    case IDM_GROUP_CLOSE:
        for (i = 0; i < n; i++) PostMessageW(windows[i], WM_CLOSE, 0, 0);
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
                /* the button acts when it is released, like on Windows; a group's button lists its windows */
                if (curr->group == curr && curr->group_count > 1) show_group_list(hwnd, curr);
                else click_task(curr);
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
                if (curr->group == curr && curr->group_count > 1) show_group_menu(hwnd, curr, menu_pt);
                else show_task_context_menu(hwnd, curr, menu_pt);
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
        if (wparam == 2)
        {
            TASK_ITEM *item;
            KillTimer(hwnd, 2);
            for (item = g_taskband.tasks; item; item = item->next)
            {
                if (!item->icon_dirty) continue;
                item->icon_dirty = FALSE;
                if (item->icon) DestroyIcon(item->icon);
                item->icon = get_window_task_icon(item->target_hwnd);
                InvalidateRect(hwnd, button_rect(item), FALSE);
            }
            return 0;
        }
        if (wparam == 1)
        {
            taskband_sync_windows();
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
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
    wc.hInstance = explorer_hInstance;
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
        if (curr->icon) DestroyIcon(curr->icon);
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
