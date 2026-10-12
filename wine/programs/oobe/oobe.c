/*
 * WineExplorer Desktop Environment - oobe.c
 * Out-of-Box Experience (OOBE) User Provisioning Wizard
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
#include <ctype.h>

#include "wexhost.h"
#include "resource.h"

typedef struct {
    char name[64];
    char real_name[128];
    char password[128];
    bool is_admin;
} UserAccount;

typedef struct {
    UserAccount users[5];
    int user_count;
    bool automatic_updates;
    bool unattended;
    char unattend_path[MAX_PATH];
} OobeContext;

static OobeContext g_ctx;
static HINSTANCE g_hInstance = NULL;
static HWND g_hBackground = NULL;

static void LogOobe(const char *msg)
{
    FILE *f = fopen("Z:\\var\\log\\wineexplorer-oobe.log", "a");
    if (!f) f = fopen("/var/log/wineexplorer-oobe.log", "a");
    if (!f) f = fopen("wineexplorer-oobe.log", "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
        fclose(f);
    }
}

static void SanitizeUsername(const char *in, char *out, size_t out_len)
{
    size_t i, j = 0;
    for (i = 0; in[i] && j + 1 < out_len; ++i) {
        char c = (char)tolower((unsigned char)in[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out[j++] = c;
        }
    }
    out[j] = '\0';
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

    /* Read users from [WexOobe] or [UserData] */
    for (i = 1; i <= 5; ++i) {
        WCHAR key[32];
        swprintf(key, 32, L"User%d", i);
        if (GetPrivateProfileStringW(L"WexOobe", key, L"", buf, 256, wpath)) {
            char raw[128];
            char clean[64];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, raw, sizeof(raw), NULL, NULL);
            SanitizeUsername(raw, clean, sizeof(clean));
            if (clean[0]) {
                int idx = g_ctx.user_count++;
                WCHAR admin_key[32], pass_key[32];
                snprintf(g_ctx.users[idx].name, sizeof(g_ctx.users[idx].name), "%s", clean);
                snprintf(g_ctx.users[idx].real_name, sizeof(g_ctx.users[idx].real_name), "%s", raw);
                swprintf(admin_key, 32, L"User%dAdmin", i);
                g_ctx.users[idx].is_admin = (GetPrivateProfileIntW(L"WexOobe", admin_key, (idx == 0) ? 1 : 0, wpath) != 0);
                swprintf(pass_key, 32, L"User%dPassword", i);
                if (GetPrivateProfileStringW(L"WexOobe", pass_key, L"", buf, 256, wpath)) {
                    WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_ctx.users[idx].password, sizeof(g_ctx.users[idx].password), NULL, NULL);
                }
            }
        }
    }

    /* Fallback to UserData FullName if no WexOobe users */
    if (g_ctx.user_count == 0) {
        if (GetPrivateProfileStringW(L"UserData", L"FullName", L"", buf, 256, wpath)) {
            char raw[128];
            char clean[64];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, raw, sizeof(raw), NULL, NULL);
            SanitizeUsername(raw, clean, sizeof(clean));
            if (clean[0]) {
                g_ctx.user_count = 1;
                snprintf(g_ctx.users[0].name, sizeof(g_ctx.users[0].name), "%s", clean);
                snprintf(g_ctx.users[0].real_name, sizeof(g_ctx.users[0].real_name), "%s", raw);
                g_ctx.users[0].is_admin = true;
            }
        }
    }

    LogOobe("Parsed unattended OOBE accounts.");
}

static void ProvisionUsers(void)
{
    int i;
    bool autologin;
    char logmsg[256];

    LogOobe("Provisioning user accounts via AccountsService...");

    for (i = 0; i < g_ctx.user_count; ++i) {
        UserAccount *u = &g_ctx.users[i];
        DWORD err;

        snprintf(logmsg, sizeof(logmsg), "Creating user '%s' (admin=%d)", u->name, u->is_admin);
        LogOobe(logmsg);

        /* 1. CreateUser via AccountsService */
        err = WexUserCreate(u->name, u->real_name[0] ? u->real_name : u->name, u->is_admin ? 1 : 0, u->password);
        snprintf(logmsg, sizeof(logmsg), "WexUserCreate('%s') result: %lu", u->name, err);
        LogOobe(logmsg);

    }

    /* 2. Automatic logon only for a single account without a password (as XP did); AccountsService writes the
     * display manager's setting, the session is WineExplorer for everyone.  /etc/skel (with the template Wine
     * prefix) was copied into each home by CreateUser. */
    autologin = (g_ctx.user_count == 1 && g_ctx.users[0].password[0] == '\0');
    LogOobe(autologin ? "Single account without a password: automatic logon." : "Automatic logon off.");
    for (i = 0; i < g_ctx.user_count; ++i)
        WexUserSetSession(g_ctx.users[i].name, "wine-explorer", autologin);

    /* 4. Cleanup setup state */
    LogOobe("Cleaning up temporary setup files and permissions...");
    remove("Z:\\etc\\wineexplorer\\setup\\stage");
    remove("/etc/wineexplorer/setup/stage");
    remove("Z:\\etc\\wineexplorer\\setup\\stage-oobe");
    remove("/etc/wineexplorer/setup/stage-oobe");
    remove("Z:\\etc\\polkit-1\\rules.d\\49-wexsetup.rules");
    remove("/etc/polkit-1/rules.d/49-wexsetup.rules");

    LogOobe("OOBE user provisioning and cleanup completed.");
}

/* Background Window Procedure: Classic Windows XP Luna Blue */
static LRESULT CALLBACK BackgroundWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            HBRUSH hbr;
            HFONT hFont, hOld;

            GetClientRect(hwnd, &rc);
            hbr = CreateSolidBrush(RGB(0, 51, 153));
            FillRect(hdc, &rc, hbr);
            DeleteObject(hbr);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            hFont = CreateFontA(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");
            hOld = SelectObject(hdc, hFont);
            TextOutA(hdc, 40, 30, "Welcome to Debian GNU/Linux with WineExplorer", 45);
            SelectObject(hdc, hOld);
            DeleteObject(hFont);

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

/* OOBE Wizard Page Procedures */
static INT_PTR CALLBACK OobeWelcomeProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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

static INT_PTR CALLBACK OobeUpdatesProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            CheckRadioButton(hwnd, IDC_RADIO_UPDATES_ON, IDC_RADIO_UPDATES_OFF, IDC_RADIO_UPDATES_ON);
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                g_ctx.automatic_updates = (IsDlgButtonChecked(hwnd, IDC_RADIO_UPDATES_ON) == BST_CHECKED);
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK OobeConnectivityProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
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

static INT_PTR CALLBACK OobeUsersProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            if (g_ctx.user_count > 0) {
                SetDlgItemTextA(hwnd, IDC_USER1, g_ctx.users[0].real_name[0] ? g_ctx.users[0].real_name : g_ctx.users[0].name);
            }
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                int ids[] = {IDC_USER1, IDC_USER2, IDC_USER3, IDC_USER4, IDC_USER5};
                int i;
                g_ctx.user_count = 0;
                for (i = 0; i < 5; ++i) {
                    char raw[128];
                    char clean[64];
                    GetDlgItemTextA(hwnd, ids[i], raw, sizeof(raw));
                    SanitizeUsername(raw, clean, sizeof(clean));
                    if (clean[0]) {
                        int idx = g_ctx.user_count++;
                        snprintf(g_ctx.users[idx].name, sizeof(g_ctx.users[idx].name), "%s", clean);
                        snprintf(g_ctx.users[idx].real_name, sizeof(g_ctx.users[idx].real_name), "%s", raw);
                        g_ctx.users[idx].is_admin = (idx == 0);
                    }
                }

                if (g_ctx.user_count == 0) {
                    MessageBoxA(hwnd, "Please enter at least one user name to use this computer.", "OOBE", MB_OK | MB_ICONEXCLAMATION);
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK OobePasswordProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                if (g_ctx.user_count > 0) {
                    char label[128];
                    snprintf(label, sizeof(label), "Setting password for user: %s", g_ctx.users[0].name);
                    SetDlgItemTextA(hwnd, IDC_PASS_USER_LABEL, label);
                }
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                char p1[128], p2[128];
                GetDlgItemTextA(hwnd, IDC_PASS_EDIT1, p1, sizeof(p1));
                GetDlgItemTextA(hwnd, IDC_PASS_EDIT2, p2, sizeof(p2));

                if (strcmp(p1, p2) != 0) {
                    MessageBoxA(hwnd, "The passwords do not match. Please re-enter your password.", "OOBE", MB_OK | MB_ICONEXCLAMATION);
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }

                if (g_ctx.user_count > 0) {
                    snprintf(g_ctx.users[0].password, sizeof(g_ctx.users[0].password), "%s", p1);
                }
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK OobeFinishProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)PSWIZB_FINISH);
                return TRUE;
            } else if (pnmh->code == PSN_WIZFINISH) {
                ProvisionUsers();
            }
            break;
        }
    }
    return FALSE;
}

static int CALLBACK OobeSheetCallback(HWND hwndDlg, UINT uMsg, LPARAM lParam)
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
        x = (screen_w - w) / 2;
        y = (screen_h - h) / 2;
        if (x < 40) x = 40;
        if (y < 40) y = 40;
        SetWindowPos(hwndDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    }
    return 0;
}

static void ShowOobeWizard(HWND hParent)
{
    PROPSHEETPAGEA psp[6];
    PROPSHEETHEADERA psh;
    int page_count = 0;

    if (g_ctx.unattended && g_ctx.user_count > 0) {
        LogOobe("Unattended installation: provisioning configured users directly.");
        ProvisionUsers();
        return;
    }

    memset(psp, 0, sizeof(psp));

    #define ADD_OOBE_PAGE(id, proc) do { \
        psp[page_count].dwSize = sizeof(PROPSHEETPAGEA); \
        psp[page_count].dwFlags = PSP_USETITLE; \
        psp[page_count].hInstance = g_hInstance; \
        psp[page_count].pszTemplate = MAKEINTRESOURCEA(id); \
        psp[page_count].pfnDlgProc = proc; \
        psp[page_count].pszTitle = "Welcome"; \
        page_count++; \
    } while(0)

    ADD_OOBE_PAGE(IDD_OOBE_WELCOME,      OobeWelcomeProc);
    ADD_OOBE_PAGE(IDD_OOBE_UPDATES,      OobeUpdatesProc);
    ADD_OOBE_PAGE(IDD_OOBE_CONNECTIVITY, OobeConnectivityProc);
    ADD_OOBE_PAGE(IDD_OOBE_USERS,        OobeUsersProc);
    ADD_OOBE_PAGE(IDD_OOBE_PASSWORD,     OobePasswordProc);
    ADD_OOBE_PAGE(IDD_OOBE_FINISH,       OobeFinishProc);

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERA);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_WIZARD | PSH_USECALLBACK;
    psh.pfnCallback = OobeSheetCallback;
    psh.hwndParent = hParent;
    psh.hInstance = g_hInstance;
    psh.pszCaption = "Welcome to Debian GNU/Linux with WineExplorer";
    psh.nPages = page_count;
    psh.nStartPage = 0;
    psh.ppsp = psp;

    PropertySheetA(&psh);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA wc;
    int screen_w, screen_h;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_hInstance = hInstance;
    InitCommonControls();

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.automatic_updates = true;

    LogOobe("=== WineExplorer OOBE (oobe.exe) Started ===");
    CheckUnattended();

    /* Register and create background window */
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = BackgroundWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "WexOobeBackground";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w < 800) screen_w = 1024;
    if (screen_h < 600) screen_h = 768;

    g_hBackground = CreateWindowExA(
        0, "WexOobeBackground", "Welcome to Debian GNU/Linux with WineExplorer",
        WS_POPUP | WS_VISIBLE, 0, 0, screen_w, screen_h,
        NULL, NULL, hInstance, NULL
    );

    UpdateWindow(g_hBackground);
    ShowOobeWizard(g_hBackground);

    DestroyWindow(g_hBackground);

    /* Signal completion to runner script */
    FILE *fdone = fopen("Z:\\etc\\wineexplorer\\setup\\stage-oobe-done", "w");
    if (!fdone) fdone = fopen("/etc/wineexplorer/setup/stage-oobe-done", "w");
    if (fdone) {
        fprintf(fdone, "done\n");
        fclose(fdone);
    }

    system("wineserver -k 2>/dev/null || true");
    return 0;
}
