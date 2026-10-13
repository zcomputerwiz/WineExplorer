/*
 * WineExplorer Desktop Environment - Mouse Properties: Pointers Page
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

#include "main_private.h"
#include <commdlg.h>

typedef struct _CURSOR_ENTRY
{
    UINT id_name;
    LPCWSTR ocr_id;
    WCHAR file_path[MAX_PATH];
    HICON hicon;
} CURSOR_ENTRY;

static CURSOR_ENTRY g_cursors[] = {
    { IDS_CURSOR_NORMAL,      (LPCWSTR)MAKEINTRESOURCEW(OCR_NORMAL),      L"", NULL },
    { IDS_CURSOR_HELP,        (LPCWSTR)MAKEINTRESOURCEW(OCR_HELP),        L"", NULL },
    { IDS_CURSOR_APPSTARTING, (LPCWSTR)MAKEINTRESOURCEW(OCR_APPSTARTING), L"", NULL },
    { IDS_CURSOR_WAIT,        (LPCWSTR)MAKEINTRESOURCEW(OCR_WAIT),        L"", NULL },
    { IDS_CURSOR_CROSS,       (LPCWSTR)MAKEINTRESOURCEW(OCR_CROSS),       L"", NULL },
    { IDS_CURSOR_IBEAM,       (LPCWSTR)MAKEINTRESOURCEW(OCR_IBEAM),       L"", NULL },
    { IDS_CURSOR_NWPEN,       NULL,                                       L"", NULL },
    { IDS_CURSOR_NO,          (LPCWSTR)MAKEINTRESOURCEW(OCR_NO),          L"", NULL },
    { IDS_CURSOR_SIZENS,      (LPCWSTR)MAKEINTRESOURCEW(OCR_SIZENS),      L"", NULL },
    { IDS_CURSOR_SIZEWE,      (LPCWSTR)MAKEINTRESOURCEW(OCR_SIZEWE),      L"", NULL },
    { IDS_CURSOR_SIZENWSE,    (LPCWSTR)MAKEINTRESOURCEW(OCR_SIZENWSE),    L"", NULL },
    { IDS_CURSOR_SIZENESW,    (LPCWSTR)MAKEINTRESOURCEW(OCR_SIZENESW),    L"", NULL },
    { IDS_CURSOR_SIZEALL,     (LPCWSTR)MAKEINTRESOURCEW(OCR_SIZEALL),     L"", NULL },
    { IDS_CURSOR_UPARROW,     (LPCWSTR)MAKEINTRESOURCEW(OCR_UP),          L"", NULL },
    { IDS_CURSOR_HAND,        (LPCWSTR)MAKEINTRESOURCEW(OCR_HAND),        L"", NULL },
};

static BOOL g_cursor_shadow = TRUE;

static void load_default_cursor_icons(void)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(g_cursors); i++)
    {
        if (g_cursors[i].ocr_id)
            g_cursors[i].hicon = (HICON)LoadImageW(NULL, g_cursors[i].ocr_id, IMAGE_CURSOR, 32, 32, LR_SHARED | LR_DEFAULTSIZE);
        else
            g_cursors[i].hicon = (HICON)LoadImageW(NULL, (LPCWSTR)MAKEINTRESOURCEW(OCR_NORMAL), IMAGE_CURSOR, 32, 32, LR_SHARED | LR_DEFAULTSIZE);
    }
}

static void free_cursor_icons(void)
{
    /* Shared cursors don't need DestroyIcon, custom ones loaded via file would */
}

INT_PTR CALLBACK pointer_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hCombo = GetDlgItem(hwnd, ID_SCHEMECOMBO);
        HWND hList = GetDlgItem(hwnd, ID_CURSORLIST);
        WCHAR str[128];
        int i;

        load_default_cursor_icons();

        SystemParametersInfoW(SPI_GETCURSORSHADOW, 0, &g_cursor_shadow, 0);
        CheckDlgButton(hwnd, ID_CURSORSHADOW, g_cursor_shadow ? BST_CHECKED : BST_UNCHECKED);

        /* Schemes */
        LoadStringW(g_hinst, IDS_SCHEME_DEFAULT, str, ARRAY_SIZE(str));
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)str);

        LoadStringW(g_hinst, IDS_SCHEME_INVERTED, str, ARRAY_SIZE(str));
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)str);

        LoadStringW(g_hinst, IDS_SCHEME_BLACK, str, ARRAY_SIZE(str));
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)str);

        LoadStringW(g_hinst, IDS_SCHEME_STANDARD, str, ARRAY_SIZE(str));
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)str);

        LoadStringW(g_hinst, IDS_NONE, str, ARRAY_SIZE(str));
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)str);

        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

        /* Cursor items */
        SendMessageW(hList, LB_RESETCONTENT, 0, 0);
        for (i = 0; i < ARRAY_SIZE(g_cursors); i++)
        {
            LoadStringW(g_hinst, g_cursors[i].id_name, str, ARRAY_SIZE(str));
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)str);
        }
        SendMessageW(hList, LB_SETCURSEL, 0, 0);

        SendDlgItemMessageW(hwnd, ID_PREVIEW, STM_SETICON, (WPARAM)g_cursors[0].hicon, 0);

        return TRUE;
    }

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lparam;
        if (mis->CtlID == ID_CURSORLIST)
        {
            mis->itemHeight = 36;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;
        if (dis->CtlID == ID_CURSORLIST && dis->itemID != (UINT)-1)
        {
            HBRUSH bg_brush;
            COLORREF old_text;
            WCHAR text[128];
            RECT rc_text;

            if (dis->itemState & ODS_SELECTED)
            {
                bg_brush = GetSysColorBrush(COLOR_HIGHLIGHT);
                old_text = SetTextColor(dis->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
            }
            else
            {
                bg_brush = GetSysColorBrush(COLOR_WINDOW);
                old_text = SetTextColor(dis->hDC, GetSysColor(COLOR_WINDOWTEXT));
            }

            FillRect(dis->hDC, &dis->rcItem, bg_brush);
            SetBkMode(dis->hDC, TRANSPARENT);

            /* Draw icon */
            if (dis->itemID < ARRAY_SIZE(g_cursors) && g_cursors[dis->itemID].hicon)
            {
                DrawIconEx(dis->hDC, dis->rcItem.left + 4, dis->rcItem.top + 2,
                           g_cursors[dis->itemID].hicon, 32, 32, 0, NULL, DI_NORMAL);
            }

            /* Draw role name */
            LoadStringW(g_hinst, g_cursors[dis->itemID].id_name, text, ARRAY_SIZE(text));
            rc_text = dis->rcItem;
            rc_text.left += 42;
            DrawTextW(dis->hDC, text, -1, &rc_text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

            SetTextColor(dis->hDC, old_text);
            if (dis->itemState & ODS_FOCUS)
                DrawFocusRect(dis->hDC, &dis->rcItem);

            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case ID_CURSORLIST:
            if (HIWORD(wparam) == LBN_SELCHANGE)
            {
                int sel = (int)SendDlgItemMessageW(hwnd, ID_CURSORLIST, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < ARRAY_SIZE(g_cursors))
                {
                    SendDlgItemMessageW(hwnd, ID_PREVIEW, STM_SETICON, (WPARAM)g_cursors[sel].hicon, 0);
                }
            }
            break;

        case ID_CURSORSHADOW:
            g_cursor_shadow = (IsDlgButtonChecked(hwnd, ID_CURSORSHADOW) == BST_CHECKED);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            break;

        case ID_DEFAULT:
        {
            int sel = (int)SendDlgItemMessageW(hwnd, ID_CURSORLIST, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < ARRAY_SIZE(g_cursors))
            {
                if (g_cursors[sel].ocr_id)
                    g_cursors[sel].hicon = (HICON)LoadImageW(NULL, g_cursors[sel].ocr_id, IMAGE_CURSOR, 32, 32, LR_SHARED | LR_DEFAULTSIZE);
                g_cursors[sel].file_path[0] = 0;
                SendDlgItemMessageW(hwnd, ID_PREVIEW, STM_SETICON, (WPARAM)g_cursors[sel].hicon, 0);
                InvalidateRect(GetDlgItem(hwnd, ID_CURSORLIST), NULL, TRUE);
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;
        }

        case ID_BROWSE:
        {
            OPENFILENAMEW ofn;
            WCHAR file[MAX_PATH] = { 0 };
            int sel = (int)SendDlgItemMessageW(hwnd, ID_CURSORLIST, LB_GETCURSEL, 0, 0);

            memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Cursors (*.cur;*.ani)\0*.cur;*.ani\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameW(&ofn) && sel >= 0 && sel < ARRAY_SIZE(g_cursors))
            {
                HICON hnew = (HICON)LoadImageW(NULL, file, IMAGE_CURSOR, 32, 32, LR_LOADFROMFILE);
                if (hnew)
                {
                    g_cursors[sel].hicon = hnew;
                    lstrcpynW(g_cursors[sel].file_path, file, MAX_PATH);
                    SendDlgItemMessageW(hwnd, ID_PREVIEW, STM_SETICON, (WPARAM)hnew, 0);
                    InvalidateRect(GetDlgItem(hwnd, ID_CURSORLIST), NULL, TRUE);
                    SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
                }
            }
            break;
        }

        case ID_SCHEMECOMBO:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            }
            break;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            SystemParametersInfoW(SPI_SETCURSORSHADOW, 0, (void *)(ULONG_PTR)g_cursor_shadow,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            SystemParametersInfoW(SPI_SETCURSORS, 0, 0, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
        free_cursor_icons();
        break;
    }

    return FALSE;
}
