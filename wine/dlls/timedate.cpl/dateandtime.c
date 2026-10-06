/*
 * WineExplorer Desktop Environment - Date & Time Page (timedate.cpl)
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "timedate.h"

static SYSTEMTIME g_selected_time;

static void update_time_display(HWND hwnd)
{
    WCHAR time_str[32];
    GetLocalTime(&g_selected_time);
    swprintf(time_str, 32, L"%02d:%02d:%02d", g_selected_time.wHour, g_selected_time.wMinute, g_selected_time.wSecond);
    SetDlgItemTextW(hwnd, IDC_TIME_EDIT, time_str);
}

INT_PTR CALLBACK dateandtime_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hCal = GetDlgItem(hwnd, IDC_MONTHCAL);
        HWND hSpin = GetDlgItem(hwnd, IDC_TIME_SPIN);

        GetLocalTime(&g_selected_time);
        SendMessageW(hCal, MCM_SETCURSEL, 0, (LPARAM)&g_selected_time);

        SendMessageW(hSpin, UDM_SETRANGE, 0, MAKELONG(59, 0));
        update_time_display(hwnd);

        SetTimer(hwnd, 1, 1000, NULL);
        return TRUE;
    }

    case WM_TIMER:
        update_time_display(hwnd);
        return TRUE;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == MCN_SELECT)
        {
            NMSELCHANGE *sc = (NMSELCHANGE*)lparam;
            g_selected_time.wYear = sc->stSelStart.wYear;
            g_selected_time.wMonth = sc->stSelStart.wMonth;
            g_selected_time.wDay = sc->stSelStart.wDay;
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        else if (nmhdr->code == PSN_APPLY)
        {
            WCHAR time_str[32];
            int h, m, s;
            GetDlgItemTextW(hwnd, IDC_TIME_EDIT, time_str, 32);
            if (swscanf(time_str, L"%d:%d:%d", &h, &m, &s) == 3)
            {
                g_selected_time.wHour = h;
                g_selected_time.wMinute = m;
                g_selected_time.wSecond = s;
                SetLocalTime(&g_selected_time);
            }
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        break;
    }
    return FALSE;
}
