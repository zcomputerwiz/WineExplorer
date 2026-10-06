/*
 * WineExplorer Desktop Environment - Advanced Page (sysdm.cpl)
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
