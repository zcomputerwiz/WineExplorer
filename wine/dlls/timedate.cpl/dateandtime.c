/*
 * WineExplorer Desktop Environment - Date & Time Page (timedate.cpl)
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "timedate.h"

static SYSTEMTIME g_selected_time;
static BOOL g_time_modified = FALSE;

static LRESULT CALLBACK dtp_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    HWND page;

    switch (uMsg)
    {
    case WM_SETFOCUS:
    case WM_LBUTTONDOWN:
        page = GetParent(hwnd);
        KillTimer(page, 1);
        SendDlgItemMessageW(page, IDC_CLOCK_CTRL, CLM_STOPCLOCK, 0, 0);
        break;

    case WM_KEYDOWN:
        if (wParam != VK_LEFT && wParam != VK_RIGHT && wParam != VK_TAB)
        {
            page = GetParent(hwnd);
            KillTimer(page, 1);
            SendDlgItemMessageW(page, IDC_CLOCK_CTRL, CLM_STOPCLOCK, 0, 0);
        }
        break;

    case WM_KILLFOCUS:
        page = GetParent(hwnd);
        if (!g_time_modified)
        {
            SendDlgItemMessageW(page, IDC_CLOCK_CTRL, CLM_STARTCLOCK, 0, 0);
            SetTimer(page, 1, 1000, NULL);
        }
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
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
            SetWindowSubclass(hDtp, dtp_subclass_proc, 1, 0);
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

    case WM_TIMECHANGE:
    {
        HWND hCal = GetDlgItem(hwnd, IDC_MONTHCAL);
        SYSTEMTIME cur;
        GetLocalTime(&cur);
        if (hCal)
        {
            SendMessageW(hCal, MCM_SETTODAY, 0, (LPARAM)&cur);
            InvalidateRect(hCal, NULL, TRUE);
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (!nmhdr) break;

        if (nmhdr->code == MCN_SELCHANGE)
        {
            NMSELCHANGE *sc = (NMSELCHANGE *)lparam;
            if (sc->stSelStart.wYear != g_selected_time.wYear ||
                sc->stSelStart.wMonth != g_selected_time.wMonth ||
                sc->stSelStart.wDay != g_selected_time.wDay)
            {
                g_selected_time.wYear = sc->stSelStart.wYear;
                g_selected_time.wMonth = sc->stSelStart.wMonth;
                g_selected_time.wDay = sc->stSelStart.wDay;
                g_time_modified = TRUE;
                PropSheet_Changed(GetParent(hwnd), hwnd);
            }
        }
        else if (nmhdr->code == DTN_DATETIMECHANGE)
        {
            NMDATETIMECHANGE *dtc = (NMDATETIMECHANGE *)lparam;
            if (dtc->dwFlags == GDT_VALID)
            {
                if (dtc->st.wHour != g_selected_time.wHour ||
                    dtc->st.wMinute != g_selected_time.wMinute ||
                    dtc->st.wSecond != g_selected_time.wSecond)
                {
                    g_selected_time.wHour = dtc->st.wHour;
                    g_selected_time.wMinute = dtc->st.wMinute;
                    g_selected_time.wSecond = dtc->st.wSecond;
                    SendDlgItemMessageW(hwnd, IDC_CLOCK_CTRL, CLM_SETTIME, 0, (LPARAM)&dtc->st);
                    g_time_modified = TRUE;
                    KillTimer(hwnd, 1);
                    SendDlgItemMessageW(hwnd, IDC_CLOCK_CTRL, CLM_STOPCLOCK, 0, 0);
                    PropSheet_Changed(GetParent(hwnd), hwnd);
                }
            }
        }
        else if (nmhdr->code == PSN_APPLY)
        {
            HWND hCal = GetDlgItem(hwnd, IDC_MONTHCAL);
            HWND hDtp = GetDlgItem(hwnd, IDC_TIME_EDIT);
            SYSTEMTIME cur_st;
            char cmd[64];

            if (!g_time_modified)
            {
                SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }

            if (hCal && SendMessageW(hCal, MCM_GETCURSEL, 0, (LPARAM)&cur_st))
            {
                g_selected_time.wYear = cur_st.wYear;
                g_selected_time.wMonth = cur_st.wMonth;
                g_selected_time.wDay = cur_st.wDay;
            }

            if (hDtp && SendMessageW(hDtp, DTM_GETSYSTEMTIME, 0, (LPARAM)&cur_st) == GDT_VALID)
            {
                g_selected_time.wHour = cur_st.wHour;
                g_selected_time.wMinute = cur_st.wMinute;
                g_selected_time.wSecond = cur_st.wSecond;
                g_selected_time.wMilliseconds = 0;
            }

            snprintf(cmd, sizeof(cmd), "date -s %04u-%02u-%02uT%02u:%02u:%02u",
                     g_selected_time.wYear, g_selected_time.wMonth, g_selected_time.wDay,
                     g_selected_time.wHour, g_selected_time.wMinute, g_selected_time.wSecond);

            if (!uac_run_elevated(hwnd, L"Date and Time Properties", cmd))
            {
                SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }

            SetLocalTime(&g_selected_time);

            if (hCal)
            {
                SendMessageW(hCal, MCM_SETTODAY, 0, (LPARAM)&g_selected_time);
                InvalidateRect(hCal, NULL, TRUE);
            }

            SendMessageTimeoutW(HWND_BROADCAST, WM_TIMECHANGE, 0, 0, SMTO_ABORTIFHUNG, 1000, NULL);

            SendDlgItemMessageW(hwnd, IDC_CLOCK_CTRL, CLM_STARTCLOCK, 0, 0);
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
        if (hDtp)
            RemoveWindowSubclass(hDtp, dtp_subclass_proc, 1);
        break;
    }
    }
    return FALSE;
}
