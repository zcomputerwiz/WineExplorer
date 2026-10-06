/*
 * WineExplorer Desktop Environment - TaskBand Implementation
 *
 * Implements running application window tracking, taskbar buttons, active/flashing states,
 * window activation/minimization, grouping, and authentic system menus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "wine/debug.h"
#include "theme_engine.h"
#include "taskband.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

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

    /* Ignore our own explorer process windows EXCEPT cabinet/explore folder windows */
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == GetCurrentProcessId())
    {
        WCHAR cls[64];
        if (GetClassNameW(hwnd, cls, ARRAY_SIZE(cls)))
        {
            if (wcscmp(cls, L"CabinetWClass") != 0 && wcscmp(cls, L"ExploreWClass") != 0)
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

static HICON get_window_task_icon(HWND hwnd)
{
    HICON icon = NULL;
    DWORD_PTR res;

    /* 1. Try WM_GETICON with ICON_SMALL2 */
    if (SendMessageTimeoutW(hwnd, WM_GETICON, 2 /* ICON_SMALL2 */, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &res) && res)
        icon = (HICON)res;

    /* 2. Try WM_GETICON with ICON_SMALL */
    if (!icon && SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL, 0,
                                     SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &res) && res)
        icon = (HICON)res;

    /* 3. Try class small icon */
    if (!icon)
        icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);

    /* 4. Try WM_GETICON with ICON_BIG */
    if (!icon && SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0,
                                     SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &res) && res)
        icon = (HICON)res;

    /* 5. Fallback to default application icon */
    if (!icon)
        icon = LoadIconW(NULL, (const WCHAR *)IDI_APPLICATION);

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
    int x = 0;
    TASK_ITEM *curr;

    if (count <= 0)
    {
        if (g_taskband.hwnd) InvalidateRect(g_taskband.hwnd, NULL, TRUE);
        return;
    }

    if (count * button_width > available_width)
        button_width = max(MIN_TASK_BUTTON_WIDTH, available_width / count);

    g_taskband.button_width = button_width;

    curr = g_taskband.tasks;
    while (curr)
    {
        curr->rect.left = x;
        curr->rect.top = 2;
        curr->rect.right = x + button_width - 2;
        curr->rect.bottom = g_taskband.button_height - 2;

        x += button_width;
        curr = curr->next;
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

static void draw_task_button(HDC hdc, TASK_ITEM *item, HFONT font)
{
    RECT rc = item->rect;
    HFONT old_font;
    int icon_size = 16;
    int icon_y = rc.top + (rc.bottom - rc.top - icon_size) / 2;
    int icon_x = rc.left + 4;
    RECT text_rc;

    /* 1. Draw Button Edge & Background */
    if (theme_is_visual_styles_active())
    {
        /* Themed button drawing */
        HBRUSH face = GetSysColorBrush(item->active ? COLOR_BTNHIGHLIGHT : COLOR_BTNFACE);
        FillRect(hdc, &rc, face);
        theme_draw_classic_bevel(hdc, &rc, item->active ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT | BF_SOFT | BF_ADJUST);
    }
    else
    {
        /* Authentic Server 2003 / Classic NT 3D button rendering */
        if (item->flashing)
        {
            HBRUSH flash_brush = CreateSolidBrush(RGB(255, 170, 0)); /* Highlight flash color */
            FillRect(hdc, &rc, flash_brush);
            DeleteObject(flash_brush);
            theme_draw_classic_bevel(hdc, &rc, EDGE_RAISED, BF_RECT | BF_SOFT | BF_ADJUST);
        }
        else if (item->active)
        {
            /* Active button has sunken bevel with checkered/pressed face */
            HBRUSH btn_face = GetSysColorBrush(COLOR_BTNFACE);
            FillRect(hdc, &rc, btn_face);
            theme_draw_classic_bevel(hdc, &rc, EDGE_SUNKEN, BF_RECT | BF_SOFT | BF_ADJUST);
        }
        else
        {
            /* Inactive button has raised 3D bevel */
            HBRUSH btn_face = GetSysColorBrush(COLOR_BTNFACE);
            FillRect(hdc, &rc, btn_face);
            theme_draw_classic_bevel(hdc, &rc, EDGE_RAISED, BF_RECT | BF_SOFT | BF_ADJUST);
        }
    }

    /* 2. Draw 16x16 window icon */
    if (item->icon)
    {
        DrawIconEx(hdc, icon_x, icon_y, item->icon, icon_size, icon_size, 0, NULL, DI_NORMAL);
    }

    /* 3. Draw Window Title Text */
    text_rc.left = icon_x + icon_size + 4;
    text_rc.top = rc.top;
    text_rc.right = rc.right - 4;
    text_rc.bottom = rc.bottom;

    old_font = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));

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
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client_rc;
        HFONT font;
        LOGFONTW lf;
        TASK_ITEM *curr;

        GetClientRect(hwnd, &client_rc);
        FillRect(hdc, &client_rc, GetSysColorBrush(COLOR_BTNFACE));

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -11;
        lf.lfWeight = FW_NORMAL;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        font = CreateFontIndirectW(&lf);

        curr = g_taskband.tasks;
        while (curr)
        {
            RECT intersect;
            if (IntersectRect(&intersect, &ps.rcPaint, &curr->rect))
                draw_task_button(hdc, curr, font);
            curr = curr->next;
        }

        DeleteObject(font);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        TASK_ITEM *curr = g_taskband.tasks;

        while (curr)
        {
            if (PtInRect(&curr->rect, pt))
            {
                HWND fg = GetAncestor(GetForegroundWindow(), GA_ROOTOWNER);
                if ((curr->target_hwnd == fg || curr->active) && !IsIconic(curr->target_hwnd))
                {
                    /* Minimize when active button is clicked */
                    ShowWindow(curr->target_hwnd, SW_MINIMIZE);
                }
                else
                {
                    /* Restore / Bring to front */
                    if (IsIconic(curr->target_hwnd))
                        ShowWindow(curr->target_hwnd, SW_RESTORE);
                    else
                        ShowWindow(curr->target_hwnd, SW_SHOWNORMAL);
                    SetForegroundWindow(curr->target_hwnd);
                }
                taskband_sync_windows();
                break;
            }
            curr = curr->next;
        }
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

    switch (wparam & 0x7fff)
    {
    case HSHELL_WINDOWCREATED:
        taskband_add_window(hwnd);
        break;

    case HSHELL_WINDOWDESTROYED:
        taskband_remove_window(hwnd);
        break;

    case HSHELL_WINDOWACTIVATED:
    case HSHELL_RUDEAPPACTIVATED:
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
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = TASKBAND_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);

    g_taskband.hwnd = CreateWindowExW(0, TASKBAND_CLASS, NULL,
                                      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      x, y, width, height,
                                      parent_tray, NULL, wc.hInstance, NULL);

    /* Register as shell hook window to receive top-level window notifications */
    RegisterShellHookWindow(g_taskband.hwnd);
    g_taskband.shell_hook_msg = RegisterWindowMessageW(L"SHELLHOOK");

    SetTimer(g_taskband.hwnd, 1, 200, NULL);
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
