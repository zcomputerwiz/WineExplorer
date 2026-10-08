/*
 * WineExplorer Desktop Environment - Power Management Implementation
 *
 * Implements authentic "Shut Down Windows" dialog and Linux power management
 * (systemd-logind D-Bus, systemctl poweroff/reboot, session termination).
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <wexhost.h>

#include "wine/debug.h"
#include "theme_engine.h"
#include "power_mgmt.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define IDC_SHUTDOWN_COMBO 101
#define IDC_SHUTDOWN_DESC  102
#define IDC_SHUTDOWN_OK    IDOK
#define IDC_SHUTDOWN_CANCEL IDCANCEL

static const WCHAR *action_names[] = {
    L"Shut down",
    L"Restart",
    L"Log off",
    L"Stand by"
};

static const WCHAR *action_descs[] = {
    L"Ends your session and shuts down the computer so that you can safely turn off power.",
    L"Ends your session, shuts down Windows, and starts Windows again.",
    L"Ends your session and leaves the computer running so that someone else can log on.",
    L"Maintains your session, keeping the computer running on low power."
};

static BOOL CALLBACK enum_end_session_proc(HWND hwnd, LPARAM lparam)
{
    struct { UINT msg; WPARAM wparam; LPARAM lparam; BOOL veto; } *ctx = (void *)lparam;
    DWORD pid;
    DWORD_PTR result = 1;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER)) return TRUE;

    /* a window that does not answer in 5 s is hung and does not block shutdown, as on Windows */
    if (SendMessageTimeoutW(hwnd, ctx->msg, ctx->wparam, ctx->lparam, SMTO_ABORTIFHUNG, 5000, &result) &&
        ctx->msg == WM_QUERYENDSESSION && !result)
    {
        ctx->veto = TRUE;
        return FALSE;
    }
    return TRUE;
}

/* Ask every top-level window whether the session may end; on yes tell them it is ending. */
static BOOL end_session_with_consent(void)
{
    struct { UINT msg; WPARAM wparam; LPARAM lparam; BOOL veto; } ctx = { WM_QUERYENDSESSION, 0, 0, FALSE };

    EnumWindows(enum_end_session_proc, (LPARAM)&ctx);
    if (ctx.veto) return FALSE;

    ctx.msg = WM_ENDSESSION; ctx.wparam = TRUE; ctx.lparam = 0;
    EnumWindows(enum_end_session_proc, (LPARAM)&ctx);
    return TRUE;
}

static void report_power_error(HWND owner, DWORD err)
{
    if (err == ERROR_CANCELLED || err == ERROR_SUCCESS) return;
    MessageBoxW(owner, err == ERROR_NOT_SUPPORTED ? L"This action is not available on this computer." :
                L"You do not have permission to do this.", L"Turn Off Computer", MB_OK | MB_ICONWARNING);
}

void power_mgmt_execute_action(int action)
{
    HWND owner = GetDesktopWindow();
    DWORD can = WEX_POWER_NO, err;

    TRACE("Executing power action: %d\n", action);

    switch (action)
    {
    case POWER_ACTION_SHUTDOWN:
    case POWER_ACTION_RESTART:
    {
        DWORD wex = (action == POWER_ACTION_SHUTDOWN) ? WEX_POWER_SHUTDOWN : WEX_POWER_REBOOT;

        if (WexPowerCan(wex, &can) || can == WEX_POWER_NO)
        {
            report_power_error(owner, ERROR_NOT_SUPPORTED);
            return;
        }
        if (!end_session_with_consent())
        {
            MessageBoxW(owner, L"A program is preventing Windows from shutting down.", L"Turn Off Computer",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        theme_play_sound(L"Shutdown");
        /* login1 (through polkit) powers off the machine and ends the whole session, Wine included */
        err = WexPowerDo(owner, wex);
        report_power_error(owner, err);
        break;
    }

    case POWER_ACTION_LOGOFF:
        theme_play_sound(L"Logoff");
        ExitWindowsEx(EWX_LOGOFF, 0);
        /* Exiting the explorer shell terminates the display manager session */
        ExitProcess(0);
        break;

    case POWER_ACTION_STANDBY:
        if (WexPowerCan(WEX_POWER_SUSPEND, &can) || can == WEX_POWER_NO)
            report_power_error(owner, ERROR_NOT_SUPPORTED);
        else
            report_power_error(owner, WexPowerDo(owner, WEX_POWER_SUSPEND));
        break;
    }
}

/* Helper to construct dialog in memory without requiring .rc recompilation */
void power_mgmt_show_shutdown_dialog(HWND parent)
{
    /* Use programmatic dialog creation */
    HWND dlg;
    HWND combo, desc, ok_btn, cancel_btn, label, icon_ctl;
    HICON hWinLogo;
    HFONT font, bold_font;
    LOGFONTW lf;
    MSG msg;
    int dlg_w = 400, dlg_h = 240;
    int scr_w = GetSystemMetrics(SM_CXSCREEN);
    int scr_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (scr_w - dlg_w) / 2;
    int y = (scr_h - dlg_h) / 2;
    int i;
    BOOL modal_running = TRUE;

    dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
                          L"#32770", L"Shut Down Windows",
                          WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
                          x, y, dlg_w, dlg_h, parent, NULL, GetModuleHandleW(NULL), NULL);

    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -11;
    lf.lfWeight = FW_NORMAL;
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    font = CreateFontIndirectW(&lf);

    lf.lfWeight = FW_BOLD;
    bold_font = CreateFontIndirectW(&lf);

    /* Windows Logo Icon */
    hWinLogo = LoadIconW(NULL, (const WCHAR *)IDI_WINLOGO);
    icon_ctl = CreateWindowW(L"Static", NULL, WS_CHILD | WS_VISIBLE | SS_ICON,
                             20, 20, 32, 32, dlg, NULL, NULL, NULL);
    SendMessageW(icon_ctl, STM_SETICON, (WPARAM)hWinLogo, 0);

    /* Label */
    label = CreateWindowW(L"Static", L"What do you want the computer to do?",
                          WS_CHILD | WS_VISIBLE | SS_LEFT,
                          70, 25, 300, 20, dlg, NULL, NULL, NULL);
    SendMessageW(label, WM_SETFONT, (WPARAM)bold_font, TRUE);

    /* ComboBox */
    combo = CreateWindowW(L"ComboBox", NULL,
                          WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                          70, 55, 290, 150, dlg, (HMENU)IDC_SHUTDOWN_COMBO, NULL, NULL);
    SendMessageW(combo, WM_SETFONT, (WPARAM)font, TRUE);

    for (i = 0; i < 4; i++)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)action_names[i]);
    SendMessageW(combo, CB_SETCURSEL, 0, 0);

    /* Description */
    desc = CreateWindowW(L"Static", action_descs[0],
                         WS_CHILD | WS_VISIBLE | SS_LEFT,
                         70, 95, 290, 50, dlg, (HMENU)IDC_SHUTDOWN_DESC, NULL, NULL);
    SendMessageW(desc, WM_SETFONT, (WPARAM)font, TRUE);

    /* Buttons */
    ok_btn = CreateWindowW(L"Button", L"OK",
                           WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                           120, 160, 75, 24, dlg, (HMENU)IDC_SHUTDOWN_OK, NULL, NULL);
    SendMessageW(ok_btn, WM_SETFONT, (WPARAM)font, TRUE);

    cancel_btn = CreateWindowW(L"Button", L"Cancel",
                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                               210, 160, 75, 24, dlg, (HMENU)IDC_SHUTDOWN_CANCEL, NULL, NULL);
    SendMessageW(cancel_btn, WM_SETFONT, (WPARAM)font, TRUE);

    SetFocus(ok_btn);

    /* Modal Message Loop */
    while (modal_running && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_COMMAND)
        {
            if (LOWORD(msg.wParam) == IDC_SHUTDOWN_COMBO && HIWORD(msg.wParam) == CBN_SELCHANGE)
            {
                int sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < 4)
                    SetWindowTextW(desc, action_descs[sel]);
            }
            else if (LOWORD(msg.wParam) == IDC_SHUTDOWN_OK)
            {
                int sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
                DestroyWindow(dlg);
                modal_running = FALSE;
                power_mgmt_execute_action(sel);
                break;
            }
            else if (LOWORD(msg.wParam) == IDC_SHUTDOWN_CANCEL)
            {
                DestroyWindow(dlg);
                modal_running = FALSE;
                break;
            }
        }
        else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            DestroyWindow(dlg);
            modal_running = FALSE;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(font);
    DeleteObject(bold_font);
}
