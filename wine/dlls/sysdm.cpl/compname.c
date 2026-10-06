/*
 * WineExplorer Desktop Environment - Computer Name Page (sysdm.cpl)
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

#include "sysdm.h"

INT_PTR CALLBACK compname_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        WCHAR comp_name[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = ARRAY_SIZE(comp_name);
        HKEY hKey;

        if (GetComputerNameW(comp_name, &size))
        {
            SetDlgItemTextW(hwnd, IDC_COMP_NAME_LABEL, comp_name);
        }

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\LanmanServer\\Parameters",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            WCHAR desc[256];
            size = sizeof(desc);
            if (RegQueryValueExW(hKey, L"srvcomment", NULL, NULL, (BYTE*)desc, &size) == ERROR_SUCCESS)
            {
                SetDlgItemTextW(hwnd, IDC_COMP_DESC, desc);
            }
            RegCloseKey(hKey);
        }
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_COMP_CHANGE_BTN)
        {
            MessageBoxW(hwnd, L"Computer name changes can be configured in your system configuration.",
                        L"System Properties", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR*)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            WCHAR desc[256];
            HKEY hKey;
            GetDlgItemTextW(hwnd, IDC_COMP_DESC, desc, ARRAY_SIZE(desc));
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\LanmanServer\\Parameters",
                                0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
            {
                RegSetValueExW(hKey, L"srvcomment", 0, REG_SZ, (BYTE*)desc, (wcslen(desc) + 1) * sizeof(WCHAR));
                RegCloseKey(hKey);
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
