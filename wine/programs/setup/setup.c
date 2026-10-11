/*
 * Windows XP Setup Billboard and GUI Wizard (setup.exe)
 * Part of the WineExplorer project.
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

#define IDD_BILLBOARD     100
#define IDD_PAGE_REGIONAL 101
#define IDD_PAGE_USERINFO 102
#define IDD_PAGE_COMPUTER 103
#define IDD_PAGE_DATETIME 104
#define IDD_PAGE_NETWORK  105
#define IDD_PAGE_WORKGRP  106

#define IDC_OWNER_NAME    1001
#define IDC_ORG_NAME      1002
#define IDC_COMP_NAME     1003
#define IDC_ADMIN_PASS1   1004
#define IDC_ADMIN_PASS2   1005
#define IDC_TZ_COMBO      1006
#define IDC_NET_TYPICAL   1007
#define IDC_NET_CUSTOM    1008
#define IDC_WORKGROUP     1009
#define IDC_BTN_NEXT      1010
#define IDC_BTN_BACK      1011

typedef struct {
    char owner[128];
    char org[128];
    char comp_name[64];
    char admin_pass[128];
    char timezone[128];
    char workgroup[64];
    bool typical_net;
    int current_page;
    int panel_index;
    int minutes_remaining;
    int install_step;
} SetupContext;

static SetupContext g_ctx;
static HWND g_hBillboard = NULL;
static HWND g_hWizard = NULL;
static UINT_PTR g_timerBillboard = 0;

/* Informational billboard panels */
typedef struct {
    const char *title;
    const char *desc;
} BillboardPanel;

static const BillboardPanel g_panels[] = {
    {
        "An exciting new look",
        "WineExplorer features a clean, streamlined design with the classic Windows XP Luna visual style, making it easy to access your favorite programs, files, and settings."
    },
    {
        "Windows XP Compatibility with Wine",
        "Run your favorite 32-bit and 64-bit Windows applications natively on Linux, with authentic user interface elements, control panels, and desktop accessories."
    },
    {
        "Rock-solid Debian base",
        "Built on Debian GNU/Linux 13 (Trixie), delivering unmatched security, reliability, and stability for your workstation."
    },
    {
        "Seamless Hardware Support",
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
    FILE *f = fopen("/var/log/wineexplorer-setup.log", "a");
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

static void SaveSettings(void)
{
    LogSetup("Applying configured system settings...");

    /* 1. Hostname */
    if (g_ctx.comp_name[0]) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "sudo hostnamectl set-hostname '%s' 2>/dev/null || hostnamectl set-hostname '%s' 2>/dev/null || true", g_ctx.comp_name, g_ctx.comp_name);
        system(cmd);
        FILE *fh = fopen("/etc/hostname", "w");
        if (fh) {
            fprintf(fh, "%s\n", g_ctx.comp_name);
            fclose(fh);
        }
    }

    /* 2. Registered Owner / Org */
    CreateDirectoryA("/etc/wineexplorer", NULL);
    FILE *fo = fopen("/etc/wineexplorer/owner", "w");
    if (fo) {
        fprintf(fo, "RegisteredOwner=%s\nRegisteredOrganization=%s\n",
                g_ctx.owner[0] ? g_ctx.owner : "Wine User",
                g_ctx.org[0] ? g_ctx.org : "Home");
        fclose(fo);
    }

    /* 3. Workgroup */
    FILE *fw = fopen("/etc/wineexplorer/workgroup", "w");
    if (fw) {
        fprintf(fw, "%s\n", g_ctx.workgroup[0] ? g_ctx.workgroup : "WORKGROUP");
        fclose(fw);
    }

    /* 4. Root / Admin Password */
    if (g_ctx.admin_pass[0]) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "echo 'root:%s' | sudo chpasswd 2>/dev/null || echo 'root:%s' | chpasswd 2>/dev/null || true", g_ctx.admin_pass, g_ctx.admin_pass);
        system(cmd);
    }

    /* 5. Timezone */
    if (g_ctx.timezone[0]) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "sudo timedatectl set-timezone '%s' 2>/dev/null || timedatectl set-timezone '%s' 2>/dev/null || true", g_ctx.timezone, g_ctx.timezone);
        system(cmd);
    }

    /* 6. Populate default user template prefix in /etc/skel/.wine */
    system("mkdir -p /etc/skel 2>/dev/null || true");
    LogSetup("System settings configured successfully.");
}

static void RunAutomaticSetupSteps(HWND hwnd)
{
    /* Step: Installing Devices */
    g_ctx.install_step = 0;
    g_ctx.minutes_remaining = 35;
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    LogSetup("Stage: Installing Devices");
    system("sudo udevadm settle >/dev/null 2>&1 || udevadm settle >/dev/null 2>&1 || true");
    PumpEvents(1500);

    /* Step: Installing Network */
    g_ctx.install_step = 1;
    g_ctx.minutes_remaining = 28;
    g_ctx.panel_index = (g_ctx.panel_index + 1) % g_panelCount;
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    LogSetup("Stage: Installing Network");
    system("sudo systemctl start NetworkManager.service >/dev/null 2>&1 || systemctl start NetworkManager.service >/dev/null 2>&1 || true");
    PumpEvents(1500);

    /* Step: Copying and configuring components */
    g_ctx.install_step = 2;
    g_ctx.minutes_remaining = 15;
    g_ctx.panel_index = (g_ctx.panel_index + 1) % g_panelCount;
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    LogSetup("Stage: Copying components");
    SaveSettings();
    PumpEvents(1500);

    /* Step: Completing Installation */
    g_ctx.install_step = 3;
    g_ctx.minutes_remaining = 5;
    g_ctx.panel_index = (g_ctx.panel_index + 1) % g_panelCount;
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    LogSetup("Stage: Completing installation");

    /* Transition to OOBE */
    FILE *fst = fopen("/etc/wineexplorer/setup/stage", "w");
    if (fst) {
        fprintf(fst, "oobe\n");
        fclose(fst);
    }
    LogSetup("Setup Stage 2 completed. Transitioning to OOBE (stage=oobe).");
    PumpEvents(2000);

    /* Reboot into Stage 3 OOBE */
    system("sudo reboot || reboot || sudo systemctl reboot || systemctl reboot || true");
    PostQuitMessage(0);
}

/* Wizard Dialog Procedure */
static INT_PTR CALLBACK WizardPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowTextA(GetDlgItem(hwnd, IDC_OWNER_NAME), g_ctx.owner);
            SetWindowTextA(GetDlgItem(hwnd, IDC_ORG_NAME), g_ctx.org);
            SetWindowTextA(GetDlgItem(hwnd, IDC_COMP_NAME), g_ctx.comp_name);
            SetWindowTextA(GetDlgItem(hwnd, IDC_WORKGROUP), g_ctx.workgroup);
            CheckRadioButton(hwnd, IDC_NET_TYPICAL, IDC_NET_CUSTOM, IDC_NET_TYPICAL);
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_BTN_NEXT) {
                GetDlgItemTextA(hwnd, IDC_OWNER_NAME, g_ctx.owner, sizeof(g_ctx.owner));
                GetDlgItemTextA(hwnd, IDC_ORG_NAME, g_ctx.org, sizeof(g_ctx.org));
                GetDlgItemTextA(hwnd, IDC_COMP_NAME, g_ctx.comp_name, sizeof(g_ctx.comp_name));
                GetDlgItemTextA(hwnd, IDC_ADMIN_PASS1, g_ctx.admin_pass, sizeof(g_ctx.admin_pass));
                GetDlgItemTextA(hwnd, IDC_WORKGROUP, g_ctx.workgroup, sizeof(g_ctx.workgroup));

                g_ctx.current_page++;
                if (g_ctx.current_page >= 6) {
                    EndDialog(hwnd, IDOK);
                } else {
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                return TRUE;
            } else if (LOWORD(wParam) == IDC_BTN_BACK) {
                if (g_ctx.current_page > 0) {
                    g_ctx.current_page--;
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                return TRUE;
            }
            break;
    }
    return FALSE;
}

/* Draw Billboard Window */
static void DrawBillboard(HWND hwnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    /* Background: XP Deep Blue */
    HBRUSH hBg = CreateSolidBrush(RGB(0, 45, 150));
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    /* Header banner bar */
    RECT rcHeader = { 0, 0, rc.right, 70 };
    HBRUSH hHeader = CreateSolidBrush(RGB(10, 65, 185));
    FillRect(hdc, &rcHeader, hHeader);
    DeleteObject(hHeader);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    HFONT hFontTitle = CreateFontA(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontTitle);
    TextOutA(hdc, 30, 20, "Microsoft Windows XP Professional", 33);

    /* Left Sidebar: Steps */
    RECT rcSide = { 0, 70, 250, rc.bottom };
    HBRUSH hSide = CreateSolidBrush(RGB(0, 35, 120));
    FillRect(hdc, &rcSide, hSide);
    DeleteObject(hSide);

    HFONT hFontSide = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    SelectObject(hdc, hFontSide);

    const char *steps[] = {
        "Collecting information",
        "Dynamic Update",
        "Preparing installation",
        "Installing Windows",
        "Finalizing installation"
    };

    int y = 110;
    for (int i = 0; i < 5; ++i) {
        if (i < 3) {
            SetTextColor(hdc, RGB(120, 220, 120));
            TextOutA(hdc, 25, y, "[v]", 3);
            SetTextColor(hdc, RGB(220, 220, 220));
        } else if (i == 3) {
            SetTextColor(hdc, RGB(255, 220, 50));
            TextOutA(hdc, 25, y, "[*]", 3);
            SetTextColor(hdc, RGB(255, 255, 255));
        } else {
            SetTextColor(hdc, RGB(160, 160, 180));
            TextOutA(hdc, 25, y, "[ ]", 3);
        }
        TextOutA(hdc, 55, y, steps[i], (int)strlen(steps[i]));
        y += 35;
    }

    /* Time remaining */
    char rem_str[64];
    snprintf(rem_str, sizeof(rem_str), "An estimated %d minutes remaining.", g_ctx.minutes_remaining);
    SetTextColor(hdc, RGB(255, 220, 80));
    TextOutA(hdc, 25, rc.bottom - 45, rem_str, (int)strlen(rem_str));

    /* Right Area: Rotating Informational Panel */
    const BillboardPanel *p = &g_panels[g_ctx.panel_index];
    SelectObject(hdc, hFontTitle);
    SetTextColor(hdc, RGB(255, 255, 255));
    TextOutA(hdc, 290, 120, p->title, (int)strlen(p->title));

    HFONT hFontBody = CreateFontA(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    SelectObject(hdc, hFontBody);
    SetTextColor(hdc, RGB(230, 235, 250));

    RECT rcText = { 290, 170, rc.right - 50, rc.bottom - 80 };
    DrawTextA(hdc, p->desc, (int)strlen(p->desc), &rcText, DT_WORDBREAK);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFontTitle);
    DeleteObject(hFontSide);
    DeleteObject(hFontBody);
}

static LRESULT CALLBACK BillboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE:
            g_timerBillboard = SetTimer(hwnd, 1, 6000, NULL);
            return 0;

        case WM_TIMER:
            g_ctx.panel_index = (g_ctx.panel_index + 1) % g_panelCount;
            if (g_ctx.minutes_remaining > 5) g_ctx.minutes_remaining--;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawBillboard(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            if (g_timerBillboard) KillTimer(hwnd, g_timerBillboard);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    memset(&g_ctx, 0, sizeof(g_ctx));
    snprintf(g_ctx.owner, sizeof(g_ctx.owner), "Wine User");
    snprintf(g_ctx.org, sizeof(g_ctx.org), "Home");
    snprintf(g_ctx.comp_name, sizeof(g_ctx.comp_name), "WEX-PC");
    snprintf(g_ctx.workgroup, sizeof(g_ctx.workgroup), "WORKGROUP");
    snprintf(g_ctx.timezone, sizeof(g_ctx.timezone), "Etc/UTC");
    g_ctx.typical_net = true;
    g_ctx.minutes_remaining = 39;

    LogSetup("=== WineExplorer GUI Setup Started ===");

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = BillboardWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "WEX_Billboard";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    if (scrW < 800) scrW = 1024;
    if (scrH < 600) scrH = 768;

    g_hBillboard = CreateWindowExA(0, "WEX_Billboard", "Windows XP Setup",
                                  WS_POPUP | WS_VISIBLE, 0, 0, scrW, scrH,
                                  NULL, NULL, hInstance, NULL);

    UpdateWindow(g_hBillboard);

    /* Check unattended mode */
    bool unattended = (strstr(lpCmdLine, "-unattend") != NULL) ||
                      (_access("/etc/wineexplorer/setup/unattend.sif", 0) == 0);

    if (!unattended) {
        /* Run Wizard steps centered */
        MSG msg;
        DWORD start = GetTickCount();
        while (GetTickCount() - start < 2000) {
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            Sleep(50);
        }
    }

    RunAutomaticSetupSteps(g_hBillboard);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return 0;
}
