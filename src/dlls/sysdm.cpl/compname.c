/*
 * WineExplorer Desktop Environment - Computer Name Page (sysdm.cpl)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * The computer name and description are the host's static and "pretty" hostnames (systemd hostname1);
 * changing them goes through polkit like on any Linux desktop.
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

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <wexhost.h>

#include "sysdm.h"

static WEX_HOSTNAME g_host;
static BOOL g_have_host;

static void show_name(HWND hwnd)
{
    WCHAR name[64];
    const char *src = g_host.static_name[0] ? g_host.static_name : g_host.transient_name;

    MultiByteToWideChar(CP_UTF8, 0, src, -1, name, ARRAY_SIZE(name));
    SetDlgItemTextW(hwnd, IDC_COMP_NAME_LABEL, name);
}

static INT_PTR CALLBACK change_name_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        WCHAR name[64];
        MultiByteToWideChar(CP_UTF8, 0, g_host.static_name, -1, name, ARRAY_SIZE(name));
        SetDlgItemTextW(hwnd, IDC_COMPCHG_NAME, name);
        SendDlgItemMessageW(hwnd, IDC_COMPCHG_NAME, EM_LIMITTEXT, 63, 0);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK)
        {
            WCHAR wname[64];
            char name[128];
            DWORD err;

            GetDlgItemTextW(hwnd, IDC_COMPCHG_NAME, wname, ARRAY_SIZE(wname));
            if (!wname[0]) return TRUE;
            WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, sizeof(name), NULL, NULL);
            err = WexHostnameSet(hwnd, name, FALSE);
            if (!err)
            {
                lstrcpynA(g_host.static_name, name, sizeof(g_host.static_name));
                MessageBoxW(hwnd, L"You must restart this computer for the changes to take effect.",
                            L"Computer Name Changes", MB_OK | MB_ICONINFORMATION);
                EndDialog(hwnd, IDOK);
            }
            else if (err != ERROR_CANCELLED)
                MessageBoxW(hwnd, err == ERROR_INVALID_PARAMETER ? L"The computer name is not valid." :
                            L"You do not have permission to rename this computer.",
                            L"Computer Name Changes", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL)
        {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK compname_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        WCHAR comp_name[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = ARRAY_SIZE(comp_name);

        if (GetComputerNameW(comp_name, &size))
            SetDlgItemTextW(hwnd, IDC_COMP_NAME_LABEL, comp_name);

        g_have_host = !WexHostnameGet(&g_host);
        if (g_have_host)
        {
            WCHAR desc[128];
            MultiByteToWideChar(CP_UTF8, 0, g_host.pretty_name, -1, desc, ARRAY_SIZE(desc));
            SetDlgItemTextW(hwnd, IDC_COMP_DESC, desc);
            show_name(hwnd);
        }
        else
        {
            EnableWindow(GetDlgItem(hwnd, IDC_COMP_DESC), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_COMP_CHANGE_BTN), FALSE);
        }
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_COMP_CHANGE_BTN && g_have_host)
        {
            if (DialogBoxParamW(GetModuleHandleW(L"sysdm.cpl"), MAKEINTRESOURCEW(IDD_COMPNAME_CHANGE), hwnd,
                                change_name_proc, 0) == IDOK)
                show_name(hwnd);
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_COMP_DESC && HIWORD(wparam) == EN_CHANGE)
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY && g_have_host)
        {
            WCHAR desc[128];
            char pretty[128];

            GetDlgItemTextW(hwnd, IDC_COMP_DESC, desc, ARRAY_SIZE(desc));
            WideCharToMultiByte(CP_UTF8, 0, desc, -1, pretty, sizeof(pretty), NULL, NULL);
            if (strcmp(pretty, g_host.pretty_name))
            {
                DWORD err = WexHostnameSet(hwnd, pretty, TRUE);
                if (!err) lstrcpynA(g_host.pretty_name, pretty, sizeof(g_host.pretty_name));
                else if (err != ERROR_CANCELLED)
                    MessageBoxW(hwnd, L"You do not have permission to change the computer description.",
                                L"System Properties", MB_OK | MB_ICONWARNING);
            }
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
