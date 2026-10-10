/*
 * WineExplorer Desktop Environment - Turn Off Computer and Log Off Windows
 *
 * Windows XP's dialogs over the grayed screen: Stand By, Turn Off and Restart; Switch User and Log Off.  The
 * actions go to systemd-logind (and GDM for Switch User) through wexhost.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <uxtheme.h>
#include <vsstyle.h>

#include <wexhost.h>

#include "wine/debug.h"
#include "theme_engine.h"
#include "power_mgmt.h"
#include "shellres.h"
#include "resource.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

extern HINSTANCE explorer_hInstance;

/* ---- the actions ---- */

static BOOL CALLBACK enum_end_session_proc(HWND hwnd, LPARAM lparam)
{
    struct { UINT msg; WPARAM wparam; LPARAM lparam; BOOL veto; } *ctx = (void *)lparam;
    DWORD pid;
    DWORD_PTR result = 1;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER)) return TRUE;

    /* a window that does not answer in 5 s is hung and does not block shutdown, as on Windows */
    if (SendMessageTimeoutW(hwnd, ctx->msg, ctx->wparam, ctx->lparam, SMTO_ABORTIFHUNG, 5000, &result) &&
        ctx->msg == WM_QUERYENDSESSION && !result)
    {
        ctx->veto = TRUE;
        return FALSE;
    }
    return TRUE;
}

/* Ask every top-level window whether the session may end; on yes tell them it is ending. */
static BOOL end_session_with_consent(LPARAM flags)
{
    struct { UINT msg; WPARAM wparam; LPARAM lparam; BOOL veto; } ctx = { WM_QUERYENDSESSION, 0, flags, FALSE };

    EnumWindows(enum_end_session_proc, (LPARAM)&ctx);
    if (ctx.veto) return FALSE;

    ctx.msg = WM_ENDSESSION; ctx.wparam = TRUE;
    EnumWindows(enum_end_session_proc, (LPARAM)&ctx);
    return TRUE;
}

static void report_power_error(HWND owner, DWORD err)
{
    if (err == ERROR_CANCELLED || err == ERROR_SUCCESS) return;
    MessageBoxW(owner, err == ERROR_NOT_SUPPORTED ? L"This action is not available on this computer." :
                L"You do not have permission to do this.", L"Turn Off Computer", MB_OK | MB_ICONWARNING);
}

static BOOL power_can(DWORD action)
{
    DWORD can = WEX_POWER_NO;
    return !WexPowerCan(action, &can) && can != WEX_POWER_NO;
}

void power_mgmt_execute_action(int action)
{
    HWND owner = GetDesktopWindow();
    DWORD err;

    TRACE("Executing power action: %d\n", action);

    switch (action)
    {
    case POWER_ACTION_SHUTDOWN:
    case POWER_ACTION_RESTART:
    {
        DWORD wex = (action == POWER_ACTION_SHUTDOWN) ? WEX_POWER_SHUTDOWN : WEX_POWER_REBOOT;

        if (!power_can(wex))
        {
            report_power_error(owner, ERROR_NOT_SUPPORTED);
            return;
        }
        if (!end_session_with_consent(0))
        {
            MessageBoxW(owner, L"A program is preventing Windows from shutting down.", L"Turn Off Computer",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        theme_play_sound(L"Shutdown");
        /* login1 (through polkit) powers off the machine and ends the whole session, Wine included */
        err = WexPowerDo(owner, wex);
        report_power_error(owner, err);
        break;
    }

    case POWER_ACTION_LOGOFF:
        if (!end_session_with_consent(ENDSESSION_LOGOFF))
        {
            MessageBoxW(owner, L"A program is preventing Windows from logging off.", L"Log Off Windows",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        theme_play_sound(L"Logoff");
        /* login1 ends the login session: the display manager shows its greeter again */
        if (WexPowerDo(owner, WEX_POWER_LOGOFF))
        {
            /* no logind session: the shell ends, and with it the X session it runs */
            ExitWindowsEx(EWX_LOGOFF | EWX_FORCEIFHUNG, 0);
            ExitProcess(0);
        }
        break;

    case POWER_ACTION_SWITCHUSER:
        report_power_error(owner, WexPowerDo(owner, WEX_POWER_SWITCHUSER));
        break;

    case POWER_ACTION_STANDBY:
        if (!power_can(WEX_POWER_SUSPEND)) report_power_error(owner, ERROR_NOT_SUPPORTED);
        else report_power_error(owner, WexPowerDo(owner, WEX_POWER_SUSPEND));
        break;
    }
}

/* ---- the screen fades to gray behind the dialog ---- */

#define DIM_CLASS L"WineExplorerDimmedScreen"

static LRESULT CALLBACK dim_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps), mem = CreateCompatibleDC(hdc);
        HBITMAP bitmap = (HBITMAP)GetWindowLongPtrW(hwnd, GWLP_USERDATA), old = SelectObject(mem, bitmap);
        BITMAP bm;

        GetObjectW(bitmap, sizeof(bm), &bm);
        BitBlt(hdc, 0, 0, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* a picture of the screen in shades of gray, shown over it */
static HWND dim_screen(void)
{
    int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN), i;
    BITMAPINFO info = { { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB } };
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    WNDCLASSW wc = { 0 };
    HBITMAP bitmap;
    DWORD *bits;
    HWND hwnd;

    bitmap = CreateDIBSection(mem, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bitmap) { DeleteDC(mem); ReleaseDC(NULL, screen); return NULL; }
    SelectObject(mem, bitmap);
    BitBlt(mem, 0, 0, width, height, screen, 0, 0, SRCCOPY);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    for (i = 0; i < width * height; i++)
    {
        DWORD p = bits[i], gray = (((p >> 16) & 255) * 30 + ((p >> 8) & 255) * 59 + (p & 255) * 11) / 100;
        bits[i] = gray << 16 | gray << 8 | gray;
    }

    wc.lpfnWndProc = dim_proc;
    wc.hInstance = explorer_hInstance;
    wc.lpszClassName = DIM_CLASS;
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, DIM_CLASS, NULL, WS_POPUP, 0, 0, width, height,
                           NULL, NULL, explorer_hInstance, NULL);
    if (!hwnd) { DeleteObject(bitmap); return NULL; }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)bitmap);
    ShowWindow(hwnd, SW_SHOWNA);
    UpdateWindow(hwnd);
    return hwnd;
}

static void undim_screen(HWND hwnd)
{
    if (!hwnd) return;
    DeleteObject((HBITMAP)GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    DestroyWindow(hwnd);
}

/* ---- the dialogs ---- */

#define DIALOG_CLASS L"WineExplorerTurnOff"
#define DIALOG_W 313
#define DIALOG_H 198
#define BUTTON   33

struct choice
{
    const WCHAR *text;   /* with & before its key */
    int action;
    int image;           /* first of its normal, pressed and hot pictures in the strip */
    int disabled_image;  /* -1: none */
    BOOL enabled;
    RECT rc;             /* the picture */
};

struct dialog
{
    const WCHAR *title;
    struct choice choices[3];
    int count;
    HBITMAP background, flag, strip;
    int strip_size;      /* the pictures are square */
    int hot, pressed, result;
    RECT cancel;
    BOOL cancel_hot, cancel_pressed, done;
};

static void draw_strip_image(HDC hdc, struct dialog *dlg, int index, const RECT *rc)
{
    HDC mem;
    HBITMAP old;
    BITMAP bm;

    if (!dlg->strip) return;
    /* a picture the strip does not have (XP's Switch User has no disabled one): the normal one */
    if (GetObjectW(dlg->strip, sizeof(bm), &bm) && (index + 1) * dlg->strip_size > abs(bm.bmHeight)) index = 0;
    mem = CreateCompatibleDC(hdc);
    old = SelectObject(mem, dlg->strip);

    /* the strips have magenta (XP) or no background around the pictures */
    TransparentBlt(hdc, rc->left, rc->top, BUTTON, BUTTON, mem, 0, index * dlg->strip_size, dlg->strip_size,
                   dlg->strip_size, RGB(255, 0, 255));
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void draw_bitmap(HDC hdc, HBITMAP bitmap, int x, int y, int w, int h)
{
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP old = SelectObject(mem, bitmap);
    BITMAP bm;

    GetObjectW(bitmap, sizeof(bm), &bm);
    StretchBlt(hdc, x, y, w ? w : bm.bmWidth, h ? h : bm.bmHeight, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void paint_dialog(HWND hwnd, struct dialog *dlg)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    HFONT title = CreateFontW(-19, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Tahoma");
    HFONT label = CreateFontW(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Tahoma");
    HFONT normal = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Tahoma");
    HFONT old;
    RECT rc, text;
    int i, image;

    if (dlg->background) draw_bitmap(hdc, dlg->background, 0, 0, DIALOG_W, DIALOG_H);
    else
    {
        SetRect(&rc, 0, 0, DIALOG_W, DIALOG_H);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_ACTIVECAPTION));
    }
    if (dlg->flag)
    {
        BITMAP bm;
        GetObjectW(dlg->flag, sizeof(bm), &bm);
        draw_bitmap(hdc, dlg->flag, DIALOG_W - bm.bmWidth - 6, 2, 0, 0);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    old = SelectObject(hdc, title);
    SetRect(&text, 12, 10, DIALOG_W - 60, 40);
    DrawTextW(hdc, dlg->title, -1, &text, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, label);
    for (i = 0; i < dlg->count; i++)
    {
        struct choice *c = &dlg->choices[i];

        if (!c->enabled) image = c->disabled_image >= 0 ? c->disabled_image : c->image;
        else image = c->image + (dlg->pressed == i && dlg->hot == i ? 1 : dlg->hot == i ? 2 : 0);
        draw_strip_image(hdc, dlg, image, &c->rc);
        SetTextColor(hdc, c->enabled ? RGB(255, 255, 255) : RGB(0x9c, 0xb2, 0xe6));
        SetRect(&text, c->rc.left - 40, c->rc.bottom + 9, c->rc.right + 40, c->rc.bottom + 24);
        DrawTextW(hdc, c->text, -1, &text, DT_CENTER | DT_SINGLELINE);
    }

    /* the Cancel button, as the visual style draws buttons */
    SelectObject(hdc, normal);
    {
        HTHEME theme = theme_open(hwnd, L"Button");
        int state = dlg->cancel_pressed && dlg->cancel_hot ? PBS_PRESSED : dlg->cancel_hot ? PBS_HOT : PBS_NORMAL;

        if (theme)
        {
            DrawThemeBackground(theme, hdc, BP_PUSHBUTTON, state, &dlg->cancel, NULL);
            CloseThemeData(theme);
        }
        else DrawFrameControl(hdc, &dlg->cancel, DFC_BUTTON, DFCS_BUTTONPUSH | (state == PBS_PRESSED ? DFCS_PUSHED : 0));
        SetTextColor(hdc, RGB(0, 0, 0));
        DrawTextW(hdc, L"Cancel", -1, &dlg->cancel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, old);
    DeleteObject(title);
    DeleteObject(label);
    DeleteObject(normal);
    EndPaint(hwnd, &ps);
}

static int hit_choice(struct dialog *dlg, POINT pt)
{
    int i;

    for (i = 0; i < dlg->count; i++)
    {
        RECT rc = dlg->choices[i].rc;
        InflateRect(&rc, 4, 4);
        if (dlg->choices[i].enabled && PtInRect(&rc, pt)) return i;
    }
    return -1;
}

static void finish(HWND hwnd, struct dialog *dlg, int result)
{
    dlg->result = result;
    dlg->done = TRUE;
    ShowWindow(hwnd, SW_HIDE);
}

static LRESULT CALLBACK dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct dialog *dlg = (struct dialog *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
    int i;

    switch (msg)
    {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lparam)->lpCreateParams);
        return 0;
    case WM_PAINT:
        paint_dialog(hwnd, dlg);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
    {
        int hot = hit_choice(dlg, pt);
        BOOL cancel_hot = PtInRect(&dlg->cancel, pt);
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };

        TrackMouseEvent(&tme);
        if (hot != dlg->hot || cancel_hot != dlg->cancel_hot)
        {
            dlg->hot = hot;
            dlg->cancel_hot = cancel_hot;
            SetCursor(LoadCursorW(NULL, hot >= 0 ? (const WCHAR *)IDC_HAND : (const WCHAR *)IDC_ARROW));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        dlg->hot = -1;
        dlg->cancel_hot = FALSE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (dlg->hot >= 0) { SetCursor(LoadCursorW(NULL, (const WCHAR *)IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
        dlg->pressed = hit_choice(dlg, pt);
        dlg->cancel_pressed = PtInRect(&dlg->cancel, pt);
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (dlg->pressed >= 0 && dlg->pressed == hit_choice(dlg, pt))
            finish(hwnd, dlg, dlg->choices[dlg->pressed].action);
        else if (dlg->cancel_pressed && PtInRect(&dlg->cancel, pt))
            finish(hwnd, dlg, -1);
        dlg->pressed = -1;
        dlg->cancel_pressed = FALSE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) finish(hwnd, dlg, -1);
        return 0;
    case WM_CHAR:
        /* the underlined keys */
        for (i = 0; i < dlg->count; i++)
        {
            const WCHAR *amp = wcschr(dlg->choices[i].text, '&');
            if (dlg->choices[i].enabled && amp && towupper(amp[1]) == towupper((WCHAR)wparam))
                finish(hwnd, dlg, dlg->choices[i].action);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && !dlg->done) SetForegroundWindow(hwnd);   /* modal over everything */
        return 0;
    case WM_CLOSE:
        finish(hwnd, dlg, -1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* The bitmaps of the dialog: Windows XP's from the resource pack, else ReactOS' */
static void load_art(struct dialog *dlg, BOOL logoff)
{
    BITMAP bm;

    if (logoff)
    {
        dlg->background = shellres_bitmap(L"shell32", 14354, IDB_POWER_BACKGROUND);
        dlg->flag = shellres_bitmap(L"shell32", 14355, IDB_POWER_FLAG);
        dlg->strip = shellres_bitmap(L"shell32", 14356, IDB_LOGOFF_STRIP);
    }
    else
    {
        dlg->background = shellres_bitmap(L"msgina", 20142, IDB_POWER_BACKGROUND);
        dlg->flag = shellres_bitmap(L"msgina", 20143, IDB_POWER_FLAG);
        dlg->strip = shellres_bitmap(L"msgina", 20150, 0);
        if (!dlg->strip)
        {
            /* ReactOS' strip has Restart before Stand By */
            dlg->strip = shellres_bitmap(L"explorer", 0, IDB_TURNOFF_STRIP);
            dlg->choices[0].image = 6;
            dlg->choices[2].image = 3;
        }
    }
    dlg->strip_size = BUTTON;
    if (dlg->strip && GetObjectW(dlg->strip, sizeof(bm), &bm)) dlg->strip_size = bm.bmWidth;
}

static int run_dialog(HWND parent, struct dialog *dlg)
{
    int x = (GetSystemMetrics(SM_CXSCREEN) - DIALOG_W) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - DIALOG_H) / 2;
    int spacing = dlg->count == 3 ? 85 : 115, first = (DIALOG_W - (dlg->count - 1) * spacing - BUTTON) / 2, i;
    WNDCLASSW wc = { 0 };
    HWND dim, hwnd;
    MSG msg;

    for (i = 0; i < dlg->count; i++)
        SetRect(&dlg->choices[i].rc, first + i * spacing, 80, first + i * spacing + BUTTON, 80 + BUTTON);
    SetRect(&dlg->cancel, DIALOG_W - 69, DIALOG_H - 30, DIALOG_W - 11, DIALOG_H - 11);
    dlg->hot = dlg->pressed = dlg->result = -1;

    wc.lpfnWndProc = dialog_proc;
    wc.hInstance = explorer_hInstance;
    wc.lpszClassName = DIALOG_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);

    /* the menu that opened us goes away first: let the windows under it paint before the screen is taken */
    {
        DWORD end = GetTickCount() + 200;

        RedrawWindow(NULL, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        while ((int)(end - GetTickCount()) > 0)
        {
            if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            else MsgWaitForMultipleObjects(0, NULL, FALSE, max(1, (int)(end - GetTickCount())), QS_ALLINPUT);
        }
    }
    dim = dim_screen();
    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, DIALOG_CLASS, dlg->title, WS_POPUP | WS_BORDER,
                           x, y, DIALOG_W, DIALOG_H, NULL, NULL, explorer_hInstance, dlg);
    if (hwnd)
    {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        while (!dlg->done && GetMessageW(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        DestroyWindow(hwnd);
    }
    undim_screen(dim);
    if (dlg->background) DeleteObject(dlg->background);
    if (dlg->flag) DeleteObject(dlg->flag);
    if (dlg->strip) DeleteObject(dlg->strip);
    if (parent) SetForegroundWindow(parent);
    return dlg->result;
}

/* Windows XP's Turn off computer: Stand By (Shift: Hibernate), Turn Off, Restart */
void power_mgmt_show_shutdown_dialog(HWND parent)
{
    struct dialog dlg = { L"Turn off computer",
        { { L"&Stand By", POWER_ACTION_STANDBY, 3, 9 },
          { L"T&urn Off", POWER_ACTION_SHUTDOWN, 0, -1 },
          { L"&Restart", POWER_ACTION_RESTART, 6, -1 } }, 3 };
    int action;

    dlg.choices[0].enabled = power_can(WEX_POWER_SUSPEND);
    dlg.choices[1].enabled = dlg.choices[2].enabled = TRUE;
    load_art(&dlg, FALSE);
    if ((action = run_dialog(parent, &dlg)) >= 0) power_mgmt_execute_action(action);
}

/* Windows XP's Log Off Windows: Switch User and Log Off */
void power_mgmt_show_logoff_dialog(HWND parent)
{
    struct dialog dlg = { L"Log Off Windows",
        { { L"&Switch User", POWER_ACTION_SWITCHUSER, 0, 6 },
          { L"&Log Off", POWER_ACTION_LOGOFF, 3, -1 } }, 2 };
    int action;

    dlg.choices[0].enabled = power_can(WEX_POWER_SWITCHUSER);
    dlg.choices[1].enabled = TRUE;
    load_art(&dlg, TRUE);
    if ((action = run_dialog(parent, &dlg)) >= 0) power_mgmt_execute_action(action);
}
