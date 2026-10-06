/*
 * WineExplorer Desktop Environment - Tray Volume Slider Popup Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 vertical volume popup slider
 * dialog ("Tray Volume") displayed upon single-clicking the taskbar volume icon.
 */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <commctrl.h>

#include "wine/debug.h"
#include "resource.h"
#include "audio_backend.h"
#include "trayvol_dialog.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

static HWND g_hwnd_trayvol = NULL;

static INT_PTR CALLBACK TrayVolDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        AUDIO_STATUS_INFO status;
        HWND hTrack = GetDlgItem(hwnd, IDC_TRAY_VOLUME_SLIDER);

        audio_backend_get_status(&status);

        if (hTrack)
        {
            SendMessageW(hTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hTrack, TBM_SETPAGESIZE, 0, 10);
            /* Trackbar vertical: 0 is top (max), 100 is bottom (min) */
            SendMessageW(hTrack, TBM_SETPOS, TRUE, 100 - status.volume_percent);
        }

        CheckDlgButton(hwnd, IDC_TRAY_VOLUME_MUTE,
                       status.is_muted ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }

    case WM_VSCROLL:
    {
        HWND hTrack = GetDlgItem(hwnd, IDC_TRAY_VOLUME_SLIDER);
        if (hTrack && (HWND)lparam == hTrack)
        {
            int pos = (int)SendMessageW(hTrack, TBM_GETPOS, 0, 0);
            int volume = 100 - pos;
            if (volume < 0) volume = 0;
            if (volume > 100) volume = 100;
            audio_backend_set_volume(volume);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_TRAY_VOLUME_MUTE:
            if (HIWORD(wparam) == BN_CLICKED)
            {
                BOOL mute = (IsDlgButtonChecked(hwnd, IDC_TRAY_VOLUME_MUTE) == BST_CHECKED);
                audio_backend_set_mute(mute);
            }
            return TRUE;

        case IDCANCEL:
            trayvol_dismiss();
            return TRUE;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE)
        {
            trayvol_dismiss();
            return TRUE;
        }
        break;

    case WM_DESTROY:
        g_hwnd_trayvol = NULL;
        return 0;
    }

    return FALSE;
}

void trayvol_toggle(HWND hwnd_tray_icon, POINT pt_cursor)
{
    if (trayvol_is_visible())
    {
        trayvol_dismiss();
        return;
    }

    HINSTANCE hInst = GetModuleHandleW(NULL);
    g_hwnd_trayvol = CreateDialogW(hInst, MAKEINTRESOURCEW(IDD_TRAYMASTER), NULL, TrayVolDlgProc);
    if (!g_hwnd_trayvol)
    {
        ERR("Failed to create IDD_TRAYMASTER dialog\n");
        return;
    }

    RECT rcWin, rcWork;
    HMONITOR hMon = MonitorFromPoint(pt_cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    rcWork = mi.rcWork;

    GetWindowRect(g_hwnd_trayvol, &rcWin);
    int width = rcWin.right - rcWin.left;
    int height = rcWin.bottom - rcWin.top;

    int x = pt_cursor.x - (width / 2);
    int y = pt_cursor.y - height - 4;

    /* Clamp inside working area */
    if (x + width > rcWork.right) x = rcWork.right - width - 4;
    if (x < rcWork.left) x = rcWork.left + 4;
    if (y < rcWork.top) y = rcWork.top + 4;
    if (y + height > rcWork.bottom) y = rcWork.bottom - height - 4;

    SetWindowPos(g_hwnd_trayvol, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwnd_trayvol);
}

void trayvol_dismiss(void)
{
    if (g_hwnd_trayvol && IsWindow(g_hwnd_trayvol))
    {
        DestroyWindow(g_hwnd_trayvol);
        g_hwnd_trayvol = NULL;
    }
}

BOOL trayvol_is_visible(void)
{
    return (g_hwnd_trayvol != NULL && IsWindow(g_hwnd_trayvol) && IsWindowVisible(g_hwnd_trayvol));
}
