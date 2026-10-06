/*
 * WineExplorer Desktop Environment - Hardware Page (sysdm.cpl)
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
