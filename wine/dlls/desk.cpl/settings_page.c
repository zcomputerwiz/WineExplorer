/*
 * WineExplorer Desktop Environment - Display Properties Settings Page
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

#include "resource.h"

typedef struct _RES_PRESET
{
    int width;
    int height;
} RES_PRESET;

static const RES_PRESET presets[] = {
    { 800, 600 },
    { 1024, 768 },
    { 1280, 720 },
    { 1366, 768 },
    { 1600, 900 },
    { 1920, 1080 },
    { 2560, 1440 },
    { 3840, 2160 }
};

#define NUM_PRESETS (sizeof(presets) / sizeof(presets[0]))

static int current_preset_idx = 5; /* 1920x1080 */

static void update_resolution_label(HWND hwnd, int idx)
{
    WCHAR buf[64];
    if (idx < 0) idx = 0;
    if (idx >= (int)NUM_PRESETS) idx = NUM_PRESETS - 1;

    swprintf(buf, 64, L"%d by %d pixels", presets[idx].width, presets[idx].height);
    SetDlgItemTextW(hwnd, IDC_SET_RES_TEXT, buf);
}

INT_PTR CALLBACK settings_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hSlider = GetDlgItem(hwnd, IDC_SET_RES_SLIDER);
        HWND hBpp = GetDlgItem(hwnd, IDC_SET_BPP_COMBO);
        int cur_w = GetSystemMetrics(SM_CXSCREEN);
        int cur_h = GetSystemMetrics(SM_CYSCREEN);
        unsigned int i;

        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, NUM_PRESETS - 1));

        /* Find closest preset to current resolution */
        current_preset_idx = 5;
        for (i = 0; i < NUM_PRESETS; i++)
        {
            if (presets[i].width == cur_w && presets[i].height == cur_h)
            {
                current_preset_idx = i;
                break;
            }
        }

        SendMessageW(hSlider, TBM_SETPOS, TRUE, current_preset_idx);
        update_resolution_label(hwnd, current_preset_idx);

        SendMessageW(hBpp, CB_ADDSTRING, 0, (LPARAM)L"Highest (32 bit)");
        SendMessageW(hBpp, CB_ADDSTRING, 0, (LPARAM)L"Medium (16 bit)");
        SendMessageW(hBpp, CB_SETCURSEL, 0, 0);

        return TRUE;
    }

    case WM_HSCROLL:
    {
        HWND hSlider = GetDlgItem(hwnd, IDC_SET_RES_SLIDER);
        if ((HWND)lparam == hSlider)
        {
            current_preset_idx = SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            update_resolution_label(hwnd, current_preset_idx);
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            DEVMODEW dm;
            memset(&dm, 0, sizeof(dm));
            dm.dmSize = sizeof(dm);
            dm.dmPelsWidth = presets[current_preset_idx].width;
            dm.dmPelsHeight = presets[current_preset_idx].height;
            dm.dmBitsPerPel = 32;
            dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;

            ChangeDisplaySettingsExW(NULL, &dm, NULL, CDS_UPDATEREGISTRY, NULL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
