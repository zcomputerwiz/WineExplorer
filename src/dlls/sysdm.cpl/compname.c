/*
 * WineExplorer Desktop Environment - Computer Name Page (sysdm.cpl)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * The computer name and description are the host's static and "pretty" hostnames (systemd hostname1);
 * changing them goes through polkit like on any Linux desktop. The membership (domain or workgroup) is the join of
 * docs/DOMAIN_JOIN.md: the Change dialog asks netapi32 (NetValidateName, NetJoinDomain, NetUnjoinDomain), which reach
 * the join engine through its socket; a process that is not an elevated administrator's is offered to be opened again
 * as one. The dialogs, texts and flow are those of Windows XP's Computer Name tab (the XP VM is the reference); no
 * restart is asked for after a join, because it takes effect at once.
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
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <wexhost.h>

#include "sysdm.h"

/* Wine's lmjoin.h has only NetGetJoinInformation; these are netapi32's (wexjoin.dll answers them) */
NET_API_STATUS NET_API_FUNCTION NetJoinDomain( LPCWSTR server, LPCWSTR domain, LPCWSTR ou, LPCWSTR account, LPCWSTR password, DWORD flags );
NET_API_STATUS NET_API_FUNCTION NetUnjoinDomain( LPCWSTR server, LPCWSTR account, LPCWSTR password, DWORD flags );
NET_API_STATUS NET_API_FUNCTION NetValidateName( LPCWSTR server, LPCWSTR name, LPCWSTR account, LPCWSTR password, DWORD type );

#define NETSETUP_JOIN_DOMAIN 0x1
#define NETSETUP_ACCT_CREATE 0x2
#define NETSETUP_ACCT_DELETE 0x4
#define NAME_TYPE_DOMAIN     3
#define TITLE                L"Computer Name Changes"

static WEX_HOSTNAME g_host;
static BOOL g_have_host;
WCHAR g_member[256];              /* the domain or workgroup the computer is in */
BOOL g_joined;                    /* a domain */
BOOL g_reopen_elevated;           /* the Change dialog or the wizard asked to open System Properties again as an administrator */

/* ---------------------------------------------------------------------------------------------------- membership */

void read_membership(void)
{
    WCHAR *name = NULL;
    NETSETUP_JOIN_STATUS type = NetSetupUnknownStatus;

    lstrcpyW(g_member, L"WORKGROUP");
    g_joined = FALSE;
    if (!NetGetJoinInformation(NULL, &name, &type) && name)
    {
        if (type == NetSetupDomainName || type == NetSetupWorkgroupName) lstrcpynW(g_member, name, ARRAY_SIZE(g_member));
        if (type == NetSetupWorkgroupName) CharUpperW(g_member);   /* as Windows shows a workgroup */
        g_joined = type == NetSetupDomainName;
        NetApiBufferFree(name);
    }
}

typedef DWORD (WINAPI *message_fn)(WCHAR *, DWORD);

/* what the engine said about the last call: Samba's reason, or its own */
void engine_message(WCHAR *text, DWORD chars)
{
    HMODULE module = LoadLibraryW(L"wexjoin.dll");
    message_fn message = module ? (message_fn)GetProcAddress(module, "WexJoinLastMessage") : NULL;

    text[0] = 0;
    if (message) message(text, chars);
}

static void show_member(HWND hwnd)
{
    SetDlgItemTextW(hwnd, IDC_COMP_MEMBER_LABEL, g_joined ? L"Domain:" : L"Workgroup:");
    SetDlgItemTextW(hwnd, IDC_COMP_WORKGROUP, g_member);
}

static void show_name(HWND hwnd)
{
    WCHAR name[64];
    const char *src = g_host.static_name[0] ? g_host.static_name : g_host.transient_name;

    MultiByteToWideChar(CP_UTF8, 0, src, -1, name, ARRAY_SIZE(name));
    SetDlgItemTextW(hwnd, IDC_COMP_NAME_LABEL, name);
}

/* ---------------------------------------------------------------------------------------------------- credentials */

struct credentials
{
    const WCHAR *text;
    WCHAR user[128], password[128], domain[256];
};

static INT_PTR CALLBACK credentials_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct credentials *c = (struct credentials *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
        c = (struct credentials *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)c);
        SetDlgItemTextW(hwnd, IDC_CRED_TEXT, c->text);
        SetDlgItemTextW(hwnd, IDC_CRED_DOMAIN, c->domain);
        SendDlgItemMessageW(hwnd, IDC_CRED_USER, EM_LIMITTEXT, ARRAY_SIZE(c->user) - 1, 0);
        SendDlgItemMessageW(hwnd, IDC_CRED_PASS, EM_LIMITTEXT, ARRAY_SIZE(c->password) - 1, 0);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK)
        {
            GetDlgItemTextW(hwnd, IDC_CRED_USER, c->user, ARRAY_SIZE(c->user));
            GetDlgItemTextW(hwnd, IDC_CRED_PASS, c->password, ARRAY_SIZE(c->password));
            GetDlgItemTextW(hwnd, IDC_CRED_DOMAIN, c->domain, ARRAY_SIZE(c->domain));
            if (!c->user[0])
            {
                MessageBoxW(hwnd, L"Type the name of the user account.", TITLE, MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

/* the account as the API takes it: DOMAIN\user, or a user principal name, or the user name with the domain of the box */
static void account_name(const struct credentials *c, WCHAR *account, DWORD chars)
{
    if (wcschr(c->user, '\\') || wcschr(c->user, '@') || !c->domain[0]) lstrcpynW(account, c->user, chars);
    else swprintf(account, chars, L"%ls\\%ls", c->domain, c->user);
}

/* ---------------------------------------------------------------------------------------------------- errors */

struct join_error
{
    const WCHAR *caption;
    WCHAR text[1024], details[1024];
};

static INT_PTR CALLBACK join_error_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct join_error *e = (struct join_error *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
        e = (struct join_error *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)e);
        if (e->caption) SetWindowTextW(hwnd, e->caption);
        SendDlgItemMessageW(hwnd, IDC_ERR_ICON, STM_SETICON, (WPARAM)LoadIconW(NULL, (LPCWSTR)IDI_HAND), 0);
        SetDlgItemTextW(hwnd, IDC_ERR_TEXT, e->text);
        SetDlgItemTextW(hwnd, IDC_ERR_DETAILS, e->details);
        if (!e->details[0]) EnableWindow(GetDlgItem(hwnd, IDC_ERR_DETAILS_BTN), FALSE);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) { EndDialog(hwnd, IDOK); return TRUE; }
        if (LOWORD(wparam) == IDC_ERR_DETAILS_BTN)
        {
            HWND box = GetDlgItem(hwnd, IDC_ERR_DETAILS);
            BOOL open = !IsWindowVisible(box);
            RECT rect = { 0, 0, 0, 104 }, window;

            MapDialogRect(hwnd, &rect);
            GetWindowRect(hwnd, &window);
            ShowWindow(box, open ? SW_SHOW : SW_HIDE);
            SetWindowPos(hwnd, NULL, 0, 0, window.right - window.left,
                         (window.bottom - window.top) + (open ? rect.bottom : -rect.bottom), SWP_NOMOVE | SWP_NOZORDER);
            SetDlgItemTextW(hwnd, IDC_ERR_DETAILS_BTN, open ? L"<< Details" : L"Details >>");
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* The failure of a join or a validation: what Windows says when the controller is not found, else the error, with the
 * engine's reason under Details. */
void show_join_error(HWND owner, const WCHAR *caption, const WCHAR *domain, DWORD status, BOOL leaving)
{
    struct join_error e;
    WCHAR system[400] = { 0 }, engine[512];

    e.caption = caption;
    engine_message(engine, ARRAY_SIZE(engine));
    if (status == ERROR_NO_SUCH_DOMAIN || status == ERROR_NO_LOGON_SERVERS)
        swprintf(e.text, ARRAY_SIZE(e.text), L"A domain controller for the domain %ls could not be contacted.\r\n\r\n"
                 L"Ensure that the domain name is typed correctly.\r\n\r\n"
                 L"If the name is correct, click Details for troubleshooting information.", domain);
    else
    {
        WCHAR *end;

        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, status, 0, system, ARRAY_SIZE(system), NULL);
        for (end = system + wcslen(system); end > system && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '); end--) end[-1] = 0;
        if (!system[0]) swprintf(system, ARRAY_SIZE(system), L"Error %lu.", status);
        swprintf(e.text, ARRAY_SIZE(e.text), L"The following error occurred attempting to %ls the domain \"%ls\":\r\n\r\n%ls",
                 leaving ? L"leave" : L"join", domain, system);
    }
    swprintf(e.details, ARRAY_SIZE(e.details), L"Note: This information is intended for a network administrator. If you are "
             L"not your network's administrator, notify the administrator that you received this information.%ls%ls",
             engine[0] ? L"\r\n\r\n" : L"", engine);
    if (!engine[0]) e.details[0] = 0;
    DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_JOIN_ERROR), owner, join_error_proc, (LPARAM)&e);
}

/* a call that only an administrator's elevated process may make came back with access denied: offer to start again */
void offer_elevation(HWND owner, const WCHAR *caption)
{
    WCHAR program[MAX_PATH];
    UINT n;

    if (MessageBoxW(owner, L"Changing the domain membership needs the rights of an administrator.\n\n"
                           L"Open System Properties again as an administrator?", caption, MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    n = GetSystemDirectoryW(program, ARRAY_SIZE(program) - 16);
    if (!n) return;
    wcscat(program, L"\\rundll32.exe");
    if ((INT_PTR)ShellExecuteW(owner, L"runas", program, L"shell32.dll,Control_RunDLL sysdm.cpl,,1", NULL, SW_SHOWNORMAL) > 32)
        g_reopen_elevated = TRUE;
}

/* ---------------------------------------------------------------------------------------------------- join and leave */

/* TRUE when the computer is in the domain now */
static BOOL do_join(HWND dlg, const WCHAR *domain)
{
    struct credentials c = { L"Enter the name and password of an account with permission to join the domain." };
    WCHAR account[300], text[400];
    DWORD status;

    /* (the check needs the same rights as the join: a standard user hears it before being asked for a password) */
    status = NetValidateName(NULL, domain, NULL, NULL, NAME_TYPE_DOMAIN);
    if (status == ERROR_ACCESS_DENIED) { offer_elevation(dlg, TITLE); return FALSE; }
    if (status) { show_join_error(dlg, TITLE, domain, status, FALSE); return FALSE; }
    lstrcpynW(c.domain, domain, ARRAY_SIZE(c.domain));
    if (DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_JOIN_CREDS), dlg, credentials_proc, (LPARAM)&c) != IDOK) return FALSE;
    account_name(&c, account, ARRAY_SIZE(account));
    status = NetJoinDomain(NULL, domain, NULL, account, c.password, NETSETUP_JOIN_DOMAIN | NETSETUP_ACCT_CREATE);
    SecureZeroMemory(c.password, sizeof(c.password));
    if (status == ERROR_ACCESS_DENIED) { offer_elevation(dlg, TITLE); return FALSE; }
    if (status) { show_join_error(dlg, TITLE, domain, status, FALSE); return FALSE; }
    read_membership();
    swprintf(text, ARRAY_SIZE(text), L"Welcome to the %ls domain.", domain);
    MessageBoxW(dlg, text, TITLE, MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

/* TRUE when the computer is out of the domain now */
BOOL do_leave(HWND dlg)
{
    struct credentials c = { L"Enter the name and password of an account with permission to remove this computer from the domain." };
    WCHAR account[300], text[400];
    DWORD status;

    lstrcpynW(c.domain, g_member, ARRAY_SIZE(c.domain));
    if (DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_JOIN_CREDS), dlg, credentials_proc, (LPARAM)&c) != IDOK) return FALSE;
    account_name(&c, account, ARRAY_SIZE(account));
    status = NetUnjoinDomain(NULL, account, c.password, NETSETUP_ACCT_DELETE);
    SecureZeroMemory(c.password, sizeof(c.password));
    if (status == ERROR_ACCESS_DENIED) { offer_elevation(dlg, TITLE); return FALSE; }
    if (status) { show_join_error(dlg, TITLE, g_member, status, TRUE); return FALSE; }
    read_membership();
    swprintf(text, ARRAY_SIZE(text), L"Welcome to the %ls workgroup.", g_member);
    MessageBoxW(dlg, text, TITLE, MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- More... */

static INT_PTR CALLBACK more_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = ARRAY_SIZE(name);

        /* shown, not changed: the primary DNS suffix is the host's (its domain name), and the NetBIOS name is this name */
        if (GetComputerNameW(name, &size)) SetDlgItemTextW(hwnd, IDC_MORE_NETBIOS, name);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDCANCEL || LOWORD(wparam) == IDOK) { EndDialog(hwnd, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- Change... */

static void change_state(HWND hwnd)
{
    WCHAR name[64], domain[256], full[400];
    char utf8[128];
    BOOL domain_on = IsDlgButtonChecked(hwnd, IDC_COMPCHG_DOMAIN_RB) == BST_CHECKED, changed;

    GetDlgItemTextW(hwnd, IDC_COMPCHG_NAME, name, ARRAY_SIZE(name));
    GetDlgItemTextW(hwnd, IDC_COMPCHG_DOMAIN, domain, ARRAY_SIZE(domain));
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), NULL, NULL);
    swprintf(full, ARRAY_SIZE(full), L"%ls.%ls", name, g_joined ? g_member : L"");
    SetDlgItemTextW(hwnd, IDC_COMPCHG_FULL, full);
    EnableWindow(GetDlgItem(hwnd, IDC_COMPCHG_DOMAIN), domain_on);
    changed = (name[0] && strcmp(utf8, g_host.static_name)) || (domain_on && domain[0] && (!g_joined || _wcsicmp(domain, g_member))) ||
              (!domain_on && g_joined);
    EnableWindow(GetDlgItem(hwnd, IDOK), changed);
}

static INT_PTR CALLBACK change_name_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        WCHAR name[64];

        MultiByteToWideChar(CP_UTF8, 0, g_host.static_name, -1, name, ARRAY_SIZE(name));
        SetDlgItemTextW(hwnd, IDC_COMPCHG_NAME, name);
        SendDlgItemMessageW(hwnd, IDC_COMPCHG_NAME, EM_LIMITTEXT, 63, 0);
        SetDlgItemTextW(hwnd, IDC_COMPCHG_DOMAIN, g_joined ? g_member : L"");
        SetDlgItemTextW(hwnd, IDC_COMPCHG_WG, g_joined ? L"WORKGROUP" : g_member);
        /* the workgroup's name is not changed here (leaving a domain goes to WORKGROUP) */
        EnableWindow(GetDlgItem(hwnd, IDC_COMPCHG_WG), FALSE);
        CheckRadioButton(hwnd, IDC_COMPCHG_DOMAIN_RB, IDC_COMPCHG_WG_RB, g_joined ? IDC_COMPCHG_DOMAIN_RB : IDC_COMPCHG_WG_RB);
        change_state(hwnd);
        EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
        return TRUE;
    }

    case WM_COMMAND:
        if ((LOWORD(wparam) == IDC_COMPCHG_NAME || LOWORD(wparam) == IDC_COMPCHG_DOMAIN) && HIWORD(wparam) == EN_CHANGE)
        {
            change_state(hwnd);
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_COMPCHG_DOMAIN_RB || LOWORD(wparam) == IDC_COMPCHG_WG_RB)
        {
            change_state(hwnd);
            if (LOWORD(wparam) == IDC_COMPCHG_DOMAIN_RB) SetFocus(GetDlgItem(hwnd, IDC_COMPCHG_DOMAIN));
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_COMPCHG_MORE)
        {
            DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_COMPNAME_MORE), hwnd, more_proc, 0);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK)
        {
            WCHAR wname[64], domain[256];
            char name[128];
            BOOL domain_on = IsDlgButtonChecked(hwnd, IDC_COMPCHG_DOMAIN_RB) == BST_CHECKED, renamed = FALSE, done = TRUE;

            GetDlgItemTextW(hwnd, IDC_COMPCHG_NAME, wname, ARRAY_SIZE(wname));
            GetDlgItemTextW(hwnd, IDC_COMPCHG_DOMAIN, domain, ARRAY_SIZE(domain));
            if (!wname[0]) return TRUE;
            WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, sizeof(name), NULL, NULL);
            if (strcmp(name, g_host.static_name) && g_joined && domain_on)
            {
                /* the machine account is named for the computer: a rename that keeps it in the domain would break the
                 * trust (the engine cannot rename the account yet: docs/DOMAIN_JOIN.md); leaving the domain first is allowed */
                MessageBoxW(hwnd, L"The name of a computer in a domain cannot be changed here: the computer's account in "
                                  L"the domain would no longer match. Remove the computer from the domain first.",
                            TITLE, MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            if (!domain_on && g_joined)
            {
                /* leaving first: the account goes with the old name */
                done = do_leave(hwnd);
                if (!done)
                {
                    if (g_reopen_elevated) EndDialog(hwnd, IDCANCEL);
                    return TRUE;
                }
            }
            if (strcmp(name, g_host.static_name))
            {
                DWORD err = WexHostnameSet(hwnd, name, FALSE);

                if (err)
                {
                    if (err != ERROR_CANCELLED)
                        MessageBoxW(hwnd, err == ERROR_INVALID_PARAMETER ? L"The computer name is not valid." :
                                    L"You do not have permission to rename this computer.", TITLE, MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                lstrcpynA(g_host.static_name, name, sizeof(g_host.static_name));
                renamed = TRUE;
            }
            if (domain_on && domain[0] && (!g_joined || _wcsicmp(domain, g_member))) done = do_join(hwnd, domain);
            if (renamed || done)
            {
                if (g_reopen_elevated) { EndDialog(hwnd, IDCANCEL); return TRUE; }
                if (done) EndDialog(hwnd, IDOK);
                else change_state(hwnd);
            }
            else if (g_reopen_elevated) EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL)
        {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- the page */

INT_PTR CALLBACK compname_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        WCHAR comp_name[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = ARRAY_SIZE(comp_name);

        if (GetComputerNameW(comp_name, &size))
            SetDlgItemTextW(hwnd, IDC_COMP_NAME_LABEL, comp_name);
        read_membership();
        show_member(hwnd);

        g_have_host = !WexHostnameGet(&g_host);
        if (g_have_host)
        {
            WCHAR desc[128];
            MultiByteToWideChar(CP_UTF8, 0, g_host.pretty_name, -1, desc, ARRAY_SIZE(desc));
            SetDlgItemTextW(hwnd, IDC_COMP_DESC, desc);
            show_name(hwnd);
        }
        else
        {
            EnableWindow(GetDlgItem(hwnd, IDC_COMP_DESC), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_COMP_CHANGE_BTN), FALSE);
        }
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_COMP_CHANGE_BTN && g_have_host)
        {
            DialogBoxParamW(GetModuleHandleW(L"sysdm.cpl"), MAKEINTRESOURCEW(IDD_COMPNAME_CHANGE), hwnd,
                            change_name_proc, 0);
            show_name(hwnd);
            read_membership();
            show_member(hwnd);
            /* the administrator's System Properties is open: this one goes */
            if (g_reopen_elevated) { g_reopen_elevated = FALSE; PostMessageW(GetParent(hwnd), WM_COMMAND, IDCANCEL, 0); }
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_COMP_NETID_BTN)
        {
            show_netid_wizard(hwnd);
            read_membership();
            show_member(hwnd);
            if (g_reopen_elevated) { g_reopen_elevated = FALSE; PostMessageW(GetParent(hwnd), WM_COMMAND, IDCANCEL, 0); }
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_COMP_DESC && HIWORD(wparam) == EN_CHANGE)
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY && g_have_host)
        {
            WCHAR desc[128];
            char pretty[128];

            GetDlgItemTextW(hwnd, IDC_COMP_DESC, desc, ARRAY_SIZE(desc));
            WideCharToMultiByte(CP_UTF8, 0, desc, -1, pretty, sizeof(pretty), NULL, NULL);
            if (strcmp(pretty, g_host.pretty_name))
            {
                DWORD err = WexHostnameSet(hwnd, pretty, TRUE);
                if (!err) lstrcpynA(g_host.pretty_name, pretty, sizeof(g_host.pretty_name));
                else if (err != ERROR_CANCELLED)
                    MessageBoxW(hwnd, L"You do not have permission to change the computer description.",
                                L"System Properties", MB_OK | MB_ICONWARNING);
            }
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
