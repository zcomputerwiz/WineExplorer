/*
 * WineExplorer Desktop Environment - Startup and Recovery (sysdm.cpl)
 *
 * Based on ReactOS's System Control Panel Applet (dll/cpl/sysdm/startrec.c and its IDD_STARTUPRECOVERY dialog):
 *   Copyright 2006 Ged Murphy <gedmurphy@gmail.com>
 *   Copyright 2006 Christoph von Wittich <Christoph@ApiViewer.de>
 *   Copyright 2007 Johannes Anderwald <johannes.anderwald@reactos.org>
 * Copyright 2026 WineExplorer Desktop Environment Project: the settings come from org.wineexplorer.Boot1
 * (scripts/wex-boot1.py) through wexhost, instead of boot.ini, freeldr.ini and the registry; Edit opens the kernel
 * command line's options in Notepad, as XP opens boot.ini (Safe Mode work order D5, docs/VINTNER_BOOT.md 4.6).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <windows.h>
#include <commctrl.h>
#include <wexhost.h>

#include "sysdm.h"

#define DEFAULT_TIMEOUT 30   /* what a checked box starts at, as XP */

struct startrec
{
    WEX_BOOT boot;      /* the settings when the dialog opened */
    BOOL have_boot;     /* the service answered */
};

static const WCHAR title[] = L"Startup and Recovery";

/* A checked box enables its seconds; an unchecked one means 0 (no list; a failure screen without a countdown). */
static void set_timeout(HWND hwnd, int check, int edit, int updown, DWORD seconds, BOOL enabled)
{
    CheckDlgButton(hwnd, check, seconds ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(hwnd, updown, UDM_SETRANGE32, 0, WEX_BOOT_TIMEOUT_MAX);
    SendDlgItemMessageW(hwnd, updown, UDM_SETPOS32, 0, seconds ? seconds : DEFAULT_TIMEOUT);
    EnableWindow(GetDlgItem(hwnd, check), enabled);
    EnableWindow(GetDlgItem(hwnd, edit), enabled && seconds);
    EnableWindow(GetDlgItem(hwnd, updown), enabled && seconds);
}

static void toggle_timeout(HWND hwnd, int check, int edit, int updown)
{
    BOOL on = IsDlgButtonChecked(hwnd, check) == BST_CHECKED;
    BOOL ok;
    UINT seconds = GetDlgItemInt(hwnd, edit, &ok, FALSE);

    if (on && (!ok || !seconds)) SendDlgItemMessageW(hwnd, updown, UDM_SETPOS32, 0, DEFAULT_TIMEOUT);
    EnableWindow(GetDlgItem(hwnd, edit), on);
    EnableWindow(GetDlgItem(hwnd, updown), on);
}

static DWORD read_timeout(HWND hwnd, int check, int edit)
{
    BOOL ok;
    UINT seconds;

    if (IsDlgButtonChecked(hwnd, check) != BST_CHECKED) return 0;
    seconds = GetDlgItemInt(hwnd, edit, &ok, FALSE);
    if (!ok) return DEFAULT_TIMEOUT;
    return seconds > WEX_BOOT_TIMEOUT_MAX ? WEX_BOOT_TIMEOUT_MAX : seconds;
}

static void show_error(HWND hwnd, DWORD err, const char *message)
{
    WCHAR text[1024], reason[512] = L"", detail[512] = L"";

    if (err == ERROR_ACCESS_DENIED)
        lstrcpyW(reason, L"You must be logged on as an administrator to change the startup settings.");
    else if (err == ERROR_NOT_SUPPORTED)
        lstrcpyW(reason, L"The startup settings service is not available.");
    else
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, reason,
                       ARRAY_SIZE(reason), NULL);
    if (message && *message) MultiByteToWideChar(CP_UTF8, 0, message, -1, detail, ARRAY_SIZE(detail));
    swprintf(text, ARRAY_SIZE(text), L"The startup settings were not changed.\n\n%s%s%s", reason,
             *detail ? L"\n" : L"", detail);
    MessageBoxW(hwnd, text, title, MB_OK | MB_ICONERROR);
}

static void load(HWND hwnd, struct startrec *data)
{
    WCHAR name[64], text[64];
    int i;

    data->have_boot = !WexBootGet(&data->boot);
    if (!data->have_boot)
    {
        memset(&data->boot, 0, sizeof(data->boot));
        lstrcpyA(data->boot.os_name, "WineExplorer");
        data->boot.recovery_timeout = DEFAULT_TIMEOUT;
    }

    /* the one entry of the boot menu: Vintner Boot boots its newest good generation */
    MultiByteToWideChar(CP_UTF8, 0, data->boot.os_name, -1, name, ARRAY_SIZE(name));
    SendDlgItemMessageW(hwnd, IDC_STRECOSCOMBO, CB_ADDSTRING, 0, (LPARAM)name);
    SendDlgItemMessageW(hwnd, IDC_STRECOSCOMBO, CB_SETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(hwnd, IDC_STRECOSCOMBO), FALSE);

    set_timeout(hwnd, IDC_STRECLIST, IDC_STRRECLISTEDIT, IDC_STRRECLISTUPDWN, data->boot.menu_timeout,
                data->have_boot && data->boot.has_loader);
    set_timeout(hwnd, IDC_STRRECREC, IDC_STRRECRECEDIT, IDC_STRRECRECUPDWN, data->boot.recovery_timeout,
                data->have_boot);
    EnableWindow(GetDlgItem(hwnd, IDC_STRRECEDIT), data->have_boot && data->boot.has_options);

    /* System failure: the journal keeps the kernel's messages anyway; there are no alerts and no memory dumps
     * (no kdump), so those stay as they are and grayed. Automatically restart is kernel.panic. */
    CheckDlgButton(hwnd, IDC_STRRECWRITEEVENT, BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_STRRECWRITEEVENT), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_STRRECSENDALERT), FALSE);
    CheckDlgButton(hwnd, IDC_STRRECRESTART, data->boot.auto_restart ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_STRRECRESTART), data->have_boot);
    for (i = IDS_STARTREC_NO_DUMP; i <= IDS_STARTREC_FULL_DUMP; i++)
        if (LoadStringW(g_hInstance, i, text, ARRAY_SIZE(text)))
            SendDlgItemMessageW(hwnd, IDC_STRRECDEBUGCOMBO, CB_ADDSTRING, 0, (LPARAM)text);
    SendDlgItemMessageW(hwnd, IDC_STRRECDEBUGCOMBO, CB_SETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(hwnd, IDC_STRRECDEBUGCOMBO), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_STRRECDUMPFILE), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_STRRECOVERWRITE), FALSE);
}

static BOOL save(HWND hwnd, struct startrec *data)
{
    DWORD menu, recovery, err;
    BOOL restart;
    char message[512];

    if (!data->have_boot) return TRUE;
    menu = data->boot.has_loader ? read_timeout(hwnd, IDC_STRECLIST, IDC_STRRECLISTEDIT) : data->boot.menu_timeout;
    recovery = read_timeout(hwnd, IDC_STRRECREC, IDC_STRRECRECEDIT);
    restart = IsDlgButtonChecked(hwnd, IDC_STRRECRESTART) == BST_CHECKED;
    if (menu == data->boot.menu_timeout && recovery == data->boot.recovery_timeout &&
        restart == !!data->boot.auto_restart)
        return TRUE;   /* nothing changed: no password to ask for */

    err = WexBootSet(hwnd, menu, recovery, restart, message, sizeof(message));
    if (err == ERROR_CANCELLED) return FALSE;   /* the password dialog was cancelled: stay open */
    if (err)
    {
        show_error(hwnd, err, message);
        return FALSE;
    }
    data->boot.menu_timeout = menu;
    data->boot.recovery_timeout = recovery;
    data->boot.auto_restart = restart;
    return TRUE;
}

/* Wait for a program with the dialog disabled, its windows still painted. */
static void wait_for(HWND hwnd, HANDLE process)
{
    MSG msg;

    EnableWindow(hwnd, FALSE);
    while (MsgWaitForMultipleObjects(1, &process, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hwnd, TRUE);
    SetForegroundWindow(hwnd);
}

/* The options of the text, one space between them, comments left out: what /etc/kernel/cmdline would hold. */
static void normalize(const char *text, char *out, size_t size)
{
    const char *line = text;
    size_t len = 0;

    out[0] = 0;
    while (*line)
    {
        const char *end = line + strcspn(line, "\r\n");
        const char *p = line;

        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p < end && *p != '#')
        {
            while (p < end)
            {
                const char *word = p;
                while (p < end && *p != ' ' && *p != '\t') p++;
                if (p > word && len + (p - word) + 2 < size)
                {
                    if (len) out[len++] = ' ';
                    memcpy(out + len, word, p - word);
                    len += p - word;
                    out[len] = 0;
                }
                while (p < end && (*p == ' ' || *p == '\t')) p++;
            }
        }
        line = end;
        while (*line == '\r' || *line == '\n') line++;
    }
}

static const char options_header[] =
    "# WineExplorer startup options: the options on the kernel command line (/etc/kernel/cmdline).\r\n"
    "# Separate the options with spaces; lines that start with # are ignored. Save and close Notepad to use\r\n"
    "# them from the next start. If the computer then does not start, it starts again with the options it\r\n"
    "# had before.\r\n";

/* Edit: the options in Notepad, as XP opens boot.ini; saved and changed, they are published for the next start. */
static void edit_options(HWND hwnd, struct startrec *data)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH], cmd[MAX_PATH + 32];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char *text = NULL, before[WEX_BOOT_OPTIONS_MAX], after[WEX_BOOT_OPTIONS_MAX], message[512];
    DWORD size, done, err, generation = 0;
    HANDLE file;

    if (!GetTempPathW(ARRAY_SIZE(dir), dir)) return;
    swprintf(path, ARRAY_SIZE(path), L"%sStartup options.txt", dir);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        show_error(hwnd, GetLastError(), NULL);
        return;
    }
    WriteFile(file, options_header, strlen(options_header), &done, NULL);
    WriteFile(file, data->boot.options, strlen(data->boot.options), &done, NULL);
    WriteFile(file, "\r\n", 2, &done, NULL);
    CloseHandle(file);

    swprintf(cmd, ARRAY_SIZE(cmd), L"notepad.exe \"%s\"", path);
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hThread);
        wait_for(hwnd, pi.hProcess);
        CloseHandle(pi.hProcess);
    }

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        size = GetFileSize(file, NULL);
        if (size < WEX_BOOT_OPTIONS_MAX && (text = calloc(1, size + 1)))
            if (!ReadFile(file, text, size, &done, NULL)) done = 0, text[0] = 0;
        CloseHandle(file);
    }
    DeleteFileW(path);
    if (!text) return;

    /* Notepad may have saved it as UTF-8 with a byte order mark */
    normalize(!strncmp(text, "\xef\xbb\xbf", 3) ? text + 3 : text, after, sizeof(after));
    normalize(data->boot.options, before, sizeof(before));
    if (strcmp(before, after))
    {
        err = WexBootSetOptions(hwnd, text, &generation, message, sizeof(message));
        if (err && err != ERROR_CANCELLED) show_error(hwnd, err, message);
        else if (!err) lstrcpynA(data->boot.options, after, sizeof(data->boot.options));
    }
    free(text);
}

static INT_PTR CALLBACK startrec_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct startrec *data = (struct startrec *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
        data = (struct startrec *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)data);
        load(hwnd, data);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_STRECLIST:
            toggle_timeout(hwnd, IDC_STRECLIST, IDC_STRRECLISTEDIT, IDC_STRRECLISTUPDWN);
            return TRUE;
        case IDC_STRRECREC:
            toggle_timeout(hwnd, IDC_STRRECREC, IDC_STRRECRECEDIT, IDC_STRRECRECUPDWN);
            return TRUE;
        case IDC_STRRECEDIT:
            edit_options(hwnd, data);
            return TRUE;
        case IDOK:
            if (save(hwnd, data)) EndDialog(hwnd, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void show_startrec_dialog(HWND parent)
{
    struct startrec *data = calloc(1, sizeof(*data));

    if (!data) return;
    DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_STARTREC), parent, startrec_proc, (LPARAM)data);
    free(data);
}
