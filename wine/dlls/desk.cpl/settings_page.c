/*
 * WineExplorer Desktop Environment - Display Properties Settings Page
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>

#include "resource.h"

typedef struct _RES_ITEM
{
    int width;
    int height;
} RES_ITEM;

static RES_ITEM *g_resolutions = NULL;
static int g_res_count = 0;
static int g_selected_res_idx = 0;

static const RES_ITEM g_fallback_presets[] = {
    { 640, 480 },
    { 800, 600 },
    { 1024, 768 },
    { 1280, 720 },
    { 1280, 800 },
    { 1280, 1024 },
    { 1366, 768 },
    { 1440, 900 },
    { 1600, 900 },
    { 1680, 1050 },
    { 1920, 1080 },
    { 1920, 1200 },
    { 2560, 1440 },
    { 3840, 2160 }
};

static void init_resolutions(void)
{
    DEVMODEW dm;
    int i, j;
    int cur_w = GetSystemMetrics(SM_CXSCREEN);
    int cur_h = GetSystemMetrics(SM_CYSCREEN);

    if (g_resolutions) return;

    g_resolutions = malloc(64 * sizeof(RES_ITEM));
    g_res_count = 0;

    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);

    for (i = 0; EnumDisplaySettingsExW(NULL, i, &dm, 0); i++)
    {
        BOOL exists = FALSE;
        if (dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;

        for (j = 0; j < g_res_count; j++)
        {
            if (g_resolutions[j].width == (int)dm.dmPelsWidth &&
                g_resolutions[j].height == (int)dm.dmPelsHeight)
            {
                exists = TRUE;
                break;
            }
        }

        if (!exists && g_res_count < 64)
        {
            /* Insert sorted */
            int insert_idx = g_res_count;
            for (j = 0; j < g_res_count; j++)
            {
                if (g_resolutions[j].width > (int)dm.dmPelsWidth ||
                    (g_resolutions[j].width == (int)dm.dmPelsWidth && g_resolutions[j].height > (int)dm.dmPelsHeight))
                {
                    insert_idx = j;
                    break;
                }
            }

            for (j = g_res_count; j > insert_idx; j--)
                g_resolutions[j] = g_resolutions[j - 1];

            g_resolutions[insert_idx].width = dm.dmPelsWidth;
            g_resolutions[insert_idx].height = dm.dmPelsHeight;
            g_res_count++;
        }
    }

    if (g_res_count == 0)
    {
        /* Fallback */
        int count = ARRAY_SIZE(g_fallback_presets);
        for (i = 0; i < count; i++)
            g_resolutions[i] = g_fallback_presets[i];
        g_res_count = count;
    }

    /* Find current active resolution */
    g_selected_res_idx = 0;
    for (i = 0; i < g_res_count; i++)
    {
        if (g_resolutions[i].width == cur_w && g_resolutions[i].height == cur_h)
        {
            g_selected_res_idx = i;
            break;
        }
    }
}

static void update_resolution_label(HWND hwnd, int idx)
{
    WCHAR buf[64];
    if (idx < 0) idx = 0;
    if (idx >= g_res_count) idx = g_res_count - 1;

    swprintf(buf, 64, L"%d by %d pixels", g_resolutions[idx].width, g_resolutions[idx].height);
    SetDlgItemTextW(hwnd, IDC_SET_RES_TEXT, buf);
}

static INT_PTR CALLBACK confirm_dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        int timeout = (int)lparam;
        if (timeout <= 0) timeout = 15;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)timeout);
        SetTimer(hwnd, 1, 1000, NULL);
        return TRUE;
    }

    case WM_TIMER:
    {
        int timeout = (int)GetWindowLongPtrW(hwnd, DWLP_USER);
        timeout--;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)timeout);
        if (timeout <= 0)
        {
            KillTimer(hwnd, 1);
            EndDialog(hwnd, IDNO);
        }
        else
        {
            WCHAR buf[256];
            swprintf(buf, 256,
                     L"Your desktop has been reconfigured. Do you want to keep these settings?\n\nReverting in %d seconds",
                     timeout);
            SetDlgItemTextW(hwnd, IDC_CONFIRM_TEXT, buf);
        }
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDYES || LOWORD(wparam) == IDNO)
        {
            KillTimer(hwnd, 1);
            EndDialog(hwnd, LOWORD(wparam));
            return TRUE;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK settings_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hSlider = GetDlgItem(hwnd, IDC_SET_RES_SLIDER);
        HWND hBpp = GetDlgItem(hwnd, IDC_SET_BPP_COMBO);
        DISPLAY_DEVICEW dd;
        HDC hdc;
        int cur_bpp = 32;

        init_resolutions();

        /* Display Adapter / Monitor info */
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
        if (EnumDisplayDevicesW(NULL, 0, &dd, 0))
        {
            WCHAR disp_str[256];
            swprintf(disp_str, 256, L"Display:\n1. %s on %s",
                     dd.DeviceString[0] ? dd.DeviceString : L"Default Monitor",
                     dd.DeviceName);
            SetDlgItemTextW(hwnd, IDC_SET_DISPLAY_LBL, disp_str);
        }

        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, g_res_count - 1));
        SendMessageW(hSlider, TBM_SETPOS, TRUE, g_selected_res_idx);
        update_resolution_label(hwnd, g_selected_res_idx);

        hdc = GetDC(NULL);
        if (hdc)
        {
            cur_bpp = GetDeviceCaps(hdc, BITSPIXEL);
            ReleaseDC(NULL, hdc);
        }

        SendMessageW(hBpp, CB_ADDSTRING, 0, (LPARAM)L"Highest (32 bit)");
        SendMessageW(hBpp, CB_ADDSTRING, 0, (LPARAM)L"Medium (16 bit)");
        SendMessageW(hBpp, CB_SETCURSEL, (cur_bpp <= 16) ? 1 : 0, 0);

        return TRUE;
    }

    case WM_HSCROLL:
    {
        HWND hSlider = GetDlgItem(hwnd, IDC_SET_RES_SLIDER);
        if ((HWND)lparam == hSlider)
        {
            g_selected_res_idx = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            update_resolution_label(hwnd, g_selected_res_idx);
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_SET_ADVANCED)
        {
            MessageBoxW(hwnd, L"Advanced adapter properties are configured by the Gamescope display manager session.",
                        L"Display Properties", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        else if (LOWORD(wparam) == IDC_SET_BPP_COMBO && HIWORD(wparam) == CBN_SELCHANGE)
        {
            PropSheet_Changed(GetParent(hwnd), hwnd);
            return TRUE;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            if (g_selected_res_idx >= 0 && g_selected_res_idx < g_res_count)
            {
                DEVMODEW cur_dm;
                DEVMODEW new_dm;
                int bpp_sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_SET_BPP_COMBO), CB_GETCURSEL, 0, 0);
                int target_bpp = (bpp_sel == 1) ? 16 : 32;
                LONG ret;

                memset(&cur_dm, 0, sizeof(cur_dm));
                cur_dm.dmSize = sizeof(cur_dm);
                EnumDisplaySettingsExW(NULL, ENUM_CURRENT_SETTINGS, &cur_dm, 0);

                memset(&new_dm, 0, sizeof(new_dm));
                new_dm.dmSize = sizeof(new_dm);
                new_dm.dmPelsWidth = g_resolutions[g_selected_res_idx].width;
                new_dm.dmPelsHeight = g_resolutions[g_selected_res_idx].height;
                new_dm.dmBitsPerPel = target_bpp;
                new_dm.dmDisplayFrequency = cur_dm.dmDisplayFrequency ? cur_dm.dmDisplayFrequency : 60;
                new_dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;

                if (new_dm.dmPelsWidth != cur_dm.dmPelsWidth ||
                    new_dm.dmPelsHeight != cur_dm.dmPelsHeight ||
                    new_dm.dmBitsPerPel != cur_dm.dmBitsPerPel)
                {
                    ret = ChangeDisplaySettingsExW(NULL, &new_dm, NULL, CDS_UPDATEREGISTRY, NULL);
                    if (ret == DISP_CHANGE_SUCCESSFUL)
                    {
                        INT_PTR keep = DialogBoxParamW(GetModuleHandleW(L"desk.cpl"),
                                                       MAKEINTRESOURCEW(IDD_CONFIRMSETTINGS),
                                                       hwnd, confirm_dialog_proc, (LPARAM)15);
                        if (keep != IDYES)
                        {
                            int i;
                            /* Revert */
                            ChangeDisplaySettingsExW(NULL, &cur_dm, NULL, CDS_UPDATEREGISTRY, NULL);

                            /* Restore slider pos */
                            for (i = 0; i < g_res_count; i++)
                            {
                                if (g_resolutions[i].width == (int)cur_dm.dmPelsWidth &&
                                    g_resolutions[i].height == (int)cur_dm.dmPelsHeight)
                                {
                                    g_selected_res_idx = i;
                                    SendMessageW(GetDlgItem(hwnd, IDC_SET_RES_SLIDER), TBM_SETPOS, TRUE, i);
                                    update_resolution_label(hwnd, i);
                                    break;
                                }
                            }
                            SendMessageW(GetDlgItem(hwnd, IDC_SET_BPP_COMBO), CB_SETCURSEL,
                                         (cur_dm.dmBitsPerPel <= 16) ? 1 : 0, 0);
                        }
                    }
                    else
                    {
                        MessageBoxW(hwnd, L"The selected display mode could not be applied.",
                                    L"Display Properties", MB_OK | MB_ICONEXCLAMATION);
                    }
                }
            }
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
        if (g_resolutions)
        {
            free(g_resolutions);
            g_resolutions = NULL;
            g_res_count = 0;
        }
        break;
    }
    return FALSE;
}
