/*
 * WineExplorer Desktop Environment - User Account Control style elevation prompt
 *
 * Shows a Vista-like consent / credential dialog and then runs a command as root
 * through sudo (current user) or su (another administrator account).
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "timedate.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define UAC_MAX_USERS 16
#define UAC_NAME_LEN  64

typedef struct
{
    const WCHAR *program;
    const char *cmd;
    WCHAR cur[UAC_NAME_LEN];
    WCHAR users[UAC_MAX_USERS][UAC_NAME_LEN];
    int nusers;
    BOOL cur_is_admin;
    BOOL nopasswd;
} UAC_DATA;

static BOOL name_is_safe(const WCHAR *s)
{
    if (!*s) return FALSE;
    for (; *s; s++)
    {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_' || *s == '-' || *s == '.'))
            return FALSE;
    }
    return TRUE;
}

static void add_user(UAC_DATA *d, const WCHAR *name)
{
    int i;
    if (d->nusers >= UAC_MAX_USERS || !name_is_safe(name)) return;
    for (i = 0; i < d->nusers; i++)
        if (!wcscmp(d->users[i], name)) return;
    lstrcpynW(d->users[d->nusers++], name, UAC_NAME_LEN);
}

/* Administrators are the members of the sudo / wheel / admin groups, plus root */
static void read_admin_users(UAC_DATA *d)
{
    HANDLE file;
    char buf[8192];
    DWORD got = 0;
    char *line, *next;

    /* Root is always the primary administrator on Unix */
    add_user(d, L"root");

    file = CreateFileW(L"Z:\\etc\\group", GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(file, buf, sizeof(buf) - 1, &got, NULL)) got = 0;
    CloseHandle(file);
    buf[got] = 0;

    for (line = buf; line && *line; line = next)
    {
        char *members;
        next = strchr(line, '\n');
        if (next) *next++ = 0;

        if (strncmp(line, "sudo:", 5) && strncmp(line, "wheel:", 6) && strncmp(line, "admin:", 6))
            continue;
        members = strchr(line, ':');
        if (members) members = strchr(members + 1, ':');
        if (members) members = strchr(members + 1, ':');
        if (!members) continue;
        members++;
        while (*members)
        {
            WCHAR name[UAC_NAME_LEN];
            char *comma = strchr(members, ',');
            int len = comma ? (int)(comma - members) : (int)strlen(members);
            if (len > 0 && len < UAC_NAME_LEN)
            {
                MultiByteToWideChar(CP_ACP, 0, members, len, name, UAC_NAME_LEN);
                name[len] = 0;
                add_user(d, name);
            }
            if (!comma) break;
            members = comma + 1;
        }
    }
}

/*
 * Run a native shell command and capture its exit status.
 * Optional stdin_data (e.g. password) is fed via a temporary file with secure cleanup.
 */
static int run_native_cmd(const char *cmd_sh, const char *stdin_data)
{
    DWORD tick;
    DWORD pid;
    char status_unix[128];
    char status_dos[128];
    char in_unix[128];
    char in_dos[128];
    char shell_cmd[2048];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    int i, exit_code = -1;
    HANDLE hFile;
    DWORD read_bytes = 0;
    char code_buf[16];

    tick = GetTickCount();
    pid = GetCurrentProcessId();

    snprintf(status_unix, sizeof(status_unix), "/tmp/.uac_status_%lu_%lu", pid, tick);
    snprintf(status_dos, sizeof(status_dos), "Z:\\tmp\\.uac_status_%lu_%lu", pid, tick);
    snprintf(in_unix, sizeof(in_unix), "/tmp/.uac_in_%lu_%lu", pid, tick);
    snprintf(in_dos, sizeof(in_dos), "Z:\\tmp\\.uac_in_%lu_%lu", pid, tick);

    DeleteFileA(status_dos);
    DeleteFileA(in_dos);

    if (stdin_data && stdin_data[0])
    {
        DWORD written = 0;
        hFile = CreateFileA(in_dos, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            WriteFile(hFile, stdin_data, (DWORD)strlen(stdin_data), &written, NULL);
            CloseHandle(hFile);
        }
        snprintf(shell_cmd, sizeof(shell_cmd),
                 "/bin/sh -c \"(%s) < %s >/dev/null 2>&1; echo \\$? > %s; rm -f %s\"",
                 cmd_sh, in_unix, status_unix, in_unix);
    }
    else
    {
        snprintf(shell_cmd, sizeof(shell_cmd),
                 "/bin/sh -c \"(%s) >/dev/null 2>&1; echo \\$? > %s\"",
                 cmd_sh, status_unix);
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, shell_cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        DeleteFileA(in_dos);
        return -1;
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);

    /* Poll for completion up to 10 seconds (200 * 50ms) */
    for (i = 0; i < 200; i++)
    {
        Sleep(50);
        hFile = CreateFileA(status_dos, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, 0, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            memset(code_buf, 0, sizeof(code_buf));
            if (ReadFile(hFile, code_buf, sizeof(code_buf) - 1, &read_bytes, NULL) && read_bytes > 0)
            {
                exit_code = atoi(code_buf);
            }
            CloseHandle(hFile);
            DeleteFileA(status_dos);
            break;
        }
    }

    DeleteFileA(in_dos);
    DeleteFileA(status_dos);
    return exit_code;
}

static BOOL sudo_needs_no_password(void)
{
    return run_native_cmd("sudo -n true", NULL) == 0;
}

static BOOL run_elevated(const UAC_DATA *d, const WCHAR *user, const WCHAR *password)
{
    char sh_cmd[1024];
    char pw_utf8[256];
    char pw_input[300];
    int ret;

    WideCharToMultiByte(CP_UTF8, 0, password, -1, pw_utf8, sizeof(pw_utf8), NULL, NULL);

    if (!wcscmp(user, d->cur))
    {
        if (d->nopasswd)
        {
            snprintf(sh_cmd, sizeof(sh_cmd), "sudo %s", d->cmd);
            ret = run_native_cmd(sh_cmd, NULL);
        }
        else
        {
            snprintf(sh_cmd, sizeof(sh_cmd), "sudo -S -p '' %s", d->cmd);
            snprintf(pw_input, sizeof(pw_input), "%s\n", pw_utf8);
            ret = run_native_cmd(sh_cmd, pw_input);
            SecureZeroMemory(pw_input, sizeof(pw_input));
        }
    }
    else
    {
        char u_utf8[UAC_NAME_LEN];
        WideCharToMultiByte(CP_UTF8, 0, user, -1, u_utf8, sizeof(u_utf8), NULL, NULL);

        if (d->cur_is_admin && d->nopasswd)
        {
            snprintf(sh_cmd, sizeof(sh_cmd), "sudo -u %s %s", u_utf8, d->cmd);
            ret = run_native_cmd(sh_cmd, NULL);
        }
        else
        {
            snprintf(sh_cmd, sizeof(sh_cmd), "su %s -c '%s'", u_utf8, d->cmd);
            snprintf(pw_input, sizeof(pw_input), "%s\n", pw_utf8);
            ret = run_native_cmd(sh_cmd, pw_input);
            SecureZeroMemory(pw_input, sizeof(pw_input));
        }
    }

    SecureZeroMemory(pw_utf8, sizeof(pw_utf8));
    return ret == 0;
}

static void update_password_state(HWND hwnd, UAC_DATA *d)
{
    int sel = (int)SendDlgItemMessageW(hwnd, IDC_UAC_ACCOUNT, CB_GETCURSEL, 0, 0);
    BOOL need = TRUE;

    if (sel >= 0 && sel < d->nusers && !wcscmp(d->users[sel], d->cur) && d->nopasswd)
        need = FALSE;

    if (need)
    {
        ShowWindow(GetDlgItem(hwnd, IDC_UAC_PW_LABEL), SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_UAC_PASSWORD), SW_SHOW);
        EnableWindow(GetDlgItem(hwnd, IDC_UAC_PASSWORD), TRUE);
        SetDlgItemTextW(hwnd, IDC_UAC_PROMPT,
                        L"To continue, type an administrator password, and then click Yes.");
        SetFocus(GetDlgItem(hwnd, IDC_UAC_PASSWORD));
    }
    else
    {
        ShowWindow(GetDlgItem(hwnd, IDC_UAC_PW_LABEL), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_UAC_PASSWORD), SW_HIDE);
        SetDlgItemTextW(hwnd, IDC_UAC_PROMPT, L"To continue, click Yes.");
        SetFocus(GetDlgItem(hwnd, IDYES));
    }
}

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
    lstrcpyW(lf.lfFaceName, L"Segoe UI");
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

static INT_PTR CALLBACK uac_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    UAC_DATA *d = (UAC_DATA *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT dlg_rc;
        HBRUSH bg_card;
        HPEN pen_border;
        HGDIOBJ old_b, old_p;

        dlg_rc.left = 12;
        dlg_rc.top = 110;
        dlg_rc.right = 12 + 222;
        dlg_rc.bottom = 110 + 44;
        MapDialogRect(hwnd, &dlg_rc);

        bg_card = CreateSolidBrush(RGB(236, 244, 254));
        pen_border = CreatePen(PS_SOLID, 1, RGB(180, 208, 238));
        old_b = SelectObject(hdc, bg_card);
        old_p = SelectObject(hdc, pen_border);
        RoundRect(hdc, dlg_rc.left, dlg_rc.top, dlg_rc.right, dlg_rc.bottom, 6, 6);
        SelectObject(hdc, old_b);
        SelectObject(hdc, old_p);
        DeleteObject(bg_card);
        DeleteObject(pen_border);

        EndPaint(hwnd, &ps);
        return TRUE;
    }

    case WM_INITDIALOG:
    {
        int i;
        HFONT font_bold;
        LOGFONTW lf;

        d = (UAC_DATA *)lparam;
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)d);

        SetDlgItemTextW(hwnd, IDC_UAC_PROGRAM, d->program);

        /* Set bold font for Program name and Publisher */
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -12;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"Segoe UI");
        font_bold = CreateFontIndirectW(&lf);
        if (font_bold)
        {
            SendDlgItemMessageW(hwnd, IDC_UAC_PROGRAM, WM_SETFONT, (WPARAM)font_bold, TRUE);
            SendDlgItemMessageW(hwnd, IDC_UAC_PUB_NAME, WM_SETFONT, (WPARAM)font_bold, TRUE);
        }

        for (i = 0; i < d->nusers; i++)
        {
            WCHAR label[UAC_NAME_LEN + 20];
            if (!wcscmp(d->users[i], d->cur))
                _snwprintf(label, ARRAY_SIZE(label), L"%s (current user)", d->users[i]);
            else
                lstrcpynW(label, d->users[i], ARRAY_SIZE(label));
            SendDlgItemMessageW(hwnd, IDC_UAC_ACCOUNT, CB_ADDSTRING, 0, (LPARAM)label);
        }
        SendDlgItemMessageW(hwnd, IDC_UAC_ACCOUNT, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageW(hwnd, IDC_UAC_PASSWORD, EM_LIMITTEXT, 127, 0);
        update_password_state(hwnd, d);
        return FALSE;
    }

    case WM_CTLCOLORSTATIC:
    {
        HWND hCtrl = (HWND)lparam;
        if (hCtrl == GetDlgItem(hwnd, IDC_UAC_PW_LABEL))
        {
            HDC hdc = (HDC)wparam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        }
        break;
    }

    case WM_DRAWITEM:
        if (wparam == IDC_UAC_BANNER)
        {
            draw_banner((const DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        else if (wparam == IDC_UAC_USERTILE)
        {
            draw_user_tile((const DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_UAC_ACCOUNT:
            if (HIWORD(wparam) == CBN_SELCHANGE) update_password_state(hwnd, d);
            return TRUE;

        case IDYES:
        {
            WCHAR password[128];
            int sel = (int)SendDlgItemMessageW(hwnd, IDC_UAC_ACCOUNT, CB_GETCURSEL, 0, 0);
            BOOL ok;
            BOOL need = TRUE;

            if (sel < 0 || sel >= d->nusers) return TRUE;

            if (!wcscmp(d->users[sel], d->cur) && d->nopasswd)
                need = FALSE;

            password[0] = 0;
            if (need)
            {
                GetDlgItemTextW(hwnd, IDC_UAC_PASSWORD, password, ARRAY_SIZE(password));
                if (!password[0])
                {
                    MessageBoxW(hwnd, L"Please type an administrator password to continue.",
                                L"User Account Control", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwnd, IDC_UAC_PASSWORD));
                    return TRUE;
                }
            }

            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
            ok = run_elevated(d, d->users[sel], password);
            SecureZeroMemory(password, sizeof(password));
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));

            if (ok)
            {
                EndDialog(hwnd, IDYES);
            }
            else
            {
                MessageBoxW(hwnd, L"The password is incorrect, or the account is not allowed to make "
                            L"changes to this computer.", L"User Account Control",
                            MB_OK | MB_ICONERROR);
                SetDlgItemTextW(hwnd, IDC_UAC_PASSWORD, L"");
                SetFocus(GetDlgItem(hwnd, IDC_UAC_PASSWORD));
            }
            return TRUE;
        }

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

/* Ask the user for consent / credentials and run "cmd" as root. Returns TRUE on success. */
BOOL uac_run_elevated(HWND owner, const WCHAR *program, const char *cmd)
{
    UAC_DATA d;
    DWORD len = UAC_NAME_LEN;
    int i;

    memset(&d, 0, sizeof(d));
    d.program = program;
    d.cmd = cmd;
    if (!GetUserNameW(d.cur, &len)) d.cur[0] = 0;

    read_admin_users(&d);
    for (i = 0; i < d.nusers; i++)
    {
        if (!wcscmp(d.users[i], d.cur))
        {
            WCHAR tmp[UAC_NAME_LEN];
            lstrcpyW(tmp, d.users[0]);
            lstrcpyW(d.users[0], d.users[i]);
            lstrcpyW(d.users[i], tmp);
            d.cur_is_admin = TRUE;
            break;
        }
    }
    if (!d.nusers) return FALSE;
    d.nopasswd = d.cur_is_admin && sudo_needs_no_password();

    return DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_UAC), owner,
                           uac_dlg_proc, (LPARAM)&d) == IDYES;
}
