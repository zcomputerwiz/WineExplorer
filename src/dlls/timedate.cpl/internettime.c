/*
 * WineExplorer Desktop Environment - Internet Time Page (timedate.cpl)
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

/*
 * The Internet Time tab: what the host's time service (systemd-timesyncd) does, and "Change settings...", which opens
 * the dialog of Windows 7 and later: the check box turns network time synchronisation on or off (timedated, through
 * WexTimedateSetNtp), the server box names the servers (org.wineexplorer.TimeSync1, WexTimeSyncSetServers) and Update
 * now asks the service for a synchronisation at once (WexTimeSyncNow). Each change asks polkit for an administrator's
 * password, as changing the clock does.
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wexhost.h>

#include "timedate.h"

#define TIMER_REFRESH 1

struct inet_state
{
    WEX_TIMEDATE info;
    BOOL  have_info;
    char  servers[WEX_TIMESYNC_SERVERS_MAX + 1];
    BOOL  have_servers;
};

static void read_state(struct inet_state *st)
{
    memset(st, 0, sizeof(*st));
    st->have_info = WexTimedateGet(&st->info) == ERROR_SUCCESS;
    st->have_servers = WexTimeSyncGet(st->servers, sizeof(st->servers)) == ERROR_SUCCESS;
}

static void servers_to_wide(const char *servers, WCHAR *out, int count)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, servers, -1, out, count)) out[0] = 0;
}

static void describe(const struct inet_state *st, WCHAR *text, int count)
{
    WCHAR servers[WEX_TIMESYNC_SERVERS_MAX + 1], line[WEX_TIMESYNC_SERVERS_MAX + 128];

    if (!st->have_info || !st->info.can_ntp)
    {
        lstrcpynW(text, L"Network time synchronization is not available on this computer: the time service is missing.", count);
        return;
    }
    if (!st->info.ntp_enabled)
    {
        lstrcpynW(text, L"This computer is not set to synchronize automatically with an Internet time server.", count);
        return;
    }
    servers_to_wide(st->servers, servers, ARRAY_SIZE(servers));
    if (servers[0]) swprintf(line, ARRAY_SIZE(line), L"This computer is set to automatically synchronize with:\r\n%ls", servers);
    else lstrcpyW(line, L"This computer is set to automatically synchronize with the time servers of the system.");
    lstrcatW(line, st->info.ntp_synchronized ? L"\r\n\r\nThe clock has been synchronized." :
                                              L"\r\n\r\nThe clock has not been synchronized yet.");
    lstrcpynW(text, line, count);
}

static void update_status(HWND hwnd)
{
    struct inet_state st;
    WCHAR text[WEX_TIMESYNC_SERVERS_MAX + 256];

    read_state(&st);
    describe(&st, text, ARRAY_SIZE(text));
    SetDlgItemTextW(hwnd, IDC_INET_STATUS, text);
}

/* ------------------------------------------------------------------ the settings dialog */

static const WCHAR *const presets[] = { L"pool.ntp.org", L"time.cloudflare.com", L"time.google.com", L"time.nist.gov",
                                        L"ntp.ubuntu.com" };
#define DEFAULT_ENTRY L"(time servers of the system)"

static void dialog_status(HWND hwnd, const struct inet_state *st)
{
    WCHAR text[WEX_TIMESYNC_SERVERS_MAX + 256];

    describe(st, text, ARRAY_SIZE(text));
    SetDlgItemTextW(hwnd, IDC_INET_DLGSTATUS, text);
}

static void show_error(HWND hwnd, DWORD err, const WCHAR *what)
{
    WCHAR text[256];

    if (err == ERROR_CANCELLED) return;
    if (err == ERROR_ACCESS_DENIED) swprintf(text, ARRAY_SIZE(text), L"You do not have permission to change %ls.", what);
    else if (err == ERROR_INVALID_PARAMETER) lstrcpyW(text, L"The server name is not valid. Use host names or addresses, separated by spaces (at most 8).");
    else if (err == ERROR_NOT_SUPPORTED) swprintf(text, ARRAY_SIZE(text), L"%ls cannot be changed on this computer.", what);
    else swprintf(text, ARRAY_SIZE(text), L"%ls could not be changed (error %lu).", what, err);
    MessageBoxW(hwnd, text, L"Internet Time Settings", MB_OK | MB_ICONWARNING);
}

static INT_PTR CALLBACK settings_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        struct inet_state st;
        WCHAR servers[WEX_TIMESYNC_SERVERS_MAX + 1];
        HWND combo = GetDlgItem(hwnd, IDC_INET_SERVER);
        unsigned int i;

        read_state(&st);
        CheckDlgButton(hwnd, IDC_INET_AUTO, st.have_info && st.info.ntp_enabled ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(hwnd, IDC_INET_AUTO), st.have_info && st.info.can_ntp);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)DEFAULT_ENTRY);
        for (i = 0; i < ARRAY_SIZE(presets); i++) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)presets[i]);
        SendMessageW(combo, CB_LIMITTEXT, WEX_TIMESYNC_SERVERS_MAX, 0);
        servers_to_wide(st.servers, servers, ARRAY_SIZE(servers));
        SetWindowTextW(combo, servers[0] ? servers : DEFAULT_ENTRY);
        /* without the service (or without time synchronization at all) there is nothing to name or to ask for */
        EnableWindow(combo, st.have_servers && st.info.can_ntp);
        EnableWindow(GetDlgItem(hwnd, IDC_INET_UPDATE), st.have_servers && st.have_info && st.info.can_ntp);
        dialog_status(hwnd, &st);
        return TRUE;
    }

    case WM_TIMER:
        if (wparam == TIMER_REFRESH)
        {
            struct inet_state st;
            KillTimer(hwnd, TIMER_REFRESH);
            read_state(&st);
            dialog_status(hwnd, &st);
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_INET_UPDATE:
        {
            DWORD err = WexTimeSyncNow(hwnd);
            if (err) show_error(hwnd, err, L"the clock");
            else
            {
                SetDlgItemTextW(hwnd, IDC_INET_DLGSTATUS, L"Synchronizing...");
                SetTimer(hwnd, TIMER_REFRESH, 4000, NULL);   /* the service needs a moment to reach a server */
            }
            return TRUE;
        }

        case IDOK:
        {
            struct inet_state st;
            WCHAR wide[WEX_TIMESYNC_SERVERS_MAX + 1];
            char servers[WEX_TIMESYNC_SERVERS_MAX * 3 + 1];
            BOOL on = IsDlgButtonChecked(hwnd, IDC_INET_AUTO) == BST_CHECKED;
            DWORD err;

            read_state(&st);
            if (st.have_info && st.info.can_ntp && on != (st.info.ntp_enabled != 0))
            {
                if ((err = WexTimedateSetNtp(hwnd, on)))
                {
                    show_error(hwnd, err, L"network time synchronization");
                    return TRUE;
                }
            }
            if (st.have_servers && IsWindowEnabled(GetDlgItem(hwnd, IDC_INET_SERVER)))
            {
                GetDlgItemTextW(hwnd, IDC_INET_SERVER, wide, ARRAY_SIZE(wide));
                if (!wcscmp(wide, DEFAULT_ENTRY)) wide[0] = 0;
                if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, servers, sizeof(servers), NULL, NULL)) servers[0] = 0;
                /* the same list, written the same way, is not a change (no password, no restart of the service) */
                if (strcmp(servers, st.servers) && (err = WexTimeSyncSetServers(hwnd, servers)))
                {
                    show_error(hwnd, err, L"the Internet time servers");
                    return TRUE;
                }
            }
            EndDialog(hwnd, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the tab */

INT_PTR CALLBACK internettime_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        update_status(hwnd);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_INET_CHANGE && HIWORD(wparam) == BN_CLICKED)
        {
            DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_INETSETTINGS), hwnd, settings_dlg_proc);
            update_status(hwnd);
            return TRUE;
        }
        break;

    case WM_NOTIFY:
        if (((NMHDR *)lparam)->code == PSN_SETACTIVE) update_status(hwnd);
        break;
    }
    return FALSE;
}
