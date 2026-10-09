/*
 * WineExplorer Desktop Environment - special folders of the shell namespace
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
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>

#include "shellfolders.h"
#include "shellres.h"
#include "theme_engine.h"
#include "taskbar_props.h"

/* the classes of the virtual folders and the shell32 icon that stands in when the registry names none we can use */
static const struct
{
    int csidl;
    const WCHAR *clsid;
    int icon;
} folders[] =
{
    { CSIDL_PERSONAL,  L"{450D8FBA-AD25-11D0-98A8-0800361B1103}", 235 },
    { CSIDL_DRIVES,    L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", 16 },
    { CSIDL_NETWORK,   L"{208D2C60-3AEA-1069-A2D7-08002B30309D}", 18 },
    { CSIDL_BITBUCKET, L"{645FF040-5081-101B-9F08-00AA002F954E}", 32 },
    { CSIDL_INTERNET,  L"{871C5380-42A0-1069-A2EA-08002B30309D}", 0 },
    { CSIDL_CONTROLS,  L"{21EC2020-3AEA-1069-A2DD-08002B30309D}", 137 },
    { CSIDL_PRINTERS,  L"{2227A280-3AEA-1069-A2DE-08002B30309D}", 138 },
};

static int folder_index(int csidl)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(folders); i++) if (folders[i].csidl == csidl) return i;
    return -1;
}

const WCHAR *shellfolder_clsid(int csidl)
{
    int i = folder_index(csidl);
    return i < 0 ? NULL : folders[i].clsid;
}

void shellfolder_open(int csidl, BOOL explore)
{
    WCHAR target[MAX_PATH], param[MAX_PATH + 4];
    IShellFolder *desktop;
    LPITEMIDLIST pidl;
    STRRET str;
    BOOL named = FALSE;

    /* Internet Explorer is a program behind its namespace object (CLSID_Internet\shell\OpenHomePage) */
    if (csidl == CSIDL_INTERNET)
    {
        ShellExecuteW(NULL, L"open", L"iexplore.exe", NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    if (FAILED(SHGetSpecialFolderLocation(NULL, csidl, &pidl))) return;

    /* the parsing name: the path of a file system folder, "::{CLSID}" of a virtual one */
    if (SUCCEEDED(SHGetDesktopFolder(&desktop)))
    {
        named = SUCCEEDED(IShellFolder_GetDisplayNameOf(desktop, pidl, SHGDN_FORPARSING, &str)) &&
                SUCCEEDED(StrRetToBufW(&str, pidl, target, MAX_PATH));
        IShellFolder_Release(desktop);
    }
    if (named && explore)
    {
        /* Wine's ShellExecuteEx drops the "explore" verb of a virtual folder: ask explorer for the Folders pane */
        swprintf(param, ARRAY_SIZE(param), L"/e,%s", target);
        ShellExecuteW(NULL, L"open", L"explorer.exe", param, NULL, SW_SHOWNORMAL);
    }
    else if (named && wcsncmp(target, L"::", 2))
    {
        /* a folder with a path (My Documents, My Pictures ...) opens like any folder */
        ShellExecuteW(NULL, L"open", target, NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        SHELLEXECUTEINFOW sei = { sizeof(sei), SEE_MASK_IDLIST };

        sei.lpIDList = pidl;
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
    }
    ILFree(pidl);
}

BOOL shellfolder_name(int csidl, WCHAR *name, int len)
{
    SHFILEINFOW sfi = {0};
    LPITEMIDLIST pidl;

    name[0] = 0;
    if (FAILED(SHGetSpecialFolderLocation(NULL, csidl, &pidl))) return FALSE;
    SHGetFileInfoW((const WCHAR *)pidl, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_DISPLAYNAME);
    ILFree(pidl);
    lstrcpynW(name, sfi.szDisplayName, len);
    return name[0] != 0;
}

HICON shellfolder_icon(int csidl, const WCHAR *state, int size)
{
    WCHAR key[160], spec[MAX_PATH + 16], path[MAX_PATH], *comma;
    int i = folder_index(csidl), index = 0;
    HICON icon = NULL;
    DWORD len;

    if (i < 0) return NULL;
    swprintf(key, ARRAY_SIZE(key), L"Software\\Classes\\CLSID\\%s\\DefaultIcon", folders[i].clsid);
    len = sizeof(spec);
    if (RegGetValueW(HKEY_CURRENT_USER, key, state, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, spec, &len))
    {
        len = sizeof(spec);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, state, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, spec, &len))
            spec[0] = 0;
    }
    if (spec[0])
    {
        const WCHAR *file;

        ExpandEnvironmentStringsW(spec, path, MAX_PATH);
        if ((comma = wcsrchr(path, ','))) { index = _wtoi(comma + 1); *comma = 0; }
        file = PathFindFileNameW(path);
        /* Windows themes name icons by position in Windows' own shell32.dll; our shell32 is laid out differently,
         * so only icons from other files are taken from the registry */
        if (wcsicmp(file, L"shell32.dll") && wcsicmp(file, L"explorer.exe") && wcsicmp(file, L"mydocs.dll"))
            PrivateExtractIconsW(path, index, size, size, &icon, NULL, 1, 0);
    }
    if (!icon && csidl == CSIDL_INTERNET) icon = shellres_app_icon(L"iexplore.exe", size);
    if (!icon && folders[i].icon)
    {
        int id = folders[i].icon;
        if (csidl == CSIDL_BITBUCKET && state && !wcsicmp(state, L"full")) id = 33;
        icon = shellres_icon(id, size);
    }
    return icon;
}

/* "Desktop Items" keeps a list per Start menu style; a value of 1 hides the icon */
static const WCHAR *hidden_icons_key(void)
{
    return taskbar_settings_get()->start_menu_style == THEME_STARTMENU_CLASSIC ?
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu" :
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel";
}

BOOL shellfolder_on_desktop(int csidl)
{
    DWORD hidden = 0, size = sizeof(hidden);
    const WCHAR *clsid = shellfolder_clsid(csidl);

    if (!clsid) return FALSE;
    RegGetValueW(HKEY_CURRENT_USER, hidden_icons_key(), clsid, RRF_RT_REG_DWORD, NULL, &hidden, &size);
    return !hidden;
}

void shellfolder_set_on_desktop(int csidl, BOOL show)
{
    DWORD hidden = !show;
    const WCHAR *clsid = shellfolder_clsid(csidl);

    if (clsid) RegSetKeyValueW(HKEY_CURRENT_USER, hidden_icons_key(), clsid, REG_DWORD, &hidden, sizeof(hidden));
}

int shellfolder_desktop_items(int *csidls, int max)
{
    static const int order[] = { CSIDL_PERSONAL, CSIDL_DRIVES, CSIDL_NETWORK, CSIDL_BITBUCKET, CSIDL_INTERNET };
    int i, n = 0;

    for (i = 0; i < ARRAY_SIZE(order) && n < max; i++)
        if (shellfolder_on_desktop(order[i])) csidls[n++] = order[i];
    return n;
}
