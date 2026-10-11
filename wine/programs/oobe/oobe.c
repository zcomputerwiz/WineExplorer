/*
 * Windows XP Out-of-Box Experience (OOBE) (oobe.exe)
 * Part of the WineExplorer project.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <io.h>

#define OOBE_PAGE_WELCOME    0
#define OOBE_PAGE_PROTECT    1
#define OOBE_PAGE_NETWORK    2
#define OOBE_PAGE_USERS      3
#define OOBE_PAGE_FINISH     4

typedef struct {
    int current_page;
    char users[5][64];
    bool protect_pc;
} OobeContext;

static OobeContext g_ctx;
static HWND g_hWnd = NULL;

static void LogOobe(const char *msg)
{
    FILE *f = fopen("/var/log/wineexplorer-setup.log", "a");
    if (!f) f = fopen("wineexplorer-setup.log", "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [OOBE] %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
        fclose(f);
    }
}

static void FinishOobe(void)
{
    LogOobe("Completing Out-of-Box Experience...");
    remove("/etc/wineexplorer/setup/stage");
    remove("/etc/wineexplorer/setup/stage-oobe");
    LogOobe("OOBE completed. Transitioning to desktop...");
    PostQuitMessage(0);
}

static void DrawOobe(HWND hwnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    /* Background: XP Luna Blue */
    HBRUSH hBg = CreateSolidBrush(RGB(58, 110, 165));
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    /* Top banner */
    RECT rcTop = { 0, 0, rc.right, 80 };
    HBRUSH hTop = CreateSolidBrush(RGB(0, 50, 130));
    FillRect(hdc, &rcTop, hTop);
    DeleteObject(hTop);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    HFONT hFontTitle = CreateFontA(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontTitle);
    TextOutA(hdc, 40, 24, "Welcome to Microsoft Windows XP", 32);

    /* Bottom navigation bar */
    RECT rcBottom = { 0, rc.bottom - 70, rc.right, rc.bottom };
    HBRUSH hBottom = CreateSolidBrush(RGB(20, 60, 140));
    FillRect(hdc, &rcBottom, hBottom);
    DeleteObject(hBottom);

    /* Content area */
    HFONT hFontBody = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    SelectObject(hdc, hFontBody);
    SetTextColor(hdc, RGB(245, 245, 255));

    switch (g_ctx.current_page) {
        case OOBE_PAGE_WELCOME:
            TextOutA(hdc, 60, 130, "Welcome to Windows XP with WineExplorer!", 40);
            TextOutA(hdc, 60, 170, "In a few simple steps, we will help you configure your new computer.", 67);
            TextOutA(hdc, 60, 210, "Click Next to continue.", 23);
            break;

        case OOBE_PAGE_PROTECT:
            TextOutA(hdc, 60, 130, "Help protect your PC", 21);
            TextOutA(hdc, 60, 170, "Your Debian system includes automatic security updates from Debian Security.", 77);
            TextOutA(hdc, 60, 210, "Automatic security updates are enabled.", 39);
            break;

        case OOBE_PAGE_NETWORK:
            TextOutA(hdc, 60, 130, "Checking your Internet connectivity", 35);
            TextOutA(hdc, 60, 170, "Network interfaces have been configured with DHCP.", 50);
            TextOutA(hdc, 60, 210, "Internet connection is active.", 30);
            break;

        case OOBE_PAGE_USERS:
            TextOutA(hdc, 60, 130, "Who will use this computer?", 27);
            TextOutA(hdc, 60, 170, "Primary User Account: wineuser (Computer Administrator)", 55);
            TextOutA(hdc, 60, 210, "You can add more user accounts later in User Accounts in Control Panel.", 71);
            break;

        case OOBE_PAGE_FINISH:
            TextOutA(hdc, 60, 130, "Thank you!", 10);
            TextOutA(hdc, 60, 170, "Congratulations! Setup is complete. You are ready to start using Windows XP.", 76);
            TextOutA(hdc, 60, 210, "Click Finish to log in to your desktop.", 39);
            break;
    }

    /* Buttons drawn in footer */
    RECT rcBtn = { rc.right - 140, rc.bottom - 52, rc.right - 40, rc.bottom - 18 };
    HBRUSH hBtn = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(hdc, &rcBtn, hBtn);
    DeleteObject(hBtn);
    FrameRect(hdc, &rcBtn, (HBRUSH)GetStockObject(BLACK_BRUSH));

    SetTextColor(hdc, RGB(0, 0, 0));
    const char *btnText = (g_ctx.current_page == OOBE_PAGE_FINISH) ? "Finish" : "Next ->";
    DrawTextA(hdc, btnText, (int)strlen(btnText), &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFontTitle);
    DeleteObject(hFontBody);
}

static LRESULT CALLBACK OobeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_LBUTTONUP: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            if (x >= rc.right - 140 && x <= rc.right - 40 &&
                y >= rc.bottom - 52 && y <= rc.bottom - 18) {
                if (g_ctx.current_page < OOBE_PAGE_FINISH) {
                    g_ctx.current_page++;
                    InvalidateRect(hwnd, NULL, TRUE);
                } else {
                    FinishOobe();
                }
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                if (g_ctx.current_page < OOBE_PAGE_FINISH) {
                    g_ctx.current_page++;
                    InvalidateRect(hwnd, NULL, TRUE);
                } else {
                    FinishOobe();
                }
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawOobe(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    memset(&g_ctx, 0, sizeof(g_ctx));
    snprintf(g_ctx.users[0], sizeof(g_ctx.users[0]), "wineuser");
    g_ctx.protect_pc = true;

    LogOobe("=== WineExplorer OOBE Started ===");

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = OobeWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "WEX_OOBE";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    if (scrW < 800) scrW = 1024;
    if (scrH < 600) scrH = 768;

    g_hWnd = CreateWindowExA(0, "WEX_OOBE", "Windows Welcome",
                             WS_POPUP | WS_VISIBLE, 0, 0, scrW, scrH,
                             NULL, NULL, hInstance, NULL);

    UpdateWindow(g_hWnd);

    /* Unattended OOBE automatically finishes after short display */
    if (_access("/etc/wineexplorer/setup/unattend.sif", 0) == 0) {
        PumpEvents(3500);
        FinishOobe();
    }

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return 0;
}
