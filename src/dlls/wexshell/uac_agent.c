/*
 * WineExplorer Desktop Environment - polkit authentication agent with the UAC-style consent dialog
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Linux services (timedate1, ...) ask polkit whether the caller may do something; polkit asks the session's
 * registered authentication agent to prove the user's identity.  This is that agent: when polkit calls, we
 * show the dialog, hand the typed password to polkit's helper through wexhost, and report the result.
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
#include <wexhost.h>

#include "wine/debug.h"
#include "resource.h"
#include "uac_agent.h"

#include "module.h"
#include "wexres.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define WM_WEX_AGENT (WM_APP + 300)
#define AGENT_WND_CLASS L"WexPolkitAgentWnd"

/* Draws authentic 4-quadrant Windows Vista UAC shield with golden border */
static void draw_shield(HDC hdc, int x, int y, int w, int h)
{
    POINT pts[7];
    POINT q_tl[6], q_tr[6], q_bl[5], q_br[5];
    HBRUSH br_gold, br_blue_light, br_blue_dark;
    HPEN pen_rim, pen_gold, pen_cross;
    HGDIOBJ old_brush, old_pen;
    int mid_x = x + w / 2;
    int mid_y = y + h / 2;

    br_gold = CreateSolidBrush(RGB(246, 185, 25));
    br_blue_light = CreateSolidBrush(RGB(35, 140, 235));
    br_blue_dark = CreateSolidBrush(RGB(20, 95, 195));
    pen_rim = CreatePen(PS_SOLID, 2, RGB(255, 235, 130));
    pen_gold = CreatePen(PS_SOLID, 1, RGB(220, 160, 20));
    pen_cross = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));

    /* Outer shield polygon */
    pts[0].x = x + 2;     pts[0].y = y;
    pts[1].x = x + w - 2; pts[1].y = y;
    pts[2].x = x + w;     pts[2].y = y + 4;
    pts[3].x = x + w;     pts[3].y = y + h * 5 / 9;
    pts[4].x = mid_x;     pts[4].y = y + h;
    pts[5].x = x;         pts[5].y = y + h * 5 / 9;
    pts[6].x = x;         pts[6].y = y + 4;

    /* Top-left quadrant (Gold) */
    q_tl[0].x = x + 2; q_tl[0].y = y;
    q_tl[1].x = mid_x; q_tl[1].y = y;
    q_tl[2].x = mid_x; q_tl[2].y = mid_y;
    q_tl[3].x = x;     q_tl[3].y = mid_y;
    q_tl[4].x = x;     q_tl[4].y = y + 4;
    q_tl[5].x = x + 2; q_tl[5].y = y;
    old_brush = SelectObject(hdc, br_gold);
    old_pen = SelectObject(hdc, pen_gold);
    Polygon(hdc, q_tl, 6);

    /* Top-right quadrant (Sky Blue) */
    q_tr[0].x = mid_x;     q_tr[0].y = y;
    q_tr[1].x = x + w - 2; q_tr[1].y = y;
    q_tr[2].x = x + w;     q_tr[2].y = y + 4;
    q_tr[3].x = x + w;     q_tr[3].y = mid_y;
    q_tr[4].x = mid_x;     q_tr[4].y = mid_y;
    q_tr[5].x = mid_x;     q_tr[5].y = y;
    SelectObject(hdc, br_blue_light);
    Polygon(hdc, q_tr, 6);

    /* Bottom-left quadrant (Deep Blue) */
    q_bl[0].x = x;     q_bl[0].y = mid_y;
    q_bl[1].x = mid_x; q_bl[1].y = mid_y;
    q_bl[2].x = mid_x; q_bl[2].y = y + h;
    q_bl[3].x = x;     q_bl[3].y = y + h * 5 / 9;
    q_bl[4].x = x;     q_bl[4].y = mid_y;
    SelectObject(hdc, br_blue_dark);
    Polygon(hdc, q_bl, 5);

    /* Bottom-right quadrant (Gold) */
    q_br[0].x = mid_x; q_br[0].y = mid_y;
    q_br[1].x = x + w; q_br[1].y = mid_y;
    q_br[2].x = x + w; q_br[2].y = y + h * 5 / 9;
    q_br[3].x = mid_x; q_br[3].y = y + h;
    q_br[4].x = mid_x; q_br[4].y = mid_y;
    SelectObject(hdc, br_gold);
    Polygon(hdc, q_br, 5);

    /* Center cross */
    SelectObject(hdc, pen_cross);
    MoveToEx(hdc, mid_x, y + 1, NULL);
    LineTo(hdc, mid_x, y + h - 1);
    MoveToEx(hdc, x + 1, mid_y, NULL);
    LineTo(hdc, x + w - 1, mid_y);

    /* Golden rim border */
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    SelectObject(hdc, pen_rim);
    Polygon(hdc, pts, 7);

    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen_rim);
    DeleteObject(pen_gold);
    DeleteObject(pen_cross);
    DeleteObject(br_gold);
    DeleteObject(br_blue_light);
    DeleteObject(br_blue_dark);
}

static void draw_banner(const DRAWITEMSTRUCT *dis)
{
    HDC hdc = dis->hDC;
    HBRUSH bg = CreateSolidBrush(RGB(12, 80, 140));
    HPEN pen_line = CreatePen(PS_SOLID, 1, RGB(8, 55, 95));
    HFONT font, old_font;
    LOGFONTW lf;
    RECT text = dis->rcItem;
    HGDIOBJ old_pen;

    FillRect(hdc, &dis->rcItem, bg);
    DeleteObject(bg);

    old_pen = SelectObject(hdc, pen_line);
    MoveToEx(hdc, dis->rcItem.left, dis->rcItem.bottom - 1, NULL);
    LineTo(hdc, dis->rcItem.right, dis->rcItem.bottom - 1);
    SelectObject(hdc, old_pen);
    DeleteObject(pen_line);

    draw_shield(hdc, 14, 7, 24, 28);

    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -14;
    lf.lfWeight = FW_SEMIBOLD;
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    font = CreateFontIndirectW(&lf);
    old_font = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    text.left = 48;
    text.right -= 12;
    DrawTextW(hdc, L"Do you want to allow the following program to make changes to this computer?",
              -1, &text, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
    SelectObject(hdc, old_font);
    DeleteObject(font);
}

/* Draws the user avatar icon inside the silver frame matching uaccredentialprompt.gif */
static void draw_user_tile(const DRAWITEMSTRUCT *dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    HPEN pen_avatar = CreatePen(PS_SOLID, 1, RGB(165, 178, 192));
    HBRUSH bg_avatar = CreateSolidBrush(RGB(226, 233, 242));
    HBRUSH br_skin = CreateSolidBrush(RGB(225, 185, 145));
    HBRUSH br_hair = CreateSolidBrush(RGB(90, 65, 45));
    HBRUSH br_shirt = CreateSolidBrush(RGB(95, 150, 218));
    HGDIOBJ old_brush, old_pen;
    POINT shoulder_pts[5];

    old_brush = SelectObject(hdc, bg_avatar);
    old_pen = SelectObject(hdc, pen_avatar);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

    /* Shoulders / blue shirt */
    shoulder_pts[0].x = rc.left + 3;  shoulder_pts[0].y = rc.bottom - 1;
    shoulder_pts[1].x = rc.left + 8;  shoulder_pts[1].y = rc.top + (rc.bottom - rc.top) * 5 / 9;
    shoulder_pts[2].x = rc.right - 8; shoulder_pts[2].y = rc.top + (rc.bottom - rc.top) * 5 / 9;
    shoulder_pts[3].x = rc.right - 3; shoulder_pts[3].y = rc.bottom - 1;
    shoulder_pts[4].x = rc.left + 3;  shoulder_pts[4].y = rc.bottom - 1;
    SelectObject(hdc, br_shirt);
    SelectObject(hdc, GetStockObject(NULL_PEN));
    Polygon(hdc, shoulder_pts, 5);

    /* Head */
    SelectObject(hdc, br_skin);
    Ellipse(hdc, rc.left + 9, rc.top + 5, rc.right - 9, rc.top + (rc.bottom - rc.top) * 6 / 10);

    /* Hair */
    SelectObject(hdc, br_hair);
    Chord(hdc, rc.left + 9, rc.top + 4, rc.right - 9, rc.top + (rc.bottom - rc.top) * 5 / 10,
          rc.right - 9, rc.top + 10, rc.left + 9, rc.top + 10);

    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen_avatar);
    DeleteObject(bg_avatar);
    DeleteObject(br_skin);
    DeleteObject(br_hair);
    DeleteObject(br_shirt);
}


typedef struct
{
    WEX_AGENT_EVENT ev;
    WCHAR message[WEX_TEXT_MAX];
} UAC_REQUEST;

static HWND g_agent_wnd;
static HWND g_dialog;
static char g_dialog_cookie[WEX_COOKIE_MAX];

static void CALLBACK agent_event(const WEX_AGENT_EVENT *ev, void *context)
{
    WEX_AGENT_EVENT *copy = malloc(sizeof(*copy));

    if (!copy) return;
    *copy = *ev;
    if (!PostMessageW(g_agent_wnd, WM_WEX_AGENT, 0, (LPARAM)copy)) free(copy);
}

static void update_prompt(HWND hwnd)
{
    SetDlgItemTextW(hwnd, IDC_UAC_PROMPT, L"To continue, type an administrator password, and then click Yes.");
}

static INT_PTR CALLBACK uac_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    UAC_REQUEST *req = (UAC_REQUEST *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT card = { 12, 102, 12 + 222, 102 + 44 };
        HBRUSH bg_card = CreateSolidBrush(RGB(236, 244, 254));
        HPEN pen_border = CreatePen(PS_SOLID, 1, RGB(180, 208, 238));
        HGDIOBJ old_b, old_p;

        MapDialogRect(hwnd, &card);
        old_b = SelectObject(hdc, bg_card);
        old_p = SelectObject(hdc, pen_border);
        RoundRect(hdc, card.left, card.top, card.right, card.bottom, 6, 6);
        SelectObject(hdc, old_b);
        SelectObject(hdc, old_p);
        DeleteObject(bg_card);
        DeleteObject(pen_border);
        EndPaint(hwnd, &ps);
        return TRUE;
    }

    case WM_INITDIALOG:
    {
        DWORD i;
        HFONT font_bold;
        LOGFONTW lf;

        req = (UAC_REQUEST *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)req);
        g_dialog = hwnd;

        SetDlgItemTextW(hwnd, IDC_UAC_PROGRAM, req->message);

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -12;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        if ((font_bold = CreateFontIndirectW(&lf)))
            SendDlgItemMessageW(hwnd, IDC_UAC_PROGRAM, WM_SETFONT, (WPARAM)font_bold, TRUE);

        for (i = 0; i < req->ev.identity_count; i++)
        {
            WCHAR name[WEX_USER_MAX];
            MultiByteToWideChar(CP_UTF8, 0, req->ev.identities[i], -1, name, ARRAY_SIZE(name));
            SendDlgItemMessageW(hwnd, IDC_UAC_ACCOUNT, CB_ADDSTRING, 0, (LPARAM)name);
        }
        SendDlgItemMessageW(hwnd, IDC_UAC_ACCOUNT, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageW(hwnd, IDC_UAC_PASSWORD, EM_LIMITTEXT, 127, 0);
        update_prompt(hwnd);
        SetFocus(GetDlgItem(hwnd, IDC_UAC_PASSWORD));
        return FALSE;
    }

    case WM_CTLCOLORSTATIC:
        if ((HWND)lparam == GetDlgItem(hwnd, IDC_UAC_PW_LABEL))
        {
            SetBkMode((HDC)wparam, TRANSPARENT);
            SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        }
        if ((HWND)lparam == GetDlgItem(hwnd, IDC_UAC_ERROR))
        {
            SetBkMode((HDC)wparam, TRANSPARENT);
            SetTextColor((HDC)wparam, RGB(190, 0, 0));
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        }
        break;

    case WM_DRAWITEM:
        if (wparam == IDC_UAC_BANNER) { draw_banner((const DRAWITEMSTRUCT *)lparam); return TRUE; }
        if (wparam == IDC_UAC_USERTILE) { draw_user_tile((const DRAWITEMSTRUCT *)lparam); return TRUE; }
        break;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDYES:
        {
            WCHAR wpw[128], wuser[WEX_USER_MAX];
            char pw[WEX_PASSWORD_MAX], user[WEX_USER_MAX];
            int sel = (int)SendDlgItemMessageW(hwnd, IDC_UAC_ACCOUNT, CB_GETCURSEL, 0, 0);
            DWORD err;

            if (sel < 0 || (DWORD)sel >= req->ev.identity_count) return TRUE;
            GetDlgItemTextW(hwnd, IDC_UAC_PASSWORD, wpw, ARRAY_SIZE(wpw));
            if (!wpw[0])
            {
                SetDlgItemTextW(hwnd, IDC_UAC_ERROR, L"Please type a password.");
                SetFocus(GetDlgItem(hwnd, IDC_UAC_PASSWORD));
                return TRUE;
            }
            SendDlgItemMessageW(hwnd, IDC_UAC_ACCOUNT, CB_GETLBTEXT, sel, (LPARAM)wuser);
            WideCharToMultiByte(CP_UTF8, 0, wuser, -1, user, sizeof(user), NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, wpw, -1, pw, sizeof(pw), NULL, NULL);

            SetDlgItemTextW(hwnd, IDC_UAC_ERROR, L"");
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
            err = WexAgentRespond(req->ev.cookie, user, pw, FALSE);
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
            SecureZeroMemory(pw, sizeof(pw));
            SecureZeroMemory(wpw, sizeof(wpw));
            SetDlgItemTextW(hwnd, IDC_UAC_PASSWORD, L"");

            if (!err) EndDialog(hwnd, IDYES);
            else
            {
                SetDlgItemTextW(hwnd, IDC_UAC_ERROR, err == ERROR_ACCESS_DENIED ?
                                L"The password is incorrect. Please try again." :
                                L"The request is no longer valid.");
                if (err != ERROR_ACCESS_DENIED) EndDialog(hwnd, IDNO);
                else SetFocus(GetDlgItem(hwnd, IDC_UAC_PASSWORD));
            }
            return TRUE;
        }

        case IDNO:
        case IDCANCEL:
            WexAgentRespond(req->ev.cookie, "", "", TRUE);
            EndDialog(hwnd, IDNO);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        WexAgentRespond(req->ev.cookie, "", "", TRUE);
        EndDialog(hwnd, IDNO);
        return TRUE;

    case WM_DESTROY:
        g_dialog = NULL;
        g_dialog_cookie[0] = 0;
        break;
    }
    return FALSE;
}

static LRESULT CALLBACK agent_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_WEX_AGENT)
    {
        WEX_AGENT_EVENT *ev = (WEX_AGENT_EVENT *)lparam;

        if (ev->type == WEX_EVENT_BEGIN)
        {
            if (g_dialog)
            {
                WexAgentRespond(ev->cookie, "", "", TRUE);   /* one prompt at a time */
            }
            else
            {
                UAC_REQUEST req;
                memset(&req, 0, sizeof(req));
                req.ev = *ev;
                MultiByteToWideChar(CP_UTF8, 0, ev->message, -1, req.message, ARRAY_SIZE(req.message));
                lstrcpynA(g_dialog_cookie, ev->cookie, sizeof(g_dialog_cookie));
                DialogBoxParamW(WexResModule(), MAKEINTRESOURCEW(IDD_UAC), NULL, uac_dlg_proc, (LPARAM)&req);
            }
        }
        else if (ev->type == WEX_EVENT_CANCEL && g_dialog && !strcmp(ev->cookie, g_dialog_cookie))
        {
            EndDialog(g_dialog, IDNO);   /* polkit withdrew the request */
        }
        free(ev);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void uac_agent_start(void)
{
    WNDCLASSW wc;
    DWORD err;

    if (g_agent_wnd) return;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = agent_wnd_proc;
    wc.hInstance = explorer_hInstance;
    wc.lpszClassName = AGENT_WND_CLASS;
    RegisterClassW(&wc);
    g_agent_wnd = CreateWindowExW(0, AGENT_WND_CLASS, NULL, WS_POPUP, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (!g_agent_wnd) return;

    err = WexAgentStart(agent_event, NULL);
    if (err)
    {
        WARN("polkit agent not started (%lu): authorization prompts will not be available\n", err);
        DestroyWindow(g_agent_wnd);
        g_agent_wnd = NULL;
    }
}

void uac_agent_stop(void)
{
    if (!g_agent_wnd) return;
    WexAgentStop();
    DestroyWindow(g_agent_wnd);
    g_agent_wnd = NULL;
}
