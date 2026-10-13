/*
 * WineExplorer Desktop Environment - Advanced Page (sysdm.cpl)
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

#include "sysdm.h"

INT_PTR CALLBACK advanced_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_ADV_PERF_BTN:
            show_performance_dialog(hwnd);
            return TRUE;
        case IDC_ADV_PROFILE_BTN:
            MessageBoxW(hwnd, L"User Profiles are configured automatically in your Wine desktop session.",
                        L"User Profiles", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        case IDC_ADV_STARTUP_BTN:
            MessageBoxW(hwnd, L"Startup and recovery options are managed by systemd.",
                        L"Startup and Recovery", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        case IDC_ADV_ENVVAR_BTN:
            show_envvars_dialog(hwnd);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
