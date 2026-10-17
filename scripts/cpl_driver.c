/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Driver tool to interact with Display Properties (desk.cpl) in the real shell.
 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

#define IDC_THEME_LIST 2100

static HWND g_prop_sheet = NULL;
static HWND g_theme_page = NULL;
static HWND g_combo = NULL;
static HWND g_apply_btn = NULL;

static BOOL CALLBACK enum_child_proc(HWND hwnd, LPARAM lParam)
{
    int id = GetDlgCtrlID(hwnd);
    WCHAR cls[64];
    WCHAR text[64];
    GetClassNameW(hwnd, cls, 64);
    GetWindowTextW(hwnd, text, 64);

    if (id == IDC_THEME_LIST)
    {
        g_combo = hwnd;
        g_theme_page = GetParent(hwnd);
    }
    if (id == 0x3021 || !wcsicmp(text, L"&Apply") || !wcsicmp(text, L"Apply"))
    {
        g_apply_btn = hwnd;
    }
    return TRUE;
}

static BOOL find_cpl(void)
{
    g_prop_sheet = FindWindowW(L"#32770", L"Display Properties");
    if (!g_prop_sheet) g_prop_sheet = FindWindowW(NULL, L"Display Properties");
    if (!g_prop_sheet) return FALSE;

    g_combo = NULL;
    g_theme_page = NULL;
    g_apply_btn = NULL;
    EnumChildWindows(g_prop_sheet, enum_child_proc, 0);
    return (g_combo != NULL);
}

int main(int argc, char **argv)
{
    int i, count, cursel;
    WCHAR item_text[256];

    int retries = 50;
    while (retries-- > 0 && !find_cpl()) Sleep(200);
    if (!find_cpl())
    {
        printf("ERROR: Display Properties window not found\n");
        return 1;
    }

    count = (int)SendMessageW(g_combo, CB_GETCOUNT, 0, 0);
    cursel = (int)SendMessageW(g_combo, CB_GETCURSEL, 0, 0);
    printf("Display Properties found: Sheet=%p, Page=%p, Combo=%p, Apply=%p\n",
           g_prop_sheet, g_theme_page, g_combo, g_apply_btn);
    printf("Theme count: %d, currently selected index: %d\n", count, cursel);

    for (i = 0; i < count; i++)
    {
        SendMessageW(g_combo, CB_GETLBTEXT, i, (LPARAM)item_text);
        printf("  [%d] %ls%s\n", i, item_text, (i == cursel) ? " (ACTIVE)" : "");
    }

    if (argc > 1)
    {
        const char *target = argv[1];
        int target_idx = -1;

        for (i = 0; i < count; i++)
        {
            char ansi[256];
            SendMessageW(g_combo, CB_GETLBTEXT, i, (LPARAM)item_text);
            WideCharToMultiByte(CP_ACP, 0, item_text, -1, ansi, sizeof(ansi), NULL, NULL);
            if (strstr(ansi, target))
            {
                target_idx = i;
                break;
            }
        }

        if (target_idx < 0)
        {
            printf("ERROR: Theme '%s' not found in list\n", target);
            return 2;
        }

        printf("Selecting [%d] '%ls'...\n", target_idx, item_text);
        SendMessageW(g_combo, CB_SETCURSEL, target_idx, 0);
        SendMessageW(g_theme_page, WM_COMMAND, MAKEWPARAM(IDC_THEME_LIST, CBN_SELCHANGE), (LPARAM)g_combo);

        Sleep(200);
        if (g_apply_btn)
        {
            printf("Clicking Apply button (%p) via BM_CLICK...\n", g_apply_btn);
            SendMessageW(g_apply_btn, BM_CLICK, 0, 0);
        }
        else
        {
            printf("Sending WM_COMMAND for IDC_APPLY_BUTTON...\n");
            SendMessageW(g_prop_sheet, WM_COMMAND, MAKEWPARAM(12321, BN_CLICKED), 0);
        }
        printf("Applied theme '%s' successfully\n", target);
    }

    return 0;
}
