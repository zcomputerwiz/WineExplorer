/*
 * WineExplorer Desktop Environment - Keyboard Properties: Speed Page
 */

#include "main_private.h"

typedef struct _KEYBOARD_STATE
{
    UINT repeat_delay;   /* 0 to 3 */
    UINT repeat_speed;   /* 0 to 31 */
    UINT blink_time;     /* ms, e.g. 530 */
} KEYBOARD_STATE;

static KEYBOARD_STATE g_state;

INT_PTR CALLBACK keyboard_speed_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hDelay = GetDlgItem(hwnd, KDELAY_SCROLL);
        HWND hSpeed = GetDlgItem(hwnd, KSPEED_SCROLL);
        HWND hBlink = GetDlgItem(hwnd, KBLINK_SCROLL);
        int blink_pos;

        g_state.repeat_delay = 1;
        g_state.repeat_speed = 31;
        g_state.blink_time = GetCaretBlinkTime();

        SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &g_state.repeat_delay, 0);
        SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &g_state.repeat_speed, 0);

        /* Delay: 0 (Long) to 3 (Short) */
        SendMessageW(hDelay, TBM_SETRANGE, TRUE, MAKELONG(0, 3));
        SendMessageW(hDelay, TBM_SETPOS, TRUE, g_state.repeat_delay);

        /* Speed: 0 (Slow) to 31 (Fast) */
        SendMessageW(hSpeed, TBM_SETRANGE, TRUE, MAKELONG(0, 31));
        SendMessageW(hSpeed, TBM_SETPOS, TRUE, g_state.repeat_speed);

        /* Blink: 0 (None) to 10 (Fast) */
        SendMessageW(hBlink, TBM_SETRANGE, TRUE, MAKELONG(0, 10));
        blink_pos = 10 - (int)((g_state.blink_time - 200) * 10 / 1000);
        if (blink_pos < 0) blink_pos = 0;
        if (blink_pos > 10) blink_pos = 10;
        SendMessageW(hBlink, TBM_SETPOS, TRUE, blink_pos);

        return TRUE;
    }

    case WM_HSCROLL:
    {
        HWND hDelay = GetDlgItem(hwnd, KDELAY_SCROLL);
        HWND hSpeed = GetDlgItem(hwnd, KSPEED_SCROLL);
        HWND hBlink = GetDlgItem(hwnd, KBLINK_SCROLL);

        if ((HWND)lparam == hDelay)
        {
            g_state.repeat_delay = (UINT)SendMessageW(hDelay, TBM_GETPOS, 0, 0);
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        else if ((HWND)lparam == hSpeed)
        {
            g_state.repeat_speed = (UINT)SendMessageW(hSpeed, TBM_GETPOS, 0, 0);
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        else if ((HWND)lparam == hBlink)
        {
            int pos = (int)SendMessageW(hBlink, TBM_GETPOS, 0, 0);
            g_state.blink_time = 1200 - (pos * 100);
            SetCaretBlinkTime(g_state.blink_time);
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            SystemParametersInfoW(SPI_SETKEYBOARDDELAY, g_state.repeat_delay, 0,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            SystemParametersInfoW(SPI_SETKEYBOARDSPEED, g_state.repeat_speed, 0,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            SetCaretBlinkTime(g_state.blink_time);
            return TRUE;
        }
        break;
    }
    }

    return FALSE;
}
