/*
 * WineExplorer Desktop Environment - instant search (the taskbar search box of later Windows)
 *
 * What the search box of Windows 10 and 11 does, in the colors of the shell: while you type, a window above the
 * taskbar lists the best match and then the programs (Start menu shortcuts), Control Panel items, folders and files
 * (of your own folders) whose names match, with the selected one described on the right.  Enter or a click opens it.
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
#include <stdlib.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "instant_search.h"
#include "search_engine.h"
#include "search_ui.h"
#include "wexres.h"

#include "module.h"

#define FLYOUT_CLASS    L"WineInstantSearch"
#define LIST_WIDTH      300
#define PREVIEW_WIDTH   230
#define FLYOUT_HEIGHT   440
#define HEADER_HEIGHT   24
#define ROW_HEIGHT      24
#define BEST_HEIGHT     48
#define MAX_RESULTS     48
#define MAX_PER_GROUP   6

enum kind { KIND_PROGRAM, KIND_SETTING, KIND_FOLDER, KIND_FILE, KIND_COMPANION, KIND_COUNT };
static const WCHAR *const group_names[KIND_COUNT] = { L"Programs", L"Control Panel", L"Folders", L"Files", NULL };

typedef struct
{
    enum kind kind;
    WCHAR name[MAX_PATH];
    WCHAR path[MAX_PATH];     /* programs, folders and files */
    LPITEMIDLIST pidl;        /* Control Panel items */
    int score;
    int icon;                 /* system image list */
} RESULT;

typedef struct { WCHAR name[MAX_PATH]; WCHAR path[MAX_PATH]; } PROGRAM;
typedef struct { WCHAR name[MAX_PATH]; LPITEMIDLIST pidl; } SETTING;
typedef struct { int result; RECT rc; const WCHAR *header; BOOL best; } ROW;

static struct
{
    HWND wnd, edit;
    WCHAR query[256];
    RESULT results[MAX_RESULTS];
    int count, best, selected;
    BOOL user_selected;          /* moved with the keys or the mouse: keep it while more results come */
    ROW rows[MAX_RESULTS + KIND_COUNT + 2];
    int row_count;
    RECT open_rc, location_rc;   /* the actions of the preview */
    SEARCH_JOB *job;
    UINT job_id;
    PROGRAM *programs;
    int program_count;
    SETTING *settings;
    int setting_count;
    BOOL loaded;
    HFONT font, bold, big;
    HIMAGELIST small_list, large_list;
} g;

/* ---- what there is to find ---- */

/* 3: the name starts with the text, 2: one of its words does, 1: it is in the name, 0: no */
static int match(const WCHAR *name, const WCHAR *query)
{
    int n = lstrlenW(query);
    const WCHAR *p;

    if (!n) return 0;
    if (!wcsnicmp(name, query, n)) return 3;
    for (p = name + 1; *p; p++)
        if (wcschr(L" -_.(", p[-1]) && !wcsnicmp(p, query, n)) return 2;
    return StrStrIW(name, query) ? 1 : 0;
}

static void add_programs(const WCHAR *dir, int depth)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;

    swprintf(pattern, MAX_PATH, L"%s\\*", dir);
    if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) return;
    do
    {
        WCHAR *ext;
        int i;

        if (fd.cFileName[0] == '.' || (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        swprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (depth < 4) add_programs(path, depth + 1);
            continue;
        }
        if (!(ext = PathFindExtensionW(fd.cFileName)) || (wcsicmp(ext, L".lnk") && wcsicmp(ext, L".url"))) continue;
        *ext = 0;
        for (i = 0; i < g.program_count; i++) if (!wcsicmp(g.programs[i].name, fd.cFileName)) break;
        if (i < g.program_count) continue;   /* the same shortcut for this user and for all */
        if (!(g.program_count % 64))
        {
            PROGRAM *more = realloc(g.programs, (g.program_count + 64) * sizeof(*more));
            if (!more) break;
            g.programs = more;
        }
        lstrcpynW(g.programs[g.program_count].name, fd.cFileName, MAX_PATH);
        lstrcpynW(g.programs[g.program_count].path, path, MAX_PATH);
        g.program_count++;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

/* The Control Panel items, through the shell namespace like Windows lists them */
static void add_settings(void)
{
    IShellFolder *desktop, *cpl;
    LPITEMIDLIST root, child;
    IEnumIDList *items;
    STRRET str;

    if (FAILED(SHGetDesktopFolder(&desktop))) return;
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_CONTROLS, &root)))
    {
        if (SUCCEEDED(IShellFolder_BindToObject(desktop, root, NULL, &IID_IShellFolder, (void **)&cpl)))
        {
            if (IShellFolder_EnumObjects(cpl, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &items) == S_OK)
            {
                while (IEnumIDList_Next(items, 1, &child, NULL) == S_OK)
                {
                    WCHAR *name;
                    if (!(g.setting_count % 32))
                    {
                        SETTING *more = realloc(g.settings, (g.setting_count + 32) * sizeof(*more));
                        if (!more) { ILFree(child); break; }
                        g.settings = more;
                    }
                    if (SUCCEEDED(IShellFolder_GetDisplayNameOf(cpl, child, SHGDN_NORMAL, &str)) &&
                        SUCCEEDED(StrRetToStrW(&str, child, &name)))
                    {
                        lstrcpynW(g.settings[g.setting_count].name, name, MAX_PATH);
                        g.settings[g.setting_count].pidl = ILCombine(root, child);
                        g.setting_count++;
                        CoTaskMemFree(name);
                    }
                    ILFree(child);
                }
                IEnumIDList_Release(items);
            }
            IShellFolder_Release(cpl);
        }
        ILFree(root);
    }
    IShellFolder_Release(desktop);
}

static void load_sources(void)
{
    static const int folders[] = { CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU };
    WCHAR dir[MAX_PATH];
    unsigned int i;

    for (i = 0; i < g.setting_count; i++) ILFree(g.settings[i].pidl);
    g.program_count = g.setting_count = 0;
    for (i = 0; i < ARRAY_SIZE(folders); i++)
        if (SUCCEEDED(SHGetFolderPathW(NULL, folders[i], NULL, SHGFP_TYPE_CURRENT, dir))) add_programs(dir, 0);
    add_settings();
    g.loaded = TRUE;
}

int WINAPI Shell_GetCachedImageIndexW(const WCHAR *path, int index, UINT flags);

static int system_icon(const WCHAR *path, LPCITEMIDLIST pidl)
{
    SHFILEINFOW sfi = {0};
    UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
    HIMAGELIST list;
    int index;

    /* programs: the icon of the program, without the shortcut arrow, like the search of later Windows */
    if (path && !wcsicmp(PathFindExtensionW(path), L".lnk") &&
        SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_ICONLOCATION) && sfi.szDisplayName[0] &&
        (index = Shell_GetCachedImageIndexW(sfi.szDisplayName, sfi.iIcon, 0)) >= 0)
        return index;

    if (pidl) list = (HIMAGELIST)SHGetFileInfoW((const WCHAR *)pidl, 0, &sfi, sizeof(sfi), flags | SHGFI_PIDL);
    else list = (HIMAGELIST)SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), flags);
    if (list) g.small_list = list;
    return list ? sfi.iIcon : -1;
}

static RESULT *add_result(enum kind kind, const WCHAR *name, int score)
{
    RESULT *r;
    if (g.count >= MAX_RESULTS) return NULL;
    r = &g.results[g.count++];
    memset(r, 0, sizeof(*r));
    r->kind = kind;
    r->score = score;
    lstrcpynW(r->name, name, MAX_PATH);
    return r;
}

/* the order of the list: by kind, the best matches of a kind first */
static int compare_results(const void *a, const void *b)
{
    const RESULT *x = a, *y = b;
    if (x->kind != y->kind) return x->kind - y->kind;
    if (x->score != y->score) return y->score - x->score;
    return lstrcmpiW(x->name, y->name);
}

/* ---- layout and drawing ---- */

static void layout(void)
{
    int kind, i, y = 6, shown;
    RECT rc;

    g.row_count = 0;
    g.best = -1;
    /* the best match: the highest score; programs before the rest when they are as good */
    for (i = 0; i < g.count; i++)
        if (g.results[i].kind != KIND_COMPANION && (g.best < 0 || g.results[i].score > g.results[g.best].score)) g.best = i;

    GetClientRect(g.wnd, &rc);
    if (g.best >= 0)
    {
        ROW *row = &g.rows[g.row_count++];
        memset(row, 0, sizeof(*row));
        row->result = -1;
        row->header = L"Best match";
        SetRect(&row->rc, 0, y, LIST_WIDTH, y + HEADER_HEIGHT);
        y += HEADER_HEIGHT;
        row = &g.rows[g.row_count++];
        memset(row, 0, sizeof(*row));
        row->result = g.best;
        row->best = TRUE;
        SetRect(&row->rc, 0, y, LIST_WIDTH, y + BEST_HEIGHT);
        y += BEST_HEIGHT + 4;
    }
    for (kind = 0; kind < KIND_COUNT; kind++)
    {
        shown = 0;
        for (i = 0; i < g.count; i++)
        {
            ROW *row;
            if (g.results[i].kind != kind || i == g.best || shown == MAX_PER_GROUP) continue;
            if (y + ROW_HEIGHT * 2 > rc.bottom - ROW_HEIGHT - 8 && kind != KIND_COMPANION) break;
            if (!shown++ && group_names[kind])
            {
                row = &g.rows[g.row_count++];
                memset(row, 0, sizeof(*row));
                row->result = -1;
                row->header = group_names[kind];
                SetRect(&row->rc, 0, y, LIST_WIDTH, y + HEADER_HEIGHT);
                y += HEADER_HEIGHT;
            }
            row = &g.rows[g.row_count++];
            memset(row, 0, sizeof(*row));
            row->result = i;
            if (kind == KIND_COMPANION) y = max(y + 4, rc.bottom - ROW_HEIGHT - 8);   /* at the foot of the list */
            SetRect(&row->rc, 0, y, LIST_WIDTH, y + ROW_HEIGHT);
            y += ROW_HEIGHT;
        }
    }
}

/* sort and lay out again after results came or went; the selection stays on the same item */
static void refresh_results(void)
{
    RESULT keep = {0};
    int i;

    if (g.user_selected && g.selected >= 0 && g.selected < g.count) keep = g.results[g.selected];
    qsort(g.results, g.count, sizeof(*g.results), compare_results);
    layout();
    g.selected = g.best >= 0 ? g.best : 0;
    if (g.user_selected)
        for (i = 0; i < g.count; i++)
            if (g.results[i].kind == keep.kind && !wcscmp(g.results[i].name, keep.name) && !wcscmp(g.results[i].path, keep.path))
                g.selected = i;
}

static const RESULT *selected_result(void)
{
    return g.selected >= 0 && g.selected < g.count ? &g.results[g.selected] : NULL;
}

static void kind_text(const RESULT *r, WCHAR *text, int len)
{
    SHFILEINFOW sfi = {0};

    switch (r->kind)
    {
    case KIND_PROGRAM: lstrcpynW(text, L"Program", len); break;
    case KIND_SETTING: lstrcpynW(text, L"Control Panel", len); break;
    case KIND_FOLDER: lstrcpynW(text, L"File Folder", len); break;
    case KIND_COMPANION: lstrcpynW(text, L"Search Companion", len); break;
    default:
        if (SHGetFileInfoW(r->path, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) && sfi.szTypeName[0])
            lstrcpynW(text, sfi.szTypeName, len);
        else lstrcpynW(text, L"File", len);
    }
}

static void draw_link(HDC hdc, RECT *rc, const WCHAR *text, BOOL enabled)
{
    HFONT old = SelectObject(hdc, g.font);
    SetTextColor(hdc, enabled ? GetSysColor(COLOR_HOTLIGHT) : GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(hdc, text, -1, rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(hdc, old);
}

static void paint(HDC hdc)
{
    RECT rc, part;
    const RESULT *sel = selected_result();
    HBRUSH brush;
    int i;

    GetClientRect(g.wnd, &rc);
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(hdc, TRANSPARENT);

    /* the list */
    for (i = 0; i < g.row_count; i++)
    {
        const ROW *row = &g.rows[i];
        const RESULT *r = row->result >= 0 ? &g.results[row->result] : NULL;
        RECT text = row->rc;

        if (row->header && !r)
        {
            HFONT old = SelectObject(hdc, g.bold);
            text.left += 12;
            SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
            DrawTextW(hdc, row->header, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(hdc, old);
            continue;
        }
        if (!r) continue;
        if (row->result == g.selected)
        {
            RECT hl = row->rc;
            InflateRect(&hl, -4, 0);
            FillRect(hdc, &hl, GetSysColorBrush(COLOR_HIGHLIGHT));
        }
        SetTextColor(hdc, GetSysColor(row->result == g.selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        if (row->best)
        {
            WCHAR kind[80];
            RECT line = text;
            HFONT old;
            if (g.large_list && r->icon >= 0) ImageList_Draw(g.large_list, r->icon, hdc, text.left + 12, text.top + 8, ILD_TRANSPARENT);
            line.left += 52;
            line.bottom = line.top + BEST_HEIGHT / 2 + 2;
            old = SelectObject(hdc, g.bold);
            DrawTextW(hdc, r->name, -1, &line, DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, g.font);
            kind_text(r, kind, ARRAY_SIZE(kind));
            line.top = line.bottom + 1;
            line.bottom = text.bottom;
            DrawTextW(hdc, kind, -1, &line, DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, old);
        }
        else
        {
            HFONT old = SelectObject(hdc, g.font);
            if (r->kind == KIND_COMPANION)
            {
                HICON icon = shellres_icon(23, 16);
                if (icon) { DrawIconEx(hdc, text.left + 12, text.top + 4, icon, 16, 16, 0, NULL, DI_NORMAL); DestroyIcon(icon); }
            }
            else if (g.small_list && r->icon >= 0) ImageList_Draw(g.small_list, r->icon, hdc, text.left + 12, text.top + 4, ILD_TRANSPARENT);
            text.left += 36;
            text.right -= 8;
            DrawTextW(hdc, r->name, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, old);
        }
    }
    if (!g.count || (g.count == 1 && g.results[0].kind == KIND_COMPANION))
    {
        RECT none = { 12, 12, LIST_WIDTH - 12, 60 };
        WCHAR text[300];
        HFONT old = SelectObject(hdc, g.font);
        swprintf(text, ARRAY_SIZE(text), L"No programs, Control Panel items or files match \"%s\".", g.query);
        SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(hdc, text, -1, &none, DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, old);
    }

    /* the preview: the selected item described, with what can be done with it */
    SetRect(&part, LIST_WIDTH, 0, rc.right, rc.bottom);
    brush = CreateSolidBrush(RGB(GetRValue(GetSysColor(COLOR_BTNFACE)) / 2 + 128, GetGValue(GetSysColor(COLOR_BTNFACE)) / 2 + 128,
                                 GetBValue(GetSysColor(COLOR_BTNFACE)) / 2 + 128));
    FillRect(hdc, &part, brush);
    DeleteObject(brush);
    SetRectEmpty(&g.open_rc);
    SetRectEmpty(&g.location_rc);
    if (sel)
    {
        WCHAR kind[80], where[MAX_PATH];
        RECT text = { LIST_WIDTH + 16, 76, rc.right - 16, 112 };
        HFONT old = SelectObject(hdc, g.big);
        HPEN pen, old_pen;

        if (sel->kind == KIND_COMPANION)
        {
            HICON icon = shellres_icon(23, 32);
            if (icon) { DrawIconEx(hdc, LIST_WIDTH + (PREVIEW_WIDTH - 32) / 2, 28, icon, 32, 32, 0, NULL, DI_NORMAL); DestroyIcon(icon); }
        }
        else if (g.large_list && sel->icon >= 0) ImageList_Draw(g.large_list, sel->icon, hdc, LIST_WIDTH + (PREVIEW_WIDTH - 32) / 2, 28, ILD_TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        DrawTextW(hdc, sel->name, -1, &text, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, g.font);
        kind_text(sel, kind, ARRAY_SIZE(kind));
        text.top = text.bottom + 2;
        text.bottom = text.top + 16;
        SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(hdc, kind, -1, &text, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (sel->path[0] && sel->kind != KIND_PROGRAM)
        {
            lstrcpynW(where, sel->path, MAX_PATH);
            PathRemoveFileSpecW(where);
            text.top = text.bottom + 2;
            text.bottom = text.top + 30;
            DrawTextW(hdc, where, -1, &text, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_PATH_ELLIPSIS);
        }
        /* the actions, under a line */
        pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
        old_pen = SelectObject(hdc, pen);
        MoveToEx(hdc, LIST_WIDTH + 16, 176, NULL);
        LineTo(hdc, rc.right - 16, 176);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
        SetRect(&g.open_rc, LIST_WIDTH + 24, 188, rc.right - 16, 208);
        draw_link(hdc, &g.open_rc, L"Open", TRUE);
        if (sel->path[0] && sel->kind != KIND_COMPANION)
        {
            SetRect(&g.location_rc, LIST_WIDTH + 24, 210, rc.right - 16, 230);
            draw_link(hdc, &g.location_rc, L"Open file location", TRUE);
        }
        SelectObject(hdc, old);
    }
    /* the frame */
    brush = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
    FrameRect(hdc, &rc, brush);
    DeleteObject(brush);
}

/* ---- acting ---- */

static void open_result(const RESULT *r, BOOL location)
{
    if (!r) return;
    if (location && r->path[0])
    {
        LPITEMIDLIST pidl = ILCreateFromPathW(r->path);
        if (pidl)
        {
            SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
            ILFree(pidl);
        }
    }
    else if (r->kind == KIND_COMPANION)
        ShellExecuteW(NULL, NULL, L"explorer.exe", FIND_FOLDER_PATH, NULL, SW_SHOWNORMAL);
    else if (r->pidl)
    {
        SHELLEXECUTEINFOW sei = { sizeof(sei), SEE_MASK_IDLIST };
        sei.lpIDList = r->pidl;
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
    }
    else ShellExecuteW(NULL, NULL, r->path, NULL, NULL, SW_SHOWNORMAL);

    /* done: the box empties, like on Windows */
    instant_search_hide();
    if (g.edit) SetWindowTextW(g.edit, L"");
}

static void select_row(int step)
{
    int i, at = -1;

    for (i = 0; i < g.row_count; i++) if (g.rows[i].result == g.selected && g.rows[i].result >= 0) { at = i; break; }
    for (i = at + step; i >= 0 && i < g.row_count; i += step)
        if (g.rows[i].result >= 0)
        {
            g.selected = g.rows[i].result;
            g.user_selected = TRUE;
            InvalidateRect(g.wnd, NULL, FALSE);
            return;
        }
}

static LRESULT CALLBACK flyout_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps), mem = CreateCompatibleDC(hdc);
        RECT rc;
        HBITMAP bmp, old;

        GetClientRect(hwnd, &rc);
        bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        old = SelectObject(mem, bmp);
        paint(mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;   /* the search box keeps the keyboard */
    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        int i;
        for (i = 0; i < g.row_count; i++)
            if (g.rows[i].result >= 0 && PtInRect(&g.rows[i].rc, pt) && g.selected != g.rows[i].result)
            {
                g.selected = g.rows[i].result;
                g.user_selected = TRUE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        return 0;
    }
    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        if (PtInRect(&g.open_rc, pt) || PtInRect(&g.location_rc, pt))
        {
            SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        int i;
        if (PtInRect(&g.open_rc, pt)) open_result(selected_result(), FALSE);
        else if (PtInRect(&g.location_rc, pt)) open_result(selected_result(), TRUE);
        else
            for (i = 0; i < g.row_count; i++)
                if (g.rows[i].result >= 0 && PtInRect(&g.rows[i].rc, pt))
                {
                    open_result(&g.results[g.rows[i].result], FALSE);
                    break;
                }
        return 0;
    }
    case WM_SEARCH_RESULT:
    {
        SEARCH_RESULT *found = (SEARCH_RESULT *)lparam;
        const WCHAR *name = PathFindFileNameW(found->path);
        BOOL folder = (found->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int i, same = 0;

        if (wparam == g.job_id)
        {
            for (i = 0; i < g.count; i++) if (g.results[i].kind == (folder ? KIND_FOLDER : KIND_FILE)) same++;
            if (same < MAX_PER_GROUP * 2)
            {
                RESULT *r = add_result(folder ? KIND_FOLDER : KIND_FILE, name, match(name, g.query));
                if (r)
                {
                    lstrcpynW(r->path, found->path, MAX_PATH);
                    r->icon = system_icon(r->path, NULL);
                    refresh_results();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        free(found);
        return 0;
    }
    case WM_SEARCH_FOLDER:
        free((void *)lparam);
        return 0;
    case WM_SEARCH_DONE:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void create_window(void)
{
    WNDCLASSW wc = {0};
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SHFILEINFOW sfi;

    wc.lpfnWndProc = flyout_proc;
    wc.hInstance = wexsearch_module();
    wc.lpszClassName = FLYOUT_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);
    g.wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, FLYOUT_CLASS, NULL, WS_POPUP,
                            0, 0, LIST_WIDTH + PREVIEW_WIDTH, FLYOUT_HEIGHT, NULL, NULL, wexsearch_module(), NULL);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    g.bold = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
    g.big = CreateFontIndirectW(&ncm.lfMessageFont);
    g.large_list = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_LARGEICON);
    g.small_list = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
}

/* above the search box, on the screen */
static void place(HWND edit)
{
    RECT box, work;
    int x, y, w = LIST_WIDTH + PREVIEW_WIDTH;

    GetWindowRect(GetParent(edit), &box);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    x = max(0, min(box.left, GetSystemMetrics(SM_CXSCREEN) - w));
    y = box.top - FLYOUT_HEIGHT;
    if (y < 0) y = box.bottom;   /* a taskbar at the top */
    SetWindowPos(g.wnd, HWND_TOPMOST, x, y, w, FLYOUT_HEIGHT, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    (void)work;
}

/* ---- API ---- */

void instant_search_update(HWND edit, const WCHAR *text)
{
    static const int roots[] = { CSIDL_DESKTOPDIRECTORY, CSIDL_PERSONAL, CSIDL_MYPICTURES, CSIDL_MYMUSIC, CSIDL_MYVIDEO };
    SEARCH_CRITERIA c;
    RESULT *r;
    WCHAR profile[MAX_PATH];
    int i, score;

    g.edit = edit;
    while (*text == ' ') text++;
    if (!*text)
    {
        instant_search_hide();
        return;
    }
    if (!g.wnd) create_window();
    if (!g.loaded || !IsWindowVisible(g.wnd)) load_sources();   /* new programs show the next time */
    lstrcpynW(g.query, text, ARRAY_SIZE(g.query));
    g.count = 0;
    g.user_selected = FALSE;

    for (i = 0; i < g.program_count; i++)
        if ((score = match(g.programs[i].name, g.query)) && (r = add_result(KIND_PROGRAM, g.programs[i].name, score + 1)))
        {
            lstrcpynW(r->path, g.programs[i].path, MAX_PATH);
            r->icon = system_icon(r->path, NULL);
        }
    for (i = 0; i < g.setting_count; i++)
        if ((score = match(g.settings[i].name, g.query)) && (r = add_result(KIND_SETTING, g.settings[i].name, score)))
        {
            r->pidl = g.settings[i].pidl;
            r->icon = system_icon(NULL, r->pidl);
        }
    if ((r = add_result(KIND_COMPANION, L"", 0)))
        swprintf(r->name, MAX_PATH, L"Search for files and folders with Search Companion");
    refresh_results();

    /* the files and folders of your own folders come as they are found */
    if (g.job) search_stop(g.job);
    search_criteria_init(&c);
    lstrcpynW(c.names, g.query, MAX_PATH);
    c.system_folders = FALSE;
    for (i = 0; i < ARRAY_SIZE(roots); i++)
        if (SUCCEEDED(SHGetFolderPathW(NULL, roots[i], NULL, SHGFP_TYPE_CURRENT, c.roots[c.root_count])))
            c.root_count++;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, profile)))
        swprintf(c.roots[c.root_count++], MAX_PATH, L"%s\\Downloads", profile);
    g.job = search_start(&c, g.wnd, ++g.job_id);

    place(edit);
    InvalidateRect(g.wnd, NULL, FALSE);
}

BOOL instant_search_key(UINT vk)
{
    if (!g.wnd || !IsWindowVisible(g.wnd)) return FALSE;
    switch (vk)
    {
    case VK_UP: select_row(-1); return TRUE;
    case VK_DOWN: select_row(1); return TRUE;
    case VK_RETURN: open_result(selected_result(), FALSE); return TRUE;
    case VK_ESCAPE:
        instant_search_hide();
        if (g.edit) SetWindowTextW(g.edit, L"");
        return TRUE;
    }
    return FALSE;
}

void instant_search_hide(void)
{
    if (g.job) search_stop(g.job);
    g.job = NULL;
    g.job_id++;
    if (g.wnd) ShowWindow(g.wnd, SW_HIDE);
}

BOOL instant_search_owns(HWND hwnd)
{
    return hwnd && hwnd == g.wnd;
}
