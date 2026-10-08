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

#include "shellres.h"

extern HINSTANCE explorer_hInstance;

#define MAX_PACKS 8

static struct { WCHAR name[24]; HMODULE module; BOOL tried; } packs[MAX_PACKS];

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
