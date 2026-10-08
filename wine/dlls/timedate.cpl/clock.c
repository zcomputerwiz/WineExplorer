/*
 * WineExplorer Desktop Environment - Analog Clock Control (timedate.cpl)
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

#include <windows.h>
#include <math.h>
#include "timedate.h"

#define CLOCK_CLASS L"TimeDateClock"
#define PI 3.14159265358979323846

typedef struct
{
    SYSTEMTIME time;
    BOOL stopped;
    BOOL timer_active;
} CLOCK_DATA;

static void draw_analog_clock(HDC hdc, const RECT *rc, const SYSTEMTIME *st)
{
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2;
    int radius = min((rc->right - rc->left) / 2, (rc->bottom - rc->top) / 2) - 4;
    int i;
    HBRUSH face_brush, old_brush;
    HPEN rim_pen, hour_pen, min_pen, sec_pen, old_pen;
    double angle, rad;

    /* Clock Face */
    face_brush = CreateSolidBrush(RGB(255, 255, 255));
    rim_pen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
    old_brush = SelectObject(hdc, face_brush);
    old_pen = SelectObject(hdc, rim_pen);

    Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);

    /* Hour Ticks (dots like authentic Windows XP) */
    for (i = 0; i < 12; i++)
    {
        int dot_r = (i % 3 == 0) ? 2 : 1;
        COLORREF dot_col = (i % 3 == 0) ? RGB(0, 128, 128) : RGB(80, 80, 80);
        HBRUSH dot_br = CreateSolidBrush(dot_col);
        HPEN dot_pn = CreatePen(PS_SOLID, 1, dot_col);
        HPEN prev_pn = SelectObject(hdc, dot_pn);
        HBRUSH prev_br = SelectObject(hdc, dot_br);

        rad = i * (2.0 * PI / 12.0);
        {
            int dx = cx + (int)((radius - 6) * sin(rad));
            int dy = cy - (int)((radius - 6) * cos(rad));
            Ellipse(hdc, dx - dot_r, dy - dot_r, dx + dot_r + 1, dy + dot_r + 1);
        }

        SelectObject(hdc, prev_br);
        SelectObject(hdc, prev_pn);
        DeleteObject(dot_br);
        DeleteObject(dot_pn);
    }

    /* Hour Hand */
    hour_pen = CreatePen(PS_SOLID, 3, RGB(0, 100, 100));
    SelectObject(hdc, hour_pen);
    angle = (st->wHour % 12 + st->wMinute / 60.0) * (2.0 * PI / 12.0);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)((radius * 0.52) * sin(angle)), cy - (int)((radius * 0.52) * cos(angle)));

    /* Minute Hand */
    min_pen = CreatePen(PS_SOLID, 2, RGB(0, 128, 128));
    SelectObject(hdc, min_pen);
    angle = (st->wMinute + st->wSecond / 60.0) * (2.0 * PI / 60.0);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)((radius * 0.78) * sin(angle)), cy - (int)((radius * 0.78) * cos(angle)));

    /* Second Hand (Dark Slate / Red) */
    sec_pen = CreatePen(PS_SOLID, 1, RGB(40, 40, 40));
    SelectObject(hdc, sec_pen);
    angle = st->wSecond * (2.0 * PI / 60.0);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)((radius * 0.85) * sin(angle)), cy - (int)((radius * 0.85) * cos(angle)));

    /* Center Hub */
    SelectObject(hdc, GetStockObject(BLACK_BRUSH));
    Ellipse(hdc, cx - 2, cy - 2, cx + 3, cy + 3);

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(face_brush);
    DeleteObject(rim_pen);
    DeleteObject(hour_pen);
    DeleteObject(min_pen);
    DeleteObject(sec_pen);
}

static LRESULT CALLBACK clock_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    CLOCK_DATA *data = (CLOCK_DATA *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_NCCREATE:
    {
        data = (CLOCK_DATA *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CLOCK_DATA));
        if (data)
        {
            GetLocalTime(&data->time);
            data->stopped = FALSE;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_CREATE:
        if (data)
        {
            data->timer_active = (SetTimer(hwnd, 1, 1000 - data->time.wMilliseconds, NULL) != 0);
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        SYSTEMTIME cur_st;

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        if (data)
        {
            if (!data->stopped)
                GetLocalTime(&data->time);
            cur_st = data->time;
        }
        else
        {
            GetLocalTime(&cur_st);
        }

        draw_analog_clock(hdc, &rc, &cur_st);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (data && !data->stopped)
        {
            GetLocalTime(&data->time);
            InvalidateRect(hwnd, NULL, FALSE);
            SetTimer(hwnd, 1, 1000 - data->time.wMilliseconds, NULL);
        }
        return 0;

    case CLM_STOPCLOCK:
        if (data)
        {
            KillTimer(hwnd, 1);
            data->stopped = TRUE;
            data->timer_active = FALSE;
        }
        return 0;

    case CLM_STARTCLOCK:
        if (data)
        {
            GetLocalTime(&data->time);
            data->stopped = FALSE;
            data->timer_active = (SetTimer(hwnd, 1, 1000 - data->time.wMilliseconds, NULL) != 0);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case CLM_SETTIME:
        if (data && lparam)
        {
            data->time = *(const SYSTEMTIME *)lparam;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_NCDESTROY:
        if (data)
        {
            KillTimer(hwnd, 1);
            HeapFree(GetProcessHeap(), 0, data);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

void clock_register_class(HINSTANCE hinst)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = clock_wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = CLOCK_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);
}

void clock_unregister_class(HINSTANCE hinst)
{
    UnregisterClassW(CLOCK_CLASS, hinst);
}
