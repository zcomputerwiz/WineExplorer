/*
 * WineExplorer Desktop Environment - Display Properties Appearance Page
 */

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>

#include "resource.h"
#include "preview.h"

static const WCHAR *style_names[] = {
    L"Windows Classic style",
    L"Windows XP style"
};

static const WCHAR *classic_schemes[] = {
    L"Windows Standard",
    L"Rainstorm",
    L"Desert",
    L"Eggplant",
    L"High Contrast #1",
    L"High Contrast #2",
    L"Marine (high color)",
    L"Plum (high color)",
    L"Rose",
    L"Slate",
    L"Spruce"
};

static const WCHAR *xp_schemes[] = {
    L"Default (blue)",
    L"Olive Green",
    L"Silver"
};

static const WCHAR *font_sizes[] = {
    L"Normal",
    L"Large Fonts",
    L"Extra Large Fonts"
};

static void update_appearance_preview(HWND hwnd)
{
    PREVIEW_DATA pd;
    HWND hPreview = GetDlgItem(hwnd, IDC_APP_PREVIEW);

    pd.cr_desktop = GetSysColor(COLOR_BACKGROUND);
    pd.cr_active_caption = GetSysColor(COLOR_ACTIVECAPTION);
    pd.cr_active_text = GetSysColor(COLOR_CAPTIONTEXT);
    pd.cr_inactive_caption = GetSysColor(COLOR_INACTIVECAPTION);
    pd.cr_inactive_text = GetSysColor(COLOR_INACTIVECAPTIONTEXT);
    pd.cr_btn_face = GetSysColor(COLOR_BTNFACE);
    pd.cr_btn_text = GetSysColor(COLOR_BTNTEXT);
    pd.cr_window = GetSysColor(COLOR_WINDOW);
    pd.cr_window_text = GetSysColor(COLOR_WINDOWTEXT);
    pd.cr_menu = GetSysColor(COLOR_MENU);
    pd.cr_menu_text = GetSysColor(COLOR_MENUTEXT);

    preview_update(hPreview, &pd);
}

INT_PTR CALLBACK appearance_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hStyle = GetDlgItem(hwnd, IDC_APP_STYLE);
        HWND hScheme = GetDlgItem(hwnd, IDC_APP_COLORSCHEME);
        HWND hFont = GetDlgItem(hwnd, IDC_APP_FONTSIZE);
        unsigned int i;

        for (i = 0; i < 2; i++)
            SendMessageW(hStyle, CB_ADDSTRING, 0, (LPARAM)style_names[i]);
        SendMessageW(hStyle, CB_SETCURSEL, 0, 0); /* Windows Classic style */

        for (i = 0; i < sizeof(classic_schemes)/sizeof(classic_schemes[0]); i++)
            SendMessageW(hScheme, CB_ADDSTRING, 0, (LPARAM)classic_schemes[i]);
        SendMessageW(hScheme, CB_SETCURSEL, 0, 0);

        for (i = 0; i < 3; i++)
            SendMessageW(hFont, CB_ADDSTRING, 0, (LPARAM)font_sizes[i]);
        SendMessageW(hFont, CB_SETCURSEL, 0, 0);

        update_appearance_preview(hwnd);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_APP_STYLE:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                int sel = SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0);
                HWND hScheme = GetDlgItem(hwnd, IDC_APP_COLORSCHEME);
                unsigned int i;

                SendMessageW(hScheme, CB_RESETCONTENT, 0, 0);
                if (sel == 1) /* XP Style */
                {
                    for (i = 0; i < sizeof(xp_schemes)/sizeof(xp_schemes[0]); i++)
                        SendMessageW(hScheme, CB_ADDSTRING, 0, (LPARAM)xp_schemes[i]);
                }
                else /* Classic */
                {
                    for (i = 0; i < sizeof(classic_schemes)/sizeof(classic_schemes[0]); i++)
                        SendMessageW(hScheme, CB_ADDSTRING, 0, (LPARAM)classic_schemes[i]);
                }
                SendMessageW(hScheme, CB_SETCURSEL, 0, 0);
                PropSheet_Changed(GetParent(hwnd), hwnd);
            }
            break;

        case IDC_APP_COLORSCHEME:
        case IDC_APP_FONTSIZE:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                PropSheet_Changed(GetParent(hwnd), hwnd);
            }
            break;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            PostMessageW(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);
            PostMessageW(HWND_BROADCAST, WM_THEMECHANGED, 0, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
