/*
 * WineExplorer Desktop Environment - Search Companion
 *
 * The search pane of Windows XP: a balloon on the Explorer bar gradient with the page of the current step ("What do
 * you want to search for?", "Search by any or all of the criteria below." ...), Back and Search at its foot and the
 * character below it.  The pictures come from Windows' Search Companion files in %WINDIR%\srchasst when they are
 * installed, otherwise from the shell.
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
#include <vsstyle.h>
#include <vssym32.h>

#include "resource.h"
#include "agent_char.h"
#include "search_ui.h"
#include "wexres.h"

#include "module.h"

#define PANE_CLASS    L"SearchCompanionPane"
#define CONTENT_CLASS L"SearchCompanionContent"
#define CHOOSER_CLASS L"SearchCharacterChooser"

/* geometry measured on Windows XP */
#define PANE_WIDTH     200
#define SPLITTER        4
#define CAPTION        22
#define BALLOON_X       5
#define TAIL_SPACE     88    /* below the balloon: the character */
#define LINE_GAP        5
#define TEXT_LEFT      14
#define TEXT_RIGHT     13
#define CHARACTER_X    41    /* where the character's frame goes: Rover at rest stands where XP shows him */
#define CHARACTER_BOTTOM 1
#define IDT_CHARACTER   1
#define CHOOSER_HEIGHT 306   /* the character chooser, its preview animation and greeting */
#define IDT_PREVIEW     1
#define IDT_GREETING    2

/* srchui.dll resource ids */
#define ICON_ARROW        290
#define ICON_INTERNET   24003
#define ICON_PREFS      24001
#define ICON_PEOPLE     24005
#define ICON_COMPUTER   24006
#define ICON_PICTURES     236
#define ICON_MUSIC        237
#define ICON_VIDEO        278
#define BMP_NEWSEARCH     101

enum { PAGE_START, PAGE_MEDIA, PAGE_DOCS, PAGE_ALL, PAGE_COMPUTERS, PAGE_SEARCHING, PAGE_RESULTS, PAGE_PREFS, PAGE_CHARACTERS };

enum
{
    CMD_NONE, CMD_MEDIA, CMD_DOCS, CMD_ALL, CMD_COMPUTERS, CMD_PREFS, CMD_CHARACTER, CMD_FINISHED, CMD_REFINE_NAME,
    CMD_REFINE_LOOK, CMD_REFINE_HIDDEN, CMD_NEW_SEARCH, CMD_ADVANCED, CMD_SECTION_DATE, CMD_SECTION_SIZE,
    CMD_SECTION_MORE, CMD_CHOOSE_CHARACTER, CMD_DISABLED
};

/* control ids */
enum
{
    IDC_BACK = 200, IDC_SEARCH, IDC_STOP, IDC_NAME, IDC_TEXT, IDC_LOOK, IDC_DATE_FIRST, IDC_DATE_LAST = IDC_DATE_FIRST + 4,
    IDC_DATE_KIND, IDC_DATE_FROM, IDC_DATE_TO, IDC_SIZE_FIRST, IDC_SIZE_LAST = IDC_SIZE_FIRST + 4, IDC_SIZE_HOW,
    IDC_SIZE_KB, IDC_SIZE_SPIN, IDC_TYPE, IDC_SYSTEM, IDC_HIDDEN, IDC_SUBFOLDERS, IDC_CASE, IDC_TAPE, IDC_PICTURES,
    IDC_MUSIC, IDC_VIDEO, IDC_DOCDATE_FIRST, IDC_DOCDATE_LAST = IDC_DOCDATE_FIRST + 3, IDC_CHOOSER, IDC_CHAR_PREV,
    IDC_CHAR_NEXT, IDC_CHAR_OK, IDC_CHAR_CANCEL
};

enum { ITEM_TITLE, ITEM_TEXT, ITEM_LINK, ITEM_LABEL, ITEM_CONTROL, ITEM_SECTION, ITEM_GAP };

typedef struct
{
    int type;
    WCHAR text[160];
    int icon;          /* resource id: srchui icon, shell32 icon (negative) or srchui bitmap (BMP_ flag) */
    BOOL bitmap;
    UINT cmd;
    BOOL disabled;
    HWND ctl;
    int height;        /* controls */
    int indent;
    RECT rc;
} ITEM;

typedef struct
{
    WCHAR text[MAX_PATH];
    WCHAR roots[SEARCH_MAX_ROOTS][MAX_PATH];
    int count;
} LOOK_IN;

struct search_ui
{
    HWND parent, pane, content, back, action, results;
    BOOL pane_visible, results_visible;
    int page, history[16], depth;
    ITEM items[64];
    int item_count, hot, scroll, content_height;
    HFONT font, bold;
    HMODULE srchui;
    HBITMAP rover;            /* still picture, when the animated one is not installed */
    struct agent_char *agent;
    BOOL character;
    WCHAR characters[8][MAX_PATH];   /* the character chooser: files to choose from, the one shown, its preview */
    int character_count, character_choice;
    struct agent_char *preview;
    HWND tip;
    HFONT name_font;

    /* what the pages remember between steps */
    WCHAR name[MAX_PATH], text[256], scope[MAX_PATH];
    int look, date_choice, date_kind, size_choice, size_how, size_kb, type, doc_date;
    SYSTEMTIME date_from, date_to;
    BOOL expanded[3], system, hidden, subfolders, case_sensitive, media[3];
    LOOK_IN *looks;
    int look_count;

    SEARCH_JOB *job;
    UINT job_id;
    int found;
    WCHAR searching[MAX_PATH];
    LOOK_IN *types;   /* "Type of file": text and the extensions (in roots[0]) */
    int type_count;
};

static SEARCH_UI *pane_ui(HWND hwnd)
{
    return (SEARCH_UI *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

/* ---- pictures ---- */

static HICON load_icon(SEARCH_UI *ui, int id)
{
    HICON icon = NULL;
    if (id < 0) return shellres_icon(-id, 16);
    if (ui->srchui) icon = LoadImageW(ui->srchui, MAKEINTRESOURCEW(id), IMAGE_ICON, 16, 16, 0);
    if (!icon)
    {
        /* without the Search Companion files: the shell's nearest pictures */
        int fallback = id == ICON_ARROW ? 0 : id == ICON_INTERNET ? 14 : id == ICON_PREFS ? 22 : id == ICON_PEOPLE ? 160 :
                       id == ICON_COMPUTER ? 16 : 0;
        if (fallback) icon = shellres_icon(fallback, 16);
    }
    return icon;
}

static void draw_link_picture(SEARCH_UI *ui, HDC hdc, const ITEM *it, int x, int y)
{
    if (it->bitmap)
    {
        HBITMAP bmp = ui->srchui ? LoadImageW(ui->srchui, MAKEINTRESOURCEW(it->icon), IMAGE_BITMAP, 0, 0, 0) : NULL;
        if (bmp)
        {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP prev = SelectObject(mem, bmp);
            TransparentBlt(hdc, x, y, 16, 16, mem, 0, 0, 16, 16, GetPixel(mem, 0, 0));
            SelectObject(mem, prev);
            DeleteDC(mem);
            DeleteObject(bmp);
        }
        return;
    }
    if (it->icon)
    {
        HICON icon = load_icon(ui, it->icon);
        if (icon)
        {
            DrawIconEx(hdc, x, y, icon, 16, 16, 0, NULL, DI_NORMAL);
            DestroyIcon(icon);
        }
        else if (it->icon == ICON_ARROW)
        {
            /* the green arrow of the links, drawn when srchui.dll is missing */
            HBRUSH green = CreateSolidBrush(RGB(53, 155, 53));
            RECT box = { x + 1, y + 1, x + 15, y + 15 };
            FillRect(hdc, &box, green);
            DeleteObject(green);
            SetTextColor(hdc, RGB(255, 255, 255));
            DrawTextW(hdc, L"\x2192", 1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

static BOOL themed(void)
{
    return IsThemeActive();
}

static COLORREF balloon_color(void)
{
    return themed() ? RGB(214, 223, 247) : GetSysColor(COLOR_WINDOW);
}

/* ---- the "Look in" places ---- */

static void add_look(SEARCH_UI *ui, const WCHAR *text, WCHAR roots[][MAX_PATH], int count)
{
    LOOK_IN *l;
    int i;
    if (!(l = realloc(ui->looks, (ui->look_count + 1) * sizeof(*l)))) return;
    ui->looks = l;
    l = &ui->looks[ui->look_count++];
    lstrcpynW(l->text, text, MAX_PATH);
    for (i = 0; i < count && i < SEARCH_MAX_ROOTS; i++) lstrcpynW(l->roots[i], roots[i], MAX_PATH);
    l->count = min(count, SEARCH_MAX_ROOTS);
}

static void build_looks(SEARCH_UI *ui)
{
    WCHAR roots[SEARCH_MAX_ROOTS][MAX_PATH], text[MAX_PATH], path[MAX_PATH];
    int n, i, local;
    DWORD drives = GetLogicalDrives();

    free(ui->looks);
    ui->looks = NULL;
    ui->look_count = 0;
    if (ui->scope[0])
    {
        SHFILEINFOW sfi = {0};
        SHGetFileInfoW(ui->scope, 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME);
        lstrcpyW(roots[0], ui->scope);
        add_look(ui, sfi.szDisplayName[0] ? sfi.szDisplayName : ui->scope, roots, 1);
    }
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, roots[0]))) add_look(ui, L"Desktop", roots, 1);
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, roots[0]))) add_look(ui, L"My Documents", roots, 1);

    /* My Computer: every drive; Local Hard Drives: the fixed ones, without the one that is the Linux file system */
    local = search_local_drives(roots, SEARCH_MAX_ROOTS);
    n = local;
    for (i = 0; i < 26 && n < SEARCH_MAX_ROOTS; i++)
    {
        WCHAR root[4] = { 'A' + i, ':', '\\', 0 };
        UINT type = GetDriveTypeW(root);
        int j;
        BOOL have = FALSE;
        if (!(drives & (1 << i)) || (type != DRIVE_REMOVABLE && type != DRIVE_CDROM && type != DRIVE_REMOTE)) continue;
        for (j = 0; j < n; j++) if (!wcsicmp(roots[j], root)) have = TRUE;
        if (!have) lstrcpyW(roots[n++], root);
    }
    add_look(ui, L"My Computer", roots, n);
    text[0] = 0;
    for (i = 0; i < local; i++)
    {
        WCHAR letter[4] = { roots[i][0], ':', 0 };
        if (i) lstrcatW(text, L";");
        lstrcatW(text, letter);
    }
    swprintf(path, MAX_PATH, L"Local Hard Drives (%s)", text);
    add_look(ui, path, roots, local);

    for (i = 0; i < 26; i++)
    {
        WCHAR root[4] = { 'A' + i, ':', '\\', 0 };
        SHFILEINFOW sfi = {0};
        if (!(drives & (1 << i))) continue;
        SHGetFileInfoW(root, 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME);
        lstrcpyW(roots[0], root);
        add_look(ui, sfi.szDisplayName[0] ? sfi.szDisplayName : root, roots, 1);
    }
    if (ui->look < 0 || ui->look >= ui->look_count)
    {
        /* opened on a folder: it is selected; otherwise Local Hard Drives, like Windows */
        ui->look = 0;
        if (!ui->scope[0])
            for (i = 0; i < ui->look_count; i++)
                if (!wcsncmp(ui->looks[i].text, L"Local Hard Drives", 17)) ui->look = i;
    }
}

static void fill_look_combo(SEARCH_UI *ui, HWND combo)
{
    int i;
    IImageList *images = NULL;

    if (SUCCEEDED(SHGetImageList(SHIL_SMALL, &IID_IImageList, (void **)&images)))
        SendMessageW(combo, CBEM_SETIMAGELIST, 0, (LPARAM)images);
    for (i = 0; i <= ui->look_count; i++)
    {
        COMBOBOXEXITEMW item = { CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE };
        SHFILEINFOW sfi = {0};

        item.iItem = i;
        if (i == ui->look_count)
        {
            item.pszText = (WCHAR *)L"Browse...";
            item.iImage = item.iSelectedImage = -1;
            item.mask = CBEIF_TEXT;
        }
        else
        {
            item.pszText = ui->looks[i].text;
            if (!wcscmp(ui->looks[i].text, L"My Computer"))
            {
                LPITEMIDLIST pidl;
                if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidl)))
                {
                    SHGetFileInfoW((const WCHAR *)pidl, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
                    ILFree(pidl);
                }
            }
            else SHGetFileInfoW(ui->looks[i].roots[0], 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
            item.iImage = item.iSelectedImage = sfi.iIcon;
        }
        SendMessageW(combo, CBEM_INSERTITEMW, 0, (LPARAM)&item);
    }
    SendMessageW(combo, CB_SETCURSEL, ui->look, 0);
}

static void browse_look(SEARCH_UI *ui, HWND combo)
{
    BROWSEINFOW bi = {0};
    WCHAR path[MAX_PATH], roots[1][MAX_PATH];
    LPITEMIDLIST pidl;

    bi.hwndOwner = ui->parent;
    bi.lpszTitle = L"Select the folder to search in:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    if ((pidl = SHBrowseForFolderW(&bi)) && SHGetPathFromIDListW(pidl, path))
    {
        lstrcpyW(roots[0], path);
        add_look(ui, path, roots, 1);
        ui->look = ui->look_count - 1;
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        fill_look_combo(ui, combo);
    }
    else SendMessageW(combo, CB_SETCURSEL, ui->look, 0);
    if (pidl) ILFree(pidl);
}

/* "Type of file": the registered file types by their description */
static int __cdecl compare_types(const void *a, const void *b)
{
    return lstrcmpiW(((const LOOK_IN *)a)->text, ((const LOOK_IN *)b)->text);
}

static void build_types(SEARCH_UI *ui)
{
    WCHAR ext[64], cls[MAX_PATH], desc[MAX_PATH];
    DWORD i, len, size;
    int j;

    if (ui->types) return;
    for (i = 0; len = ARRAY_SIZE(ext), !RegEnumKeyExW(HKEY_CLASSES_ROOT, i, ext, &len, NULL, NULL, NULL, NULL); i++)
    {
        LOOK_IN *t;
        if (ext[0] != '.') continue;
        size = sizeof(cls);
        if (RegGetValueW(HKEY_CLASSES_ROOT, ext, NULL, RRF_RT_REG_SZ, NULL, cls, &size) || !cls[0]) continue;
        size = sizeof(desc);
        if (RegGetValueW(HKEY_CLASSES_ROOT, cls, NULL, RRF_RT_REG_SZ, NULL, desc, &size) || !desc[0]) continue;
        for (j = 0; j < ui->type_count; j++) if (!lstrcmpiW(ui->types[j].text, desc)) break;
        if (j == ui->type_count)
        {
            if (!(t = realloc(ui->types, (ui->type_count + 1) * sizeof(*t)))) break;
            ui->types = t;
            t = &ui->types[ui->type_count++];
            lstrcpyW(t->text, desc);
            t->roots[0][0] = 0;
        }
        t = &ui->types[j];
        if (lstrlenW(t->roots[0]) + lstrlenW(ext) + 2 < MAX_PATH)
        {
            lstrcatW(t->roots[0], ext);
            lstrcatW(t->roots[0], L";");
        }
    }
    if (ui->type_count) qsort(ui->types, ui->type_count, sizeof(*ui->types), compare_types);
}

/* ---- pages ---- */

static ITEM *add_item(SEARCH_UI *ui, int type, const WCHAR *text, int icon, UINT cmd)
{
    ITEM *it;
    if (ui->item_count >= ARRAY_SIZE(ui->items)) return &ui->items[ARRAY_SIZE(ui->items) - 1];
    it = &ui->items[ui->item_count++];
    memset(it, 0, sizeof(*it));
    it->type = type;
    if (text) lstrcpynW(it->text, text, ARRAY_SIZE(it->text));
    it->icon = icon;
    it->cmd = cmd;
    return it;
}

static HWND add_control(SEARCH_UI *ui, const WCHAR *cls, const WCHAR *text, DWORD style, UINT id, int height, int indent)
{
    ITEM *it = add_item(ui, ITEM_CONTROL, NULL, 0, 0);
    it->ctl = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, height, ui->content, (HMENU)(UINT_PTR)id,
                              wexsearch_module(), NULL);
    it->height = height;
    it->indent = indent;
    SendMessageW(it->ctl, WM_SETFONT, (WPARAM)ui->font, FALSE);
    return it->ctl;
}

static ITEM *add_label(SEARCH_UI *ui, const WCHAR *text)
{
    return add_item(ui, ITEM_LABEL, text, 0, 0);
}

static HWND add_option(SEARCH_UI *ui, const WCHAR *text, UINT id, BOOL radio, BOOL checked, BOOL first)
{
    HWND ctl = add_control(ui, WC_BUTTONW, text, WS_TABSTOP | (radio ? BS_AUTORADIOBUTTON : BS_AUTOCHECKBOX) |
                           (first ? WS_GROUP : 0), id, 16, 0);
    SendMessageW(ctl, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return ctl;
}

static void destroy_controls(SEARCH_UI *ui)
{
    int i;
    for (i = 0; i < ui->item_count; i++) if (ui->items[i].ctl) DestroyWindow(ui->items[i].ctl);
    ui->item_count = 0;
}

static BOOL is_checked(SEARCH_UI *ui, UINT id)
{
    return IsDlgButtonChecked(ui->content, id) == BST_CHECKED;
}

/* Remember what the controls of the page say before it goes away */
static void save_page(SEARCH_UI *ui)
{
    int i;

    switch (ui->page)
    {
    case PAGE_ALL:
        GetDlgItemTextW(ui->content, IDC_NAME, ui->name, ARRAY_SIZE(ui->name));
        GetDlgItemTextW(ui->content, IDC_TEXT, ui->text, ARRAY_SIZE(ui->text));
        if (GetDlgItem(ui->content, IDC_LOOK))
        {
            int sel = SendDlgItemMessageW(ui->content, IDC_LOOK, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < ui->look_count) ui->look = sel;
        }
        if (GetDlgItem(ui->content, IDC_DATE_FIRST))
        {
            for (i = 0; i <= 4; i++) if (is_checked(ui, IDC_DATE_FIRST + i)) ui->date_choice = i;
            ui->date_kind = SendDlgItemMessageW(ui->content, IDC_DATE_KIND, CB_GETCURSEL, 0, 0);
            SendDlgItemMessageW(ui->content, IDC_DATE_FROM, DTM_GETSYSTEMTIME, 0, (LPARAM)&ui->date_from);
            SendDlgItemMessageW(ui->content, IDC_DATE_TO, DTM_GETSYSTEMTIME, 0, (LPARAM)&ui->date_to);
        }
        if (GetDlgItem(ui->content, IDC_SIZE_FIRST))
        {
            for (i = 0; i <= 4; i++) if (is_checked(ui, IDC_SIZE_FIRST + i)) ui->size_choice = i;
            ui->size_how = SendDlgItemMessageW(ui->content, IDC_SIZE_HOW, CB_GETCURSEL, 0, 0);
            ui->size_kb = GetDlgItemInt(ui->content, IDC_SIZE_KB, NULL, FALSE);
        }
        if (GetDlgItem(ui->content, IDC_SYSTEM))
        {
            ui->type = SendDlgItemMessageW(ui->content, IDC_TYPE, CB_GETCURSEL, 0, 0);
            ui->system = is_checked(ui, IDC_SYSTEM);
            ui->hidden = is_checked(ui, IDC_HIDDEN);
            ui->subfolders = is_checked(ui, IDC_SUBFOLDERS);
            ui->case_sensitive = is_checked(ui, IDC_CASE);
        }
        break;
    case PAGE_MEDIA:
        GetDlgItemTextW(ui->content, IDC_NAME, ui->name, ARRAY_SIZE(ui->name));
        for (i = 0; i < 3; i++) ui->media[i] = is_checked(ui, IDC_PICTURES + i);
        break;
    case PAGE_DOCS:
        GetDlgItemTextW(ui->content, IDC_NAME, ui->name, ARRAY_SIZE(ui->name));
        for (i = 0; i <= 3; i++) if (is_checked(ui, IDC_DOCDATE_FIRST + i)) ui->doc_date = i;
        break;
    }
}

static void bold_checked(SEARCH_UI *ui);

static void section(SEARCH_UI *ui, const WCHAR *title, UINT cmd, int index)
{
    ITEM *it = add_item(ui, ITEM_SECTION, title, 0, cmd);
    it->indent = index;
}

static void build_all_page(SEARCH_UI *ui)
{
    static const WCHAR *dates[] = { L"Don't remember", L"Within the last week", L"Past month", L"Within the past year", L"Specify dates" };
    static const WCHAR *sizes[] = { L"Don't remember", L"Small (less than 100 KB)", L"Medium (less than 1 MB)",
                                    L"Large (more than 1 MB)", L"Specify size (in KB)" };
    HWND ctl;
    int i;

    add_item(ui, ITEM_TITLE, L"Search by any or all of the criteria below.", 0, 0);
    add_label(ui, L"All or part &of the file name:");
    ctl = add_control(ui, WC_EDITW, ui->name, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_NAME, 20, 0);
    add_label(ui, L"A &word or phrase in the file:");
    add_control(ui, WC_EDITW, ui->text, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_TEXT, 20, 0);
    add_label(ui, L"&Look in:");
    fill_look_combo(ui, add_control(ui, WC_COMBOBOXEXW, NULL, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_LOOK, 300, 0));

    section(ui, L"When was it modified?", CMD_SECTION_DATE, 0);
    if (ui->expanded[0])
    {
        for (i = 0; i < ARRAY_SIZE(dates); i++) add_option(ui, dates[i], IDC_DATE_FIRST + i, TRUE, ui->date_choice == i, !i);
        ctl = add_control(ui, WC_COMBOBOXW, NULL, WS_TABSTOP | WS_GROUP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_DATE_KIND, 200, 16);
        SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)L"Modified Date");
        SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)L"Created Date");
        SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)L"Accessed Date");
        SendMessageW(ctl, CB_SETCURSEL, ui->date_kind, 0);
        add_label(ui, L"from")->indent = 16;
        ctl = add_control(ui, DATETIMEPICK_CLASSW, NULL, WS_TABSTOP | DTS_SHORTDATEFORMAT, IDC_DATE_FROM, 20, 48);
        SendMessageW(ctl, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&ui->date_from);
        add_label(ui, L"to")->indent = 16;
        ctl = add_control(ui, DATETIMEPICK_CLASSW, NULL, WS_TABSTOP | DTS_SHORTDATEFORMAT, IDC_DATE_TO, 20, 48);
        SendMessageW(ctl, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&ui->date_to);
    }
    section(ui, L"What size is it?", CMD_SECTION_SIZE, 1);
    if (ui->expanded[1])
    {
        for (i = 0; i < ARRAY_SIZE(sizes); i++) add_option(ui, sizes[i], IDC_SIZE_FIRST + i, TRUE, ui->size_choice == i, !i);
        ctl = add_control(ui, WC_COMBOBOXW, NULL, WS_TABSTOP | WS_GROUP | CBS_DROPDOWNLIST, IDC_SIZE_HOW, 200, 16);
        SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)L"at least");
        SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)L"at most");
        SendMessageW(ctl, CB_SETCURSEL, ui->size_how, 0);
        ctl = add_control(ui, WC_EDITW, NULL, WS_TABSTOP | WS_BORDER | ES_NUMBER, IDC_SIZE_KB, 20, 16);
        {
            HWND spin = CreateWindowExW(0, UPDOWN_CLASSW, NULL, WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_SETBUDDYINT |
                                        UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, ui->content, (HMENU)IDC_SIZE_SPIN,
                                        wexsearch_module(), NULL);
            SendMessageW(spin, UDM_SETBUDDY, (WPARAM)ctl, 0);
            SendMessageW(spin, UDM_SETRANGE32, 0, 0x7fffffff);
            SendMessageW(spin, UDM_SETPOS32, 0, ui->size_kb);
            ui->items[ui->item_count - 1].indent = -1;   /* the edit, with its spin box on the right */
            add_item(ui, ITEM_CONTROL, NULL, 0, 0)->ctl = spin;
            ui->items[ui->item_count - 1].height = -1;
        }
    }
    section(ui, L"More advanced options", CMD_SECTION_MORE, 2);
    if (ui->expanded[2])
    {
        add_label(ui, L"T&ype of file:");
        ctl = add_control(ui, WC_COMBOBOXW, NULL, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_TYPE, 260, 0);
        build_types(ui);
        SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)L"(All Files and Folders)");
        for (i = 0; i < ui->type_count; i++) SendMessageW(ctl, CB_ADDSTRING, 0, (LPARAM)ui->types[i].text);
        SendMessageW(ctl, CB_SETCURSEL, ui->type, 0);
        add_option(ui, L"Search system folders", IDC_SYSTEM, FALSE, ui->system, TRUE);
        add_option(ui, L"Search hidden files and folders", IDC_HIDDEN, FALSE, ui->hidden, FALSE);
        add_option(ui, L"Search subfolders", IDC_SUBFOLDERS, FALSE, ui->subfolders, FALSE);
        add_option(ui, L"Case sensitive", IDC_CASE, FALSE, ui->case_sensitive, FALSE);
        EnableWindow(add_option(ui, L"Search tape backup", IDC_TAPE, FALSE, FALSE, FALSE), FALSE);
    }
    (void)ctl;
}

static void build_page(SEARCH_UI *ui)
{
    WCHAR text[160];
    ITEM *it;
    HWND ctl;
    int i;

    destroy_controls(ui);
    ui->hot = -1;
    ui->scroll = 0;
    switch (ui->page)
    {
    case PAGE_START:
        add_item(ui, ITEM_TITLE, L"What do you want to search for?", 0, 0);
        add_item(ui, ITEM_LINK, L"Pictures, music, or video", ICON_ARROW, CMD_MEDIA);
        add_item(ui, ITEM_LINK, L"Documents (word processing, spreadsheet, etc.)", ICON_ARROW, CMD_DOCS);
        add_item(ui, ITEM_LINK, L"All files and folders", ICON_ARROW, CMD_ALL);
        add_item(ui, ITEM_LINK, L"Computers or people", ICON_ARROW, CMD_COMPUTERS);
        /* no Help and Support Center on this system */
        add_item(ui, ITEM_LINK, L"Information in Help and Support Center", -24, CMD_DISABLED)->disabled = TRUE;
        add_item(ui, ITEM_TEXT, L"You may also want to...", 0, 0);
        /* Windows' Internet search went through a service that is gone */
        add_item(ui, ITEM_LINK, L"Search the Internet", ICON_INTERNET, CMD_DISABLED)->disabled = TRUE;
        add_item(ui, ITEM_LINK, L"Change preferences", ICON_PREFS, CMD_PREFS);
        add_item(ui, ITEM_LINK, ui->character ? L"Turn off animated character" : L"Turn on animated character",
                 -18, CMD_CHARACTER);
        break;
    case PAGE_MEDIA:
        add_item(ui, ITEM_TITLE, L"Search for all files of a certain type, or search by type and name.", 0, 0);
        add_option(ui, L"Pictures and Photos", IDC_PICTURES, FALSE, ui->media[0], TRUE);
        add_option(ui, L"Music", IDC_MUSIC, FALSE, ui->media[1], FALSE);
        add_option(ui, L"Video", IDC_VIDEO, FALSE, ui->media[2], FALSE);
        add_item(ui, ITEM_GAP, NULL, 0, 0)->height = 6;
        add_label(ui, L"All or part &of the file name:");
        add_control(ui, WC_EDITW, ui->name, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_NAME, 20, 0);
        add_item(ui, ITEM_GAP, NULL, 0, 0)->height = 6;
        add_item(ui, ITEM_LINK, L"Use advanced search options", ICON_ARROW, CMD_ADVANCED);
        break;
    case PAGE_DOCS:
    {
        static const WCHAR *dates[] = { L"Don't remember", L"Within the last week", L"Past month", L"Within the past year" };
        add_item(ui, ITEM_TITLE, L"Last time it was modified:", 0, 0);
        for (i = 0; i < ARRAY_SIZE(dates); i++) add_option(ui, dates[i], IDC_DOCDATE_FIRST + i, TRUE, ui->doc_date == i, !i);
        add_item(ui, ITEM_GAP, NULL, 0, 0)->height = 6;
        add_label(ui, L"All or part of the &document name:");
        add_control(ui, WC_EDITW, ui->name, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_NAME, 20, 0);
        add_item(ui, ITEM_GAP, NULL, 0, 0)->height = 6;
        add_item(ui, ITEM_LINK, L"Use advanced search options", ICON_ARROW, CMD_ADVANCED);
        break;
    }
    case PAGE_ALL:
        build_all_page(ui);
        break;
    case PAGE_COMPUTERS:
        add_item(ui, ITEM_TITLE, L"What are you looking for?", 0, 0);
        /* network browsing and the address book are not part of this system */
        add_item(ui, ITEM_LINK, L"A computer on the network", ICON_COMPUTER, CMD_DISABLED)->disabled = TRUE;
        add_item(ui, ITEM_LINK, L"People in your address book", ICON_PEOPLE, CMD_DISABLED)->disabled = TRUE;
        break;
    case PAGE_SEARCHING:
        add_item(ui, ITEM_TITLE, L"Searching...", 0, 0);
        add_item(ui, ITEM_TEXT, ui->searching, 0, 1);   /* the folder, on one line */
        break;
    case PAGE_RESULTS:
        if (!ui->found) lstrcpyW(text, L"Search is complete. There are no results to display.");
        else if (ui->found == 1) lstrcpyW(text, L"There was 1 file found. Did you find what you wanted?");
        else swprintf(text, ARRAY_SIZE(text), L"There were %d files found. Did you find what you wanted?", ui->found);
        add_item(ui, ITEM_TITLE, text, 0, 0);
        if (ui->found)
        {
            add_item(ui, ITEM_LINK, L"Yes, finished searching", ICON_ARROW, CMD_FINISHED);
            /* comes with the Indexing Service */
            add_item(ui, ITEM_LINK, L"Yes, but make future searches faster", ICON_ARROW, CMD_DISABLED)->disabled = TRUE;
        }
        add_item(ui, ITEM_TITLE, ui->found ? L"No, refine this search and..." : L"Refine this search and...", 0, 0);
        add_item(ui, ITEM_LINK, L"Change file name or keywords", ICON_ARROW, CMD_REFINE_NAME);
        add_item(ui, ITEM_LINK, L"Look in more locations", ICON_ARROW, CMD_REFINE_LOOK);
        add_item(ui, ITEM_LINK, L"Change whether hidden and system files are included", ICON_ARROW, CMD_REFINE_HIDDEN);
        it = add_item(ui, ITEM_LINK, L"Start a new search", BMP_NEWSEARCH, CMD_NEW_SEARCH);
        it->bitmap = TRUE;
        break;
    case PAGE_PREFS:
        add_item(ui, ITEM_TITLE, L"How do you want to use Search Companion?", 0, 0);
        add_item(ui, ITEM_LINK, ui->character ? L"Without an animated screen character" : L"With an animated screen character",
                 ICON_ARROW, CMD_CHARACTER);
        add_item(ui, ITEM_LINK, L"With a different character", ICON_ARROW, CMD_CHOOSE_CHARACTER);
        add_item(ui, ITEM_LINK, L"With Indexing Service (for faster local searches)", ICON_ARROW, CMD_DISABLED)->disabled = TRUE;
        add_item(ui, ITEM_LINK, L"Change files and folders search behavior", ICON_ARROW, CMD_DISABLED)->disabled = TRUE;
        add_item(ui, ITEM_LINK, L"Change Internet search behavior", ICON_ARROW, CMD_DISABLED)->disabled = TRUE;
        add_item(ui, ITEM_LINK, L"Don't show balloon tips", ICON_ARROW, CMD_DISABLED)->disabled = TRUE;
        add_item(ui, ITEM_LINK, L"Turn AutoComplete off", ICON_ARROW, CMD_DISABLED)->disabled = TRUE;
        break;
    case PAGE_CHARACTERS:
        add_item(ui, ITEM_TITLE, L"Which character would you like to use?", 0, 0);
        ctl = add_control(ui, CHOOSER_CLASS, NULL, WS_CLIPCHILDREN, IDC_CHOOSER, CHOOSER_HEIGHT, 0);
        SetWindowLongW(ctl, GWL_EXSTYLE, WS_EX_CONTROLPARENT);   /* Tab goes on to its Back and Next */
        break;
    }

    /* the buttons at the foot of the balloon */
    ShowWindow(ui->back, ui->page == PAGE_START || ui->page == PAGE_SEARCHING ? SW_HIDE : SW_SHOW);
    ShowWindow(ui->action, ui->page == PAGE_MEDIA || ui->page == PAGE_DOCS || ui->page == PAGE_ALL ||
                           ui->page == PAGE_SEARCHING || ui->page == PAGE_CHARACTERS ? SW_SHOW : SW_HIDE);
    /* the character chooser has OK and Cancel there */
    SetWindowTextW(ui->back, ui->page == PAGE_CHARACTERS ? L"OK" : L"&Back");
    SetWindowLongPtrW(ui->back, GWLP_ID, ui->page == PAGE_CHARACTERS ? IDC_CHAR_OK : IDC_BACK);
    SetWindowTextW(ui->action, ui->page == PAGE_SEARCHING ? L"Stop" : ui->page == PAGE_CHARACTERS ? L"Cancel" : L"Search");
    SetWindowLongPtrW(ui->action, GWLP_ID, ui->page == PAGE_SEARCHING ? IDC_STOP :
                                           ui->page == PAGE_CHARACTERS ? IDC_CHAR_CANCEL : IDC_SEARCH);
    bold_checked(ui);
}

/* Windows writes the chosen option in bold */
static void bold_checked(SEARCH_UI *ui)
{
    int i;
    for (i = 0; i < ui->item_count; i++)
    {
        HWND ctl = ui->items[i].ctl;
        WCHAR cls[32];
        LONG style;
        if (!ctl || !GetClassNameW(ctl, cls, ARRAY_SIZE(cls)) || wcsicmp(cls, WC_BUTTONW)) continue;
        style = GetWindowLongW(ctl, GWL_STYLE) & BS_TYPEMASK;
        if (style != BS_AUTORADIOBUTTON && style != BS_AUTOCHECKBOX) continue;
        SendMessageW(ctl, WM_SETFONT, (WPARAM)(SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED ? ui->bold : ui->font), TRUE);
    }
    /* the dates and the size only apply when they are asked for */
    EnableWindow(GetDlgItem(ui->content, IDC_DATE_KIND), is_checked(ui, IDC_DATE_FIRST + 4));
    EnableWindow(GetDlgItem(ui->content, IDC_DATE_FROM), is_checked(ui, IDC_DATE_FIRST + 4));
    EnableWindow(GetDlgItem(ui->content, IDC_DATE_TO), is_checked(ui, IDC_DATE_FIRST + 4));
    EnableWindow(GetDlgItem(ui->content, IDC_SIZE_HOW), is_checked(ui, IDC_SIZE_FIRST + 4));
    EnableWindow(GetDlgItem(ui->content, IDC_SIZE_KB), is_checked(ui, IDC_SIZE_FIRST + 4));
    EnableWindow(GetDlgItem(ui->content, IDC_SIZE_SPIN), is_checked(ui, IDC_SIZE_FIRST + 4));
}

/* ---- layout and painting ---- */

static int text_height(SEARCH_UI *ui, HDC hdc, const WCHAR *text, int width, BOOL bold)
{
    RECT rc = { 0, 0, width, 0 };
    HFONT old = SelectObject(hdc, bold ? ui->bold : ui->font);
    DrawTextW(hdc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old);
    return rc.bottom;
}

/* places the items in the content window; returns the height they need */
static int layout_items(SEARCH_UI *ui, int width)
{
    HDC hdc = GetDC(ui->content);
    int y = 16 - ui->scroll, i, left = TEXT_LEFT, right = width - TEXT_RIGHT;

    for (i = 0; i < ui->item_count; i++)
    {
        ITEM *it = &ui->items[i];
        int h;

        switch (it->type)
        {
        case ITEM_TITLE:
            if (i) y += 6;
            h = text_height(ui, hdc, it->text, right - left, TRUE);
            SetRect(&it->rc, left, y, right, y + h);
            y += h + 8;
            break;
        case ITEM_TEXT:
            y += 4;
            h = it->cmd ? 13 : text_height(ui, hdc, it->text, right - left, FALSE);
            SetRect(&it->rc, left, y, right, y + h);
            y += h + 8;
            break;
        case ITEM_LINK:
            h = max(16, text_height(ui, hdc, it->text, right - left - 20, FALSE));
            SetRect(&it->rc, left, y, right, y + h);
            y += h + LINE_GAP;
            break;
        case ITEM_LABEL:
            SetRect(&it->rc, left + it->indent, y, right, y + 13);
            y += 14;
            break;
        case ITEM_SECTION:
            h = max(19, text_height(ui, hdc, it->text, right - left - 24, TRUE));
            SetRect(&it->rc, left, y, right, y + h);
            y += h + 7;
            break;
        case ITEM_GAP:
            y += it->height;
            break;
        case ITEM_CONTROL:
        {
            WCHAR cls[32];
            int x = left + max(it->indent, 0), w = right - x, hgt = it->height;

            if (it->height < 0) break;   /* placed with its buddy */
            GetClassNameW(it->ctl, cls, ARRAY_SIZE(cls));
            if (!wcsicmp(cls, WC_COMBOBOXW) || !wcsicmp(cls, WC_COMBOBOXEXW)) hgt = 21;
            if (it->indent == -1) { x = left + 16; w = 60; }   /* size in KB */
            if (it->indent == 48) { x = left + 48; w = right - x; }
            if (it->indent == 16 && (!wcsicmp(cls, WC_COMBOBOXW))) w = right - x;
            if (!wcsicmp(cls, WC_COMBOBOXW) || !wcsicmp(cls, WC_COMBOBOXEXW))
                SetWindowPos(it->ctl, NULL, x, y, w, it->height, SWP_NOZORDER | SWP_NOACTIVATE);
            else
                SetWindowPos(it->ctl, NULL, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
            if (it->indent == -1)
            {
                /* the spin box follows the edit */
                HWND spin = GetDlgItem(ui->content, IDC_SIZE_SPIN);
                SendMessageW(spin, UDM_SETBUDDY, (WPARAM)it->ctl, 0);
            }
            /* "from"/"to" share the line with their picker */
            if (it->indent == 48 && i && ui->items[i - 1].type == ITEM_LABEL)
            {
                OffsetRect(&ui->items[i - 1].rc, 0, y - ui->items[i - 1].rc.top + 3);
            }
            y += hgt + ((wcsicmp(cls, WC_BUTTONW)) ? 9 : 0);
            break;
        }
        }
        /* a label right before a date picker is on the picker's line */
        if (it->type == ITEM_LABEL && i + 1 < ui->item_count && ui->items[i + 1].indent == 48) y -= 14;
    }
    ReleaseDC(ui->content, hdc);
    return y + ui->scroll + 8;
}

static int balloon_bottom(SEARCH_UI *ui, int height)
{
    return height - (ui->character ? TAIL_SPACE : 8);
}

/* ---- the animated character ---- */

static RECT character_rect(SEARCH_UI *ui)
{
    RECT rc, frame = {0};
    SIZE size;

    if (!ui->agent) return frame;
    size = agent_size(ui->agent);
    GetClientRect(ui->pane, &rc);
    SetRect(&frame, CHARACTER_X, rc.bottom - CHARACTER_BOTTOM - size.cy, CHARACTER_X + size.cx, rc.bottom - CHARACTER_BOTTOM);
    return frame;
}

/* move the animation on and come back when the next frame is due */
static void character_tick(SEARCH_UI *ui)
{
    BOOL changed;
    DWORD next;

    if (!ui->agent || !ui->character || !ui->pane_visible)
    {
        KillTimer(ui->pane, IDT_CHARACTER);
        return;
    }
    next = agent_advance(ui->agent, &changed);
    if (changed)
    {
        RECT rc = character_rect(ui);
        InvalidateRect(ui->pane, &rc, FALSE);
    }
    SetTimer(ui->pane, IDT_CHARACTER, next == INFINITE ? 1000 : next, NULL);
}

static void character_play(SEARCH_UI *ui, const WCHAR *name, const WCHAR *otherwise)
{
    if (!ui->agent || !ui->character) return;
    if (!agent_play(ui->agent, name) && otherwise) agent_play(ui->agent, otherwise);
    character_tick(ui);
}

/* The characters of Windows' Search Companion (%WINDIR%\srchasst\chars); the one chosen, else Rover */
static void load_character(SEARCH_UI *ui)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH], file[MAX_PATH] = L"rover.acs";
    DWORD size = sizeof(file);

    agent_close(ui->agent);
    ui->agent = NULL;
    if (!ui->character) return;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\Search", L"CharacterFile", RRF_RT_REG_SZ, NULL, file, &size);
    GetWindowsDirectoryW(dir, MAX_PATH);
    swprintf(path, MAX_PATH, L"%s\\srchasst\\chars\\%s", dir, file);
    if (!(ui->agent = agent_open(path)) && wcsicmp(file, L"rover.acs"))
    {
        swprintf(path, MAX_PATH, L"%s\\srchasst\\chars\\rover.acs", dir);
        ui->agent = agent_open(path);
    }
    character_play(ui, L"Show", NULL);
}

/* ---- "Which character would you like to use?" ---- */


/* the characters there are: Search Companion's own, and Microsoft Agent's */
static void list_characters(SEARCH_UI *ui)
{
    static const WCHAR *const dirs[] = { L"srchasst\\chars", L"msagent\\chars" };
    WCHAR windir[MAX_PATH], current[MAX_PATH] = L"rover.acs", pattern[MAX_PATH];
    DWORD size = sizeof(current);
    WIN32_FIND_DATAW fd;
    HANDLE find;
    unsigned int i;

    RegGetValueW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\Search", L"CharacterFile", RRF_RT_REG_SZ, NULL, current, &size);
    GetWindowsDirectoryW(windir, MAX_PATH);
    ui->character_count = ui->character_choice = 0;
    for (i = 0; i < ARRAY_SIZE(dirs); i++)
    {
        swprintf(pattern, MAX_PATH, L"%s\\%s\\*.acs", windir, dirs[i]);
        if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) continue;
        do
        {
            if (ui->character_count == ARRAY_SIZE(ui->characters)) break;
            if (!wcsicmp(fd.cFileName, current)) ui->character_choice = ui->character_count;
            swprintf(ui->characters[ui->character_count++], MAX_PATH, L"%s\\%s\\%s", windir, dirs[i], fd.cFileName);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
}

static RECT preview_rect(SEARCH_UI *ui, HWND chooser)
{
    RECT rc, frame = {0};
    SIZE size;

    if (!ui->preview) return frame;
    size = agent_size(ui->preview);
    GetClientRect(chooser, &rc);
    frame.left = (rc.right - size.cx) / 2;
    frame.top = 40 + max(0, (rc.bottom - 40 - 34 - size.cy) / 2);
    frame.right = frame.left + size.cx;
    frame.bottom = frame.top + size.cy;
    return frame;
}

/* the greeting balloon points at the preview's head */
static void place_greeting(SEARCH_UI *ui, HWND chooser)
{
    RECT frame = preview_rect(ui, chooser);
    POINT pt = { frame.right - 12, frame.top + (frame.bottom - frame.top) / 3 };

    ClientToScreen(chooser, &pt);
    SendMessageW(ui->tip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
}

/* show the character of the current choice, with its greeting in a balloon */
static void show_preview(SEARCH_UI *ui, HWND chooser)
{
    WCHAR name[64], description[256], greeting[256];
    TTTOOLINFOW ti = { sizeof(ti), TTF_TRACK | TTF_ABSOLUTE, chooser, 1 };

    agent_close(ui->preview);
    ui->preview = ui->character_count ? agent_open(ui->characters[ui->character_choice]) : NULL;
    if (ui->preview)
    {
        agent_play(ui->preview, L"Show");
        SetTimer(chooser, IDT_PREVIEW, 10, NULL);
    }
    InvalidateRect(chooser, NULL, TRUE);
    EnableWindow(GetDlgItem(chooser, IDC_CHAR_PREV), ui->character_choice > 0);
    EnableWindow(GetDlgItem(chooser, IDC_CHAR_NEXT), ui->character_choice < ui->character_count - 1);

    SendMessageW(ui->tip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
    if (!ui->preview || !agent_names(ui->preview, name, description, greeting, ARRAY_SIZE(greeting)) || !greeting[0]) return;
    ti.lpszText = greeting;
    SendMessageW(ui->tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    place_greeting(ui, chooser);
    SendMessageW(ui->tip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    SetTimer(chooser, IDT_GREETING, 6000, NULL);
}

static LRESULT CALLBACK chooser_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SEARCH_UI *ui = pane_ui(GetParent(GetParent(hwnd)));

    if (!ui) return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg)
    {
    case WM_CREATE:
    {
        TTTOOLINFOW ti = { sizeof(ti), TTF_TRACK | TTF_ABSOLUTE, hwnd, 1 };
        HWND button;
        int i;

        for (i = 0; i < 2; i++)
        {
            button = CreateWindowExW(0, WC_BUTTONW, i ? L"Next" : L"&Back", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 58, 22, hwnd, (HMENU)(UINT_PTR)(i ? IDC_CHAR_NEXT : IDC_CHAR_PREV), wexsearch_module(), NULL);
            SendMessageW(button, WM_SETFONT, (WPARAM)ui->font, FALSE);
        }
        ui->tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_BALLOON | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                  0, 0, 0, 0, hwnd, NULL, wexsearch_module(), NULL);
        ti.lpszText = (WCHAR *)L"";
        SendMessageW(ui->tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(ui->tip, TTM_SETMAXTIPWIDTH, 0, 180);
        list_characters(ui);
        show_preview(ui, hwnd);
        return 0;
    }
    case WM_MOVE:   /* scrolled with the balloon */
        if (IsWindowVisible(ui->tip)) place_greeting(ui, hwnd);
        return 0;
    case WM_SIZE:
        SetWindowPos(GetDlgItem(hwnd, IDC_CHAR_PREV), NULL, 6, HIWORD(lparam) - 28, 58, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(hwnd, IDC_CHAR_NEXT), NULL, LOWORD(lparam) - 64, HIWORD(lparam) - 28, 58, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_PAINT:
    {
        WCHAR name[64], description[256], greeting[256];
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc, band;
        HBRUSH brush;
        HFONT old;

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetStockObject(WHITE_BRUSH));
        brush = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
        FrameRect(hdc, &rc, brush);
        DeleteObject(brush);
        /* the name on a band of the selection color */
        SetRect(&band, 10, 10, rc.right - 10, 34);
        FillRect(hdc, &band, GetSysColorBrush(COLOR_HIGHLIGHT));
        if (ui->preview && agent_names(ui->preview, name, description, greeting, ARRAY_SIZE(greeting)))
        {
            old = SelectObject(hdc, ui->name_font ? ui->name_font : ui->bold);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            DrawTextW(hdc, name, -1, &band, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, old);
        }
        if (ui->preview)
        {
            RECT frame = preview_rect(ui, hwnd);
            agent_draw(ui->preview, hdc, frame.left, frame.top);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wparam == IDT_GREETING)
        {
            TTTOOLINFOW ti = { sizeof(ti), 0, hwnd, 1 };
            KillTimer(hwnd, IDT_GREETING);
            SendMessageW(ui->tip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        }
        else if (wparam == IDT_PREVIEW && ui->preview)
        {
            BOOL changed;
            DWORD next = agent_advance(ui->preview, &changed);
            if (changed)
            {
                RECT frame = preview_rect(ui, hwnd);
                InvalidateRect(hwnd, &frame, TRUE);
            }
            SetTimer(hwnd, IDT_PREVIEW, next == INFINITE ? 1000 : next, NULL);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_CHAR_PREV && ui->character_choice > 0) ui->character_choice--;
        else if (LOWORD(wparam) == IDC_CHAR_NEXT && ui->character_choice < ui->character_count - 1) ui->character_choice++;
        else return 0;
        show_preview(ui, hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_PREVIEW);
        KillTimer(hwnd, IDT_GREETING);
        DestroyWindow(ui->tip);
        ui->tip = NULL;
        agent_close(ui->preview);
        ui->preview = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void save_character(SEARCH_UI *ui);

/* OK in the chooser: the chosen character takes over */
static void choose_character(SEARCH_UI *ui)
{
    if (ui->character_count)
    {
        const WCHAR *file = PathFindFileNameW(ui->characters[ui->character_choice]);
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\Search", L"CharacterFile", REG_SZ, file,
                        (lstrlenW(file) + 1) * sizeof(WCHAR));
        ui->character = TRUE;
        save_character(ui);
        load_character(ui);
    }
}

static BOOL has_buttons(SEARCH_UI *ui)
{
    return IsWindowVisible(ui->back) || IsWindowVisible(ui->action);
}

/* the balloon, its content window and the buttons for the pane size */
static void layout_pane(SEARCH_UI *ui)
{
    RECT rc;
    int bottom, top, inner_w, buttons = 0, content_h, avail;
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };

    GetClientRect(ui->pane, &rc);
    bottom = balloon_bottom(ui, rc.bottom);
    inner_w = PANE_WIDTH - 2 * BALLOON_X - 2;
    if (has_buttons(ui)) buttons = 39;

    /* the balloon grows upwards with its content, up to just below the caption, then the content scrolls */
    ui->scroll = 0;
    ui->content_height = layout_items(ui, inner_w);
    avail = bottom - (CAPTION + 1 + 6) - 2 - buttons;
    content_h = min(ui->content_height, max(avail, 20));
    top = bottom - content_h - 2 - buttons;
    SetWindowPos(ui->content, NULL, BALLOON_X + 1, top + 1, inner_w, content_h, SWP_NOZORDER | SWP_NOACTIVATE);
    si.nMin = 0;
    si.nMax = ui->content_height - 1;
    si.nPage = content_h;
    si.nPos = 0;
    SetScrollInfo(ui->content, SB_VERT, &si, TRUE);
    ShowScrollBar(ui->content, SB_VERT, ui->content_height > content_h);
    if (ui->content_height > content_h) layout_items(ui, inner_w - GetSystemMetrics(SM_CXVSCROLL));

    /* no copied bits: a button moved where the other one was would keep its picture */
    if (IsWindowVisible(ui->back) && IsWindowVisible(ui->action))
    {
        SetWindowPos(ui->back, NULL, 26, bottom - 31, 72, 22, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        SetWindowPos(ui->action, NULL, 106, bottom - 31, 72, 22, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
    else
    {
        SetWindowPos(ui->back, NULL, 106, bottom - 31, 72, 22, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        SetWindowPos(ui->action, NULL, 106, bottom - 31, 72, 22, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
    InvalidateRect(ui->pane, NULL, TRUE);
    InvalidateRect(ui->content, NULL, TRUE);
}

static void paint_pane(SEARCH_UI *ui, HDC hdc)
{
    HTHEME theme = themed() ? OpenThemeData(ui->pane, L"ExplorerBar") : NULL;
    RECT rc, cap, body, content;
    HBRUSH brush;
    HPEN pen, old_pen;
    int bottom;
    COLORREF face = GetSysColor(COLOR_BTNFACE);
    COLORREF caption_color = RGB(GetRValue(face) + (255 - GetRValue(face)) * 3 / 10,
                                 GetGValue(face) + (255 - GetGValue(face)) * 3 / 10,
                                 GetBValue(face) + (255 - GetBValue(face)) * 3 / 10);

    GetClientRect(ui->pane, &rc);
    /* caption strip with the close button, like every Explorer bar */
    SetRect(&cap, 0, 0, PANE_WIDTH, CAPTION);
    brush = CreateSolidBrush(caption_color);
    FillRect(hdc, &cap, brush);
    DeleteObject(brush);
    pen = CreatePen(PS_SOLID, 1, RGB(GetRValue(face) * 73 / 100, GetGValue(face) * 72 / 100, GetBValue(face) * 71 / 100));
    old_pen = SelectObject(hdc, pen);
    MoveToEx(hdc, 0, CAPTION, NULL);
    LineTo(hdc, PANE_WIDTH, CAPTION);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
    {
        HFONT old = SelectObject(hdc, ui->font), marlett;
        RECT text = { 6, 0, PANE_WIDTH - 24, CAPTION }, close = { PANE_WIDTH - 22, 3, PANE_WIDTH - 6, CAPTION - 3 };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        DrawTextW(hdc, L"Search Companion", -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        marlett = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, SYMBOL_CHARSET, 0, 0, 0, 0, L"Marlett");
        SelectObject(hdc, marlett);
        DrawTextW(hdc, L"r", 1, &close, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        SelectObject(hdc, old);
        DeleteObject(marlett);
    }

    /* the body: the Explorer bar gradient */
    SetRect(&body, 0, CAPTION + 1, PANE_WIDTH, rc.bottom);
    if (theme) DrawThemeBackground(theme, hdc, 0, 0, &body, NULL);
    else FillRect(hdc, &body, GetSysColorBrush(COLOR_BTNFACE));
    /* the splitter between the pane and the view */
    SetRect(&body, PANE_WIDTH, 0, rc.right, rc.bottom);
    FillRect(hdc, &body, GetSysColorBrush(COLOR_BTNFACE));

    /* the balloon: rounded, white edge, with its tail to the character */
    GetWindowRect(ui->content, &content);
    MapWindowPoints(NULL, ui->pane, (POINT *)&content, 2);
    bottom = balloon_bottom(ui, rc.bottom);
    brush = CreateSolidBrush(balloon_color());
    pen = CreatePen(PS_SOLID, 1, themed() ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWFRAME));
    old_pen = SelectObject(hdc, pen);
    SelectObject(hdc, brush);
    RoundRect(hdc, BALLOON_X, content.top - 1, PANE_WIDTH - BALLOON_X, bottom + 1, 18, 18);
    if (ui->character)
    {
        POINT tail[3] = { { 21, bottom - 1 }, { 28, bottom - 1 }, { 33, bottom + 19 } };
        Polygon(hdc, tail, 3);
        /* the edge of the balloon is open where the tail joins */
        SelectObject(hdc, GetStockObject(NULL_PEN));
        {
            HPEN fill = CreatePen(PS_SOLID, 1, balloon_color()), prev = SelectObject(hdc, fill);
            MoveToEx(hdc, 22, bottom, NULL);
            LineTo(hdc, 28, bottom);
            SelectObject(hdc, prev);
            DeleteObject(fill);
        }
        if (ui->agent)
        {
            SIZE size = agent_size(ui->agent);
            agent_draw(ui->agent, hdc, CHARACTER_X, rc.bottom - CHARACTER_BOTTOM - size.cy);
        }
        else if (ui->rover)
        {
            BITMAP bm;
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP prev = SelectObject(mem, ui->rover);
            GetObjectW(ui->rover, sizeof(bm), &bm);
            TransparentBlt(hdc, 53, rc.bottom - bm.bmHeight - 7, bm.bmWidth, bm.bmHeight, mem, 0, 0, bm.bmWidth,
                           bm.bmHeight, RGB(255, 0, 255));
            SelectObject(mem, prev);
            DeleteDC(mem);
        }
    }
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
    DeleteObject(brush);
    if (theme) CloseThemeData(theme);
}

static void paint_content(SEARCH_UI *ui, HDC hdc)
{
    RECT rc;
    HBRUSH brush = CreateSolidBrush(balloon_color());
    int i;

    GetClientRect(ui->content, &rc);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
    SetBkMode(hdc, TRANSPARENT);
    for (i = 0; i < ui->item_count; i++)
    {
        ITEM *it = &ui->items[i];
        RECT text = it->rc;

        switch (it->type)
        {
        case ITEM_TITLE:
        case ITEM_SECTION:
            SelectObject(hdc, ui->bold);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            if (it->type == ITEM_SECTION) text.right -= 24;
            if (it->type == ITEM_SECTION && text_height(ui, hdc, it->text, text.right - text.left, TRUE) < 19)
                DrawTextW(hdc, it->text, -1, &text, DT_NOPREFIX | DT_VCENTER | DT_SINGLELINE);
            else DrawTextW(hdc, it->text, -1, &text, DT_WORDBREAK | DT_NOPREFIX);   /* long titles wrap, like Windows */
            if (it->type == ITEM_SECTION)
            {
                /* the round button that opens and closes the section */
                BOOL open = ui->expanded[it->indent];
                HTHEME theme = themed() ? OpenThemeData(ui->content, L"ExplorerBar") : NULL;
                int x = it->rc.right - 19, y = it->rc.top + (it->rc.bottom - it->rc.top - 19) / 2;
                RECT arrow = { x, y, x + 19, y + 19 };
                if (theme)
                {
                    DrawThemeBackground(theme, hdc, open ? EBP_NORMALGROUPCOLLAPSE : EBP_NORMALGROUPEXPAND,
                                        ui->hot == i ? 2 : 1, &arrow, NULL);
                    CloseThemeData(theme);
                }
                else DrawFrameControl(hdc, &arrow, DFC_SCROLL, open ? DFCS_SCROLLUP : DFCS_SCROLLDOWN);
            }
            break;
        case ITEM_TEXT:
        case ITEM_LABEL:
            SelectObject(hdc, ui->font);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            if (it->type == ITEM_LABEL) DrawTextW(hdc, it->text, -1, &text, DT_SINGLELINE);   /* with its access key */
            else DrawTextW(hdc, it->text, -1, &text, it->cmd ? DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS
                                                             : DT_WORDBREAK | DT_NOPREFIX);
            break;
        case ITEM_LINK:
        {
            BOOL hot = i == ui->hot && !it->disabled;
            draw_link_picture(ui, hdc, it, it->rc.left, it->rc.top);
            text.left += 20;
            SelectObject(hdc, ui->font);
            SetTextColor(hdc, it->disabled ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_WINDOWTEXT));
            if (hot)
            {
                LOGFONTW lf;
                HFONT under;
                GetObjectW(ui->font, sizeof(lf), &lf);
                lf.lfUnderline = TRUE;
                under = CreateFontIndirectW(&lf);
                SelectObject(hdc, under);
                DrawTextW(hdc, it->text, -1, &text, DT_WORDBREAK | DT_NOPREFIX);
                SelectObject(hdc, ui->font);
                DeleteObject(under);
            }
            else DrawTextW(hdc, it->text, -1, &text, DT_WORDBREAK | DT_NOPREFIX);
            break;
        }
        }
    }
}

static int hit_item(SEARCH_UI *ui, int x, int y)
{
    POINT pt = { x, y };
    int i;
    for (i = 0; i < ui->item_count; i++)
        if ((ui->items[i].type == ITEM_LINK || ui->items[i].type == ITEM_SECTION) && PtInRect(&ui->items[i].rc, pt))
            return i;
    return -1;
}

/* ---- the search ---- */

static void notify_parent(SEARCH_UI *ui, int what)
{
    PostMessageW(ui->parent, WM_SEARCHUI_CHANGED, what, 0);
}

static void go_page(SEARCH_UI *ui, int page, BOOL remember)
{
    save_page(ui);
    if (remember && ui->depth < ARRAY_SIZE(ui->history)) ui->history[ui->depth++] = ui->page;
    ui->page = page;
    build_page(ui);
    layout_pane(ui);
}

static FILETIME days_ago(int days)
{
    FILETIME ft;
    ULARGE_INTEGER t;
    GetSystemTimeAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    t.QuadPart -= (ULONGLONG)days * 24 * 3600 * 10000000;
    ft.dwLowDateTime = t.LowPart;
    ft.dwHighDateTime = t.HighPart;
    return ft;
}

static FILETIME day_bound(const SYSTEMTIME *st, BOOL end)
{
    SYSTEMTIME day = *st;
    FILETIME local, ft;
    day.wHour = end ? 23 : 0;
    day.wMinute = end ? 59 : 0;
    day.wSecond = end ? 59 : 0;
    day.wMilliseconds = end ? 999 : 0;
    SystemTimeToFileTime(&day, &local);
    LocalFileTimeToFileTime(&local, &ft);
    return ft;
}

static void start_search(SEARCH_UI *ui)
{
    static const WCHAR media_types[3][128] =
    {
        L".bmp;.jpg;.jpeg;.jpe;.gif;.png;.tif;.tiff;.ico;.wmf;.emf;.pcx;",
        L".mp3;.wma;.wav;.mid;.midi;.rmi;.ogg;.flac;.m4a;.aac;.aif;.aiff;.au;.snd;",
        L".avi;.wmv;.mpg;.mpeg;.mp4;.mkv;.mov;.asf;.m1v;.mpe;.webm;",
    };
    SEARCH_CRITERIA c;
    LOOK_IN *look;
    int i, from = ui->page;

    save_page(ui);
    search_criteria_init(&c);
    lstrcpynW(c.names, ui->name, ARRAY_SIZE(c.names));
    look = (ui->look >= 0 && ui->look < ui->look_count) ? &ui->looks[ui->look] : NULL;
    if (from == PAGE_ALL)
    {
        int days[] = { 0, 7, 31, 366 };
        lstrcpynW(c.text, ui->text, ARRAY_SIZE(c.text));
        if (ui->date_choice >= 1 && ui->date_choice <= 3)
        {
            c.date_kind = SEARCH_DATE_MODIFIED;
            c.date_from = days_ago(days[ui->date_choice]);
            GetSystemTimeAsFileTime(&c.date_to);
            c.date_to.dwHighDateTime += 1;   /* and later */
        }
        else if (ui->date_choice == 4)
        {
            c.date_kind = SEARCH_DATE_MODIFIED + max(ui->date_kind, 0);
            c.date_from = day_bound(&ui->date_from, FALSE);
            c.date_to = day_bound(&ui->date_to, TRUE);
        }
        switch (ui->size_choice)
        {
        case 1: c.size_max = 100 * 1024; break;
        case 2: c.size_max = 1024 * 1024; break;
        case 3: c.size_min = 1024 * 1024 + 1; break;
        case 4:
            if (ui->size_how == 1) c.size_max = (ULONGLONG)ui->size_kb * 1024 + 1023;
            else c.size_min = (ULONGLONG)ui->size_kb * 1024;
            if (ui->size_how == 1 && !ui->size_kb) c.size_max = 1;
            break;
        }
        if (ui->type > 0 && ui->type - 1 < ui->type_count) lstrcpynW(c.extensions, ui->types[ui->type - 1].roots[0], ARRAY_SIZE(c.extensions));
        c.system_folders = ui->system;
        c.hidden = ui->hidden;
        c.subfolders = ui->subfolders;
        c.case_sensitive = ui->case_sensitive;
    }
    else if (from == PAGE_MEDIA)
    {
        for (i = 0; i < 3; i++) if (ui->media[i]) lstrcatW(c.extensions, media_types[i]);
        if (!c.extensions[0]) for (i = 0; i < 3; i++) lstrcatW(c.extensions, media_types[i]);
        /* pictures, music and video: on the hard drives, like Windows */
    }
    else if (from == PAGE_DOCS)
    {
        int days[] = { 0, 7, 31, 366 };
        lstrcpyW(c.extensions, L".doc;.docx;.dot;.rtf;.wri;.txt;.xls;.xlsx;.csv;.ppt;.pptx;.pps;.pdf;.odt;.ods;.odp;.htm;.html;.xml;");
        if (ui->doc_date >= 1)
        {
            c.date_kind = SEARCH_DATE_MODIFIED;
            c.date_from = days_ago(days[ui->doc_date]);
            GetSystemTimeAsFileTime(&c.date_to);
            c.date_to.dwHighDateTime += 1;
        }
    }
    if (look && from == PAGE_ALL)
    {
        for (i = 0; i < look->count; i++) lstrcpyW(c.roots[i], look->roots[i]);
        c.root_count = look->count;
    }
    else
    {
        if (ui->scope[0] && from != PAGE_ALL) { lstrcpyW(c.roots[0], ui->scope); c.root_count = 1; }
        else c.root_count = search_local_drives(c.roots, SEARCH_MAX_ROOTS);
    }

    if (ui->job) search_stop(ui->job);
    ui->found = 0;
    ui->job_id++;
    ui->searching[0] = 0;
    search_results_clear(ui->results, FALSE);
    if (!ui->results_visible)
    {
        ui->results_visible = TRUE;
        notify_parent(ui, SEARCHUI_RESULTS_SHOWN);
    }
    ui->job = search_start(&c, ui->pane, ui->job_id);
    ui->history[0] = from;   /* Back from the results returns to the criteria */
    ui->depth = 1;
    ui->page = PAGE_SEARCHING;
    build_page(ui);
    layout_pane(ui);
    character_play(ui, from == PAGE_MEDIA ? L"ImageSearching" : L"Searching", L"Searching");
}

static void search_finished(SEARCH_UI *ui)
{
    if (ui->job) search_stop(ui->job);
    ui->job = NULL;
    search_results_done(ui->results);
    ui->page = PAGE_RESULTS;
    build_page(ui);
    layout_pane(ui);
    notify_parent(ui, SEARCHUI_SEARCH_DONE);
    character_play(ui, ui->found ? L"Pleased" : L"Embarrassed", L"RestPose");
}

static void save_character(SEARCH_UI *ui)
{
    DWORD value = ui->character;
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\Search", L"Character", REG_DWORD, &value, sizeof(value));
}

static void run_command(SEARCH_UI *ui, UINT cmd)
{
    switch (cmd)
    {
    case CMD_MEDIA: go_page(ui, PAGE_MEDIA, TRUE); break;
    case CMD_DOCS: go_page(ui, PAGE_DOCS, TRUE); break;
    case CMD_ALL:
    case CMD_ADVANCED: go_page(ui, PAGE_ALL, TRUE); break;
    case CMD_COMPUTERS: go_page(ui, PAGE_COMPUTERS, TRUE); break;
    case CMD_PREFS: go_page(ui, PAGE_PREFS, TRUE); break;
    case CMD_CHOOSE_CHARACTER: go_page(ui, PAGE_CHARACTERS, TRUE); break;
    case CMD_CHARACTER:
        ui->character = !ui->character;
        save_character(ui);
        load_character(ui);
        build_page(ui);
        layout_pane(ui);
        break;
    case CMD_FINISHED:
        search_ui_show_pane(ui, FALSE);
        break;
    case CMD_REFINE_NAME:
    case CMD_REFINE_LOOK:
    case CMD_REFINE_HIDDEN:
        if (cmd == CMD_REFINE_HIDDEN) ui->expanded[2] = TRUE;
        ui->depth = 0;
        go_page(ui, PAGE_ALL, FALSE);
        SetFocus(GetDlgItem(ui->content, cmd == CMD_REFINE_LOOK ? IDC_LOOK : cmd == CMD_REFINE_NAME ? IDC_NAME : IDC_HIDDEN));
        break;
    case CMD_NEW_SEARCH:
        ui->name[0] = ui->text[0] = 0;
        ui->depth = 0;
        search_results_clear(ui->results, FALSE);
        go_page(ui, PAGE_START, FALSE);
        break;
    case CMD_SECTION_DATE:
    case CMD_SECTION_SIZE:
    case CMD_SECTION_MORE:
        save_page(ui);
        ui->expanded[cmd - CMD_SECTION_DATE] = !ui->expanded[cmd - CMD_SECTION_DATE];
        build_page(ui);
        layout_pane(ui);
        break;
    }
}

static void go_back(SEARCH_UI *ui)
{
    if (ui->page == PAGE_SEARCHING) return;
    save_page(ui);
    ui->page = ui->depth ? ui->history[--ui->depth] : PAGE_START;
    build_page(ui);
    layout_pane(ui);
}

/* ---- windows ---- */

static void scroll_content(SEARCH_UI *ui, int pos)
{
    RECT rc;
    GetClientRect(ui->content, &rc);
    pos = max(0, min(pos, ui->content_height - rc.bottom));
    if (pos == ui->scroll) return;
    ui->scroll = pos;
    SetScrollPos(ui->content, SB_VERT, pos, TRUE);
    layout_items(ui, rc.right);
    InvalidateRect(ui->content, NULL, TRUE);
}

static LRESULT CALLBACK content_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SEARCH_UI *ui = pane_ui(GetParent(hwnd));

    if (!ui) return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_content(ui, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
    {
        /* themed check boxes and radio buttons draw the balloon under them */
        RECT rc;
        HBRUSH brush = CreateSolidBrush(balloon_color());
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wparam, &rc, brush);
        DeleteObject(brush);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        static HBRUSH brush;
        static COLORREF color;
        if (!brush || color != balloon_color())
        {
            if (brush) DeleteObject(brush);
            color = balloon_color();
            brush = CreateSolidBrush(color);
        }
        SetBkColor((HDC)wparam, color);
        SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)brush;
    }
    case WM_MOUSEMOVE:
    {
        int hot = hit_item(ui, (short)LOWORD(lparam), (short)HIWORD(lparam));
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        if (hot >= 0 && ui->items[hot].disabled) hot = -1;
        if (hot != ui->hot)
        {
            ui->hot = hot;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (ui->hot >= 0) { ui->hot = -1; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_SETCURSOR:
        if (ui->hot >= 0) { SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONUP:
    {
        int hit = hit_item(ui, (short)LOWORD(lparam), (short)HIWORD(lparam));
        if (hit >= 0 && !ui->items[hit].disabled) run_command(ui, ui->items[hit].cmd);
        return 0;
    }
    case WM_VSCROLL:
    {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        RECT rc;
        int pos = ui->scroll;
        GetScrollInfo(hwnd, SB_VERT, &si);
        GetClientRect(hwnd, &rc);
        switch (LOWORD(wparam))
        {
        case SB_LINEUP: pos -= 16; break;
        case SB_LINEDOWN: pos += 16; break;
        case SB_PAGEUP: pos -= rc.bottom; break;
        case SB_PAGEDOWN: pos += rc.bottom; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        }
        scroll_content(ui, pos);
        return 0;
    }
    case WM_MOUSEWHEEL:
        scroll_content(ui, ui->scroll - GET_WHEEL_DELTA_WPARAM(wparam) / 120 * 32);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_LOOK && HIWORD(wparam) == CBN_SELCHANGE &&
            SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0) == ui->look_count)
            browse_look(ui, (HWND)lparam);
        else if (HIWORD(wparam) == BN_CLICKED) bold_checked(ui);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK pane_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SEARCH_UI *ui = pane_ui(hwnd);

    if (!ui) return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps), mem = CreateCompatibleDC(hdc);
        RECT rc;
        HBITMAP bmp, prev;
        GetClientRect(hwnd, &rc);
        bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        prev = SelectObject(mem, bmp);
        paint_pane(ui, mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, prev);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        paint_pane(ui, (HDC)wparam);   /* what the buttons show around them */
        return 0;
    case WM_SIZE:
        layout_pane(ui);
        return 0;
    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        RECT frame = character_rect(ui);
        if (pt.y < CAPTION && pt.x >= PANE_WIDTH - 24 && pt.x < PANE_WIDTH)
            search_ui_show_pane(ui, FALSE);
        else if (PtInRect(&frame, pt) && agent_hit(ui->agent, pt.x - frame.left, pt.y - frame.top))
            character_play(ui, L"ClickedOn", NULL);
        return 0;
    }
    case WM_TIMER:
        if (wparam == IDT_CHARACTER) character_tick(ui);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_BACK: go_back(ui); break;
        case IDOK:   /* Enter in a field */
            if (IsWindowVisible(ui->action) && GetWindowLongPtrW(ui->action, GWLP_ID) == IDC_SEARCH) start_search(ui);
            else if (ui->page == PAGE_CHARACTERS) { choose_character(ui); go_back(ui); }
            break;
        case IDC_SEARCH: start_search(ui); break;
        case IDC_STOP: search_finished(ui); break;
        case IDC_CHAR_OK: choose_character(ui); go_back(ui); break;
        case IDC_CHAR_CANCEL: go_back(ui); break;
        }
        return 0;
    case WM_SEARCH_RESULT:
        if (wparam == ui->job_id && ui->job)
        {
            ui->found++;
            search_results_add(ui->results, (SEARCH_RESULT *)lparam);
        }
        else free((void *)lparam);
        return 0;
    case WM_SEARCH_FOLDER:
        if (wparam == ui->job_id && ui->page == PAGE_SEARCHING)
        {
            lstrcpynW(ui->searching, (WCHAR *)lparam, MAX_PATH);
            if (ui->item_count > 1) lstrcpynW(ui->items[1].text, ui->searching, ARRAY_SIZE(ui->items[1].text));
            InvalidateRect(ui->content, NULL, FALSE);
        }
        free((void *)lparam);
        return 0;
    case WM_SEARCH_DONE:
        if (wparam == ui->job_id && ui->job) search_finished(ui);
        return 0;
    case WM_THEMECHANGED:
        layout_pane(ui);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ---- API ---- */

SEARCH_UI *search_ui_create(HWND parent, const WCHAR *scope, BOOL results)
{
    WNDCLASSW wc = {0};
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    WCHAR dir[MAX_PATH];
    SEARCH_UI *ui = calloc(1, sizeof(*ui));
    DWORD value = 1, size = sizeof(value);
    SYSTEMTIME now;

    if (!ui) return NULL;
    wc.lpfnWndProc = pane_proc;
    wc.hInstance = wexsearch_module();
    wc.lpszClassName = PANE_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);
    wc.lpfnWndProc = content_proc;
    wc.lpszClassName = CONTENT_CLASS;
    RegisterClassW(&wc);
    wc.lpfnWndProc = chooser_proc;
    wc.lpszClassName = CHOOSER_CLASS;
    RegisterClassW(&wc);

    ui->parent = parent;
    ui->look = -1;
    ui->system = ui->subfolders = TRUE;
    GetLocalTime(&now);
    ui->date_from = ui->date_to = now;
    if (scope) lstrcpynW(ui->scope, scope, MAX_PATH);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\Search", L"Character", RRF_RT_REG_DWORD, NULL, &value, &size);
    ui->character = value != 0;

    /* the Search Companion's own pictures, where Windows installs them */
    GetWindowsDirectoryW(dir, MAX_PATH);
    {
        WCHAR path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%s\\srchasst\\srchui.dll", dir);
        ui->srchui = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    }
    ui->rover = LoadImageW(wexsearch_module(), MAKEINTRESOURCEW(IDB_SEARCH_ROVER), IMAGE_BITMAP, 0, 0, 0);

    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ui->font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    ui->bold = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 13 / 10;   /* the name in the character chooser */
    ui->name_font = CreateFontIndirectW(&ncm.lfMessageFont);

    ui->pane = CreateWindowExW(WS_EX_CONTROLPARENT, PANE_CLASS, NULL, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                               0, 0, PANE_WIDTH + SPLITTER, 100, parent, NULL, wexsearch_module(), NULL);
    SetWindowLongPtrW(ui->pane, GWLP_USERDATA, (LONG_PTR)ui);
    ui->content = CreateWindowExW(WS_EX_CONTROLPARENT, CONTENT_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
                                  0, 0, 10, 10, ui->pane, NULL, wexsearch_module(), NULL);
    ui->back = CreateWindowExW(0, WC_BUTTONW, L"&Back", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 72, 22, ui->pane,
                               (HMENU)IDC_BACK, wexsearch_module(), NULL);
    ui->action = CreateWindowExW(0, WC_BUTTONW, L"Search", WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 72, 22, ui->pane,
                                 (HMENU)IDC_SEARCH, wexsearch_module(), NULL);
    SendMessageW(ui->back, WM_SETFONT, (WPARAM)ui->font, FALSE);
    SendMessageW(ui->action, WM_SETFONT, (WPARAM)ui->font, FALSE);
    ui->results = search_results_create(parent);

    build_looks(ui);
    /* opened on a folder ("Search" in its window): straight to all files and folders, there */
    ui->page = scope ? PAGE_ALL : PAGE_START;
    build_page(ui);
    ui->pane_visible = TRUE;
    ui->results_visible = results;
    ShowWindow(ui->pane, SW_SHOW);
    load_character(ui);
    return ui;
}

void search_ui_destroy(SEARCH_UI *ui)
{
    if (!ui) return;
    if (ui->job) search_stop(ui->job);
    KillTimer(ui->pane, IDT_CHARACTER);
    agent_close(ui->agent);
    destroy_controls(ui);
    DestroyWindow(ui->pane);
    DestroyWindow(ui->results);
    if (ui->srchui) FreeLibrary(ui->srchui);
    if (ui->rover) DeleteObject(ui->rover);
    DeleteObject(ui->font);
    DeleteObject(ui->bold);
    DeleteObject(ui->name_font);
    free(ui->looks);
    free(ui->types);
    free(ui);
}

BOOL search_ui_layout(SEARCH_UI *ui, RECT *rect)
{
    ShowWindow(ui->pane, ui->pane_visible ? SW_SHOWNA : SW_HIDE);
    if (ui->pane_visible)
    {
        SetWindowPos(ui->pane, NULL, rect->left, rect->top, PANE_WIDTH + SPLITTER, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        rect->left += PANE_WIDTH + SPLITTER;
    }
    ShowWindow(ui->results, ui->results_visible ? SW_SHOWNA : SW_HIDE);
    if (!ui->results_visible) return FALSE;
    SetWindowPos(ui->results, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    rect->right = rect->left;
    return TRUE;
}

BOOL search_ui_pane_visible(SEARCH_UI *ui)
{
    return ui && ui->pane_visible;
}

BOOL search_ui_results_visible(SEARCH_UI *ui)
{
    return ui && ui->results_visible;
}

int search_ui_result_count(SEARCH_UI *ui, BOOL selected)
{
    return ui ? search_results_count(ui->results, selected) : 0;
}

void search_ui_show_pane(SEARCH_UI *ui, BOOL show)
{
    if (ui->pane_visible == show) return;
    ui->pane_visible = show;
    if (!show && ui->job) search_finished(ui);
    if (show) character_play(ui, L"Show", NULL);   /* the character comes back each time the pane opens */
    else character_tick(ui);                       /* stops it */
    notify_parent(ui, show ? SEARCHUI_RESULTS_SHOWN : SEARCHUI_PANE_CLOSED);
}

void search_ui_set_scope(SEARCH_UI *ui, const WCHAR *folder)
{
    lstrcpynW(ui->scope, folder ? folder : L"", MAX_PATH);
    ui->look = -1;
    build_looks(ui);
    if (ui->page == PAGE_ALL) { build_page(ui); layout_pane(ui); }
}

/* Tab and Enter in the pane, like in a dialog */
BOOL search_ui_translate(SEARCH_UI *ui, MSG *msg)
{
    if (!ui || !ui->pane_visible || !IsChild(ui->pane, msg->hwnd)) return FALSE;
    return IsDialogMessageW(ui->pane, msg);
}
