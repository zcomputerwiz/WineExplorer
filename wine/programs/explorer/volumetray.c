/*
 * WineExplorer Desktop Environment - Volume System Tray Applet Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 system tray volume monitor,
 * featuring dynamic speaker / mute icons, master volume tooltips, left-click
 * popup slider, double-click mixer launch, and right-click audio properties menu.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <shellapi.h>

#include "wine/debug.h"
#include "resource.h"
#include "audio_backend.h"
#include "trayvol_dialog.h"
#include "volumetray.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define WM_VOLUMETRAY_NOTIFY (WM_APP + 210)
#define IDT_VOLUMETRAY_TIMER 102
#define VOLUMETRAY_CLASS_NAME L"Volume Tray Window"

static HWND g_hwnd_volumetray = NULL;
static NOTIFYICONDATAW g_vol_nid = {0};
static AUDIO_STATUS_INFO g_audio_status;
static int g_current_vol_icon = 0;
static BOOL g_vol_icon_shown = FALSE;

static void update_vol_tray_icon_and_tooltip(HWND hwnd)
{
    HINSTANCE hInst = GetModuleHandleW(NULL);
    int new_icon_id;
    WCHAR new_tip[128] = {0};

    audio_backend_get_status(&g_audio_status);

    if (!g_audio_status.available)
    {
        /* No audio device: Windows shows no volume icon at all */
        if (g_vol_icon_shown)
        {
            Shell_NotifyIconW(NIM_DELETE, &g_vol_nid);
            g_vol_icon_shown = FALSE;
        }
        return;
    }

    if (g_audio_status.is_muted)
    {
        new_icon_id = IDI_MUTE;
        lstrcpyW(new_tip, L"Volume (muted)");
    }
    else
    {
        new_icon_id = IDI_VOLUME;
        wsprintfW(new_tip, L"Volume: %d%%", g_audio_status.volume_percent);
    }

    if (!g_vol_icon_shown || new_icon_id != g_current_vol_icon || wcscmp(new_tip, g_vol_nid.szTip) != 0)
    {
        g_current_vol_icon = new_icon_id;
        if (g_vol_nid.hIcon) DestroyIcon(g_vol_nid.hIcon);
        g_vol_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(new_icon_id));
        lstrcpynW(g_vol_nid.szTip, new_tip, 128);

        Shell_NotifyIconW(g_vol_icon_shown ? NIM_MODIFY : NIM_ADD, &g_vol_nid);
        g_vol_icon_shown = TRUE;
    }
}

static void show_volume_context_menu(HWND hwnd, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, IDM_VOL_OPEN_MIXER, L"&Open Volume Control");
    AppendMenuW(hMenu, MF_STRING, IDM_VOL_AUDIO_PROPS, L"&Adjust Audio Properties");

    SetMenuDefaultItem(hMenu, IDM_VOL_OPEN_MIXER, FALSE);

    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                              pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (cmd)
    {
    case IDM_VOL_OPEN_MIXER:
        ShellExecuteW(NULL, L"open", L"sndvol32.exe", NULL, NULL, SW_SHOWNORMAL);
        break;

    case IDM_VOL_AUDIO_PROPS:
        ShellExecuteW(NULL, L"open", L"control.exe", L"mmsys.cpl", NULL, SW_SHOWNORMAL);
        break;
    }
}

static LRESULT CALLBACK VolumeTrayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        audio_backend_init();

        memset(&g_vol_nid, 0, sizeof(g_vol_nid));
        g_vol_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_vol_nid.hWnd = hwnd;
        g_vol_nid.uID = 1002;
        g_vol_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_vol_nid.uCallbackMessage = WM_VOLUMETRAY_NOTIFY;

        update_vol_tray_icon_and_tooltip(hwnd);
        SetTimer(hwnd, IDT_VOLUMETRAY_TIMER, 500, NULL);
        return 0;
    }

    case WM_TIMER:
        if (wparam == IDT_VOLUMETRAY_TIMER)
        {
            update_vol_tray_icon_and_tooltip(hwnd);
        }
        return 0;

    case WM_VOLUMETRAY_NOTIFY:
    {
        static DWORD last_dblclk;
        POINT pt;
        GetCursorPos(&pt);

        switch (LOWORD(lparam))
        {
        case WM_LBUTTONUP:
            /* a click opens the slider on release (the press would deactivate it again); not after a double click */
            if (GetTickCount() - last_dblclk > 500) trayvol_toggle(hwnd, pt);
            break;

        case WM_LBUTTONDBLCLK:
            last_dblclk = GetTickCount();
            trayvol_dismiss();
            ShellExecuteW(NULL, L"open", L"sndvol32.exe", NULL, NULL, SW_SHOWNORMAL);
            break;

        case WM_RBUTTONUP:
            trayvol_dismiss();
            show_volume_context_menu(hwnd, pt);
            break;

        case WM_MOUSEWHEEL:
        {
            short delta = (short)HIWORD(wparam);
            int step = (delta > 0) ? 5 : -5;
            audio_backend_step_volume(step);
            update_vol_tray_icon_and_tooltip(hwnd);
            break;
        }
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, IDT_VOLUMETRAY_TIMER);
        trayvol_dismiss();
        if (g_vol_icon_shown)
        {
            Shell_NotifyIconW(NIM_DELETE, &g_vol_nid);
            g_vol_icon_shown = FALSE;
        }
        if (g_vol_nid.hIcon)
        {
            DestroyIcon(g_vol_nid.hIcon);
            g_vol_nid.hIcon = NULL;
        }
        audio_backend_shutdown();
        g_hwnd_volumetray = NULL;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

BOOL volumetray_init(HWND hwnd_tray)
{
    if (g_hwnd_volumetray) return TRUE;

    WNDCLASSEXW wc = {0};
    HINSTANCE hInst = GetModuleHandleW(NULL);

    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = VolumeTrayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = VOLUMETRAY_CLASS_NAME;

    RegisterClassExW(&wc);

    g_hwnd_volumetray = CreateWindowExW(0, VOLUMETRAY_CLASS_NAME, L"Volume Tray Window",
                                        WS_OVERLAPPED, 0, 0, 0, 0,
                                        NULL, NULL, hInst, NULL);

    TRACE("VolumeTray initialized with hwnd %p\n", g_hwnd_volumetray);
    return (g_hwnd_volumetray != NULL);
}

void volumetray_shutdown(void)
{
    if (g_hwnd_volumetray)
    {
        DestroyWindow(g_hwnd_volumetray);
        g_hwnd_volumetray = NULL;
    }
}

void volumetray_update(void)
{
    if (g_hwnd_volumetray)
    {
        update_vol_tray_icon_and_tooltip(g_hwnd_volumetray);
    }
}
