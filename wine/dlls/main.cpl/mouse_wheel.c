/*
 * WineExplorer Desktop Environment - Mouse Properties: Wheel Page
 */

#include "main_private.h"

typedef struct _WHEEL_STATE
{
    UINT scroll_lines;
    BOOL is_page_scroll;
} WHEEL_STATE;

static WHEEL_STATE g_state;

INT_PTR CALLBACK wheel_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hSpin = GetDlgItem(hwnd, IDC_SPIN_SCROLL_LINES);
        UINT lines = 3;

        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        g_state.scroll_lines = lines;
        g_state.is_page_scroll = (lines == WHEEL_PAGESCROLL);

        SendMessageW(hSpin, UDM_SETRANGE32, 1, 100);
        SendMessageW(hSpin, UDM_SETPOS32, 0, g_state.is_page_scroll ? 3 : (int)g_state.scroll_lines);

        if (g_state.is_page_scroll)
        {
            CheckRadioButton(hwnd, IDRAD_SCROLL_LINES, IDRAD_SCROLL_PAGE, IDRAD_SCROLL_PAGE);
            EnableWindow(GetDlgItem(hwnd, IDE_BUDDY_SCROLL_LINES), FALSE);
            EnableWindow(hSpin, FALSE);
        }
        else
        {
            CheckRadioButton(hwnd, IDRAD_SCROLL_LINES, IDRAD_SCROLL_PAGE, IDRAD_SCROLL_LINES);
            EnableWindow(GetDlgItem(hwnd, IDE_BUDDY_SCROLL_LINES), TRUE);
            EnableWindow(hSpin, TRUE);
        }

        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDRAD_SCROLL_LINES:
            g_state.is_page_scroll = FALSE;
            EnableWindow(GetDlgItem(hwnd, IDE_BUDDY_SCROLL_LINES), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_SPIN_SCROLL_LINES), TRUE);
            PropSheet_Changed(GetParent(hwnd), hwnd);
            break;

        case IDRAD_SCROLL_PAGE:
            g_state.is_page_scroll = TRUE;
            EnableWindow(GetDlgItem(hwnd, IDE_BUDDY_SCROLL_LINES), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_SPIN_SCROLL_LINES), FALSE);
            PropSheet_Changed(GetParent(hwnd), hwnd);
            break;

        case IDE_BUDDY_SCROLL_LINES:
            if (HIWORD(wparam) == EN_CHANGE)
            {
                BOOL translated;
                UINT val = GetDlgItemInt(hwnd, IDE_BUDDY_SCROLL_LINES, &translated, FALSE);
                if (translated && val >= 1 && val <= 100)
                {
                    g_state.scroll_lines = val;
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
            }
            break;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            UINT target_lines = g_state.is_page_scroll ? WHEEL_PAGESCROLL : g_state.scroll_lines;
            SystemParametersInfoW(SPI_SETWHEELSCROLLLINES, target_lines, 0,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            return TRUE;
        }
        break;
    }
    }

    return FALSE;
}
