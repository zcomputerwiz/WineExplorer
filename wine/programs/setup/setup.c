/*
 * WineExplorer Desktop Environment - setup.c
 * GUI-mode Setup Billboard and Configuration Wizard
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

#define WIN32_LEAN_AND_MEAN
#define _WIN32_IE 0x0501
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <io.h>
#include <time.h>

#include "wexhost.h"
#include "resource.h"

typedef struct {
    char owner[128];
    char org[128];
    char comp_name[64];
    char admin_pass[128];
    char timezone[128];
    char workgroup[64];
    char locale[64];
    char layout[64];
    bool typical_net;
    bool unattended;
    char unattend_path[MAX_PATH];
    int current_page;
    int panel_index;
    int minutes_remaining;
    int install_step;
} SetupContext;

static SetupContext g_ctx;
static HWND g_hBillboard = NULL;
static HINSTANCE g_hInstance = NULL;

typedef struct {
    const char *title;
    const char *desc;
} BillboardPanel;

static const BillboardPanel g_panels[] = {
    {
        "An exciting new look",
        "Debian GNU/Linux with WineExplorer features a clean, streamlined design with the classic Luna visual style, making it easy to access your favorite programs, files, and settings."
    },
    {
        "Authentic Shell Experience",
        "Experience the authentic Windows XP and Server 2003 shell, control panels, accessories, and system applets built seamlessly on top of Debian."
    },
    {
        "Rock-solid Debian base",
        "Built on Debian GNU/Linux 13 (Trixie), delivering unmatched security, reliability, and stability for your workstation."
    },
    {
        "Seamless Hardware Detection",
        "Automatic detection and support for audio, network interfaces, multi-monitor displays, and USB peripherals."
    },
    {
        "Productivity and Connectivity",
        "Integrated networking, modern web standards, and comprehensive terminal tools designed for developers and power users alike."
    }
};

static const int g_panelCount = sizeof(g_panels) / sizeof(g_panels[0]);

static void LogSetup(const char *msg)
{
    FILE *f = fopen("Z:\\var\\log\\wineexplorer-setup.log", "a");
    if (!f) f = fopen("/var/log/wineexplorer-setup.log", "a");
    if (!f) f = fopen("wineexplorer-setup.log", "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
        fclose(f);
    }
}

static void PumpEvents(DWORD ms)
{
    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < ms) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(20);
    }
}

static bool ValidateComputerName(const char *name)
{
    size_t len, i;
    const char *invalid_chars = " \\/?:*\"<>|'`,;=+[]";

    if (!name || !*name) return false;
    len = strlen(name);
    if (len > 15) return false;

    for (i = 0; i < len; ++i) {
        if (strchr(invalid_chars, name[i])) return false;
        if ((unsigned char)name[i] < 33 || (unsigned char)name[i] > 126) return false;
    }
    return true;
}

static void CheckUnattended(void)
{
    int i;
    WCHAR wpath[MAX_PATH];
    WCHAR buf[256];
    const char *paths[] = {
        "Z:\\etc\\wineexplorer\\setup\\unattend.sif",
        "Z:\\run\\live\\medium\\setup\\unattend.sif",
        "Z:\\setup\\unattend.sif",
        "/etc/wineexplorer/setup/unattend.sif",
        "/run/live/medium/setup/unattend.sif",
        "/setup/unattend.sif",
        NULL
    };

    for (i = 0; paths[i]; ++i) {
        if (access(paths[i], 0) == 0) {
            g_ctx.unattended = true;
            snprintf(g_ctx.unattend_path, sizeof(g_ctx.unattend_path), "%s", paths[i]);
            break;
        }
    }

    if (!g_ctx.unattended) return;

    MultiByteToWideChar(CP_UTF8, 0, g_ctx.unattend_path, -1, wpath, MAX_PATH);

    /* [UserData] */
    if (GetPrivateProfileStringW(L"UserData", L"FullName", L"", buf, 256, wpath)) {
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_ctx.owner, sizeof(g_ctx.owner), NULL, NULL);
    }
    if (GetPrivateProfileStringW(L"UserData", L"OrgName", L"", buf, 256, wpath)) {
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_ctx.org, sizeof(g_ctx.org), NULL, NULL);
    }
    if (GetPrivateProfileStringW(L"UserData", L"ComputerName", L"", buf, 256, wpath)) {
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_ctx.comp_name, sizeof(g_ctx.comp_name), NULL, NULL);
    }

    /* [GuiUnattended] */
    if (GetPrivateProfileStringW(L"GuiUnattended", L"AdminPassword", L"", buf, 256, wpath)) {
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_ctx.admin_pass, sizeof(g_ctx.admin_pass), NULL, NULL);
    }
    if (GetPrivateProfileStringW(L"GuiUnattended", L"TimeZone", L"", buf, 256, wpath)) {
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_ctx.timezone, sizeof(g_ctx.timezone), NULL, NULL);
    }

    /* [Identification] */
    if (GetPrivateProfileStringW(L"Identification", L"JoinWorkgroup", L"WORKGROUP", buf, 256, wpath)) {
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_ctx.workgroup, sizeof(g_ctx.workgroup), NULL, NULL);
    }

    LogSetup("Parsed unattended answer file settings successfully.");
}

static void SaveSettings(void)
{
    DWORD err;
    char logmsg[256];
    FILE *fh, *fo, *fw;

    LogSetup("Applying system settings via D-Bus and host integration...");

    /* 1. Hostname via WexHostnameSet (calls org.freedesktop.hostname1) */
    if (g_ctx.comp_name[0]) {
        err = WexHostnameSet(NULL, g_ctx.comp_name, FALSE);
        snprintf(logmsg, sizeof(logmsg), "WexHostnameSet('%s') result: %lu", g_ctx.comp_name, err);
        LogSetup(logmsg);

        fh = fopen("Z:\\etc\\hostname", "w");
        if (!fh) fh = fopen("/etc/hostname", "w");
        if (fh) {
            fprintf(fh, "%s\n", g_ctx.comp_name);
            fclose(fh);
        }
    }

    /* 2. Registered Owner and Org */
    CreateDirectoryA("Z:\\etc\\wineexplorer", NULL);
    CreateDirectoryA("/etc/wineexplorer", NULL);
    fo = fopen("Z:\\etc\\wineexplorer\\owner", "w");
    if (!fo) fo = fopen("/etc/wineexplorer/owner", "w");
    if (fo) {
        fprintf(fo, "RegisteredOwner=%s\nRegisteredOrganization=%s\n",
                g_ctx.owner[0] ? g_ctx.owner : "Wine User",
                g_ctx.org[0] ? g_ctx.org : "Home");
        fclose(fo);
    }

    /* 3. Workgroup */
    fw = fopen("Z:\\etc\\wineexplorer\\workgroup", "w");
    if (!fw) fw = fopen("/etc/wineexplorer/workgroup", "w");
    if (fw) {
        fprintf(fw, "%s\n", g_ctx.workgroup[0] ? g_ctx.workgroup : "WORKGROUP");
        fclose(fw);
    }

    /* 4. Root / Admin Password via WexRootPasswordSet (AccountsService User0) */
    if (g_ctx.admin_pass[0]) {
        err = WexRootPasswordSet(g_ctx.admin_pass);
        snprintf(logmsg, sizeof(logmsg), "WexRootPasswordSet result: %lu", err);
        LogSetup(logmsg);
    }

    /* 5. Timezone via WexTimedateSetTimezone (calls org.freedesktop.timedate1) */
    if (g_ctx.timezone[0]) {
        err = WexTimedateSetTimezone(NULL, g_ctx.timezone);   /* timedate1 links /etc/localtime itself */
        snprintf(logmsg, sizeof(logmsg), "WexTimedateSetTimezone('%s') result: %lu", g_ctx.timezone, err);
        LogSetup(logmsg);
    }

    /* 6. Locale via WexLocaleSet (calls org.freedesktop.locale1) */
    if (g_ctx.locale[0] || g_ctx.layout[0]) {
        err = WexLocaleSet(g_ctx.locale[0] ? g_ctx.locale : "en_US.UTF-8",
                           g_ctx.layout[0] ? g_ctx.layout : "us");
        snprintf(logmsg, sizeof(logmsg), "WexLocaleSet result: %lu", err);
        LogSetup(logmsg);
    }

    /* The template prefix in /etc/skel/.wine is built by wex-gui-setup.sh once setup.exe has finished: system()
     * here would run Wine's cmd.exe, not a Linux shell. */
}

/* Billboard Painting */
static void PaintBillboard(HWND hwnd, HDC hdc)
{
    RECT rc, rc_left, rc_right, rc_title, rc_est, rc_panel_title, rc_panel_desc;
    HBRUSH hbr_left, hbr_right;
    HPEN hPen, hOldPen;
    HFONT hTitleFont, hStepFont, hDescFont, hOldFont;
    int left_width, y, i;
    char est_time[128];
    const BillboardPanel *cur_panel;
    const char *steps[] = {
        "Collecting information",
        "Dynamic update",
        "Preparing installation",
        "Installing Debian & Wine",
        "Finalizing installation"
    };

    GetClientRect(hwnd, &rc);
    left_width = 240;

    /* Left panel: Dark blue / teal gradient background */
    rc_left.left = 0; rc_left.top = 0; rc_left.right = left_width; rc_left.bottom = rc.bottom;
    hbr_left = CreateSolidBrush(RGB(10, 36, 106));
    FillRect(hdc, &rc_left, hbr_left);
    DeleteObject(hbr_left);

    /* Right panel: Light gray / off-white background */
    rc_right.left = left_width; rc_right.top = 0; rc_right.right = rc.right; rc_right.bottom = rc.bottom;
    hbr_right = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(hdc, &rc_right, hbr_right);
    DeleteObject(hbr_right);

    /* Separator line */
    hPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    hOldPen = SelectObject(hdc, hPen);
    MoveToEx(hdc, left_width, 0, NULL);
    LineTo(hdc, left_width, rc.bottom);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);

    /* Left pane texts */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    hTitleFont = CreateFontA(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");
    hOldFont = SelectObject(hdc, hTitleFont);

    rc_title.left = 20; rc_title.top = 25; rc_title.right = left_width - 15; rc_title.bottom = 60;
    DrawTextA(hdc, "WineExplorer Setup", -1, &rc_title, DT_LEFT | DT_TOP);

    hStepFont = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");
    SelectObject(hdc, hStepFont);

    y = 90;
    for (i = 0; i < 5; ++i) {
        if (i == g_ctx.install_step + 1) {
            SetTextColor(hdc, RGB(100, 255, 100));
            TextOutA(hdc, 15, y, "->", 2);
            SetTextColor(hdc, RGB(255, 255, 255));
        } else {
            SetTextColor(hdc, RGB(180, 200, 230));
        }
        TextOutA(hdc, 35, y, steps[i], (int)strlen(steps[i]));
        y += 28;
    }

    /* Estimated time remaining */
    SetTextColor(hdc, RGB(255, 255, 255));
    snprintf(est_time, sizeof(est_time), "Setup will complete in\napproximately: %d minutes", g_ctx.minutes_remaining);
    rc_est.left = 20; rc_est.top = rc.bottom - 80; rc_est.right = left_width - 15; rc_est.bottom = rc.bottom - 15;
    DrawTextA(hdc, est_time, -1, &rc_est, DT_LEFT | DT_TOP);

    /* Right pane: Showcase panel */
    SelectObject(hdc, hTitleFont);
    SetTextColor(hdc, RGB(10, 36, 106));

    cur_panel = &g_panels[g_ctx.panel_index];
    rc_panel_title.left = left_width + 40; rc_panel_title.top = 50; rc_panel_title.right = rc.right - 40; rc_panel_title.bottom = 90;
    DrawTextA(hdc, cur_panel->title, -1, &rc_panel_title, DT_LEFT | DT_TOP);

    hDescFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");
    SelectObject(hdc, hDescFont);
    SetTextColor(hdc, RGB(40, 40, 40));

    rc_panel_desc.left = left_width + 40; rc_panel_desc.top = 100; rc_panel_desc.right = rc.right - 60; rc_panel_desc.bottom = 280;
    DrawTextA(hdc, cur_panel->desc, -1, &rc_panel_desc, DT_LEFT | DT_TOP | DT_WORDBREAK);

    SelectObject(hdc, hOldFont);
    DeleteObject(hTitleFont);
    DeleteObject(hStepFont);
    DeleteObject(hDescFont);
}

/* Billboard Window Procedure */
static LRESULT CALLBACK BillboardWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            PaintBillboard(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

/* Wizard Page Procedures */
static INT_PTR CALLBACK RegionalPageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)PSWIZB_NEXT);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK UserInfoPageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            SetDlgItemTextA(hwnd, IDC_OWNER_NAME, g_ctx.owner);
            SetDlgItemTextA(hwnd, IDC_ORG_NAME, g_ctx.org);
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                GetDlgItemTextA(hwnd, IDC_OWNER_NAME, g_ctx.owner, sizeof(g_ctx.owner));
                GetDlgItemTextA(hwnd, IDC_ORG_NAME, g_ctx.org, sizeof(g_ctx.org));
                if (!g_ctx.owner[0]) {
                    MessageBoxA(hwnd, "You must enter your name before continuing.", "Setup", MB_OK | MB_ICONEXCLAMATION);
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK ComputerPageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            SetDlgItemTextA(hwnd, IDC_COMP_NAME, g_ctx.comp_name);
            SetDlgItemTextA(hwnd, IDC_ADMIN_PASS1, g_ctx.admin_pass);
            SetDlgItemTextA(hwnd, IDC_ADMIN_PASS2, g_ctx.admin_pass);
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                char comp[64], pass1[128], pass2[128];
                GetDlgItemTextA(hwnd, IDC_COMP_NAME, comp, sizeof(comp));
                GetDlgItemTextA(hwnd, IDC_ADMIN_PASS1, pass1, sizeof(pass1));
                GetDlgItemTextA(hwnd, IDC_ADMIN_PASS2, pass2, sizeof(pass2));

                if (!ValidateComputerName(comp)) {
                    MessageBoxA(hwnd,
                                "The computer name entered is invalid. Standard names may contain letters, numbers, and hyphens (up to 15 characters), but no spaces or quotes.",
                                "Setup", MB_OK | MB_ICONEXCLAMATION);
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }

                if (strcmp(pass1, pass2) != 0) {
                    MessageBoxA(hwnd, "The passwords you entered do not match. Please re-enter your password.", "Setup", MB_OK | MB_ICONEXCLAMATION);
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }

                snprintf(g_ctx.comp_name, sizeof(g_ctx.comp_name), "%s", comp);
                snprintf(g_ctx.admin_pass, sizeof(g_ctx.admin_pass), "%s", pass1);
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK DateTimePageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG: {
            int i;
            HWND hCombo = GetDlgItem(hwnd, IDC_TZ_COMBO);
            const char *tzs[] = {
                "Etc/UTC", "America/New_York", "America/Chicago", "America/Denver",
                "America/Los_Angeles", "Europe/London", "Europe/Paris", "Europe/Berlin",
                "Asia/Tokyo", "Asia/Shanghai", NULL
            };
            for (i = 0; tzs[i]; ++i) {
                SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)tzs[i]);
            }
            SendMessageA(hCombo, CB_SETCURSEL, 0, 0);
            return TRUE;
        }
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                char tz[128];
                GetDlgItemTextA(hwnd, IDC_TZ_COMBO, tz, sizeof(tz));
                if (tz[0]) snprintf(g_ctx.timezone, sizeof(g_ctx.timezone), "%s", tz);
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK NetworkPageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            CheckRadioButton(hwnd, IDC_NET_TYPICAL, IDC_NET_CUSTOM, IDC_NET_TYPICAL);
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK WorkgrpPageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            CheckRadioButton(hwnd, IDC_RADIO_WORKGROUP, IDC_RADIO_DOMAIN, IDC_RADIO_WORKGROUP);
            SetDlgItemTextA(hwnd, IDC_WORKGROUP, g_ctx.workgroup[0] ? g_ctx.workgroup : "WORKGROUP");
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_FINISH));
                return TRUE;
            } else if (pnmh->code == PSN_WIZFINISH) {
                GetDlgItemTextA(hwnd, IDC_WORKGROUP, g_ctx.workgroup, sizeof(g_ctx.workgroup));
            }
            break;
        }
    }
    return FALSE;
}

static int CALLBACK PropSheetCallback(HWND hwndDlg, UINT uMsg, LPARAM lParam)
{
    (void)lParam;
    if (uMsg == PSCB_INITIALIZED) {
        RECT rc;
        int w, h, x, y;
        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        if (screen_w < 800) screen_w = 1024;
        if (screen_h < 600) screen_h = 768;

        GetWindowRect(hwndDlg, &rc);
        w = rc.right - rc.left;
        h = rc.bottom - rc.top;
        x = 240 + (screen_w - 240 - w) / 2;
        y = (screen_h - h) / 2;
        if (x < 240) x = 240;
        if (y < 40) y = 40;
        SetWindowPos(hwndDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    }
    return 0;
}

static void ShowSetupWizard(HWND hParent)
{
    PROPSHEETPAGEA psp[6];
    PROPSHEETHEADERA psh;
    int page_count = 0;

    if (g_ctx.unattended) {
        LogSetup("Unattended installation: skipping interactive wizard pages.");
        return;
    }

    memset(psp, 0, sizeof(psp));

    #define ADD_PAGE(id, proc) do { \
        psp[page_count].dwSize = sizeof(PROPSHEETPAGEA); \
        psp[page_count].dwFlags = PSP_USETITLE; \
        psp[page_count].hInstance = g_hInstance; \
        psp[page_count].pszTemplate = MAKEINTRESOURCEA(id); \
        psp[page_count].pfnDlgProc = proc; \
        psp[page_count].pszTitle = "Setup"; \
        page_count++; \
    } while(0)

    ADD_PAGE(IDD_PAGE_REGIONAL, RegionalPageProc);
    ADD_PAGE(IDD_PAGE_USERINFO, UserInfoPageProc);
    ADD_PAGE(IDD_PAGE_COMPUTER, ComputerPageProc);
    ADD_PAGE(IDD_PAGE_DATETIME, DateTimePageProc);
    ADD_PAGE(IDD_PAGE_NETWORK,  NetworkPageProc);
    ADD_PAGE(IDD_PAGE_WORKGRP,  WorkgrpPageProc);

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERA);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_WIZARD | PSH_USECALLBACK;
    psh.pfnCallback = PropSheetCallback;
    psh.hwndParent = hParent;
    psh.hInstance = g_hInstance;
    psh.pszCaption = "Debian GNU/Linux with WineExplorer Setup";
    psh.nPages = page_count;
    psh.nStartPage = 0;
    psh.ppsp = psp;

    PropertySheetA(&psh);
}

static void RunAutomaticSetupSteps(HWND hwnd)
{
    DWORD hw_needed = 0;
    char hw_log[128];
    FILE *fstage;

    /* Step 0: Installing Devices */
    g_ctx.install_step = 0;
    g_ctx.minutes_remaining = 32;
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    LogSetup("Stage: Installing Devices");

    WexHardwareList(NULL, 0, &hw_needed);
    snprintf(hw_log, sizeof(hw_log), "Detected hardware devices size: %lu bytes", hw_needed);
    LogSetup(hw_log);
    PumpEvents(2000);

    /* Step 1: Installing Network */
    g_ctx.install_step = 1;
    g_ctx.minutes_remaining = 25;
    g_ctx.panel_index = (g_ctx.panel_index + 1) % g_panelCount;
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    LogSetup("Stage: Installing Network");
    WexNetEnable();
    PumpEvents(2000);

    /* Step 2: Copying and configuring components */
    g_ctx.install_step = 2;
    g_ctx.minutes_remaining = 10;
    g_ctx.panel_index = (g_ctx.panel_index + 1) % g_panelCount;
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    LogSetup("Stage: Copying components");
    SaveSettings();
    PumpEvents(2000);

    /* Step 3: Finalizing installation */
    g_ctx.install_step = 3;
    g_ctx.minutes_remaining = 1;
    g_ctx.panel_index = (g_ctx.panel_index + 1) % g_panelCount;
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    LogSetup("Stage: Finalizing Installation");
    PumpEvents(2000);

    /* Transition stage to OOBE */
    fstage = fopen("Z:\\etc\\wineexplorer\\setup\\stage", "w");
    if (!fstage) fstage = fopen("/etc/wineexplorer/setup/stage", "w");
    if (fstage) {
        fprintf(fstage, "oobe\n");
        fclose(fstage);
    }
    LogSetup("GUI Setup complete. Ready for OOBE.");
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA wc;
    int screen_w, screen_h;
    FILE *fdone;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_hInstance = hInstance;
    InitCommonControls();

    memset(&g_ctx, 0, sizeof(g_ctx));
    snprintf(g_ctx.owner, sizeof(g_ctx.owner), "Wine User");
    snprintf(g_ctx.org, sizeof(g_ctx.org), "Home");
    snprintf(g_ctx.comp_name, sizeof(g_ctx.comp_name), "WEX-PC");
    snprintf(g_ctx.workgroup, sizeof(g_ctx.workgroup), "WORKGROUP");
    snprintf(g_ctx.timezone, sizeof(g_ctx.timezone), "Etc/UTC");
    snprintf(g_ctx.locale, sizeof(g_ctx.locale), "en_US.UTF-8");
    snprintf(g_ctx.layout, sizeof(g_ctx.layout), "us");
    g_ctx.typical_net = true;
    g_ctx.minutes_remaining = 33;
    g_ctx.install_step = 0;

    LogSetup("=== WineExplorer GUI-mode Setup (setup.exe) Started ===");
    CheckUnattended();

    /* Register and create Billboard Window */
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = BillboardWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "WexSetupBillboard";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w < 800) screen_w = 1024;
    if (screen_h < 600) screen_h = 768;

    g_hBillboard = CreateWindowExA(
        0, "WexSetupBillboard", "Debian GNU/Linux with WineExplorer Setup",
        WS_POPUP | WS_VISIBLE, 0, 0, screen_w, screen_h,
        NULL, NULL, hInstance, NULL
    );

    UpdateWindow(g_hBillboard);
    PumpEvents(500);

    /* Show Interactive Wizard (or skip if unattended) */
    ShowSetupWizard(g_hBillboard);

    /* Continue automatic stages */
    RunAutomaticSetupSteps(g_hBillboard);

    DestroyWindow(g_hBillboard);

    /* Signal completion to runner script */
    fdone = fopen("Z:\\etc\\wineexplorer\\setup\\stage-gui-done", "w");
    if (!fdone) fdone = fopen("/etc/wineexplorer/setup/stage-gui-done", "w");
    if (fdone) {
        fprintf(fdone, "done\n");
        fclose(fdone);
    }

    return 0;
}
