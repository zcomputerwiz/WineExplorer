/*
 * WineExplorer User Account Control Consent Dialog (consent.exe)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <windows.h>
#include <commctrl.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>

#include "resource.h"
#ifdef __WINE__
#include "wine/debug.h"
WINE_DEFAULT_DEBUG_CHANNEL(consent);
#else
#define TRACE(...) do {} while (0)
#define WARN(...) do {} while (0)
#define ERR(...) do {} while (0)
#endif
#include "../wexelevate/cred_dialog.h"
#include "../wexelevate/cred_dialog.c"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

struct consent_data
{
    HWND hwnd_parent;
    WCHAR cmdline[32768];
    WCHAR exepath[MAX_PATH];
    WCHAR image[MAX_PATH];     /* /image: the frozen image of the elevation broker, shown and checked instead of the first word of the command line */
    WCHAR details[32768 + 2 * MAX_PATH];
    BOOL broker;               /* /broker: started by the elevation broker, which waits for the exit code */
    BOOL secure;               /* /secure: on the secure desktop, which the driver already covers and dims */
    DWORD parent_pid;          /* /ppid: the broker; this process ends when the broker does */
#if defined(WEXCONSENT_TEST_HOOK) || defined(WEX_CRED_DIALOG_TEST_HOOK)
    WCHAR testid[32];          /* /testid: the test hook */
#endif
    WCHAR progname[256];
    WCHAR publisher[256];
    WCHAR origin[128];
    BOOL is_verified;
    BOOL credentials_mode;
    BOOL details_visible;

    /* E1-S17: Bound conversation fields */
    UINT64 request_id;
    HANDLE channel_pipe;
    BYTE channel_token[16];
    BYTE nonce[32];
    BYTE binding_digest[32];
    BYTE request_digest[32];
    DWORD attempt;
    BOOL confirmed;
    char account[WEX_MAX_ACCOUNT_NAME_LEN + 1];
    WCHAR password[256];
};

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

    pts[0].x = x + 2;     pts[0].y = y;
    pts[1].x = x + w - 2; pts[1].y = y;
    pts[2].x = x + w;     pts[2].y = y + 4;
    pts[3].x = x + w;     pts[3].y = y + h * 5 / 9;
    pts[4].x = mid_x;     pts[4].y = y + h;
    pts[5].x = x;         pts[5].y = y + h * 5 / 9;
    pts[6].x = x;         pts[6].y = y + 4;

    q_tl[0].x = x + 2; q_tl[0].y = y;
    q_tl[1].x = mid_x; q_tl[1].y = y;
    q_tl[2].x = mid_x; q_tl[2].y = mid_y;
    q_tl[3].x = x;     q_tl[3].y = mid_y;
    q_tl[4].x = x;     q_tl[4].y = y + 4;
    q_tl[5].x = x + 2; q_tl[5].y = y;
    old_brush = SelectObject(hdc, br_gold);
    old_pen = SelectObject(hdc, pen_gold);
    Polygon(hdc, q_tl, 6);

    q_tr[0].x = mid_x;     q_tr[0].y = y;
    q_tr[1].x = x + w - 2; q_tr[1].y = y;
    q_tr[2].x = x + w;     q_tr[2].y = y + 4;
    q_tr[3].x = x + w;     q_tr[3].y = mid_y;
    q_tr[4].x = mid_x;     q_tr[4].y = mid_y;
    q_tr[5].x = mid_x;     q_tr[5].y = y;
    SelectObject(hdc, br_blue_light);
    Polygon(hdc, q_tr, 6);

    q_bl[0].x = x;     q_bl[0].y = mid_y;
    q_bl[1].x = mid_x; q_bl[1].y = mid_y;
    q_bl[2].x = mid_x; q_bl[2].y = y + h;
    q_bl[3].x = x;     q_bl[3].y = y + h * 5 / 9;
    q_bl[4].x = x;     q_bl[4].y = mid_y;
    SelectObject(hdc, br_blue_dark);
    Polygon(hdc, q_bl, 5);

    q_br[0].x = mid_x; q_br[0].y = mid_y;
    q_br[1].x = x + w; q_br[1].y = mid_y;
    q_br[2].x = x + w; q_br[2].y = y + h * 5 / 9;
    q_br[3].x = mid_x; q_br[3].y = y + h;
    q_br[4].x = mid_x; q_br[4].y = mid_y;
    SelectObject(hdc, br_gold);
    Polygon(hdc, q_br, 5);

    SelectObject(hdc, pen_cross);
    MoveToEx(hdc, mid_x, y + 1, NULL);
    LineTo(hdc, mid_x, y + h - 1);
    MoveToEx(hdc, x + 1, mid_y, NULL);
    LineTo(hdc, x + w - 1, mid_y);

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

static void draw_banner(const DRAWITEMSTRUCT *dis, BOOL is_verified)
{
    HDC hdc = dis->hDC;
    COLORREF bg_color = is_verified ? RGB(12, 80, 140) : RGB(195, 130, 10);
    COLORREF line_color = is_verified ? RGB(8, 55, 95) : RGB(160, 100, 5);
    HBRUSH bg = CreateSolidBrush(bg_color);
    HPEN pen_line = CreatePen(PS_SOLID, 1, line_color);
    HFONT font, old_font;
    LOGFONTW lf;
    RECT text = dis->rcItem;
    HGDIOBJ old_pen;
    const WCHAR *msg;

    FillRect(hdc, &dis->rcItem, bg);
    DeleteObject(bg);

    old_pen = SelectObject(hdc, pen_line);
    MoveToEx(hdc, dis->rcItem.left, dis->rcItem.bottom - 1, NULL);
    LineTo(hdc, dis->rcItem.right, dis->rcItem.bottom - 1);
    SelectObject(hdc, old_pen);
    DeleteObject(pen_line);

    draw_shield(hdc, 14, 5, 24, 26);

    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -13;
    lf.lfWeight = FW_SEMIBOLD;
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    font = CreateFontIndirectW(&lf);
    old_font = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    text.left = 46;
    text.right -= 10;

    msg = is_verified ?
          L"A program needs your permission to continue" :
          L"An unidentified program wants access to your computer";

    DrawTextW(hdc, msg, -1, &text, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
    SelectObject(hdc, old_font);
    DeleteObject(font);
}

static void parse_command_line(const WCHAR *cmd, struct consent_data *data)
{
    const WCHAR *p = cmd;
    WCHAR *dst;

    memset(data, 0, sizeof(*data));
    data->channel_pipe = INVALID_HANDLE_VALUE;
    lstrcpyW(data->origin, L"Hard drive on this computer");

    while (*p)
    {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (!wcsnicmp(p, L"/hwnd", 5) || !wcsnicmp(p, L"-hwnd", 5))
        {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            data->hwnd_parent = (HWND)(ULONG_PTR)wcstoul(p, (WCHAR **)&p, 16);
        }
        else if (!wcsnicmp(p, L"/broker", 7))
        {
            p += 7;
            data->broker = TRUE;
        }
        else if (!wcsnicmp(p, L"/secure", 7))
        {
            p += 7;
            data->secure = TRUE;
        }
        else if (!wcsnicmp(p, L"/credentials", 12))
        {
            p += 12;
            data->credentials_mode = TRUE;
        }
        else if (!wcsnicmp(p, L"/ppid", 5))
        {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            data->parent_pid = wcstoul(p, (WCHAR **)&p, 10);
        }
        else if (!wcsnicmp(p, L"/request", 8))
        {
            p += 8;
            while (*p == ' ' || *p == '\t') p++;
            data->request_id = _wcstoui64(p, (WCHAR **)&p, 16);
        }
        else if (!wcsnicmp(p, L"/token", 6))
        {
            int i;
            p += 6;
            while (*p == ' ' || *p == '\t') p++;
            for (i = 0; i < 16 && *p; i++)
            {
                WCHAR hex[3] = { p[0], p[1], 0 };
                if (!p[0] || !p[1]) break;
                data->channel_token[i] = (BYTE)wcstoul(hex, NULL, 16);
                p += 2;
            }
        }
        else if (!wcsnicmp(p, L"/channel", 8))
        {
            p += 8;
            while (*p == ' ' || *p == '\t') p++;
            data->channel_pipe = (HANDLE)(ULONG_PTR)wcstoul(p, (WCHAR **)&p, 0);
        }
        else if (!wcsnicmp(p, L"/pipe", 5))
        {
            WCHAR pipename[MAX_PATH];
            int i = 0;
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            while (*p && *p != ' ' && i < MAX_PATH - 1) pipename[i++] = *p++;
            pipename[i] = 0;
            data->channel_pipe = CreateFileW(pipename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        }
#if defined(WEXCONSENT_TEST_HOOK) || defined(WEX_CRED_DIALOG_TEST_HOOK)
        else if (!wcsnicmp(p, L"/testid", 7))
        {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            for (dst = data->testid; *p && *p != ' ' && dst < data->testid + ARRAY_SIZE(data->testid) - 1;) *dst++ = *p++;
            *dst = 0;
        }
#endif
        else if (!wcsnicmp(p, L"/image", 6))
        {
            p += 6;
            while (*p == ' ' || *p == '\t') p++;
            dst = data->image;
            if (*p == '"')
            {
                p++;
                for (; *p && *p != '"' && dst < data->image + MAX_PATH - 1;) *dst++ = *p++;
                if (*p == '"') p++;
            }
            *dst = 0;
        }
        else if (!wcsnicmp(p, L"/cmd", 4) || !wcsnicmp(p, L"-cmd", 4))
        {
            p += 4;
            while (*p == ' ' || *p == '\t') p++;
            lstrcpynW(data->cmdline, p, ARRAY_SIZE(data->cmdline));
            break;
        }
        else
        {
            p++;
        }
    }

    if (!data->cmdline[0])
        lstrcpyW(data->cmdline, L"setup.exe");

    /* Extract executable path from cmdline */
    p = data->cmdline;
    while (*p == ' ' || *p == '\t') p++;
    dst = data->exepath;

    if (data->broker && data->image[0])
    {
        /* the broker says what it will run; the first word of the command line is only text */
        lstrcpynW(data->exepath, data->image, MAX_PATH);
        dst = data->exepath + lstrlenW(data->exepath);
    }
    else if (*p == '"')
    {
        p++;
        while (*p && *p != '"' && (dst - data->exepath) < (MAX_PATH - 1))
            *dst++ = *p++;
    }
    else
    {
        while (*p && *p != ' ' && *p != '\t' && (dst - data->exepath) < (MAX_PATH - 1))
            *dst++ = *p++;
    }
    *dst = 0;

    /* Extract friendly program name */
    {
        DWORD ver_size, handle = 0;
        BOOL got_name = FALSE;

        ver_size = GetFileVersionInfoSizeW(data->exepath, &handle);
        if (ver_size)
        {
            void *ver_data = malloc(ver_size);
            if (ver_data && GetFileVersionInfoW(data->exepath, 0, ver_size, ver_data))
            {
                WCHAR *desc = NULL;
                UINT desc_len = 0;

                if (VerQueryValueW(ver_data, L"\\StringFileInfo\\040904b0\\FileDescription", (void **)&desc, &desc_len) && desc_len > 1)
                {
                    lstrcpynW(data->progname, desc, ARRAY_SIZE(data->progname));
                    got_name = TRUE;
                }
                else if (VerQueryValueW(ver_data, L"\\StringFileInfo\\040904e4\\FileDescription", (void **)&desc, &desc_len) && desc_len > 1)
                {
                    lstrcpynW(data->progname, desc, ARRAY_SIZE(data->progname));
                    got_name = TRUE;
                }
            }
            free(ver_data);
        }

        if (!got_name)
        {
            const WCHAR *base = wcsrchr(data->exepath, '\\');
            lstrcpynW(data->progname, base ? base + 1 : data->exepath, ARRAY_SIZE(data->progname));
        }
    }

    /* Verify Authenticode signature */
    {
        WINTRUST_FILE_INFO file_info = { sizeof(file_info), data->exepath };
        WINTRUST_DATA trust_data = { sizeof(trust_data) };
        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        LONG status;

        trust_data.dwUIChoice = WTD_UI_NONE;
        trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
        trust_data.dwUnionChoice = WTD_CHOICE_FILE;
        trust_data.pFile = &file_info;
        trust_data.dwStateAction = WTD_STATEACTION_VERIFY;

        status = WinVerifyTrust(NULL, &action, &trust_data);
        trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(NULL, &action, &trust_data);

        if (status == ERROR_SUCCESS)
        {
            data->is_verified = TRUE;
            lstrcpyW(data->publisher, L"Verified publisher");
        }
        else
        {
            data->is_verified = FALSE;
            lstrcpyW(data->publisher, L"Unknown publisher");
        }
    }
}

static BOOL is_credentials_mode_required(void)
{
    TOKEN_ELEVATION_TYPE type;
    DWORD size, mode = 2, value_type;
    LSTATUS status;
    HKEY hkey;

    if (!GetTokenInformation(GetCurrentThreadEffectiveToken(), TokenElevationType, &type, sizeof(type), &size)
            || type == TokenElevationTypeDefault)
        return TRUE; /* Unknown identity must not fall back to consent-only. */

    status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                          0, KEY_READ, &hkey);
    if (status == ERROR_SUCCESS)
    {
        DWORD val_size = sizeof(mode);
        status = RegQueryValueExW(hkey, L"ConsentPromptBehaviorAdmin", NULL, &value_type, (BYTE *)&mode, &val_size);
        RegCloseKey(hkey);
        if (status != ERROR_FILE_NOT_FOUND && (status != ERROR_SUCCESS
                || value_type != REG_DWORD || val_size != sizeof(mode) || mode > 5))
            return TRUE;
    }
    else if (status != ERROR_FILE_NOT_FOUND) return TRUE;
    return (mode == 1 || mode == 3);
}

#define HOOK_DIR L"Z:\\tmp\\wexelev\\"

#ifdef WEXCONSENT_TEST_HOOK
#include <sddl.h>

static void test_hook_path(const struct consent_data *data, const WCHAR *suffix, WCHAR *path)
{
    lstrcpyW(path, HOOK_DIR L"consent.");
    lstrcatW(path, data->testid);
    lstrcatW(path, suffix);
}

static void test_hook_shown(const struct consent_data *data)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD done;
    char *utf8;
    int n;

    test_hook_path(data, L".shown", path);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    n = WideCharToMultiByte(CP_UTF8, 0, data->details, -1, NULL, 0, NULL, NULL);
    if ((utf8 = malloc(n)))
    {
        WideCharToMultiByte(CP_UTF8, 0, data->details, -1, utf8, n, NULL, NULL);
        WriteFile(file, utf8, n - 1, &done, NULL);
        WriteFile(file, "\nprogram=", 9, &done, NULL);
        free(utf8);
        n = WideCharToMultiByte(CP_UTF8, 0, data->progname, -1, NULL, 0, NULL, NULL);
        if ((utf8 = malloc(n)))
        {
            WideCharToMultiByte(CP_UTF8, 0, data->progname, -1, utf8, n, NULL, NULL);
            WriteFile(file, utf8, n - 1, &done, NULL);
            free(utf8);
        }
        WriteFile(file, "\npublisher=", 11, &done, NULL);
        n = WideCharToMultiByte(CP_UTF8, 0, data->publisher, -1, NULL, 0, NULL, NULL);
        if ((utf8 = malloc(n)))
        {
            WideCharToMultiByte(CP_UTF8, 0, data->publisher, -1, utf8, n, NULL, NULL);
            WriteFile(file, utf8, n - 1, &done, NULL);
            free(utf8);
        }
        WriteFile(file, "\n", 1, &done, NULL);
    }
    {
        /* the token of this process: the broker's child must be SYSTEM, not the caller */
        BYTE buf[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
        HANDLE token;
        DWORD len;
        WCHAR *sid;
        char line[128];

        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            if (GetTokenInformation(token, TokenUser, buf, sizeof(buf), &len) && ConvertSidToStringSidW(((TOKEN_USER *)buf)->User.Sid, &sid))
            {
                n = snprintf(line, sizeof(line), "user=%ls\n", sid);
                WriteFile(file, line, n, &done, NULL);
                LocalFree(sid);
            }
            CloseHandle(token);
        }
    }
    {
        /* the desktop this window is on: the harness checks that the prompt is on the secure desktop (or not) */
        WCHAR name[64] = L"?";
        char line[128];

        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, name, sizeof(name), NULL);
        n = snprintf(line, sizeof(line), "desktop=%ls\n", name);
        WriteFile(file, line, n, &done, NULL);
    }
    CloseHandle(file);
}

static int test_hook_answer(const struct consent_data *data)
{
    WCHAR path[MAX_PATH];

    test_hook_path(data, L".approve", path);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return 1;
    test_hook_path(data, L".cancel", path);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return 2;
    return 0;
}
#endif

#ifdef WEX_CRED_DIALOG_TEST_HOOK
static void test_cred_hook_path(const struct consent_data *data, const WCHAR *suffix, WCHAR *path)
{
    lstrcpyW(path, HOOK_DIR L"cred.");
    lstrcatW(path, data->testid);
    lstrcatW(path, suffix);
}

static void test_cred_hook_shown(const struct consent_data *data)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD done;
    char line[256];
    int n;

    test_cred_hook_path(data, L".shown", path);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    WriteFile(file, "shown=1\n", 8, &done, NULL);
    WriteFile(file, "explanation=The administrator account's rights apply inside this Wine prefix only, with no host or root access. The requesting user's host account and files are unchanged.\n", 170, &done, NULL);
    n = snprintf(line, sizeof(line), "request_id=%08lx%08lx\n", (DWORD)(data->request_id >> 32), (DWORD)data->request_id);
    WriteFile(file, line, n, &done, NULL);
    CloseHandle(file);
}

static void test_cred_hook_record_invalid(const struct consent_data *data)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD done;

    test_cred_hook_path(data, L".invalid", path);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteFile(file, "invalid=1\n", 10, &done, NULL);
        CloseHandle(file);
    }
}

static void test_cred_hook_action(struct consent_data *data, HWND hwnd)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    char buf[128];
    DWORD read_len = 0;

    test_cred_hook_path(data, L".action", path);
    file = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    if (ReadFile(file, buf, sizeof(buf) - 1, &read_len, NULL) && read_len > 0)
    {
        buf[read_len] = '\0';
        CloseHandle(file);
        DeleteFileW(path);

        if (!strncmp(buf, "confirm:", 8))
        {
            char *acc = buf + 8;
            char *pw = strchr(acc, ':');
            if (pw)
            {
                *pw++ = '\0';
                SetDlgItemTextA(hwnd, IDC_UAC_ACCOUNT, acc);
                SetDlgItemTextA(hwnd, IDC_UAC_PASSWORD, pw);
                PostMessageW(hwnd, WM_COMMAND, IDYES, 0);
            }
        }
        else if (!strncmp(buf, "invalid:", 8))
        {
            char *acc = buf + 8;
            SetDlgItemTextA(hwnd, IDC_UAC_ACCOUNT, acc);
            PostMessageW(hwnd, WM_COMMAND, IDYES, 0);
        }
        else if (!strncmp(buf, "cancel", 6))
        {
            PostMessageW(hwnd, WM_COMMAND, IDNO, 0);
        }
    }
    else
    {
        CloseHandle(file);
    }
}
#endif

/* Click consent dialog proc */
static INT_PTR CALLBACK consent_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct consent_data *data = (struct consent_data *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HFONT font_bold;
        LOGFONTW lf;

        data = (struct consent_data *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)data);

        SetDlgItemTextW(hwnd, IDC_UAC_PROGRAM, data->progname);
        SetDlgItemTextW(hwnd, IDC_UAC_PUBLISHER, data->publisher);
        SetDlgItemTextW(hwnd, IDC_UAC_ORIGIN, data->origin);
        if (data->broker)
        {
            /* everything the broker will run, in full, shown from the start */
            lstrcpyW(data->details, L"Program location: ");
            lstrcatW(data->details, data->exepath);
            lstrcatW(data->details, L"\r\nCommand line: ");
            lstrcatW(data->details, data->cmdline);
            SetDlgItemTextW(hwnd, IDC_UAC_DETAILS_TEXT, data->details);
            SetDlgItemTextW(hwnd, IDC_UAC_ORIGIN, data->exepath);
            data->details_visible = TRUE;
            ShowWindow(GetDlgItem(hwnd, IDC_UAC_DETAILS_TEXT), SW_SHOW);
            SetDlgItemTextW(hwnd, IDC_UAC_DETAILS_BTN, L"&Hide");
        }
        else SetDlgItemTextW(hwnd, IDC_UAC_DETAILS_TEXT, data->cmdline);
#ifdef WEXCONSENT_TEST_HOOK
        test_hook_shown(data);
        SetTimer(hwnd, 1, 50, NULL);
#endif

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -12;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        if ((font_bold = CreateFontIndirectW(&lf)))
            SendDlgItemMessageW(hwnd, IDC_UAC_PROGRAM, WM_SETFONT, (WPARAM)font_bold, TRUE);

        SetFocus(GetDlgItem(hwnd, IDNO));
        return FALSE;
    }

#ifdef WEXCONSENT_TEST_HOOK
    case WM_TIMER:
    {
        int answer = test_hook_answer(data);
        if (answer) PostMessageW(hwnd, WM_COMMAND, answer == 1 ? IDYES : IDNO, 0);
        return TRUE;
    }
#endif

    case WM_DRAWITEM:
        if (wparam == IDC_UAC_BANNER)
        {
            draw_banner((const DRAWITEMSTRUCT *)lparam, data->is_verified);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_UAC_DETAILS_BTN:
            data->details_visible = !data->details_visible;
            ShowWindow(GetDlgItem(hwnd, IDC_UAC_DETAILS_TEXT), data->details_visible ? SW_SHOW : SW_HIDE);
            SetDlgItemTextW(hwnd, IDC_UAC_DETAILS_BTN, data->details_visible ? L"&Hide" : L"&Details");
            return TRUE;

        case IDYES:
            EndDialog(hwnd, IDYES);
            return TRUE;

        case IDNO:
        case IDCANCEL:
            EndDialog(hwnd, IDNO);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hwnd, IDNO);
        return TRUE;
    }
    return FALSE;
}

/* Credential consent dialog proc */
static INT_PTR CALLBACK credentials_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct consent_data *data = (struct consent_data *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HFONT font_bold;
        LOGFONTW lf;

        data = (struct consent_data *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)data);

        SetDlgItemTextW(hwnd, IDC_UAC_PROGRAM, data->progname);
        SetDlgItemTextW(hwnd, IDC_UAC_PUBLISHER, data->publisher);
        SetDlgItemTextW(hwnd, IDC_UAC_EXPLANATION,
            L"The administrator account's rights apply inside this Wine prefix only, "
            L"with no host or root access. The requesting user's host account and files are unchanged.");

        if (!data->secure)
            SetDlgItemTextW(hwnd, IDC_UAC_DESKTOP_WARN, L"Warning: Prompt running on ordinary desktop (insecure desktop policy).");
        else
            SetDlgItemTextW(hwnd, IDC_UAC_DESKTOP_WARN, L"");

        SetDlgItemTextW(hwnd, IDC_UAC_ERROR, L"");

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -12;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
        if ((font_bold = CreateFontIndirectW(&lf)))
            SendDlgItemMessageW(hwnd, IDC_UAC_PROGRAM, WM_SETFONT, (WPARAM)font_bold, TRUE);

#ifdef WEX_CRED_DIALOG_TEST_HOOK
        test_cred_hook_shown(data);
        SetTimer(hwnd, 2, 50, NULL);
#endif

        SetFocus(GetDlgItem(hwnd, IDC_UAC_ACCOUNT));
        return FALSE;
    }

#ifdef WEX_CRED_DIALOG_TEST_HOOK
    case WM_TIMER:
    {
        if (wparam == 2)
        {
            test_cred_hook_action(data, hwnd);
            return TRUE;
        }
        break;
    }
#endif

    case WM_DRAWITEM:
        if (wparam == IDC_UAC_BANNER)
        {
            draw_banner((const DRAWITEMSTRUCT *)lparam, data->is_verified);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDYES:
        {
            WCHAR waccount[128];
            char ascii_account[128];

            GetDlgItemTextW(hwnd, IDC_UAC_ACCOUNT, waccount, ARRAY_SIZE(waccount));
            WideCharToMultiByte(CP_ACP, 0, waccount, -1, ascii_account, sizeof(ascii_account), NULL, NULL);

            if (!wex_valid_account_name(ascii_account))
            {
                SetDlgItemTextW(hwnd, IDC_UAC_ERROR, L"Invalid account name format. Rights apply inside Wine prefix only.");
                SetDlgItemTextW(hwnd, IDC_UAC_PASSWORD, L"");
                SetFocus(GetDlgItem(hwnd, IDC_UAC_ACCOUNT));
#ifdef WEX_CRED_DIALOG_TEST_HOOK
                test_cred_hook_record_invalid(data);
#endif
                return TRUE;
            }

            GetDlgItemTextW(hwnd, IDC_UAC_PASSWORD, data->password, ARRAY_SIZE(data->password));
            lstrcpynA(data->account, ascii_account, sizeof(data->account));
            data->account[sizeof(data->account) - 1] = '\0';

            /* Transmit S1 Answer record with answer_kind = CONFIRM */
            if (data->channel_pipe && data->channel_pipe != INVALID_HANDLE_VALUE)
            {
                BYTE answer_buf[WEX_S1_ANSWER_LENGTH];
                size_t ans_len = 0;
                DWORD written = 0;

                if (wex_build_answer_record(data->request_id,
                                            data->attempt ? data->attempt : 1,
                                            0, 0,
                                            WEX_ANSWER_KIND_CONFIRM,
                                            data->nonce,
                                            data->binding_digest,
                                            data->request_digest,
                                            data->channel_token,
                                            answer_buf, sizeof(answer_buf),
                                            &ans_len) == WEX_STATUS_OK)
                {
                    WriteFile(data->channel_pipe, answer_buf, (DWORD)ans_len, &written, NULL);
                    FlushFileBuffers(data->channel_pipe);
                }
                wex_secure_wipe(answer_buf, sizeof(answer_buf));
            }

            wex_secure_wipe(data->password, sizeof(data->password));
            SetDlgItemTextW(hwnd, IDC_UAC_PASSWORD, L"");
            data->confirmed = TRUE;
            EndDialog(hwnd, IDYES);
            return TRUE;
        }

        case IDNO:
        case IDCANCEL:
        {
            /* Transmit S1 Answer record with answer_kind = CANCEL */
            if (!data->confirmed && data->channel_pipe && data->channel_pipe != INVALID_HANDLE_VALUE)
            {
                BYTE answer_buf[WEX_S1_ANSWER_LENGTH];
                size_t ans_len = 0;
                DWORD written = 0;

                if (wex_build_answer_record(data->request_id,
                                            data->attempt ? data->attempt : 1,
                                            0, 0,
                                            WEX_ANSWER_KIND_CANCEL,
                                            data->nonce,
                                            data->binding_digest,
                                            data->request_digest,
                                            data->channel_token,
                                            answer_buf, sizeof(answer_buf),
                                            &ans_len) == WEX_STATUS_OK)
                {
                    WriteFile(data->channel_pipe, answer_buf, (DWORD)ans_len, &written, NULL);
                    FlushFileBuffers(data->channel_pipe);
                }
                wex_secure_wipe(answer_buf, sizeof(answer_buf));
            }

            wex_secure_wipe(data->password, sizeof(data->password));
            SetDlgItemTextW(hwnd, IDC_UAC_PASSWORD, L"");
            data->confirmed = FALSE;
            EndDialog(hwnd, IDNO);
            return TRUE;
        }
        }
        break;

    case WM_CLOSE:
        PostMessageW(hwnd, WM_COMMAND, IDNO, 0);
        return TRUE;

    case WM_DESTROY:
        wex_secure_wipe(data->password, sizeof(data->password));
        wex_secure_wipe(data->account, sizeof(data->account));
        break;
    }
    return FALSE;
}

/* The broker returns the input to the user's desktop on every exit path, but a consent window that outlived a
 * broker that was killed would stay on the secure desktop (and hold the display on X11). It ends with its broker. */
static DWORD WINAPI parent_watch(void *param)
{
    WaitForSingleObject((HANDLE)param, INFINITE);
    ExitProcess(ERROR_CANCELLED);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, WCHAR *cmdline, int show)
{
    struct consent_data data;
    HWND hdim = NULL;
    INT_PTR res;
    DWORD dialog_error;

    InitCommonControls();
    parse_command_line(cmdline, &data);

    if (!data.credentials_mode && !data.broker)
        data.credentials_mode = is_credentials_mode_required();

    if (data.credentials_mode)
    {
        /* Disabled by default unless spawned by broker with a valid bound channel */
        if (!data.broker || !data.request_id || data.channel_pipe == INVALID_HANDLE_VALUE)
        {
            /* Checking a password is not authorization to become an administrator.
             * A request-bound polkit/backend identity contract is not implemented
             * yet. Do not collect secrets or substitute a test approval fixture. */
            WARN("Administrator credential authorization is not available.\n");
            return ERROR_NOT_SUPPORTED;
        }

        if (data.parent_pid)
        {
            HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, data.parent_pid), thread;

            if (!parent) return ERROR_CANCELLED;   /* the broker is gone already */
            if ((thread = CreateThread(NULL, 0, parent_watch, parent, 0, NULL))) CloseHandle(thread);
        }

        /* Cosmetic dimmer on the shared desktop. The secure desktop is covered and dimmed by the display driver. */
        if (!data.secure) hdim = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                               L"STATIC", L"", WS_POPUP | WS_VISIBLE,
                               GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                               GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
                               NULL, NULL, inst, NULL);
        if (hdim)
            SetLayeredWindowAttributes(hdim, 0, 160, LWA_ALPHA);

        res = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_CREDENTIALS), hdim ? hdim : data.hwnd_parent,
                              credentials_dlg_proc, (LPARAM)&data);
        dialog_error = res == -1 ? GetLastError() : ERROR_SUCCESS;
        if (res == -1 && !dialog_error) dialog_error = ERROR_GEN_FAILURE;

        if (hdim)
            DestroyWindow(hdim);

        TRACE("Credential dialog finished with result %Id, confirmed=%d\n", res, data.confirmed);
        /* Helper exit never approves: only explicit Confirm on channel does */
        if (res == -1) return dialog_error;
        return (data.confirmed && res == IDYES) ? ERROR_SUCCESS : ERROR_CANCELLED;
    }

    if (data.broker && data.parent_pid)
    {
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, data.parent_pid), thread;

        if (!parent) return ERROR_CANCELLED;   /* the broker is gone already */
        if ((thread = CreateThread(NULL, 0, parent_watch, parent, 0, NULL))) CloseHandle(thread);
    }

    /* Cosmetic dimmer on the shared desktop. The secure desktop is covered and dimmed by the display driver. */
    if (!data.secure) hdim = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                           L"STATIC", L"", WS_POPUP | WS_VISIBLE,
                           GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                           GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
                           NULL, NULL, inst, NULL);
    if (hdim)
        SetLayeredWindowAttributes(hdim, 0, 160, LWA_ALPHA);

    res = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_CONSENT), hdim ? hdim : data.hwnd_parent,
                          consent_dlg_proc, (LPARAM)&data);
    dialog_error = res == -1 ? GetLastError() : ERROR_SUCCESS;
    if (res == -1 && !dialog_error) dialog_error = ERROR_GEN_FAILURE;

    if (hdim)
        DestroyWindow(hdim);

    TRACE("Consent dialog finished with result %Id\n", res);
    if (res == -1) return dialog_error;
    return (res == IDYES) ? ERROR_SUCCESS : ERROR_CANCELLED;
}
