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
 * The Internet Time tab of Windows XP. The host's time service (systemd-timesyncd, through timedated) does the
 * work: the check box turns network time synchronisation on or off (WexTimedateSetNtp). The server is the one the
 * system is configured with (timesyncd.conf), and the service synchronises by itself, so the server box and
 * Update Now are gray; the status line says what the service reports.
 */

#include <windows.h>
#include <commctrl.h>
#include <wexhost.h>

#include "timedate.h"

static BOOL g_ntp_modified;   /* PSN_APPLY reaches every visited page, so only act on a real edit */

static void update_status(HWND hwnd, const WEX_TIMEDATE *info, BOOL have_info)
{
    const WCHAR *text;

    if (!have_info || !info->can_ntp)
        text = L"Network time synchronization is not available on this computer: the time service is missing.";
    else if (!info->ntp_enabled)
        text = L"Automatic synchronization is turned off.";
    else if (info->ntp_synchronized)
        text = L"The clock is synchronized with the network time servers of the system.";
    else
        text = L"Automatic synchronization is on; the clock has not been synchronized yet.";
    SetDlgItemTextW(hwnd, IDC_INET_STATUS, text);
}

INT_PTR CALLBACK internettime_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        WEX_TIMEDATE info;
        BOOL have = WexTimedateGet(&info) == ERROR_SUCCESS;

        g_ntp_modified = FALSE;
        CheckDlgButton(hwnd, IDC_INET_AUTO, have && info.ntp_enabled ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(hwnd, IDC_INET_AUTO), have && info.can_ntp);
        /* these two have no counterpart: the service takes its servers from the system's configuration */
        SendDlgItemMessageW(hwnd, IDC_INET_SERVER, CB_ADDSTRING, 0, (LPARAM)L"Set by the system");
        SendDlgItemMessageW(hwnd, IDC_INET_SERVER, CB_SETCURSEL, 0, 0);
        EnableWindow(GetDlgItem(hwnd, IDC_INET_SERVER), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_INET_UPDATE), FALSE);
        update_status(hwnd, &info, have);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_INET_AUTO && HIWORD(wparam) == BN_CLICKED)
        {
            g_ntp_modified = TRUE;
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nm = (NMHDR *)lparam;

        if (nm->code == PSN_APPLY && g_ntp_modified)
        {
            BOOL on = IsDlgButtonChecked(hwnd, IDC_INET_AUTO) == BST_CHECKED;
            DWORD err = WexTimedateSetNtp(GetParent(hwnd), on);

            if (err)
            {
                if (err != ERROR_CANCELLED)
                    MessageBoxW(hwnd, err == ERROR_NOT_SUPPORTED ?
                                L"Network time synchronization cannot be changed on this computer." :
                                L"You do not have permission to change network time synchronization.",
                                L"Date and Time Properties", MB_OK | MB_ICONWARNING);
                SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }
            g_ntp_modified = FALSE;
            {
                WEX_TIMEDATE info;
                BOOL have = WexTimedateGet(&info) == ERROR_SUCCESS;
                update_status(hwnd, &info, have);
            }
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
