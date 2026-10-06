/*
 * WineExplorer Desktop Environment - Power Management Implementation
 *
 * Implements authentic "Shut Down Windows" dialog and Linux power management
 * (systemd-logind D-Bus, systemctl poweroff/reboot, session termination).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

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

static void execute_linux_power_command(const char *cmd)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char full_cmd[512];

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    /* Direct execution of Linux host command */
    snprintf(full_cmd, sizeof(full_cmd), "/bin/sh -c \"%s\"", cmd);
    TRACE("Executing Linux power command: %s\n", full_cmd);

    if (CreateProcessA(NULL, full_cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void power_mgmt_execute_action(int action)
{
    TRACE("Executing power action: %d\n", action);

    switch (action)
    {
    case POWER_ACTION_SHUTDOWN:
        theme_play_sound(L"Shutdown");
        ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0);

        /* Linux Power Off: systemd-logind via D-Bus, or systemctl poweroff */
        execute_linux_power_command(
            "systemctl poweroff || "
            "dbus-send --system --print-reply --dest=org.freedesktop.login1 "
            "/org/freedesktop/login1 org.freedesktop.login1.Manager.PowerOff boolean:true || "
            "shutdown -h now"
        );
        ExitProcess(0);
        break;

    case POWER_ACTION_RESTART:
        theme_play_sound(L"Shutdown");
        ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);

        /* Linux Reboot: systemd-logind via D-Bus, or systemctl reboot */
        execute_linux_power_command(
            "systemctl reboot || "
            "dbus-send --system --print-reply --dest=org.freedesktop.login1 "
            "/org/freedesktop/login1 org.freedesktop.login1.Manager.Reboot boolean:true || "
            "shutdown -r now"
        );
        ExitProcess(0);
        break;

    case POWER_ACTION_LOGOFF:
        theme_play_sound(L"Logoff");
        ExitWindowsEx(EWX_LOGOFF, 0);
        /* Exiting the explorer shell terminates the display manager session */
        ExitProcess(0);
        break;

    case POWER_ACTION_STANDBY:
        /* Suspend via systemd */
        execute_linux_power_command(
            "systemctl suspend || "
            "dbus-send --system --print-reply --dest=org.freedesktop.login1 "
            "/org/freedesktop/login1 org.freedesktop.login1.Manager.Suspend boolean:true"
        );
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
