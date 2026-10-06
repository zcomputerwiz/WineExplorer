/*
 * WineExplorer Desktop Environment - Hardware Page (Shared for Mouse & Keyboard)
 */

#include "main_private.h"

static void init_hardware_listview(HWND hwnd, BOOL is_mouse)
{
    HWND hList = GetDlgItem(hwnd, IDC_DEV_LIST);
    LVCOLUMNW lvc;
    LVITEMW lvi;

    SendMessageW(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    memset(&lvc, 0, sizeof(lvc));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.cx = 140;
    lvc.pszText = (LPWSTR)(is_mouse ? L"Pointing Device" : L"Keyboard Device");
    SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

    lvc.cx = 90;
    lvc.pszText = (LPWSTR)L"Status";
    SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_TEXT;
    lvi.iItem = 0;
    lvi.pszText = (LPWSTR)(is_mouse ? L"Wayland Virtual Pointer (libinput)" : L"Standard 101/102-Key or Natural PS/2 Keyboard");
    SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

    lvi.iSubItem = 1;
    lvi.pszText = (LPWSTR)L"Working";
    SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lvi);

    SendMessageW(hList, LVM_SETITEMSTATE, 0,
                 MAKELPARAM(LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED));
}

INT_PTR CALLBACK mouse_hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        init_hardware_listview(hwnd, TRUE);
        SetDlgItemTextW(hwnd, IDC_DEV_DESC,
                        L"Location: Attached to Linux input subsystem (Wayland / libinput)\nManufacturer: Standard system device");
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_DEV_PROPERTIES)
        {
            MessageBoxW(hwnd,
                        L"Device: Wayland Virtual Pointer\nDriver: winex11 / winewayland input layer\nStatus: Device is operational and functioning normally.",
                        L"Device Properties", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK keyboard_hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        init_hardware_listview(hwnd, FALSE);
        SetDlgItemTextW(hwnd, IDC_DEV_DESC,
                        L"Location: Attached to Linux input subsystem (Wayland / xkbcommon)\nManufacturer: Standard system keyboard");
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_DEV_PROPERTIES)
        {
            MessageBoxW(hwnd,
                        L"Device: Standard Keyboard\nDriver: winex11 / winewayland keyboard layer\nStatus: Device is operational and functioning normally.",
                        L"Device Properties", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
