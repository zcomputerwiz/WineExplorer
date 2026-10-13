/*
 * WineExplorer Desktop Environment - Start menu settings, pinned programs and most frequently used programs
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

#include <stdlib.h>
#include <wchar.h>
#include <windows.h>
#include <shlwapi.h>

#include "startsettings.h"

static const WCHAR advanced_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
static const WCHAR pinned_key[] = L"Software\\WineExplorer\\StartMenu\\Pinned";
static const WCHAR mfu_key[] = L"Software\\WineExplorer\\StartMenu\\Mfu";
static const WCHAR general_key[] = L"Software\\WineExplorer\\StartMenu";

#define HIDDEN 0xffffffff

/* the programs the Start menu offers before the user's own launches fill the list */
static const WCHAR *const default_mfu[] = { L"wmplayer.exe", L"notepad.exe", L"calc.exe", L"mspaint.exe", L"cmd.exe" };

static struct { const WCHAR *name; size_t offset; int def; } const value_table[] =
{
    { L"Start_ShowMyComputer", offsetof(START_SETTINGS, my_computer), START_LINK },
    { L"Start_ShowMyDocs", offsetof(START_SETTINGS, my_docs), START_LINK },
    { L"Start_ShowMyMusic", offsetof(START_SETTINGS, my_music), START_LINK },
    { L"Start_ShowMyPics", offsetof(START_SETTINGS, my_pics), START_LINK },
    { L"Start_ShowNetPlaces", offsetof(START_SETTINGS, net_places), START_LINK },
    { L"Start_ShowControlPanel", offsetof(START_SETTINGS, control_panel), START_LINK },
    { L"Start_ShowNetConn", offsetof(START_SETTINGS, connect_to), START_MENU },
    { L"Start_ShowPrinters", offsetof(START_SETTINGS, printers), START_LINK },
    { L"Start_ShowHelp", offsetof(START_SETTINGS, help), START_LINK },
    { L"Start_ShowSearch", offsetof(START_SETTINGS, search), START_LINK },
    { L"Start_ShowRun", offsetof(START_SETTINGS, run), START_LINK },
    { L"Start_ShowSetProgramAccessAndDefaults", offsetof(START_SETTINGS, program_access), START_LINK },
    { L"Start_ShowRecentDocs", offsetof(START_SETTINGS, recent_docs), START_MENU },
    { L"Start_MinMFU", offsetof(START_SETTINGS, min_mfu), 6 },
    { L"Start_LargeMFUIcons", offsetof(START_SETTINGS, large_icons), 1 },
    { L"Start_NotifyNewApps", offsetof(START_SETTINGS, notify_new_apps), 1 },
};

void startsettings_load(START_SETTINGS *s)
{
    HKEY key;
    unsigned int i;
    DWORD val, size;

    for (i = 0; i < ARRAY_SIZE(value_table); i++) *(int *)((char *)s + value_table[i].offset) = value_table[i].def;
    s->show_internet = s->show_email = TRUE;
    s->hover_open = TRUE;
    /* "Display as a menu" for the places only exists where there is a folder to open (the rest is a link) */
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, advanced_key, 0, KEY_READ, &key))
    {
        for (i = 0; i < ARRAY_SIZE(value_table); i++)
        {
            size = sizeof(val);
            if (!RegQueryValueExW(key, value_table[i].name, NULL, NULL, (BYTE *)&val, &size))
                *(int *)((char *)s + value_table[i].offset) = val;
        }
        RegCloseKey(key);
    }
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, general_key, 0, KEY_READ, &key))
    {
        size = sizeof(val);
        if (!RegQueryValueExW(key, L"ShowInternet", NULL, NULL, (BYTE *)&val, &size)) s->show_internet = val != 0;
        size = sizeof(val);
        if (!RegQueryValueExW(key, L"ShowEmail", NULL, NULL, (BYTE *)&val, &size)) s->show_email = val != 0;
        size = sizeof(val);
        if (!RegQueryValueExW(key, L"HoverOpen", NULL, NULL, (BYTE *)&val, &size)) s->hover_open = val != 0;
        RegCloseKey(key);
    }
    if (s->min_mfu < 0) s->min_mfu = 0;
    if (s->min_mfu > 30) s->min_mfu = 30;
}

void startsettings_save(const START_SETTINGS *s)
{
    HKEY key;
    unsigned int i;
    DWORD val;

    if (!RegCreateKeyExW(HKEY_CURRENT_USER, advanced_key, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL))
    {
        for (i = 0; i < ARRAY_SIZE(value_table); i++)
        {
            val = *(const int *)((const char *)s + value_table[i].offset);
            RegSetValueExW(key, value_table[i].name, 0, REG_DWORD, (BYTE *)&val, sizeof(val));
        }
        RegCloseKey(key);
    }
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, general_key, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL))
    {
        val = s->show_internet; RegSetValueExW(key, L"ShowInternet", 0, REG_DWORD, (BYTE *)&val, sizeof(val));
        val = s->show_email; RegSetValueExW(key, L"ShowEmail", 0, REG_DWORD, (BYTE *)&val, sizeof(val));
        val = s->hover_open; RegSetValueExW(key, L"HoverOpen", 0, REG_DWORD, (BYTE *)&val, sizeof(val));
        RegCloseKey(key);
    }
}

/* the identity of a program: programs of the Windows directory by file name, others by full path, lower case */
static void program_key(const WCHAR *path, WCHAR *key)
{
    WCHAR windir[MAX_PATH];

    lstrcpynW(key, path, MAX_PATH);
    GetWindowsDirectoryW(windir, MAX_PATH);
    if (!StrCmpNIW(path, windir, lstrlenW(windir))) lstrcpynW(key, PathFindFileNameW(path), MAX_PATH);
    CharLowerW(key);
}

void startprograms_title(const WCHAR *path, WCHAR *title, int len)
{
    static const struct { const WCHAR *exe, *name; } known[] =
    {
        { L"notepad.exe", L"Notepad" }, { L"calc.exe", L"Calculator" }, { L"mspaint.exe", L"Paint" },
        { L"cmd.exe", L"Command Prompt" }, { L"wmplayer.exe", L"Windows Media Player" }, { L"wordpad.exe", L"WordPad" },
        { L"explorer.exe", L"Windows Explorer" }, { L"iexplore.exe", L"Internet Explorer" },
    };
    const WCHAR *file = PathFindFileNameW(path);
    unsigned int i;
    WCHAR *dot;

    for (i = 0; i < ARRAY_SIZE(known); i++)
        if (!lstrcmpiW(file, known[i].exe)) { lstrcpynW(title, known[i].name, len); return; }
    lstrcpynW(title, file, len);
    if ((dot = wcsrchr(title, '.'))) *dot = 0;
}

/* ---- pinned programs: values "0000", "0001" ... with "path|title" ---- */

int startprograms_pinned(START_PROGRAM *out, int max)
{
    HKEY key;
    int n = 0, i;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, pinned_key, 0, KEY_READ, &key)) return 0;
    for (i = 0; i < 100 && n < max; i++)
    {
        WCHAR name[8], data[MAX_PATH + 100], *bar;
        DWORD size = sizeof(data);

        swprintf(name, ARRAY_SIZE(name), L"%04d", i);
        if (RegQueryValueExW(key, name, NULL, NULL, (BYTE *)data, &size)) continue;
        memset(&out[n], 0, sizeof(out[n]));
        if ((bar = wcschr(data, '|'))) { *bar++ = 0; lstrcpynW(out[n].title, bar, ARRAY_SIZE(out[n].title)); }
        lstrcpynW(out[n].path, data, MAX_PATH);
        if (!out[n].title[0]) startprograms_title(out[n].path, out[n].title, ARRAY_SIZE(out[n].title));
        out[n++].pinned = TRUE;
    }
    RegCloseKey(key);
    return n;
}

BOOL startprograms_is_pinned(const WCHAR *path)
{
    START_PROGRAM list[32];
    WCHAR a[MAX_PATH], b[MAX_PATH];
    int n = startprograms_pinned(list, ARRAY_SIZE(list)), i;

    program_key(path, a);
    for (i = 0; i < n; i++)
    {
        program_key(list[i].path, b);
        if (!lstrcmpW(a, b)) return TRUE;
    }
    return FALSE;
}

static void write_pinned(const START_PROGRAM *list, int n)
{
    HKEY key;
    int i;

    RegDeleteTreeW(HKEY_CURRENT_USER, pinned_key);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, pinned_key, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL)) return;
    for (i = 0; i < n; i++)
    {
        WCHAR name[8], data[MAX_PATH + 100];
        swprintf(name, ARRAY_SIZE(name), L"%04d", i);
        swprintf(data, ARRAY_SIZE(data), L"%s|%s", list[i].path, list[i].title);
        RegSetValueExW(key, name, 0, REG_SZ, (BYTE *)data, (lstrlenW(data) + 1) * sizeof(WCHAR));
    }
    RegCloseKey(key);
}

void startprograms_pin(const WCHAR *path, const WCHAR *title)
{
    START_PROGRAM list[32];
    int n;

    if (startprograms_is_pinned(path)) return;
    n = startprograms_pinned(list, ARRAY_SIZE(list) - 1);
    memset(&list[n], 0, sizeof(list[n]));
    lstrcpynW(list[n].path, path, MAX_PATH);
    lstrcpynW(list[n].title, title, ARRAY_SIZE(list[n].title));
    write_pinned(list, n + 1);
}

void startprograms_unpin(const WCHAR *path)
{
    START_PROGRAM list[32];
    WCHAR a[MAX_PATH], b[MAX_PATH];
    int n = startprograms_pinned(list, ARRAY_SIZE(list)), i, k = 0;

    program_key(path, a);
    for (i = 0; i < n; i++)
    {
        program_key(list[i].path, b);
        if (lstrcmpW(a, b)) list[k++] = list[i];
    }
    write_pinned(list, k);
}

/* ---- most frequently used ---- */

typedef struct { WCHAR path[MAX_PATH]; DWORD count; } MFU_ENTRY;

static int compare_mfu(const void *a, const void *b)
{
    DWORD x = ((const MFU_ENTRY *)a)->count, y = ((const MFU_ENTRY *)b)->count;
    return x < y ? 1 : x > y ? -1 : 0;
}

static BOOL list_has(const START_PROGRAM *out, int n, const WCHAR *path)
{
    WCHAR a[MAX_PATH], b[MAX_PATH];
    int i;

    program_key(path, a);
    for (i = 0; i < n; i++)
    {
        program_key(out[i].path, b);
        if (!lstrcmpW(a, b)) return TRUE;
    }
    return FALSE;
}

static BOOL is_hidden(const WCHAR *path)
{
    HKEY key;
    WCHAR k[MAX_PATH];
    DWORD val = 0, size = sizeof(val);

    program_key(path, k);
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, mfu_key, 0, KEY_READ, &key))
    {
        if (RegQueryValueExW(key, k, NULL, NULL, (BYTE *)&val, &size)) val = 0;
        RegCloseKey(key);
    }
    return val == HIDDEN;
}

int startprograms_mfu(START_PROGRAM *out, int max)
{
    START_PROGRAM pinned[32];
    MFU_ENTRY *entries = HeapAlloc(GetProcessHeap(), 0, 200 * sizeof(*entries));
    int np = startprograms_pinned(pinned, ARRAY_SIZE(pinned)), n = 0, count = 0, i;
    HKEY key;

    if (entries && !RegOpenKeyExW(HKEY_CURRENT_USER, mfu_key, 0, KEY_READ, &key))
    {
        for (i = 0; count < 200; i++)
        {
            DWORD nlen = MAX_PATH, size = sizeof(DWORD), type;
            if (RegEnumValueW(key, i, entries[count].path, &nlen, NULL, &type, (BYTE *)&entries[count].count, &size)) break;
            if (type == REG_DWORD && entries[count].count != HIDDEN && entries[count].count > 0) count++;
        }
        RegCloseKey(key);
    }
    if (entries) qsort(entries, count, sizeof(*entries), compare_mfu);
    for (i = 0; i < count && n < max; i++)
    {
        if (list_has(pinned, np, entries[i].path) || list_has(out, n, entries[i].path)) continue;
        memset(&out[n], 0, sizeof(out[n]));
        lstrcpynW(out[n].path, entries[i].path, MAX_PATH);
        startprograms_title(out[n].path, out[n].title, ARRAY_SIZE(out[n].title));
        n++;
    }
    for (i = 0; i < (int)ARRAY_SIZE(default_mfu) && n < max; i++)
    {
        if (list_has(pinned, np, default_mfu[i]) || list_has(out, n, default_mfu[i]) || is_hidden(default_mfu[i])) continue;
        memset(&out[n], 0, sizeof(out[n]));
        lstrcpynW(out[n].path, default_mfu[i], MAX_PATH);
        startprograms_title(out[n].path, out[n].title, ARRAY_SIZE(out[n].title));
        n++;
    }
    HeapFree(GetProcessHeap(), 0, entries);
    return n;
}

void startprograms_count_launch(const WCHAR *path)
{
    HKEY key;
    WCHAR k[MAX_PATH];
    DWORD val = 0, size = sizeof(val);

    if (!path || !path[0]) return;
    /* only programs count, not documents or folders */
    if (!PathMatchSpecW(path, L"*.exe") && !PathMatchSpecW(path, L"*.lnk")) return;
    program_key(path, k);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, mfu_key, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &key, NULL)) return;
    if (RegQueryValueExW(key, k, NULL, NULL, (BYTE *)&val, &size)) val = 0;
    if (val != HIDDEN)
    {
        val++;
        RegSetValueExW(key, k, 0, REG_DWORD, (BYTE *)&val, sizeof(val));
    }
    RegCloseKey(key);
}

void startprograms_remove_from_list(const WCHAR *path)
{
    HKEY key;
    WCHAR k[MAX_PATH];
    DWORD val = HIDDEN;

    program_key(path, k);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, mfu_key, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL)) return;
    RegSetValueExW(key, k, 0, REG_DWORD, (BYTE *)&val, sizeof(val));
    RegCloseKey(key);
}

void startprograms_clear_list(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, mfu_key);
}
