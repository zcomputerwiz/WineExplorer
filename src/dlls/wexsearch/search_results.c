/*
 * WineExplorer Desktop Environment - the Search Results list
 *
 * The right part of an Explorer window that searches: Name, In Folder, Size, Type and Date Modified like Windows XP's
 * Search Results folder, with the search watermark of the visual style (shellstyle.dll) in the corner.
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
#include <commoncontrols.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include "search_ui.h"
#include "wexres.h"

#include "module.h"

#define RESULTS_CLASS L"SearchResultsView"
#define IDC_RESULTS_LIST 100
#define IDM_OPEN_CONTAINING 0x7FF0
#define IDB_SEARCH_LISTVIEW_BMP 18   /* shellstyle.dll: the watermark of search folders */

enum { COL_NAME, COL_FOLDER, COL_SIZE, COL_TYPE, COL_DATE };

typedef struct
{
    HWND hwnd, list;
    WNDPROC list_proc;
    HBITMAP watermark;
    BOOL searched;       /* a search ran: no more "to start your search" hint */
    int sort_column;
    BOOL sort_down;
} RESULTS;

static RESULTS *get_results(HWND hwnd)
{
    return (RESULTS *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static HBITMAP load_watermark(void)
{
    return shellres_style_bitmap(IDB_SEARCH_LISTVIEW_BMP);
}

static void paint_background(RESULTS *res, HDC hdc)
{
    RECT rc, header;
    HWND hdr = (HWND)SendMessageW(res->list, LVM_GETHEADER, 0, 0);
    int top = 0;

    GetClientRect(res->list, &rc);
    if (hdr && GetWindowRect(hdr, &header)) top = header.bottom - header.top;
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
    if (res->watermark)
    {
        BITMAP bm;
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP prev = SelectObject(mem, res->watermark);
        GetObjectW(res->watermark, sizeof(bm), &bm);
        BitBlt(hdc, rc.right - bm.bmWidth, rc.bottom - bm.bmHeight, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
        SelectObject(mem, prev);
        DeleteDC(mem);
    }
    if (!SendMessageW(res->list, LVM_GETITEMCOUNT, 0, 0))
    {
        HFONT font = (HFONT)SendMessageW(res->list, WM_GETFONT, 0, 0), old = SelectObject(hdc, font);
        RECT text = { 4, top + 4, rc.right - 4, top + 40 };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        DrawTextW(hdc, res->searched ? L"Search is complete. There are no results to display."
                                     : L"To start your search, follow the instructions in the left pane.",
                  -1, &text, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    }
}

/* The list draws on our background: the watermark stays in the corner while the items scroll */
static LRESULT CALLBACK list_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    RESULTS *res = get_results(GetParent(hwnd));

    switch (msg)
    {
    case WM_ERASEBKGND:
        paint_background(res, (HDC)wparam);
        return 1;
    case WM_VSCROLL:
    case WM_HSCROLL:
    case WM_MOUSEWHEEL:
    case WM_KEYDOWN:
    {
        LRESULT ret = CallWindowProcW(res->list_proc, hwnd, msg, wparam, lparam);
        InvalidateRect(hwnd, NULL, TRUE);
        return ret;
    }
    }
    return CallWindowProcW(res->list_proc, hwnd, msg, wparam, lparam);
}

/* ---- the columns ---- */

static void type_name(const SEARCH_RESULT *r, WCHAR *out, int len)
{
    SHFILEINFOW sfi = {0};
    SHGetFileInfoW(r->path, r->attributes, &sfi, sizeof(sfi), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES);
    lstrcpynW(out, sfi.szTypeName, len);
}

static void date_text(const FILETIME *ft, WCHAR *out, int len)
{
    FILETIME local;
    SYSTEMTIME st;
    WCHAR date[64], time[64];

    FileTimeToLocalFileTime(ft, &local);
    FileTimeToSystemTime(&local, &st);
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date, ARRAY_SIZE(date));
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, time, ARRAY_SIZE(time));
    swprintf(out, len, L"%s %s", date, time);
}

static void column_text(const SEARCH_RESULT *r, int column, WCHAR *out, int len)
{
    out[0] = 0;
    switch (column)
    {
    case COL_NAME: lstrcpynW(out, PathFindFileNameW(r->path), len); break;
    case COL_FOLDER:
        lstrcpynW(out, r->path, len);
        PathRemoveFileSpecW(out);
        break;
    case COL_SIZE:
        if (!(r->attributes & FILE_ATTRIBUTE_DIRECTORY)) StrFormatKBSizeW(r->size, out, len);
        break;
    case COL_TYPE: type_name(r, out, len); break;
    case COL_DATE: date_text(&r->modified, out, len); break;
    }
}

static int CALLBACK compare_results(LPARAM a, LPARAM b, LPARAM param)
{
    const SEARCH_RESULT *ra = (const SEARCH_RESULT *)a, *rb = (const SEARCH_RESULT *)b;
    RESULTS *res = (RESULTS *)param;
    WCHAR ta[MAX_PATH], tb[MAX_PATH];
    int cmp;

    switch (res->sort_column)
    {
    case COL_SIZE: cmp = ra->size < rb->size ? -1 : ra->size > rb->size; break;
    case COL_DATE: cmp = CompareFileTime(&ra->modified, &rb->modified); break;
    default:
        column_text(ra, res->sort_column, ta, MAX_PATH);
        column_text(rb, res->sort_column, tb, MAX_PATH);
        cmp = lstrcmpiW(ta, tb);
        break;
    }
    return res->sort_down ? -cmp : cmp;
}

static SEARCH_RESULT *item_result(RESULTS *res, int item)
{
    LVITEMW lvi = { LVIF_PARAM };
    lvi.iItem = item;
    return SendMessageW(res->list, LVM_GETITEMW, 0, (LPARAM)&lvi) ? (SEARCH_RESULT *)lvi.lParam : NULL;
}

/* ---- actions ---- */

static void open_item(RESULTS *res, int item)
{
    SEARCH_RESULT *r = item_result(res, item);
    if (r) ShellExecuteW(res->hwnd, NULL, r->path, NULL, NULL, SW_SHOWNORMAL);
}

/* results whose file went away (deleted, moved) leave the list */
static void drop_missing(RESULTS *res)
{
    int i;
    for (i = SendMessageW(res->list, LVM_GETITEMCOUNT, 0, 0) - 1; i >= 0; i--)
    {
        SEARCH_RESULT *r = item_result(res, i);
        if (r && GetFileAttributesW(r->path) == INVALID_FILE_ATTRIBUTES)
        {
            SendMessageW(res->list, LVM_DELETEITEM, i, 0);
            free(r);
        }
    }
    InvalidateRect(res->list, NULL, TRUE);
}

/* The shell's context menu of the selected items (those that share the folder of the first one); folder and item
 * receive the folder and the first item, to be freed by the caller */
static IContextMenu *selection_menu(RESULTS *res, LPITEMIDLIST *folder, LPITEMIDLIST *item_pidl)
{
    LPITEMIDLIST pidls[64];
    LPCITEMIDLIST children[64];
    IShellFolder *parent;
    IContextMenu *menu = NULL;
    int item = -1, count = 0, i;

    *folder = *item_pidl = NULL;
    while ((item = SendMessageW(res->list, LVM_GETNEXTITEM, item, LVNI_SELECTED)) >= 0 && count < ARRAY_SIZE(pidls))
    {
        SEARCH_RESULT *r = item_result(res, item);
        LPITEMIDLIST pidl;

        if (!r || FAILED(SHParseDisplayName(r->path, NULL, &pidl, 0, NULL))) continue;
        if (count && !ILIsParent(*folder, pidl, TRUE)) { ILFree(pidl); continue; }
        pidls[count++] = pidl;
        if (count == 1)
        {
            *item_pidl = ILClone(pidl);
            *folder = ILClone(pidl);
            ILRemoveLastID(*folder);
        }
    }
    if (count && SUCCEEDED(SHBindToParent(pidls[0], &IID_IShellFolder, (void **)&parent, NULL)))
    {
        for (i = 0; i < count; i++) children[i] = ILFindLastID(pidls[i]);
        IShellFolder_GetUIObjectOf(parent, res->hwnd, count, children, &IID_IContextMenu, NULL, (void **)&menu);
        IShellFolder_Release(parent);
    }
    for (i = 0; i < count; i++) ILFree(pidls[i]);
    return menu;
}

/* The menu of the selected items: what their folder offers, plus "Open Containing Folder" like Windows XP */
static void item_menu(RESULTS *res, POINT pt)
{
    LPITEMIDLIST folder, first;
    IContextMenu *menu = selection_menu(res, &folder, &first);
    HMENU popup;
    UINT cmd;

    if (!first) return;
    popup = CreatePopupMenu();
    if (menu) IContextMenu_QueryContextMenu(menu, popup, 0, 1, IDM_OPEN_CONTAINING - 1, CMF_NORMAL);
    InsertMenuW(popup, 1, MF_BYPOSITION | MF_STRING | (SendMessageW(res->list, LVM_GETSELECTEDCOUNT, 0, 0) == 1 ? 0 : MF_GRAYED),
                IDM_OPEN_CONTAINING, L"Open Containing &Folder");
    cmd = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, res->hwnd, NULL);
    if (cmd == IDM_OPEN_CONTAINING)
    {
        LPCITEMIDLIST child = ILFindLastID(first);
        SHOpenFolderAndSelectItems(folder, 1, &child, 0);
    }
    else if (cmd && menu)
    {
        CMINVOKECOMMANDINFO ici = { sizeof(ici) };
        ici.hwnd = res->hwnd;
        ici.lpVerb = MAKEINTRESOURCEA(cmd - 1);
        ici.nShow = SW_SHOWNORMAL;
        IContextMenu_InvokeCommand(menu, &ici);
        drop_missing(res);
    }
    DestroyMenu(popup);
    if (menu) IContextMenu_Release(menu);
    ILFree(folder);
    ILFree(first);
}

static void invoke_selection(RESULTS *res, const char *verb)
{
    LPITEMIDLIST folder, first;
    IContextMenu *menu = selection_menu(res, &folder, &first);

    if (menu)
    {
        CMINVOKECOMMANDINFO ici = { sizeof(ici) };
        ici.hwnd = res->hwnd;
        ici.lpVerb = verb;
        ici.nShow = SW_SHOWNORMAL;
        IContextMenu_InvokeCommand(menu, &ici);
        IContextMenu_Release(menu);
        drop_missing(res);
    }
    ILFree(folder);
    ILFree(first);
}

static LRESULT on_notify(RESULTS *res, NMHDR *nm)
{
    if (nm->hwndFrom != res->list) return 0;
    switch (nm->code)
    {
    case LVN_GETDISPINFOW:
    {
        NMLVDISPINFOW *di = (NMLVDISPINFOW *)nm;
        SEARCH_RESULT *r = (SEARCH_RESULT *)di->item.lParam;
        if (!r) break;
        if (di->item.mask & LVIF_TEXT) column_text(r, di->item.iSubItem, di->item.pszText, di->item.cchTextMax);
        if (di->item.mask & LVIF_IMAGE)
        {
            SHFILEINFOW sfi = {0};
            SHGetFileInfoW(r->path, 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
            di->item.iImage = sfi.iIcon;
        }
        break;
    }
    case LVN_COLUMNCLICK:
    {
        NMLISTVIEW *lv = (NMLISTVIEW *)nm;
        if (res->sort_column == lv->iSubItem) res->sort_down = !res->sort_down;
        else { res->sort_column = lv->iSubItem; res->sort_down = FALSE; }
        SendMessageW(res->list, LVM_SORTITEMS, (WPARAM)res, (LPARAM)compare_results);
        break;
    }
    case NM_DBLCLK:
    case NM_RETURN:
    {
        int item = SendMessageW(res->list, LVM_GETNEXTITEM, -1, LVNI_FOCUSED);
        if (item >= 0) open_item(res, item);
        break;
    }
    case LVN_KEYDOWN:
        if (((NMLVKEYDOWN *)nm)->wVKey == VK_DELETE) invoke_selection(res, "delete");
        break;
    case NM_RCLICK:
    {
        POINT pt;
        GetCursorPos(&pt);
        item_menu(res, pt);
        break;
    }
    case LVN_DELETEALLITEMS:
        return FALSE;   /* LVN_DELETEITEM for each: the results are freed there */
    case LVN_DELETEITEM:
        free((void *)((NMLISTVIEW *)nm)->lParam);
        break;
    }
    return 0;
}

static LRESULT CALLBACK results_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    RESULTS *res = get_results(hwnd);

    switch (msg)
    {
    case WM_CREATE:
    {
        static const struct { const WCHAR *name; int width, format; } columns[] =
        {
            { L"Name", 180, LVCFMT_LEFT }, { L"In Folder", 180, LVCFMT_LEFT }, { L"Size", 60, LVCFMT_RIGHT },
            { L"Type", 120, LVCFMT_LEFT }, { L"Date Modified", 120, LVCFMT_LEFT },
        };
        IImageList *images = NULL;
        int i;

        res = calloc(1, sizeof(*res));
        res->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)res);
        res->list = CreateWindowExW(0, WC_LISTVIEWW, NULL,
                                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS |
                                    LVS_SHAREIMAGELISTS, 0, 0, 0, 0, hwnd, (HMENU)IDC_RESULTS_LIST, wexsearch_module(), NULL);
        SendMessageW(res->list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_HEADERDRAGDROP, LVS_EX_HEADERDRAGDROP);
        SendMessageW(res->list, LVM_SETTEXTBKCOLOR, 0, CLR_NONE);
        if (SUCCEEDED(SHGetImageList(SHIL_SMALL, &IID_IImageList, (void **)&images)))
            SendMessageW(res->list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)images);
        for (i = 0; i < ARRAY_SIZE(columns); i++)
        {
            LVCOLUMNW col = { LVCF_TEXT | LVCF_WIDTH | LVCF_FMT };
            col.pszText = (WCHAR *)columns[i].name;
            col.cx = columns[i].width;
            col.fmt = columns[i].format;
            SendMessageW(res->list, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
        }
        res->watermark = load_watermark();
        res->list_proc = (WNDPROC)SetWindowLongPtrW(res->list, GWLP_WNDPROC, (LONG_PTR)list_subclass);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(res->list, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        InvalidateRect(res->list, NULL, TRUE);
        return 0;
    case WM_SETFOCUS:
        SetFocus(res->list);
        return 0;
    case WM_NOTIFY:
        return on_notify(res, (NMHDR *)lparam);
    case WM_THEMECHANGED:
        if (res->watermark) DeleteObject(res->watermark);
        res->watermark = load_watermark();
        InvalidateRect(res->list, NULL, TRUE);
        break;
    case WM_DESTROY:
        SendMessageW(res->list, LVM_DELETEALLITEMS, 0, 0);
        if (res->watermark) DeleteObject(res->watermark);
        free(res);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ---- API ---- */

HWND search_results_create(HWND parent)
{
    WNDCLASSW wc = {0};

    wc.lpfnWndProc = results_proc;
    wc.hInstance = wexsearch_module();
    wc.lpszClassName = RESULTS_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);
    return CreateWindowExW(WS_EX_CLIENTEDGE, RESULTS_CLASS, NULL, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, NULL,
                           wexsearch_module(), NULL);
}

void search_results_clear(HWND results, BOOL searched)
{
    RESULTS *res = get_results(results);
    if (!res) return;
    res->searched = searched;
    SendMessageW(res->list, LVM_DELETEALLITEMS, 0, 0);
    InvalidateRect(res->list, NULL, TRUE);
}

void search_results_add(HWND results, SEARCH_RESULT *r)
{
    RESULTS *res = get_results(results);
    LVITEMW lvi = { LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM };
    int i;

    if (!res) { free(r); return; }
    lvi.iItem = INT_MAX;
    lvi.pszText = LPSTR_TEXTCALLBACKW;
    lvi.iImage = I_IMAGECALLBACK;
    lvi.lParam = (LPARAM)r;
    i = SendMessageW(res->list, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
    if (i < 0) { free(r); return; }
    for (lvi.iSubItem = 1; lvi.iSubItem <= COL_DATE; lvi.iSubItem++)
        SendMessageW(res->list, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);
    if (i == 0) InvalidateRect(res->list, NULL, TRUE);   /* the hint goes away */
}

void search_results_done(HWND results)
{
    RESULTS *res = get_results(results);
    if (!res) return;
    res->searched = TRUE;
    InvalidateRect(res->list, NULL, TRUE);
}

int search_results_count(HWND results, BOOL selected)
{
    RESULTS *res = get_results(results);
    if (!res) return 0;
    return SendMessageW(res->list, selected ? LVM_GETSELECTEDCOUNT : LVM_GETITEMCOUNT, 0, 0);
}
