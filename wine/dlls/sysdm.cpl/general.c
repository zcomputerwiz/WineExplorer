/*
 * WineExplorer Desktop Environment - General System Information Page (sysdm.cpl)
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

#include "sysdm.h"

static void draw_windows_flag(HDC hdc, const RECT *rc)
{
    /* Four-color Windows flag emblem */
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2 - 6;
    int size = 26;
    int gap = 3;

    HBRUSH red_brush = CreateSolidBrush(RGB(235, 60, 40));
    HBRUSH green_brush = CreateSolidBrush(RGB(90, 185, 50));
    HBRUSH blue_brush = CreateSolidBrush(RGB(40, 130, 230));
    HBRUSH yellow_brush = CreateSolidBrush(RGB(250, 190, 30));
    RECT pane;

    /* Top Left: Red */
    SetRect(&pane, cx - size - gap, cy - size - gap, cx - gap, cy - gap);
    FillRect(hdc, &pane, red_brush);

    /* Top Right: Green */
    SetRect(&pane, cx + gap, cy - size - gap, cx + size + gap, cy - gap);
    FillRect(hdc, &pane, green_brush);

    /* Bottom Left: Blue */
    SetRect(&pane, cx - size - gap, cy + gap, cx - gap, cy + size + gap);
    FillRect(hdc, &pane, blue_brush);

    /* Bottom Right: Yellow */
    SetRect(&pane, cx + gap, cy + gap, cx + size + gap, cy + size + gap);
    FillRect(hdc, &pane, yellow_brush);

    DeleteObject(red_brush);
    DeleteObject(green_brush);
    DeleteObject(blue_brush);
    DeleteObject(yellow_brush);

    /* Title text under emblem */
    {
        HFONT font, old_font;
        LOGFONTW lf;
        RECT text_rc = { rc->left, cy + size + 14, rc->right, rc->bottom };

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -13;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        font = CreateFontIndirectW(&lf);

        old_font = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(20, 40, 100));
        DrawTextW(hdc, L"Microsoft\nWindows", -1, &text_rc, DT_CENTER);

        SelectObject(hdc, old_font);
        DeleteObject(font);
    }
}

INT_PTR CALLBACK general_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HKEY hKey;
        WCHAR buf[256];
        DWORD size;
        MEMORYSTATUSEX mem;

        /* 1. Registered info */
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"RegisteredOwner", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
                SetDlgItemTextW(hwnd, IDC_GEN_REG_USER, buf);

            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"RegisteredOrganization", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
                SetDlgItemTextW(hwnd, IDC_GEN_REG_ORG, buf);

            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"ProductId", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
                SetDlgItemTextW(hwnd, IDC_GEN_REG_PID, buf);

            RegCloseKey(hKey);
        }

        /* 2. Processor Info */
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
            {
                SetDlgItemTextW(hwnd, IDC_GEN_CPU_LABEL, buf);
            }
            RegCloseKey(hKey);
        }

        /* 3. RAM Info */
        memset(&mem, 0, sizeof(mem));
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem))
        {
            double gb = (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
            if (gb >= 1.0)
                swprintf(buf, 256, L"%.2f GB of RAM", gb);
            else
                swprintf(buf, 256, L"%lu MB of RAM", (DWORD)(mem.ullTotalPhys / (1024 * 1024)));
            SetDlgItemTextW(hwnd, IDC_GEN_RAM_LABEL, buf);
        }

        return TRUE;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lparam;
        if (dis->CtlID == IDC_GEN_LOGO_STATIC)
        {
            draw_windows_flag(dis->hDC, &dis->rcItem);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
