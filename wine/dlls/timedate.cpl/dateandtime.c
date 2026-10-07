/*
 * WineExplorer Desktop Environment - Date & Time Page (timedate.cpl)
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "timedate.h"

static SYSTEMTIME g_selected_time;
static BOOL g_time_modified = FALSE;
static WNDPROC g_old_dtp_proc = NULL;

static LRESULT CALLBACK dtp_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_KEYDOWN)
    {
        /* If user interacts with keys other than left/right arrow navigation, mark modified */
        if (wparam != VK_LEFT && wparam != VK_RIGHT)
        {
            HWND page = GetParent(hwnd);
            g_time_modified = TRUE;
            KillTimer(page, 1);
            PropSheet_Changed(GetParent(page), page);
        }
    }
    return CallWindowProcW(g_old_dtp_proc, hwnd, msg, wparam, lparam);
}

INT_PTR CALLBACK dateandtime_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hCal = GetDlgItem(hwnd, IDC_MONTHCAL);
        HWND hDtp = GetDlgItem(hwnd, IDC_TIME_EDIT);

        GetLocalTime(&g_selected_time);
        g_time_modified = FALSE;

        SendMessageW(hCal, MCM_SETCURSEL, 0, (LPARAM)&g_selected_time);

        if (hDtp)
        {
            SendMessageW(hDtp, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&g_selected_time);
            if (!g_old_dtp_proc)
            {
                g_old_dtp_proc = (WNDPROC)SetWindowLongPtrW(hDtp, GWLP_WNDPROC, (LONG_PTR)dtp_subclass_proc);
            }
        }

        SetTimer(hwnd, 1, 1000 - g_selected_time.wMilliseconds, NULL);
        return TRUE;
    }

    case WM_TIMER:
    {
        HWND hDtp = GetDlgItem(hwnd, IDC_TIME_EDIT);
        if (!g_time_modified)
        {
            GetLocalTime(&g_selected_time);
            if (hDtp)
                SendMessageW(hDtp, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&g_selected_time);
        }
        SetTimer(hwnd, 1, 1000 - g_selected_time.wMilliseconds, NULL);
        return TRUE;
    }

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == MCN_SELECT)
        {
            NMSELCHANGE *sc = (NMSELCHANGE *)lparam;
            g_selected_time.wYear = sc->stSelStart.wYear;
            g_selected_time.wMonth = sc->stSelStart.wMonth;
            g_selected_time.wDay = sc->stSelStart.wDay;
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        else if (nmhdr->code == DTN_DATETIMECHANGE)
        {
            NMDATETIMECHANGE *dtc = (NMDATETIMECHANGE *)lparam;
            if (dtc->dwFlags == GDT_VALID)
            {
                g_selected_time.wHour = dtc->st.wHour;
                g_selected_time.wMinute = dtc->st.wMinute;
                g_selected_time.wSecond = dtc->st.wSecond;
            }
            g_time_modified = TRUE;
            KillTimer(hwnd, 1);
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        else if (nmhdr->code == PSN_APPLY)
        {
            HWND hDtp = GetDlgItem(hwnd, IDC_TIME_EDIT);
            SYSTEMTIME cur_st;
            if (hDtp && SendMessageW(hDtp, DTM_GETSYSTEMTIME, 0, (LPARAM)&cur_st) == GDT_VALID)
            {
                g_selected_time.wHour = cur_st.wHour;
                g_selected_time.wMinute = cur_st.wMinute;
                g_selected_time.wSecond = cur_st.wSecond;
                g_selected_time.wMilliseconds = 0;
            }
            SetLocalTime(&g_selected_time);
            SendMessageW(HWND_BROADCAST, WM_TIMECHANGE, 0, 0);

            g_time_modified = FALSE;
            SetTimer(hwnd, 1, 1000, NULL);
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
    {
        HWND hDtp = GetDlgItem(hwnd, IDC_TIME_EDIT);
        KillTimer(hwnd, 1);
        if (hDtp && g_old_dtp_proc)
        {
            SetWindowLongPtrW(hDtp, GWLP_WNDPROC, (LONG_PTR)g_old_dtp_proc);
            g_old_dtp_proc = NULL;
        }
        break;
    }
    }
    return FALSE;
}
