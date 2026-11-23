/*
 * WineExplorer Desktop Environment - Network Identification Wizard (sysdm.cpl)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * The wizard of the Computer Name page's Network ID button, with the pages and texts of Windows XP's (the XP VM is the
 * reference): welcome; how the computer is used (a business network or home); what kind of network (with a domain or
 * without); the information to gather; the user account and its domain; the computer's name and domain (when no account
 * for the computer is found there); the workgroup; the end. The join is the Change dialog's (docs/DOMAIN_JOIN.md):
 * NetValidateName and NetJoinDomain reach the join engine through its socket, and a process that is not an elevated
 * administrator's is offered to be opened again as one. Not here, because they do not apply to this system: the local
 * user account of Windows' wizard's last pages (a domain user is admitted at the first logon, docs/DOMAIN_LOGON.md), and
 * the workgroup's name (Samba's setting, shown, not changed). No restart is asked for, because a join takes effect at
 * once. The pictures on the pages are drawn here, not taken from Windows.
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
#include <commctrl.h>
#include <lm.h>
#include <stdio.h>
#include <string.h>
#include <wexhost.h>
#include <wexjoin.h>

#include "sysdm.h"

#ifndef PSH_WIZARD97
#define PSH_WIZARD97 PSH_WIZARD97_NEW
#endif

BOOL do_leave(HWND dlg);

#define CAPTION              L"Network Identification Wizard"
#define WIZ_PASS_CHARS       128

static struct
{
    HWND sheet;
    HWND account_page;   /* the password stays in its box: read for the join only, into a buffer wiped after it */
    HFONT title_font;
    BOOL business, domain_net, account_filled;
    WCHAR user[128], acct_domain[256], comp_name[64], comp_domain[256];
    int history[16], depth;
    WCHAR done_text[512];
} W;

/* ---------------------------------------------------------------------------------------------------- the pictures */

/* the picture on the first and last pages (164 x 314): a dark blue field with a network of nodes, drawn here */
static HBITMAP make_picture(int width, int height, BOOL header)
{
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ old = SelectObject(dc, bitmap);
    static const POINT nodes[] = { { 20, 40 }, { 90, 24 }, { 138, 70 }, { 60, 110 }, { 120, 150 }, { 30, 190 }, { 100, 226 }, { 140, 270 }, { 50, 280 } };
    static const int links[][2] = { { 0, 1 }, { 1, 2 }, { 0, 3 }, { 1, 3 }, { 2, 4 }, { 3, 4 }, { 3, 5 }, { 4, 6 }, { 5, 6 }, { 6, 7 }, { 5, 8 }, { 6, 8 } };
    unsigned int i;
    int y;

    ReleaseDC(NULL, screen);
    if (header)
    {
        RECT rect = { 0, 0, width, height };
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 51, 153));
        HBRUSH node = CreateSolidBrush(RGB(0, 51, 153));
        static const POINT hn[] = { { 10, 12 }, { 38, 10 }, { 24, 26 }, { 10, 40 }, { 38, 38 } };

        FillRect(dc, &rect, white);
        SelectObject(dc, pen);
        SelectObject(dc, node);
        for (i = 0; i < 4; i++) { MoveToEx(dc, hn[i].x, hn[i].y, NULL); LineTo(dc, hn[i + 1].x, hn[i + 1].y); }
        MoveToEx(dc, hn[0].x, hn[0].y, NULL); LineTo(dc, hn[2].x, hn[2].y);
        for (i = 0; i < ARRAY_SIZE(hn); i++) Ellipse(dc, hn[i].x - 4, hn[i].y - 4, hn[i].x + 5, hn[i].y + 5);
        DeleteObject(pen); DeleteObject(node); DeleteObject(white);
    }
    else
    {
        HPEN line = CreatePen(PS_SOLID, 2, RGB(70, 110, 200)), outline = CreatePen(PS_SOLID, 2, RGB(190, 210, 255));
        HBRUSH node = CreateSolidBrush(RGB(30, 60, 150));

        for (y = 0; y < height; y++)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 8 + y * 40 / height, 60 + y * 60 / height));
            SelectObject(dc, pen);
            MoveToEx(dc, 0, y, NULL); LineTo(dc, width, y);
            SelectObject(dc, GetStockObject(BLACK_PEN));
            DeleteObject(pen);
        }
        SelectObject(dc, line);
        for (i = 0; i < ARRAY_SIZE(links); i++)
        {
            MoveToEx(dc, nodes[links[i][0]].x, nodes[links[i][0]].y, NULL);
            LineTo(dc, nodes[links[i][1]].x, nodes[links[i][1]].y);
        }
        SelectObject(dc, outline);
        SelectObject(dc, node);
        for (i = 0; i < ARRAY_SIZE(nodes); i++) Ellipse(dc, nodes[i].x - 8, nodes[i].y - 8, nodes[i].x + 9, nodes[i].y + 9);
        DeleteObject(line); DeleteObject(outline); DeleteObject(node);
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    return bitmap;
}

/* ---------------------------------------------------------------------------------------------------- the pages */

static void set_buttons(HWND sheet, DWORD flags) { PostMessageW(sheet, PSM_SETWIZBUTTONS, 0, flags); }
static void next_page(HWND page, int id) { SetWindowLongPtrW(page, DWLP_MSGRESULT, id); }
static void stay(HWND page) { SetWindowLongPtrW(page, DWLP_MSGRESULT, -1); }

/* the way back: where the user came from */
static int previous_page(void) { return W.depth > 0 ? W.history[--W.depth] : 0; }

/* to the next page, remembering this one for Back */
static void go(HWND page, int from, int to)
{
    if (W.depth < ARRAY_SIZE(W.history)) W.history[W.depth++] = from;
    next_page(page, to);
}

static void set_done(const WCHAR *what)
{
    swprintf(W.done_text, ARRAY_SIZE(W.done_text),
             L"You have successfully completed the Network Identification Wizard.\r\n\r\n%ls\r\n\r\nTo close this wizard, click Finish.", what);
}

/* the account for the API: DOMAIN\user, or the name as typed when it holds the domain already */
static void account_for_api(WCHAR *account, DWORD chars)
{
    if (wcschr(W.user, '\\') || wcschr(W.user, '@') || !W.acct_domain[0]) lstrcpynW(account, W.user, chars);
    else swprintf(account, chars, L"%ls\\%ls", W.acct_domain, W.user);
}

static void close_wizard(void) { PostMessageW(W.sheet, PSM_PRESSBUTTON, PSBTN_CANCEL, 0); }

/* a failure of a call that needs the system group: offer the elevated copy; else the error. TRUE when the page stays. */
static BOOL report(HWND page, const WCHAR *domain, DWORD status)
{
    WCHAR upper[256];

    if (!status) return FALSE;
    if (status == ERROR_ACCESS_DENIED)
    {
        offer_elevation(page, CAPTION);
        if (g_reopen_elevated) close_wizard();
        return TRUE;
    }
    lstrcpynW(upper, domain, ARRAY_SIZE(upper));
    CharUpperW(upper);
    show_join_error(page, CAPTION, upper, status, 0);
    return TRUE;
}

static void fill_account_page(HWND page)
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1], user[128];
    DWORD size;

    W.account_page = page;
    if (W.account_filled) return;
    W.account_filled = TRUE;
    size = ARRAY_SIZE(user);
    if (GetUserNameW(user, &size)) SetDlgItemTextW(page, IDC_NID_USER, user);
    size = ARRAY_SIZE(name);
    if (GetComputerNameW(name, &size)) SetDlgItemTextW(page, IDC_NID_DOMAIN, name);
    SendDlgItemMessageW(page, IDC_NID_USER, EM_SETSEL, 0, -1);
    SendDlgItemMessageW(page, IDC_NID_PASS, EM_LIMITTEXT, WIZ_PASS_CHARS - 1, 0);
}

static void comp_domain_buttons(HWND page)
{
    WCHAR domain[256];

    GetDlgItemTextW(page, IDC_NID_COMP_DOMAIN, domain, ARRAY_SIZE(domain));
    set_buttons(GetParent(page), PSWIZB_BACK | (domain[0] ? PSWIZB_NEXT : 0));
}

/* the join: the computer's name (when changed), then NetJoinDomain; TRUE when the page may be left */
static BOOL do_wizard_join(HWND page)
{
    WCHAR account[300], text[300], pass[WIZ_PASS_CHARS];
    WEX_HOSTNAME host;
    char name[128];
    DWORD status;

    if (g_joined && _wcsicmp(W.comp_domain, g_member))
    {
        swprintf(text, ARRAY_SIZE(text), L"This computer is a member of the %ls domain. Remove it from that domain first "
                 L"(System Properties, Computer Name, Change).", g_member);
        MessageBoxW(page, text, CAPTION, MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    WideCharToMultiByte(CP_UTF8, 0, W.comp_name, -1, name, sizeof(name), NULL, NULL);
    if (!WexHostnameGet(&host) && strcmp(name, host.static_name))
    {
        DWORD err = WexHostnameSet(page, name, FALSE);

        if (err)
        {
            if (err != ERROR_CANCELLED)
                MessageBoxW(page, err == ERROR_INVALID_PARAMETER ? L"The computer name is not valid." :
                            L"You do not have permission to rename this computer.", CAPTION, MB_OK | MB_ICONWARNING);
            return FALSE;
        }
    }
    account_for_api(account, ARRAY_SIZE(account));
    pass[0] = 0;
    if (W.account_page) GetDlgItemTextW(W.account_page, IDC_NID_PASS, pass, ARRAY_SIZE(pass));
    status = NetJoinDomain(NULL, W.comp_domain, NULL, account, pass, WEX_NETSETUP_JOIN_DOMAIN | WEX_NETSETUP_ACCT_CREATE);
    SecureZeroMemory(pass, sizeof(pass));
    if (report(page, W.comp_domain, status)) return FALSE;
    read_membership();
    CharUpperW(W.comp_domain);
    swprintf(text, ARRAY_SIZE(text), L"This computer is now a member of the %ls domain.", W.comp_domain);
    set_done(text);
    return TRUE;
}

static INT_PTR CALLBACK page_proc(HWND page, UINT msg, WPARAM wparam, LPARAM lparam)
{
    int id = (int)GetWindowLongPtrW(page, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
        id = (int)((PROPSHEETPAGEW *)lparam)->lParam;
        SetWindowLongPtrW(page, DWLP_USER, id);
        if (id == IDD_NETID_WELCOME || id == IDD_NETID_DONE)
            SendDlgItemMessageW(page, IDC_NID_TITLE, WM_SETFONT, (WPARAM)W.title_font, TRUE);
        if (id == IDD_NETID_USAGE) CheckRadioButton(page, IDC_NID_BUSINESS, IDC_NID_HOME, IDC_NID_BUSINESS);
        if (id == IDD_NETID_KIND) CheckRadioButton(page, IDC_NID_DOMAIN_NET, IDC_NID_NO_DOMAIN_NET, IDC_NID_DOMAIN_NET);
        if (id == IDD_NETID_ACCOUNT)
        {
            SendDlgItemMessageW(page, IDC_NID_USER, EM_LIMITTEXT, ARRAY_SIZE(W.user) - 1, 0);
            SendDlgItemMessageW(page, IDC_NID_DOMAIN, EM_LIMITTEXT, ARRAY_SIZE(W.acct_domain) - 1, 0);
        }
        if (id == IDD_NETID_WORKGROUP) SetDlgItemTextW(page, IDC_NID_WORKGROUP, g_member);
        if (id == IDD_NETID_INFO)
        {
            static const struct { int id; const WCHAR *text; } items[] =
            {
                { IDC_NID_B1, L"\x2022  User name" }, { IDC_NID_B2, L"\x2022  Password" }, { IDC_NID_B3, L"\x2022  User account domain" },
                { IDC_NID_B4, L"\x2022  Computer name" }, { IDC_NID_B5, L"\x2022  Computer domain" },
            };
            unsigned int i;
            for (i = 0; i < ARRAY_SIZE(items); i++) SetDlgItemTextW(page, items[i].id, items[i].text);
        }
        return TRUE;

    case WM_COMMAND:
        if (id == IDD_NETID_COMPDOM && LOWORD(wparam) == IDC_NID_COMP_DOMAIN && HIWORD(wparam) == EN_CHANGE)
            comp_domain_buttons(page);
        return FALSE;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        HWND sheet = GetParent(page);

        W.sheet = sheet;
        switch (nmhdr->code)
        {
        case PSN_SETACTIVE:
            switch (id)
            {
            case IDD_NETID_WELCOME: set_buttons(sheet, PSWIZB_NEXT); break;
            case IDD_NETID_DONE:
                SetDlgItemTextW(page, IDC_NID_DONE_TEXT, W.done_text);
                set_buttons(sheet, PSWIZB_BACK | PSWIZB_FINISH);
                break;
            case IDD_NETID_ACCOUNT:
                fill_account_page(page);
                set_buttons(sheet, PSWIZB_BACK | PSWIZB_NEXT);
                break;
            case IDD_NETID_COMPDOM:
            {
                WCHAR text[400], name[MAX_COMPUTERNAME_LENGTH + 1], upper[256];
                DWORD size = ARRAY_SIZE(name);

                lstrcpynW(upper, W.acct_domain, ARRAY_SIZE(upper));
                CharUpperW(upper);
                swprintf(text, ARRAY_SIZE(text), L"Windows cannot find an account for your computer on the %ls domain.", upper);
                SetDlgItemTextW(page, IDC_NID_COMP_TEXT, text);
                SendDlgItemMessageW(page, IDC_NID_COMP_NAME, EM_LIMITTEXT, ARRAY_SIZE(W.comp_name) - 1, 0);
                if (!GetDlgItemTextW(page, IDC_NID_COMP_NAME, text, 2) && GetComputerNameW(name, &size))
                {
                    SetDlgItemTextW(page, IDC_NID_COMP_NAME, name);
                    SendDlgItemMessageW(page, IDC_NID_COMP_NAME, EM_SETSEL, 0, -1);
                }
                comp_domain_buttons(page);
                break;
            }
            default: set_buttons(sheet, PSWIZB_BACK | PSWIZB_NEXT); break;
            }
            return TRUE;

        case PSN_WIZBACK:
            next_page(page, previous_page());
            return TRUE;

        case PSN_WIZNEXT:
            switch (id)
            {
            case IDD_NETID_WELCOME: go(page, id, IDD_NETID_USAGE); break;
            case IDD_NETID_USAGE:
                W.business = IsDlgButtonChecked(page, IDC_NID_BUSINESS) == BST_CHECKED;
                if (W.business) go(page, id, IDD_NETID_KIND);
                else { set_done(L"No changes were made."); go(page, id, IDD_NETID_DONE); }
                break;
            case IDD_NETID_KIND:
                W.domain_net = IsDlgButtonChecked(page, IDC_NID_DOMAIN_NET) == BST_CHECKED;
                go(page, id, W.domain_net ? IDD_NETID_INFO : IDD_NETID_WORKGROUP);
                break;
            case IDD_NETID_INFO: go(page, id, IDD_NETID_ACCOUNT); break;
            case IDD_NETID_ACCOUNT:
            {
                DWORD status;

                GetDlgItemTextW(page, IDC_NID_USER, W.user, ARRAY_SIZE(W.user));
                GetDlgItemTextW(page, IDC_NID_DOMAIN, W.acct_domain, ARRAY_SIZE(W.acct_domain));
                if (!W.user[0] || !W.acct_domain[0])
                {
                    MessageBoxW(page, W.user[0] ? L"Type the name of the user account's domain." : L"Type the name of the user account.",
                                CAPTION, MB_OK | MB_ICONWARNING);
                    stay(page);
                    break;
                }
                if (g_joined && !_wcsicmp(W.acct_domain, g_member))
                {
                    WCHAR text[300];

                    swprintf(text, ARRAY_SIZE(text), L"This computer is already a member of the %ls domain.", g_member);
                    set_done(text);
                    go(page, id, IDD_NETID_DONE);
                    break;
                }
                status = NetValidateName(NULL, W.acct_domain, NULL, NULL, WEX_NETSETUP_NAME_DOMAIN);
                if (report(page, W.acct_domain, status)) { stay(page); break; }
                go(page, id, IDD_NETID_COMPDOM);
                break;
            }
            case IDD_NETID_COMPDOM:
                GetDlgItemTextW(page, IDC_NID_COMP_NAME, W.comp_name, ARRAY_SIZE(W.comp_name));
                GetDlgItemTextW(page, IDC_NID_COMP_DOMAIN, W.comp_domain, ARRAY_SIZE(W.comp_domain));
                if (!W.comp_name[0]) { MessageBoxW(page, L"Type the name of the computer.", CAPTION, MB_OK | MB_ICONWARNING); stay(page); break; }
                if (!do_wizard_join(page)) { stay(page); break; }
                go(page, id, IDD_NETID_DONE);
                break;
            case IDD_NETID_WORKGROUP:
                /* a computer in a domain that is told "no domain" leaves it (as the Change dialog's Workgroup does) */
                if (g_joined)
                {
                    if (!do_leave(page)) { if (g_reopen_elevated) close_wizard(); stay(page); break; }
                    swprintf(W.done_text, ARRAY_SIZE(W.done_text),
                             L"You have successfully completed the Network Identification Wizard.\r\n\r\n"
                             L"This computer is now a member of the %ls workgroup.\r\n\r\nTo close this wizard, click Finish.", g_member);
                }
                else set_done(L"No changes were made.");
                go(page, id, IDD_NETID_DONE);
                break;
            }
            return TRUE;

        case PSN_WIZFINISH:
            SetWindowLongPtrW(page, DWLP_MSGRESULT, FALSE);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
        return FALSE;
    }
    return FALSE;
}

void show_netid_wizard(HWND parent)
{
    static const struct { int id; const WCHAR *title, *subtitle; } pages[] =
    {
        { IDD_NETID_WELCOME, NULL, NULL },
        { IDD_NETID_USAGE, L"Connecting to the Network", L"How do you use this computer?" },
        { IDD_NETID_KIND, L"Connecting to the Network", L"What kind of network do you use?" },
        { IDD_NETID_INFO, L"Network Information", L"Gather domain and account information before you proceed." },
        { IDD_NETID_ACCOUNT, L"User Account and Domain Information", L"A user account gives you access to files and resources on a network." },
        { IDD_NETID_COMPDOM, L"Computer Domain", L"Your computer must also belong to a domain." },
        { IDD_NETID_WORKGROUP, L"Workgroup", L"A workgroup is a group of computers that share files and resources." },
        { IDD_NETID_DONE, NULL, NULL },
    };
    PROPSHEETPAGEW psp[ARRAY_SIZE(pages)];
    PROPSHEETHEADERW psh;
    HBITMAP watermark, header;
    LOGFONTW lf;
    unsigned int i;

    memset(&W, 0, sizeof(W));
    read_membership();
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -MulDiv(12, 96, 72);
    lf.lfWeight = FW_BOLD;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    W.title_font = CreateFontIndirectW(&lf);
    watermark = make_picture(164, 314, FALSE);
    header = make_picture(49, 49, TRUE);

    memset(psp, 0, sizeof(psp));
    for (i = 0; i < ARRAY_SIZE(pages); i++)
    {
        psp[i].dwSize = sizeof(PROPSHEETPAGEW);
        psp[i].dwFlags = pages[i].title ? PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE : PSP_HIDEHEADER;
        psp[i].hInstance = g_hInstance;
        psp[i].pszTemplate = MAKEINTRESOURCEW(pages[i].id);
        psp[i].pfnDlgProc = page_proc;
        psp[i].lParam = pages[i].id;
        psp[i].pszHeaderTitle = pages[i].title;
        psp[i].pszHeaderSubTitle = pages[i].subtitle;
    }
    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_WIZARD97 | PSH_WATERMARK | PSH_HEADER | PSH_USEHBMWATERMARK | PSH_USEHBMHEADER;
    psh.hwndParent = parent;
    psh.hInstance = g_hInstance;
    psh.pszCaption = CAPTION;
    psh.nPages = ARRAY_SIZE(pages);
    psh.ppsp = psp;
    psh.hbmWatermark = watermark;
    psh.hbmHeader = header;
    PropertySheetW(&psh);

    DeleteObject(W.title_font);
    DeleteObject(watermark);
    DeleteObject(header);
}
