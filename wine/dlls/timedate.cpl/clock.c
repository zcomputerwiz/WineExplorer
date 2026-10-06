/*
 * WineExplorer Desktop Environment - Analog Clock Control (timedate.cpl)
 */

#include <windows.h>
#include <math.h>
#include "timedate.h"

#define CLOCK_CLASS L"TimeDateClock"
#define PI 3.14159265358979323846

static void draw_analog_clock(HDC hdc, const RECT *rc)
{
    SYSTEMTIME st;
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2;
    int radius = min((rc->right - rc->left) / 2, (rc->bottom - rc->top) / 2) - 4;
    int i;
    HBRUSH face_brush, old_brush;
    HPEN rim_pen, tick_pen, hour_pen, min_pen, sec_pen, old_pen;
    double angle, rad;

    GetLocalTime(&st);

    /* Clock Face */
    face_brush = CreateSolidBrush(RGB(255, 255, 255));
    rim_pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    old_brush = SelectObject(hdc, face_brush);
    old_pen = SelectObject(hdc, rim_pen);

    Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);

    /* Hour Ticks */
    tick_pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    SelectObject(hdc, tick_pen);

    for (i = 0; i < 12; i++)
    {
        int x1, y1, x2, y2;
        rad = i * (2.0 * PI / 12.0);
        x1 = cx + (int)((radius - 6) * sin(rad));
        y1 = cy - (int)((radius - 6) * cos(rad));
        x2 = cx + (int)(radius * sin(rad));
        y2 = cy - (int)(radius * cos(rad));
        MoveToEx(hdc, x1, y1, NULL);
        LineTo(hdc, x2, y2);
    }

    /* Hour Hand */
    hour_pen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
    SelectObject(hdc, hour_pen);
    angle = (st.wHour % 12 + st.wMinute / 60.0) * (2.0 * PI / 12.0);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)((radius * 0.5) * sin(angle)), cy - (int)((radius * 0.5) * cos(angle)));

    /* Minute Hand */
    min_pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    SelectObject(hdc, min_pen);
    angle = (st.wMinute + st.wSecond / 60.0) * (2.0 * PI / 60.0);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)((radius * 0.75) * sin(angle)), cy - (int)((radius * 0.75) * cos(angle)));

    /* Second Hand (Red) */
    sec_pen = CreatePen(PS_SOLID, 1, RGB(220, 20, 20));
    SelectObject(hdc, sec_pen);
    angle = st.wSecond * (2.0 * PI / 60.0);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)((radius * 0.85) * sin(angle)), cy - (int)((radius * 0.85) * cos(angle)));

    /* Center Hub */
    SelectObject(hdc, GetStockObject(BLACK_BRUSH));
    Ellipse(hdc, cx - 3, cy - 3, cx + 4, cy + 4);

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(face_brush);
    DeleteObject(rim_pen);
    DeleteObject(tick_pen);
    DeleteObject(hour_pen);
    DeleteObject(min_pen);
    DeleteObject(sec_pen);
}

static LRESULT CALLBACK clock_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        draw_analog_clock(hdc, &rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;

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
