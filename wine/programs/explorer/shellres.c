/*
 * WineExplorer Desktop Environment - shell resources (icons and bitmaps by Windows resource id)
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
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include "shellres.h"
#include "resource.h"

extern HINSTANCE explorer_hInstance;

#define MAX_PACKS 8

static struct { WCHAR name[24]; WCHAR path[MAX_PATH]; HMODULE module; BOOL tried; } packs[MAX_PACKS];

/* Folder of the active theme's icon packs: "IconPack=" in the [WineExplorer] section of the .theme file that
 * is current (HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\CurrentTheme). */
static BOOL theme_pack_dir(WCHAR *dir)
{
    WCHAR theme[MAX_PATH], value[MAX_PATH], windir[MAX_PATH];
    DWORD size = sizeof(theme);

    dir[0] = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes", L"CurrentTheme",
                     RRF_RT_REG_SZ, NULL, theme, &size)) return FALSE;
    if (!GetPrivateProfileStringW(L"WineExplorer", L"IconPack", L"", value, MAX_PATH, theme) || !value[0]) return FALSE;
    GetWindowsDirectoryW(windir, MAX_PATH);
    {
        /* %ResourceDir% is the Resources folder of the Windows directory */
        const WCHAR *tail = value;
        if (!wcsnicmp(value, L"%ResourceDir%", 13)) tail = value + 13;
        else { lstrcpynW(dir, value, MAX_PATH); return TRUE; }
        lstrcpyW(dir, windir);
        wcscat(dir, L"\\Resources");
        wcscat(dir, tail);
    }
    return TRUE;
}

/* The pack DLL of a module, loaded as data, or NULL */
static HMODULE pack_module(const WCHAR *name)
{
    int i;
    WCHAR path[MAX_PATH], dir[MAX_PATH];

    for (i = 0; i < MAX_PACKS && packs[i].name[0]; i++)
        if (!wcsicmp(packs[i].name, name)) return packs[i].module;
    if (i == MAX_PACKS) return NULL;

    lstrcpynW(packs[i].name, name, ARRAY_SIZE(packs[i].name));
    if (theme_pack_dir(dir))
    {
        swprintf(path, MAX_PATH, L"%s\\xpres_%s.dll", dir, name);
        packs[i].module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    }
    if (!packs[i].module)
    {
        GetWindowsDirectoryW(path, MAX_PATH);
        wcscat(path, L"\\Resources\\Icons\\xpres_");
        wcscat(path, name);
        wcscat(path, L".dll");
        packs[i].module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    }
    if (packs[i].module) lstrcpyW(packs[i].path, path);
    return packs[i].module;
}

/* Forget the loaded packs: the next lookup reads the active theme again */
void shellres_reset(void)
{
    int i;
    for (i = 0; i < MAX_PACKS; i++)
    {
        if (packs[i].module) FreeLibrary(packs[i].module);
        memset(&packs[i], 0, sizeof(packs[i]));
    }
}

BOOL shellres_has_pack(const WCHAR *module)
{
    return pack_module(module) != NULL;
}

/* ---- shell32's own icons ---- */

static const WCHAR shell_icons_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons";
#define SHELL32_FIRST_ICONS 54   /* shell32's icons 1-54 have no gaps: the icon with id n is index n - 1 */

/* a Shell Icons value that points into one of our packs */
static BOOL is_pack_value(const WCHAR *data)
{
    WCHAR lower[MAX_PATH + 16];
    lstrcpynW(lower, data, ARRAY_SIZE(lower));
    CharLowerW(lower);
    return wcsstr(lower, L"\\xpres_shell32.dll,") != NULL;
}

/* Wine numbers shell32's icons like Windows, except these, which are other pictures in Wine */
static BOOL other_picture(int id)
{
    static const WORD ids[] = { 220, 223, 269, 271, 272, 273, 276, 279, 281, 289 };
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(ids); i++) if (ids[i] == id) return TRUE;
    return FALSE;
}

struct shell_icons_ctx { HKEY key; const WCHAR *pack; BOOL changed; };

static BOOL CALLBACK add_shell_icon(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    struct shell_icons_ctx *ctx = (struct shell_icons_ctx *)param;
    WCHAR value[16], data[MAX_PATH + 16], old[MAX_PATH + 16];
    DWORD size = sizeof(old);
    int id;

    if (!IS_INTRESOURCE(name)) return TRUE;
    id = LOWORD(name);
    if (other_picture(id)) return TRUE;
    /* the Windows names are icon indexes; past the first icons shell32 also takes the negative resource id */
    if (id <= SHELL32_FIRST_ICONS) swprintf(value, ARRAY_SIZE(value), L"%d", id - 1);
    else swprintf(value, ARRAY_SIZE(value), L"-%d", id);
    swprintf(data, ARRAY_SIZE(data), L"%s,-%d", ctx->pack, id);

    if (!RegQueryValueExW(ctx->key, value, NULL, NULL, (BYTE *)old, &size))
    {
        if (!wcsicmp(old, data) || !is_pack_value(old)) return TRUE;   /* up to date, or the user's own */
    }
    RegSetValueExW(ctx->key, value, 0, REG_SZ, (const BYTE *)data, (lstrlenW(data) + 1) * sizeof(WCHAR));
    ctx->changed = TRUE;
    return TRUE;
}

/* shell32 takes its own icons from the Shell Icons key first, as on Windows: point it at the theme's pack, or back at
 * shell32 without one.  Values that point into another pack are ours and go away; other values are left alone.
 * TRUE if anything changed (then shellres_reload_shell_icons() shows it in this process). */
BOOL shellres_sync_shell_icons(void)
{
    HMODULE pack = pack_module(L"shell32");
    const WCHAR *path = NULL;
    WCHAR name[16], data[MAX_PATH + 16];
    DWORD i, name_len, data_len, type;
    BOOL changed = FALSE;
    HKEY key;
    int n;

    for (n = 0; pack && n < MAX_PACKS; n++) if (packs[n].module == pack) path = packs[n].path;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, shell_icons_key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL)) return FALSE;

    for (i = 0;;)
    {
        name_len = ARRAY_SIZE(name);
        data_len = sizeof(data);
        if (RegEnumValueW(key, i, name, &name_len, NULL, &type, (BYTE *)data, &data_len)) break;
        if (type == REG_SZ && is_pack_value(data) &&
            (!path || wcsnicmp(data, path, lstrlenW(path)) || data[lstrlenW(path)] != ',' ||
             other_picture(name[0] == '-' ? _wtoi(name + 1) : _wtoi(name) + 1)))
        {
            RegDeleteValueW(key, name);
            changed = TRUE;
            continue;
        }
        i++;
    }
    if (path)
    {
        struct shell_icons_ctx ctx = { key, path, FALSE };
        EnumResourceNamesW(pack, (LPCWSTR)RT_GROUP_ICON, add_shell_icon, (LONG_PTR)&ctx);
        changed |= ctx.changed;
    }
    RegCloseKey(key);
    return changed;
}

/* Load shell32's icons in this process's system image list again (FileIconInit(TRUE)), after Shell Icons changed */
void shellres_reload_shell_icons(void)
{
    BOOL (WINAPI *file_icon_init)(BOOL) = (void *)GetProcAddress(LoadLibraryW(L"shell32.dll"), (LPCSTR)660);
    if (file_icon_init) file_icon_init(TRUE);
}

/* The icon of a program by executable name (notepad.exe), from the theme's Apps folder of the icon pack */
HICON shellres_app_icon(const WCHAR *exe, int size)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH], base[MAX_PATH];
    WCHAR *dot;

    if (!theme_pack_dir(dir)) return NULL;
    lstrcpynW(base, wcsrchr(exe, '\\') ? wcsrchr(exe, '\\') + 1 : exe, MAX_PATH);
    if ((dot = wcsrchr(base, '.'))) *dot = 0;
    swprintf(path, MAX_PATH, L"%s\\Apps\\%s.ico", dir, base);
    return LoadImageW(NULL, path, IMAGE_ICON, size, size, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
}

/* an icon of any module of the genuine pack (explorer, browseui ...) by resource id */
HICON shellres_module_icon(const WCHAR *module, int id, int size)
{
    HMODULE pack = pack_module(module);
    return pack ? LoadImageW(pack, MAKEINTRESOURCEW(id), IMAGE_ICON, size, size, LR_DEFAULTCOLOR) : NULL;
}

HICON shellres_icon(int id, int size)
{
    HMODULE pack = pack_module(L"shell32"), shell32;
    HICON icon = NULL;

    if (pack) icon = LoadImageW(pack, MAKEINTRESOURCEW(id), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (icon) return icon;

    shell32 = LoadLibraryExW(L"shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!shell32) return NULL;
    icon = LoadImageW(shell32, MAKEINTRESOURCEW(id), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    FreeLibrary(shell32);
    return icon;
}

HBITMAP shellres_bitmap(const WCHAR *module, int id, int fallback_id)
{
    HMODULE pack = pack_module(module);
    HBITMAP bmp = NULL;

    if (pack) bmp = LoadImageW(pack, MAKEINTRESOURCEW(id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!bmp && fallback_id) bmp = LoadImageW(explorer_hInstance, MAKEINTRESOURCEW(fallback_id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    return bmp;
}

/* The Go button of an address bar, as on Windows XP: a flat button with the green arrow and "Go".  The XP arrows
 * are shell32 230 and 231 (20x20 with alpha; browseui's IDB_GO 290/291 are the gray classic ones), as are ReactOS'. */
HWND shellres_go_button(HWND parent, UINT id)
{
    HWND go = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST |
                              TBSTYLE_TRANSPARENT | CCS_NORESIZE | CCS_NODIVIDER | CCS_NOPARENTALIGN, 0, 0, 60, 22,
                              parent, NULL, explorer_hInstance, NULL);
    TBBUTTON button = { 0, id, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE };
    HBITMAP bmp;
    int i;

    SendMessageW(go, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(go, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    for (i = 0; i < 2; i++)
    {
        HIMAGELIST list;
        BITMAP bm;

        if (!(bmp = shellres_bitmap(L"shell32", 230 + i, i ? IDB_BROWSER_GO_HOT : IDB_BROWSER_GO))) continue;
        GetObjectW(bmp, sizeof(bm), &bm);
        if (bm.bmBitsPixel == 32)
        {
            list = ImageList_Create(bm.bmWidth, bm.bmHeight, ILC_COLOR32, 1, 0);
            ImageList_Add(list, bmp, NULL);
        }
        else
        {
            list = ImageList_Create(bm.bmWidth, bm.bmHeight, ILC_COLOR24 | ILC_MASK, 1, 0);
            ImageList_AddMasked(list, bmp, RGB(255, 0, 255));
        }
        DeleteObject(bmp);
        SendMessageW(go, i ? TB_SETHOTIMAGELIST : TB_SETIMAGELIST, 0, (LPARAM)list);
    }
    button.iString = SendMessageW(go, TB_ADDSTRINGW, 0, (LPARAM)L"Go ");
    SendMessageW(go, TB_ADDBUTTONSW, 1, (LPARAM)&button);
    SendMessageW(go, TB_AUTOSIZE, 0, 0);
    return go;
}

/* width of the button made by shellres_go_button */
int shellres_go_width(HWND go)
{
    RECT rc;
    return SendMessageW(go, TB_GETITEMRECT, 0, (LPARAM)&rc) ? rc.right + 2 : 50;
}

HBITMAP shellres_style_bitmap(int id)
{
    WCHAR theme[MAX_PATH], color[64], path[MAX_PATH];
    HMODULE style = NULL;
    HBITMAP bmp = NULL;

    if (IsThemeActive() && SUCCEEDED(GetCurrentThemeName(theme, MAX_PATH, color, ARRAY_SIZE(color), NULL, 0)))
    {
        PathRemoveFileSpecW(theme);
        swprintf(path, MAX_PATH, L"%s\\Shell\\%s\\shellstyle.dll", theme, color);
        style = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    }
    if (!style)
    {
        GetSystemDirectoryW(path, MAX_PATH);
        lstrcatW(path, L"\\shellstyle.dll");
        style = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    }
    if (style)
    {
        bmp = LoadImageW(style, MAKEINTRESOURCEW(id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        FreeLibrary(style);
    }
    return bmp;
}

void shellres_premultiply(HBITMAP bitmap)
{
    BITMAP bm;
    DWORD *px;
    int i, n;
    BOOL has_alpha = FALSE;

    if (!GetObjectW(bitmap, sizeof(bm), &bm) || bm.bmBitsPixel != 32 || !bm.bmBits) return;
    px = bm.bmBits;
    n = bm.bmWidth * abs(bm.bmHeight);
    for (i = 0; i < n && !has_alpha; i++) has_alpha = (px[i] >> 24) != 0;
    if (!has_alpha)
    {
        for (i = 0; i < n; i++) px[i] |= 0xff000000;
        return;
    }
    for (i = 0; i < n; i++)
    {
        DWORD a = px[i] >> 24;
        px[i] = (a << 24) | ((((px[i] >> 16) & 255) * a / 255) << 16) | ((((px[i] >> 8) & 255) * a / 255) << 8) | ((px[i] & 255) * a / 255);
    }
}
