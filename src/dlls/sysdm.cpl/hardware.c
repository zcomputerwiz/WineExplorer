/*
 * WineExplorer Desktop Environment - Hardware Page (sysdm.cpl)
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

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "sysdm.h"

INT_PTR CALLBACK hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_HW_DEVMGR_BTN)
        {
            ShellExecuteW(hwnd, L"open", L"devmgmt.msc", NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        else if (LOWORD(wparam) == IDC_HW_PROFILES_BTN)
        {
            MessageBoxW(hwnd, L"Hardware Profiles are managed automatically by the display manager session.",
                        L"Hardware Profiles", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
