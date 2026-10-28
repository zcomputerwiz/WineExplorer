/*
 * WineExplorer Desktop Environment - oobe.c
 * Out-of-Box Experience (OOBE) User Provisioning Wizard
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

#define WIN32_LEAN_AND_MEAN
#define _WIN32_IE 0x0501
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <io.h>
#include <ctype.h>

#include "wexhost.h"
#include "resource.h"
#include "oobe_catalog.h"
#include "oobe_worker.h"

static OobeContext g_ctx;
static HINSTANCE g_hInstance = NULL;
static HWND g_hBackground = NULL;
static LONG g_worker_generation;

static void LogOobe(const char *msg)
{
    FILE *f = fopen("Z:\\var\\log\\wineexplorer-oobe.log", "a");
    if (!f) f = fopen("/var/log/wineexplorer-oobe.log", "a");
    if (!f) f = fopen("wineexplorer-oobe.log", "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
        fclose(f);
    }
}

static void SanitizeUsername(const char *in, char *out, size_t out_len)
{
    size_t i, j = 0;
    for (i = 0; in[i] && j + 1 < out_len; ++i) {
        char c = (char)tolower((unsigned char)in[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

static void CheckUnattended(void)
{
    int i;
    WCHAR wpath[MAX_PATH];
    WCHAR buf[256];
    const char *paths[] = {
        "Z:\\etc\\wineexplorer\\setup\\unattend.sif",
        "Z:\\run\\live\\medium\\setup\\unattend.sif",
        "Z:\\setup\\unattend.sif",
        "/etc/wineexplorer/setup/unattend.sif",
        "/run/live/medium/setup/unattend.sif",
        "/setup/unattend.sif",
        NULL
    };

    for (i = 0; paths[i]; ++i) {
        if (access(paths[i], 0) == 0) {
            g_ctx.unattended = true;
            snprintf(g_ctx.unattend_path, sizeof(g_ctx.unattend_path), "%s", paths[i]);
            break;
        }
    }

    if (!g_ctx.unattended) return;

    MultiByteToWideChar(CP_UTF8, 0, g_ctx.unattend_path, -1, wpath, MAX_PATH);

    /* Read users from [WexOobe] or [UserData] */
    for (i = 1; i <= 5; ++i) {
        WCHAR key[32];
        swprintf(key, 32, L"User%d", i);
        if (GetPrivateProfileStringW(L"WexOobe", key, L"", buf, 256, wpath)) {
            char raw[128];
            char clean[64];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, raw, sizeof(raw), NULL, NULL);
            SanitizeUsername(raw, clean, sizeof(clean));
            if (clean[0]) {
                int idx = g_ctx.user_count++;
                WCHAR admin_key[32], pass_key[32];
                snprintf(g_ctx.users[idx].name, sizeof(g_ctx.users[idx].name), "%s", clean);
                snprintf(g_ctx.users[idx].real_name, sizeof(g_ctx.users[idx].real_name), "%s", raw);
                swprintf(admin_key, 32, L"User%dAdmin", i);
                g_ctx.users[idx].is_admin = (GetPrivateProfileIntW(L"WexOobe", admin_key, (idx == 0) ? 1 : 0, wpath) != 0);
                swprintf(pass_key, 32, L"User%dPassword", i);
                if (GetPrivateProfileStringW(L"WexOobe", pass_key, L"", buf, 256, wpath)) {
                    WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_ctx.users[idx].password, sizeof(g_ctx.users[idx].password), NULL, NULL);
                }
            }
        }
    }

    /* Fallback to UserData FullName if no WexOobe users */
    if (g_ctx.user_count == 0) {
        if (GetPrivateProfileStringW(L"UserData", L"FullName", L"", buf, 256, wpath)) {
            char raw[128];
            char clean[64];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, raw, sizeof(raw), NULL, NULL);
            SanitizeUsername(raw, clean, sizeof(clean));
            if (clean[0]) {
                g_ctx.user_count = 1;
                snprintf(g_ctx.users[0].name, sizeof(g_ctx.users[0].name), "%s", clean);
                snprintf(g_ctx.users[0].real_name, sizeof(g_ctx.users[0].real_name), "%s", raw);
                g_ctx.users[0].is_admin = true;
            }
        }
    }

    {
        WCHAR auto_up[64];
        auto_up[0] = 0;
        if (!GetPrivateProfileStringW(L"Data", L"AutomaticUpdates", L"", auto_up, 64, wpath) || !auto_up[0]) {
            GetPrivateProfileStringW(L"Unattended", L"AutomaticUpdates", L"", auto_up, 64, wpath);
        }
        if (auto_up[0]) {
            if (!_wcsicmp(auto_up, L"no") || !_wcsicmp(auto_up, L"0") || !_wcsicmp(auto_up, L"false") || !_wcsicmp(auto_up, L"off")) {
                g_ctx.automatic_updates = false;
            } else if (!_wcsicmp(auto_up, L"yes") || !_wcsicmp(auto_up, L"1") || !_wcsicmp(auto_up, L"true") || !_wcsicmp(auto_up, L"on")) {
                g_ctx.automatic_updates = true;
            }
        }
    }

    LogOobe("Parsed unattended OOBE accounts.");
}

static void ProvisionUsers(void)
{
    int i;
    bool autologin;
    char logmsg[256];

    LogOobe("Provisioning user accounts via AccountsService...");

    for (i = 0; i < g_ctx.user_count; ++i) {
        UserAccount *u = &g_ctx.users[i];
        DWORD err;

        snprintf(logmsg, sizeof(logmsg), "Creating user '%s' (admin=%d)", u->name, u->is_admin);
        LogOobe(logmsg);

        /* 1. CreateUser via AccountsService */
        err = WexUserCreate(u->name, u->real_name[0] ? u->real_name : u->name, u->is_admin ? 1 : 0, u->password);
        snprintf(logmsg, sizeof(logmsg), "WexUserCreate('%s') result: %lu", u->name, err);
        LogOobe(logmsg);

    }

    /* 2. Automatic logon only for a single account without a password (as XP did).
     * AccountsService writes LightDM's setting; the session is WineExplorer for everyone. /etc/skel (with the template Wine
     * prefix) was copied into each home by CreateUser. */
    autologin = (g_ctx.user_count == 1 && g_ctx.users[0].password[0] == '\0');
    LogOobe(autologin ? "Single account without a password: automatic logon." : "Automatic logon off.");
    for (i = 0; i < g_ctx.user_count; ++i)
        WexUserSetSession(g_ctx.users[i].name, "wine-explorer", autologin);

    /* 3. Record automatic updates choice for wex-oobe.sh */
    {
        FILE *fup = fopen("Z:\\etc\\wineexplorer\\setup\\automatic-updates", "w");
        if (!fup) fup = fopen("/etc/wineexplorer/setup/automatic-updates", "w");
        if (fup) {
            fprintf(fup, "%s\n", g_ctx.automatic_updates ? "on" : "off");
            fclose(fup);
        }
    }

    /* 4. Cleanup setup state */
    LogOobe("Cleaning up temporary setup files and permissions...");
    remove("Z:\\etc\\wineexplorer\\setup\\stage");
    remove("/etc/wineexplorer/setup/stage");
    remove("Z:\\etc\\wineexplorer\\setup\\stage-oobe");
    remove("/etc/wineexplorer/setup/stage-oobe");
    remove("Z:\\etc\\polkit-1\\rules.d\\49-wexsetup.rules");
    remove("/etc/polkit-1/rules.d/49-wexsetup.rules");

    g_ctx.provisioned = true;
    LogOobe("OOBE user provisioning and cleanup completed.");
}

/* Background Window Procedure: Classic Windows XP Luna Blue */
static LRESULT CALLBACK BackgroundWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            HBRUSH hbr;
            HFONT hFont, hOld;

            GetClientRect(hwnd, &rc);
            hbr = CreateSolidBrush(RGB(0, 51, 153));
            FillRect(hdc, &rc, hbr);
            DeleteObject(hbr);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            hFont = CreateFontA(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");
            hOld = SelectObject(hdc, hFont);
            TextOutA(hdc, 40, 30, "Welcome to Debian GNU/Linux with WineExplorer", 45);
            SelectObject(hdc, hOld);
            DeleteObject(hFont);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

/* OOBE Wizard Page Procedures */
static INT_PTR CALLBACK OobeWelcomeProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)PSWIZB_NEXT);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK OobeUpdatesProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            CheckRadioButton(hwnd, IDC_RADIO_UPDATES_ON, IDC_RADIO_UPDATES_OFF, g_ctx.automatic_updates ? IDC_RADIO_UPDATES_ON : IDC_RADIO_UPDATES_OFF);
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                g_ctx.automatic_updates = (IsDlgButtonChecked(hwnd, IDC_RADIO_UPDATES_ON) == BST_CHECKED);
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK OobeConnectivityProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static DWORD WINAPI TermsFetchThread(LPVOID lpParam)
{
    TermsFetchContext *ctx = (TermsFetchContext *)lpParam;
    DWORD needed = 0;
    DWORD ret;
    char snapshot[65];

    ret = WexOobeGetTerms(ctx->comp->verb, ctx->candidate_sha256, sizeof(ctx->candidate_sha256), NULL, 0, &needed);
    memcpy(snapshot, ctx->candidate_sha256, sizeof(snapshot));
    if (ret == ERROR_MORE_DATA && needed > 0) {
        char *buf = malloc(needed + 1);
        if (buf) {
            ret = WexOobeGetTerms(ctx->comp->verb, ctx->candidate_sha256, sizeof(ctx->candidate_sha256), buf, needed + 1, &needed);
            if (ret == ERROR_SUCCESS && strcmp(snapshot, ctx->candidate_sha256))
                ret = ERROR_RETRY; /* Never transfer approval across snapshots. */
            if (ret == ERROR_SUCCESS) {
                int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, -1, NULL, 0);
                if (wlen > 0) {
                    WCHAR *wbuf = malloc(wlen * sizeof(WCHAR));
                    if (wbuf) {
                        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, -1, wbuf, wlen) > 0) {
                            ctx->terms_text = wbuf;
                        } else {
                            free(wbuf);
                        }
                    }
                }
                if (!ctx->terms_text) {
                    ret = ERROR_INVALID_DATA;
                }
            }
            free(buf);
        } else {
            ret = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    ctx->err = ret;
    if (!InterlockedCompareExchange(&ctx->cancelled, 0, 0)) {
        HWND target = InterlockedCompareExchangePointer((void *volatile *)&ctx->hwnd, NULL, NULL);
        if (target) {
            PostMessageA(target, WM_USER + 101, (WPARAM)ctx->generation, 0);
        }
    }
    TermsFetchContext_Release(ctx);
    return 0;
}

static INT_PTR CALLBACK OobeTermsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    TermsFetchContext *ctx = (TermsFetchContext *)GetWindowLongPtrA(hwnd, DWLP_USER);
    switch (uMsg) {
        case WM_INITDIALOG: {
            OobeComponentItem *comp = (OobeComponentItem *)lParam;
            char title[256];
            ctx = calloc(1, sizeof(TermsFetchContext));
            if (!ctx) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            ctx->ref_count = 2; /* 1 for dialog, 1 for worker thread */
            ctx->generation = InterlockedIncrement(&g_worker_generation);
            ctx->hwnd = hwnd;
            ctx->comp = comp;
            SetWindowLongPtrA(hwnd, DWLP_USER, (LONG_PTR)ctx);

            snprintf(title, sizeof(title), "%s - Vendor License Agreement", comp->title);
            SetWindowTextA(hwnd, title);
            SetDlgItemTextA(hwnd, IDC_TERMS_TEXT, "Retrieving official vendor license terms over the network... Please wait.");
            EnableWindow(GetDlgItem(hwnd, IDC_TERMS_AGREE), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);

            ctx->hThread = CreateThread(NULL, 0, TermsFetchThread, ctx, 0, NULL);
            if (!ctx->hThread) {
                ctx->ref_count = 1;
                SetDlgItemTextA(hwnd, IDC_TERMS_TEXT,
                    "Failed to create background worker thread to retrieve vendor license terms.\r\n\r\nYou may click Cancel to return and skip this optional component.");
                EnableWindow(GetDlgItem(hwnd, IDC_TERMS_AGREE), FALSE);
                EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
            }
            return TRUE;
        }
        case WM_USER + 101: {
            if (!ctx || ctx->generation != (LONG)wParam) return TRUE;
            if (ctx->err == ERROR_SUCCESS && ctx->terms_text) {
                SetDlgItemTextW(hwnd, IDC_TERMS_TEXT, ctx->terms_text);
                EnableWindow(GetDlgItem(hwnd, IDC_TERMS_AGREE), TRUE);
                if (ctx->comp->accepted && strcmp(ctx->comp->accepted_sha256, ctx->candidate_sha256) == 0) {
                    CheckDlgButton(hwnd, IDC_TERMS_AGREE, BST_CHECKED);
                    EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
                } else {
                    CheckDlgButton(hwnd, IDC_TERMS_AGREE, BST_UNCHECKED);
                    EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
                }
            } else {
                SetDlgItemTextA(hwnd, IDC_TERMS_TEXT,
                    "Unable to retrieve vendor license terms from official vendor source.\r\n\r\n"
                    "Network connectivity may be offline or the vendor host is currently unreachable.\r\n\r\n"
                    "You may click Cancel to return and skip this optional component.");
                EnableWindow(GetDlgItem(hwnd, IDC_TERMS_AGREE), FALSE);
                EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
            }
            return TRUE;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDC_TERMS_AGREE) {
                BOOL agreed = (IsDlgButtonChecked(hwnd, IDC_TERMS_AGREE) == BST_CHECKED);
                EnableWindow(GetDlgItem(hwnd, IDOK), agreed);
                return TRUE;
            } else if (id == IDOK) {
                if (ctx && ctx->comp && (IsDlgButtonChecked(hwnd, IDC_TERMS_AGREE) == BST_CHECKED)) {
                    if (!OobeAcceptTerms(ctx->comp, ctx->candidate_sha256,
                                         ctx->err == ERROR_SUCCESS && ctx->terms_text,
                                         true)) return TRUE;
                }
                EndDialog(hwnd, IDOK);
                return TRUE;
            } else if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY: {
            if (ctx) {
                InterlockedExchange(&ctx->cancelled, 1);
                InterlockedExchangePointer((void *volatile *)&ctx->hwnd, NULL);
                SetWindowLongPtrA(hwnd, DWLP_USER, 0);
                TermsFetchContext_Release(ctx);
            }
            break;
        }
    }
    return FALSE;
}

/* --- Review-only embedded terms (disabled components such as CoreFonts) ---
 * The review dialog never installs anything. For review-only components it has
 * no accept control at all: closing it records no acceptance and never selects
 * the component for install. All coordinator traffic runs on the worker thread:
 * REVIEW_INFO first (size, package count, cache state), START_REVIEW only after
 * the user authorizes a download, QUERY_REVIEW polled with cancel support, and
 * CANCEL_REVIEW when the download is stopped or the dialog closes. After a
 * successful download the terms are read back from the verified local cache
 * (GET_TERMS, no network). */
#define WM_OOBE_REV_READY    (WM_USER + 105)
#define WM_OOBE_REV_PROGRESS (WM_USER + 106)
#define WM_OOBE_REV_DONE     (WM_USER + 107)

static void FormatReviewBytes(ULONG64 bytes, char *out, size_t out_size)
{
    if (bytes >= 1073741824ULL)
        snprintf(out, out_size, "%.1f GB", (double)bytes / 1073741824.0);
    else if (bytes >= 1048576ULL)
        snprintf(out, out_size, "%.1f MB", (double)bytes / 1048576.0);
    else if (bytes >= 1024ULL)
        snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(out, out_size, "%llu bytes", (unsigned long long)bytes);
}

static void ReviewDialogStartTermsFetch(HWND hwnd, ReviewDownloadContext *ctx)
{
    TermsFetchContext *tctx;
    if (!ctx || ctx->terms) return; /* one fetch at a time */
    tctx = calloc(1, sizeof(TermsFetchContext));
    if (!tctx) {
        SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Out of memory while loading the cached terms.");
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), TRUE);
        return;
    }
    tctx->ref_count = 2; /* 1 for dialog, 1 for worker thread */
    tctx->generation = InterlockedIncrement(&g_worker_generation);
    tctx->hwnd = hwnd;
    tctx->comp = ctx->comp;
    ctx->terms = tctx;
    tctx->hThread = CreateThread(NULL, 0, TermsFetchThread, tctx, 0, NULL);
    if (!tctx->hThread) {
        /* No worker owns its reserved reference on failure. */
        tctx->ref_count = 1;
        TermsFetchContext_Release(tctx);
        ctx->terms = NULL;
        SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Failed to start the terms reader thread.");
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), TRUE);
    }
}

static DWORD WINAPI ReviewWorkerThread(LPVOID lpParam)
{
    ReviewDownloadContext *ctx = (ReviewDownloadContext *)lpParam;

    ctx->total_bytes = 0;
    ctx->done_bytes = 0;
    ctx->package_count = 0;
    ctx->cached = FALSE;
    ctx->err = WexOobeReviewInfo(ctx->comp->verb, &ctx->total_bytes, &ctx->package_count, &ctx->cached);
    {
        HWND target = InterlockedCompareExchangePointer((void *volatile *)&ctx->hwnd, NULL, NULL);
        if (target) PostMessageA(target, WM_OOBE_REV_READY, (WPARAM)ctx->generation, 0);
    }

    while (!InterlockedCompareExchange(&ctx->cancelled, 0, 0)) {
        char err_buf[256];
        BOOL running;
        int query_fails = 0;
        DWORD ret;

        /* Wait for the user to authorize this download attempt. */
        if (WaitForSingleObject(ctx->hGoEvent, INFINITE) != WAIT_OBJECT_0) break;
        if (InterlockedCompareExchange(&ctx->cancelled, 0, 0)) break;
        if (InterlockedCompareExchange(&ctx->stopped, 0, 0)) {
            /* A stop raced the authorization: report it and wait for a new attempt. */
            InterlockedExchange(&ctx->stopped, 0);
            ctx->err = ERROR_CANCELLED;
            ctx->err_text[0] = '\0';
            {
                HWND target = InterlockedCompareExchangePointer((void *volatile *)&ctx->hwnd, NULL, NULL);
                if (target) PostMessageA(target, WM_OOBE_REV_DONE, (WPARAM)ctx->generation, 0);
            }
            continue;
        }

        memset(err_buf, 0, sizeof(err_buf));
        ret = WexOobeReviewStart(ctx->comp->verb, ctx->op_id, sizeof(ctx->op_id), err_buf, sizeof(err_buf));
        running = (ret == ERROR_SUCCESS);
        while (running && !ReviewContextStopRequested(ctx)) {
            char status[16];
            char fingerprint[65];
            char qerr[256];
            ULONG64 done = 0, total = 0;
            DWORD qret;

            qret = WexOobeReviewQuery(ctx->op_id, status, sizeof(status), &done, &total,
                                      fingerprint, sizeof(fingerprint), qerr, sizeof(qerr));
            if (qret == ERROR_SUCCESS) {
                query_fails = 0;
                if (!strcmp(status, "RUNNING")) {
                    ctx->done_bytes = done;
                    if (total) ctx->total_bytes = total;
                    {
                        HWND target = InterlockedCompareExchangePointer((void *volatile *)&ctx->hwnd, NULL, NULL);
                        if (target) PostMessageA(target, WM_OOBE_REV_PROGRESS, (WPARAM)ctx->generation, 0);
                    }
                    /* 500 ms cadence; the same event also carries stop/close. */
                    if (WaitForSingleObject(ctx->hGoEvent, 500) == WAIT_OBJECT_0 &&
                        ReviewContextStopRequested(ctx)) break;
                    continue;
                }
                running = FALSE;
                if (!strcmp(status, "SUCCESS")) {
                    ret = ERROR_SUCCESS;
                } else if (!strcmp(status, "CANCELLED")) {
                    ret = ERROR_CANCELLED;
                } else if (!strcmp(status, "FAILED")) {
                    ret = ERROR_GEN_FAILURE;
                    snprintf(err_buf, sizeof(err_buf), "%s", qerr);
                } else {
                    ret = ERROR_INVALID_DATA; /* the bridge only yields the four statuses above */
                }
            } else if (qret == ERROR_INVALID_DATA) {
                /* "Unknown review operation": not transient; do not retry. */
                running = FALSE;
                ret = qret;
                snprintf(err_buf, sizeof(err_buf), "%s", qerr);
            } else if (++query_fails < 3) {
                /* Transient transport failure: QUERY_REVIEW is read-only, so a
                 * bounded retry can never resubmit the start. */
                WaitForSingleObject(ctx->hGoEvent, 500);
            } else {
                running = FALSE;
                ret = qret;
                snprintf(err_buf, sizeof(err_buf), "%s", qerr);
            }
        }
        if (running) {
            /* Stopped by the user or by dialog close while in flight. */
            char cerr[256];
            WexOobeReviewCancel(ctx->op_id, cerr, sizeof(cerr));
            ret = ERROR_CANCELLED;
            InterlockedExchange(&ctx->stopped, 0); /* a later attempt starts clean */
        }
        ctx->err = ret;
        snprintf(ctx->err_text, sizeof(ctx->err_text), "%s", err_buf);
        {
            HWND target = InterlockedCompareExchangePointer((void *volatile *)&ctx->hwnd, NULL, NULL);
            if (target) PostMessageA(target, WM_OOBE_REV_DONE, (WPARAM)ctx->generation, 0);
        }
        if (ret == ERROR_SUCCESS) break; /* no retry after success */
    }

    ReviewDownloadContext_Release(ctx);
    return 0;
}

static INT_PTR CALLBACK OobeReviewProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ReviewDownloadContext *ctx = (ReviewDownloadContext *)GetWindowLongPtrA(hwnd, DWLP_USER);
    switch (uMsg) {
        case WM_INITDIALOG: {
            OobeComponentItem *comp = (OobeComponentItem *)lParam;
            char title[256];

            ctx = calloc(1, sizeof(ReviewDownloadContext));
            if (!ctx) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            ctx->ref_count = 2; /* 1 for dialog, 1 for worker thread */
            ctx->generation = InterlockedIncrement(&g_worker_generation);
            ctx->hwnd = hwnd;
            ctx->comp = comp;
            ctx->hGoEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
            if (!ctx->hGoEvent) {
                ctx->ref_count = 1;
                ReviewDownloadContext_Release(ctx);
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            SetWindowLongPtrA(hwnd, DWLP_USER, (LONG_PTR)ctx);

            snprintf(title, sizeof(title), "%s - Embedded License Terms", comp->title);
            SetWindowTextA(hwnd, title);
            SetDlgItemTextA(hwnd, IDC_REV_INFO, "Contacting the components coordinator...");
            SetDlgItemTextA(hwnd, IDC_REV_STATUS, "");
            SetDlgItemTextW(hwnd, IDC_REV_TERMS, L"The complete license terms will appear here once they are verified.");
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_CANCEL), FALSE);
            if (comp->disabled) {
                /* Review-only component: no install-accept control for it. */
                ShowWindow(GetDlgItem(hwnd, IDC_REV_AGREE), SW_HIDE);
            } else {
                /* Install-eligible: acceptance is a separate unchecked action. */
                CheckDlgButton(hwnd, IDC_REV_AGREE, BST_UNCHECKED);
                EnableWindow(GetDlgItem(hwnd, IDC_REV_AGREE), FALSE);
                EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
                SetWindowTextA(GetDlgItem(hwnd, IDOK), "Accept");
            }

            ctx->hThread = CreateThread(NULL, 0, ReviewWorkerThread, ctx, 0, NULL);
            if (!ctx->hThread) {
                /* No worker owns its reserved reference on failure. */
                ctx->ref_count = 1;
                SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Failed to start the review worker thread.");
            }
            return TRUE;
        }
        case WM_OOBE_REV_READY: {
            char info[512];
            char size_str[48];
            char need_str[48];
            if (!ctx || ctx->generation != (LONG)wParam) return TRUE;
            FormatReviewBytes(ctx->total_bytes, size_str, sizeof(size_str));
            /* F3: the coordinator reserves total + 64 MiB of working headroom on disk
             * before it will start a download (wex-components.py review_download), so
             * show the free space the user actually needs, not just the package size. */
            FormatReviewBytes(ctx->total_bytes + (64ULL * 1024 * 1024), need_str, sizeof(need_str));
            if (ctx->err == ERROR_SUCCESS) {
                const char *dl_source;
                const char *pkg_noun;
                char status_msg[192];
                if (!strcmp(ctx->comp->verb, "d3dcompiler_47")) {
                    dl_source = "Microsoft Corporation (download.microsoft.com)\r\n"
                                "  Windows 8.1 Standalone SDK installer cabinets.";
                    pkg_noun = "installer";
                } else {
                    dl_source = "SourceForge (downloads.sourceforge.net/corefonts/)\r\n"
                                "  the pinned Core Fonts distribution archive; not a Microsoft-hosted download.";
                    pkg_noun = "font";
                }
                snprintf(info, sizeof(info),
                         "Publisher: %s\r\n"
                         "Downloaded from: %s\r\n"
                         "%lu %s packages, %s combined. Local cache: %s.\r\n"
                         "Free space needed: %s (packages plus 64 MB of working headroom).\r\n"
                         "Nothing will be executed or installed.",
                         ctx->comp->publisher[0] ? ctx->comp->publisher : "unknown",
                         dl_source,
                         ctx->package_count, pkg_noun, size_str,
                         ctx->cached ? "available (no network needed)" : "not present (download required)",
                         need_str);
                SetDlgItemTextA(hwnd, IDC_REV_INFO, info);
                if (ctx->cached) {
                    SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Loading the verified terms from the local cache...");
                    ReviewDialogStartTermsFetch(hwnd, ctx);
                } else {
                    EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), TRUE);
                    snprintf(status_msg, sizeof(status_msg),
                        "Click \"Download packages to review\" to authorize downloading the %s packages for review. Nothing downloads before you do.",
                        pkg_noun);
                    SetDlgItemTextA(hwnd, IDC_REV_STATUS, status_msg);
                }
            } else {
                snprintf(info, sizeof(info),
                         "The components coordinator is unavailable (error %lu).\r\n"
                         "You may close this dialog and skip this component.", ctx->err);
                SetDlgItemTextA(hwnd, IDC_REV_INFO, info);
                SetDlgItemTextA(hwnd, IDC_REV_STATUS, "");
            }
            return TRUE;
        }
        case WM_OOBE_REV_PROGRESS: {
            char s[160];
            char done_str[48], total_str[48];
            unsigned pct = 0;
            if (!ctx || ctx->generation != (LONG)wParam) return TRUE;
            if (ctx->total_bytes)
                pct = (unsigned)((ctx->done_bytes * 100) / ctx->total_bytes);
            FormatReviewBytes(ctx->done_bytes, done_str, sizeof(done_str));
            FormatReviewBytes(ctx->total_bytes, total_str, sizeof(total_str));
            snprintf(s, sizeof(s), "Downloading packages: %u%% (%s of %s)", pct, done_str, total_str);
            SetDlgItemTextA(hwnd, IDC_REV_STATUS, s);
            return TRUE;
        }
        case WM_OOBE_REV_DONE: {
            if (!ctx || ctx->generation != (LONG)wParam) return TRUE;
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_CANCEL), FALSE);
            if (ctx->err == ERROR_SUCCESS) {
                SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Packages downloaded and verified. Loading the terms from the local cache...");
                EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), FALSE);
                ReviewDialogStartTermsFetch(hwnd, ctx);
            } else if (ctx->err == ERROR_CANCELLED) {
                /* Q5: cancellation is not a rollback. Verified packages already in the
                 * local cache are kept; only the partially written files this run owned
                 * are removed. Say exactly that instead of the inaccurate "or changed". */
                SetDlgItemTextA(hwnd, IDC_REV_STATUS,
                    "Download cancelled. Nothing was installed. Verified packages already in the local cache are kept; only this run's partial files were removed.");
                EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), TRUE);
            } else {
                char s[300];
                snprintf(s, sizeof(s), "Download failed: %s", ctx->err_text[0] ? ctx->err_text : "unknown error");
                SetDlgItemTextA(hwnd, IDC_REV_STATUS, s);
                EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), TRUE);
            }
            return TRUE;
        }
        case WM_USER + 101: { /* cached-terms fetch finished (TermsFetchThread) */
            TermsFetchContext *tctx = ctx ? ctx->terms : NULL;
            ReviewTermsResult r;
            if (!tctx) return TRUE;
            OobeReviewTermsCompletion(ctx, tctx, (LONG)wParam, &r);
            if (!r.applied) {
                /* Stale-generation or post-close completion: it belongs to a
                 * superseded fetch or a destroyed dialog. Do not release the
                 * live fetch and do not touch window state or acceptance. */
                return TRUE;
            }
            if (r.fetch_ok) {
                SetDlgItemTextW(hwnd, IDC_REV_TERMS, tctx->terms_text);
                ctx->terms_ok = TRUE;
                memcpy(ctx->terms_sha256, tctx->candidate_sha256, sizeof(ctx->terms_sha256));
                if (r.disabled_row) {
                    SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Terms verified from the local cache. Nothing was installed.");
                } else {
                    /* Install-eligible: acceptance is a separate action that always
                     * starts unchecked — never pre-checked on open/reopen/retry (Q3). */
                    EnableWindow(GetDlgItem(hwnd, IDC_REV_AGREE), r.agree_enabled);
                    CheckDlgButton(hwnd, IDC_REV_AGREE, r.agree_checked ? BST_CHECKED : BST_UNCHECKED);
                    EnableWindow(GetDlgItem(hwnd, IDOK), r.ok_enabled);
                    SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Terms verified. Review them; acceptance is separate and unchecked.");
                }
            } else {
                SetDlgItemTextA(hwnd, IDC_REV_STATUS,
                    "Unable to load the cached terms. You may try downloading again or close this dialog.");
                EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), TRUE);
            }
            TermsFetchContext_Release(tctx);
            ctx->terms = NULL;
            return TRUE;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDC_BTN_REV_DOWNLOAD) {
                if (ctx && ctx->hGoEvent) {
                    SetEvent(ctx->hGoEvent); /* the worker performs START_REVIEW */
                    EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), FALSE);
                    EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_CANCEL), TRUE);
                    SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Starting download...");
                }
                return TRUE;
            } else if (id == IDC_BTN_REV_CANCEL) {
                if (ctx) {
                    InterlockedExchange(&ctx->stopped, 1);
                    if (ctx->hGoEvent) SetEvent(ctx->hGoEvent);
                    EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_CANCEL), FALSE);
                    SetDlgItemTextA(hwnd, IDC_REV_STATUS, "Stopping the download...");
                }
                return TRUE;
            } else if (id == IDC_REV_AGREE) {
                BOOL agreed = (IsDlgButtonChecked(hwnd, IDC_REV_AGREE) == BST_CHECKED);
                EnableWindow(GetDlgItem(hwnd, IDOK), agreed && ctx && ctx->terms_ok);
                return TRUE;
            } else if (id == IDOK) {
                if (ctx && ctx->comp && !ctx->comp->disabled &&
                    IsDlgButtonChecked(hwnd, IDC_REV_AGREE) == BST_CHECKED && ctx->terms_ok) {
                    if (!OobeAcceptTerms(ctx->comp, ctx->terms_sha256, true, true))
                        return TRUE;
                }
                EndDialog(hwnd, IDOK); /* for review-only rows this records nothing */
                return TRUE;
            } else if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY: {
            if (ctx) {
                InterlockedExchange(&ctx->cancelled, 1);
                InterlockedExchangePointer((void *volatile *)&ctx->hwnd, NULL);
                if (ctx->hGoEvent) SetEvent(ctx->hGoEvent);
                if (ctx->terms) {
                    TermsFetchContext *tctx = ctx->terms;
                    InterlockedExchange(&tctx->cancelled, 1);
                    InterlockedExchangePointer((void *volatile *)&tctx->hwnd, NULL);
                    TermsFetchContext_Release(tctx);
                    ctx->terms = NULL;
                }
                SetWindowLongPtrA(hwnd, DWLP_USER, 0);
                ReviewDownloadContext_Release(ctx);
            }
            break;
        }
    }
    return FALSE;
}

static void SetListCheckState(HWND hList, int idx, BOOL checked)
{
    LVITEMA lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
    lvi.stateMask = LVIS_STATEIMAGEMASK;
    SendMessageA(hList, LVM_SETITEMSTATE, (WPARAM)idx, (LPARAM)&lvi);
}

static void UpdateComponentDetails(HWND hwnd, int idx)
{
    char sbuf[64];
    char desc_buf[512];
    OobeComponentItem *comp;
    if (idx < 0 || idx >= g_ctx.component_count) return;
    comp = &g_ctx.components[idx];
    if (comp->disabled) {
        if (comp->review_available)
            snprintf(desc_buf, sizeof(desc_buf), "%s\r\n\r\n[DISABLED: %s]\r\nLicense terms can be reviewed with the \"Review terms...\" button; this component cannot be installed.",
                     comp->description, comp->disabled_reason);
        else
            snprintf(desc_buf, sizeof(desc_buf), "%s\r\n\r\n[DISABLED: %s]", comp->description, comp->disabled_reason);
    } else {
        snprintf(desc_buf, sizeof(desc_buf), "%s", comp->description);
    }
    SetDlgItemTextA(hwnd, IDC_COMP_DESC, desc_buf);
    snprintf(sbuf, sizeof(sbuf), "Size: %s", comp->size);
    SetDlgItemTextA(hwnd, IDC_COMP_SIZE, sbuf);
}

static void ReviewComponentTerms(HWND hwnd, int idx)
{
    HWND hList = GetDlgItem(hwnd, IDC_COMP_LIST);
    OobeComponentItem *comp;
    INT_PTR res;
    if (idx < 0 || idx >= g_ctx.component_count) return;
    comp = &g_ctx.components[idx];
    if (comp->review_available) {
        /* Review flow: reachable even for disabled components. Closing the
         * dialog never installs anything; for review-only rows it records no
         * acceptance and must not select the row for install either. */
        res = DialogBoxParamA(g_hInstance, MAKEINTRESOURCEA(IDD_OOBE_REVIEW), hwnd, OobeReviewProc, (LPARAM)comp);
        if (!comp->disabled) {
            if (res == IDOK && comp->accepted) {
                comp->checked = true;
                SetListCheckState(hList, idx, TRUE);
            } else {
                comp->checked = false;
                SetListCheckState(hList, idx, FALSE);
            }
        }
        SetFocus(hList);
        return;
    }
    if (comp->disabled) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s is disabled:\n%s", comp->title, comp->disabled_reason);
        MessageBoxA(hwnd, msg, "Component Unavailable", MB_OK | MB_ICONINFORMATION);
        SetListCheckState(hList, idx, FALSE);
        comp->checked = false;
        SetFocus(hList);
        return;
    }
    res = DialogBoxParamA(g_hInstance, MAKEINTRESOURCEA(IDD_OOBE_TERMS), hwnd, OobeTermsProc, (LPARAM)comp);
    if (res == IDOK && comp->accepted) {
        comp->checked = true;
        SetListCheckState(hList, idx, TRUE);
    } else {
        comp->checked = false;
        SetListCheckState(hList, idx, FALSE);
    }
    SetFocus(hList);
}

static void PopulateComponentsList(HWND hwnd)
{
    HWND hList = GetDlgItem(hwnd, IDC_COMP_LIST);
    char buffer[4096];
    DWORD needed = 0;
    DWORD ret;
    int i;
    LVCOLUMNA lvc;

    SendMessageA(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    memset(&lvc, 0, sizeof(lvc));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.cx = 280;
    lvc.pszText = (LPSTR)"Component";
    SendMessageA(hList, LVM_INSERTCOLUMNA, 0, (LPARAM)&lvc);

    if (g_ctx.component_count == 0) {
        ret = WexOobeGetComponents(buffer, sizeof(buffer), &needed);
        if (ret == ERROR_SUCCESS) {
            ParseCatalogBuffer(buffer, &g_ctx);
        }
    }

    SendMessageA(hList, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < g_ctx.component_count; ++i) {
        LVITEMA lvi;
        char item_text[160];
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = i;
        if (g_ctx.components[i].disabled) {
            snprintf(item_text, sizeof(item_text), "%s (Disabled)", g_ctx.components[i].title);
        } else {
            snprintf(item_text, sizeof(item_text), "%s", g_ctx.components[i].title);
        }
        lvi.pszText = item_text;
        lvi.lParam = i;
        SendMessageA(hList, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
        SetListCheckState(hList, i, g_ctx.components[i].checked ? TRUE : FALSE);
    }
    if (g_ctx.component_count > 0) {
        LVITEMA lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
        lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageA(hList, LVM_SETITEMSTATE, 0, (LPARAM)&lvi);
        UpdateComponentDetails(hwnd, 0);
    }
}

#define WM_OOBE_INSTALL_STATUS (WM_USER + 102)
#define WM_OOBE_INSTALL_PROMPT (WM_USER + 103)
#define WM_OOBE_INSTALL_DONE   (WM_USER + 104)

static InstallState *g_install_state = NULL;

static DWORD WINAPI InstallWorkerThread(LPVOID lpParam)
{
    InstallState *state = (InstallState *)lpParam;
    int i;

    for (i = 0; i < g_ctx.component_count; ++i) {
        OobeComponentItem *comp;
        char err_buf[256];
        DWORD ret;

        if (InterlockedCompareExchange(&state->cancelled, 0, 0)) break;

        comp = &g_ctx.components[i];
        if (comp->checked && !comp->installed) {
            state->current_idx = i;
            memset(err_buf, 0, sizeof(err_buf));
            snprintf(state->status_msg, sizeof(state->status_msg), "Installing %s... Please wait.", comp->title);
            if (state->hwnd) {
                PostMessageA(state->hwnd, WM_OOBE_INSTALL_STATUS, (WPARAM)i, state->generation);
            }

            ret = WexOobeInstall(comp->verb, comp->accepted_sha256, err_buf, sizeof(err_buf));
            if (InterlockedCompareExchange(&state->cancelled, 0, 0)) break;

            if (ret == ERROR_SUCCESS) {
                comp->installed = true;
                g_ctx.installed_count++;
            } else {
                g_ctx.failed_count++;
                snprintf(state->prompt_msg, sizeof(state->prompt_msg),
                         "Failed to install %s:\n%s\n\nClick Retry to try again, or Cancel to continue without this component.",
                         comp->title, err_buf[0] ? err_buf : "Installation error");
                ResetEvent(state->hPromptEvent);
                state->prompt_result = IDCANCEL;
                if (state->hwnd) {
                    PostMessageA(state->hwnd, WM_OOBE_INSTALL_PROMPT, (WPARAM)i, state->generation);
                } else {
                    break;
                }

                while (!InterlockedCompareExchange(&state->cancelled, 0, 0)) {
                    if (WaitForSingleObject(state->hPromptEvent, 200) == WAIT_OBJECT_0) break;
                }
                if (InterlockedCompareExchange(&state->cancelled, 0, 0)) break;

                if (state->prompt_result == IDRETRY) {
                    --i;
                    g_ctx.failed_count--;
                    continue;
                } else {
                    comp->checked = false;
                }
            }
        }
    }

    if (!InterlockedCompareExchange(&state->cancelled, 0, 0) && state->hwnd) {
        PostMessageA(state->hwnd, WM_OOBE_INSTALL_DONE, 0, state->generation);
    }

    InstallState_Release(state);
    return 0;
}

static INT_PTR CALLBACK OobeComponentsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG: {
            PopulateComponentsList(hwnd);
            return TRUE;
        }
        case WM_OOBE_INSTALL_STATUS: {
            if (!g_install_state || g_install_state->generation != (LONG)lParam) return TRUE;
            if (g_install_state && g_install_state->status_msg[0]) {
                SetDlgItemTextA(hwnd, IDC_COMP_STATUS, g_install_state->status_msg);
            }
            return TRUE;
        }
        case WM_OOBE_INSTALL_PROMPT: {
            if (!g_install_state || g_install_state->generation != (LONG)lParam) return TRUE;
            if (g_install_state) {
                int res = MessageBoxA(hwnd, g_install_state->prompt_msg, "Installation Failure", MB_RETRYCANCEL | MB_ICONEXCLAMATION);
                g_install_state->prompt_result = res;
                if (res != IDRETRY) {
                    SetListCheckState(GetDlgItem(hwnd, IDC_COMP_LIST), (int)wParam, FALSE);
                }
                SetEvent(g_install_state->hPromptEvent);
            }
            return TRUE;
        }
        case WM_OOBE_INSTALL_DONE: {
            if (!g_install_state || g_install_state->generation != (LONG)lParam) return TRUE;
            SetDlgItemTextA(hwnd, IDC_COMP_STATUS, "Installation finished.");
            EnableWindow(GetDlgItem(hwnd, IDC_COMP_LIST), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_TERMS), TRUE);
            SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
            if (g_install_state) {
                InstallState_Release(g_install_state);
                g_install_state = NULL;
            }
            PostMessageA(GetParent(hwnd), PSM_PRESSBUTTON, (WPARAM)PSBTN_NEXT, 0);
            return TRUE;
        }
        case WM_DESTROY: {
            if (g_install_state) {
                InterlockedExchange(&g_install_state->cancelled, 1);
                g_install_state->hwnd = NULL;
                SetEvent(g_install_state->hPromptEvent);
                InstallState_Release(g_install_state);
                g_install_state = NULL;
            }
            break;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDC_BTN_TERMS) {
                HWND hList = GetDlgItem(hwnd, IDC_COMP_LIST);
                int sel = (int)SendMessageA(hList, LVM_GETNEXTITEM, -1, MAKELPARAM(LVNI_SELECTED, 0));
                if (sel >= 0 && sel < g_ctx.component_count) {
                    ReviewComponentTerms(hwnd, sel);
                } else {
                    MessageBoxA(hwnd, "Please select a component from the list to review its terms.", "No Component Selected", MB_OK | MB_ICONINFORMATION);
                }
                return TRUE;
            }
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                if (g_install_state) {
                    SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, 0);
                } else {
                    SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                }
                return TRUE;
            } else if (pnmh->idFrom == IDC_COMP_LIST && pnmh->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                if (pnmv->uChanged & LVIF_STATE) {
                    if (pnmv->uNewState & LVIS_SELECTED) {
                        int idx = (int)pnmv->lParam;
                        UpdateComponentDetails(hwnd, idx);
                    }
                    if ((pnmv->uNewState & 0x3000) != (pnmv->uOldState & 0x3000)) {
                        BOOL checked = ((pnmv->uNewState & 0x2000) != 0);
                        int idx = pnmv->iItem;
                        if (idx >= 0 && idx < g_ctx.component_count) {
                            if (checked) {
                                if (g_ctx.components[idx].disabled) {
                                    char msg[320];
                                    if (g_ctx.components[idx].review_available)
                                        snprintf(msg, sizeof(msg), "%s is disabled and cannot be selected for install.\nYou can still review its license terms with the \"Review terms...\" button.",
                                                 g_ctx.components[idx].title);
                                    else
                                        snprintf(msg, sizeof(msg), "%s is disabled:\n%s",
                                                 g_ctx.components[idx].title, g_ctx.components[idx].disabled_reason);
                                    MessageBoxA(hwnd, msg, "Component Unavailable", MB_OK | MB_ICONINFORMATION);
                                    SetListCheckState(GetDlgItem(hwnd, IDC_COMP_LIST), idx, FALSE);
                                } else if (!g_ctx.components[idx].accepted && !g_ctx.components[idx].installed) {
                                    ReviewComponentTerms(hwnd, idx);
                                } else {
                                    g_ctx.components[idx].checked = true;
                                }
                            } else {
                                if (g_ctx.components[idx].installed) {
                                    SetListCheckState(GetDlgItem(hwnd, IDC_COMP_LIST), idx, TRUE);
                                } else {
                                    g_ctx.components[idx].checked = false;
                                }
                            }
                        }
                    }
                }
            } else if (pnmh->code == PSN_QUERYCANCEL && g_install_state) {
                MessageBoxA(hwnd, "An installer is still running. Please wait for it to finish safely before closing Setup.",
                            "Installation in progress", MB_OK | MB_ICONINFORMATION);
                SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, TRUE);
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                int i;
                bool any_to_install = false;
                if (g_install_state) {
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }
                for (i = 0; i < g_ctx.component_count; ++i) {
                    if (g_ctx.components[i].checked && !g_ctx.components[i].installed) {
                        if (!g_ctx.components[i].accepted) {
                            MessageBoxA(hwnd, "You must review and accept the vendor license agreement before installing selected components.", "Terms Acceptance Required", MB_OK | MB_ICONINFORMATION);
                            SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                            return TRUE;
                        }
                        any_to_install = true;
                    }
                }
                if (any_to_install) {
                    SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, 0);
                    EnableWindow(GetDlgItem(hwnd, IDC_COMP_LIST), FALSE);
                    EnableWindow(GetDlgItem(hwnd, IDC_BTN_TERMS), FALSE);
                    g_install_state = calloc(1, sizeof(InstallState));
                    if (!g_install_state) {
                        EnableWindow(GetDlgItem(hwnd, IDC_COMP_LIST), TRUE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BTN_TERMS), TRUE);
                        SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                        SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                        return TRUE;
                    }
                    g_install_state->ref_count = 2; /* 1 for UI, 1 for worker thread */
                    g_install_state->generation = InterlockedIncrement(&g_worker_generation);
                    g_install_state->hwnd = hwnd;
                    g_install_state->hPromptEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
                    if (g_install_state->hPromptEvent)
                        g_install_state->hThread = CreateThread(NULL, 0, InstallWorkerThread, g_install_state, 0, NULL);
                    if (!g_install_state->hThread) {
                        /* No worker owns its reserved reference on failure. */
                        InstallState_Release(g_install_state);
                        InstallState_Release(g_install_state);
                        g_install_state = NULL;
                        EnableWindow(GetDlgItem(hwnd, IDC_COMP_LIST), TRUE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BTN_TERMS), TRUE);
                        SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                        MessageBoxA(hwnd, "Failed to start installation worker thread.", "Installation Error", MB_OK | MB_ICONERROR);
                        SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                        return TRUE;
                    }
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }

                g_ctx.installed_count = 0;
                g_ctx.skipped_count = 0;
                g_ctx.declined_count = 0;
                for (i = 0; i < g_ctx.component_count; ++i) {
                    if (g_ctx.components[i].installed)
                        g_ctx.installed_count++;
                    else if (g_ctx.components[i].checked && !g_ctx.components[i].accepted)
                        g_ctx.declined_count++;
                    else
                        g_ctx.skipped_count++;
                }
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK OobeUsersProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            if (g_ctx.user_count > 0) {
                SetDlgItemTextA(hwnd, IDC_USER1, g_ctx.users[0].real_name[0] ? g_ctx.users[0].real_name : g_ctx.users[0].name);
            }
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                int ids[] = {IDC_USER1, IDC_USER2, IDC_USER3, IDC_USER4, IDC_USER5};
                int i;
                g_ctx.user_count = 0;
                for (i = 0; i < 5; ++i) {
                    char raw[128];
                    char clean[64];
                    GetDlgItemTextA(hwnd, ids[i], raw, sizeof(raw));
                    SanitizeUsername(raw, clean, sizeof(clean));
                    if (clean[0]) {
                        int idx = g_ctx.user_count++;
                        snprintf(g_ctx.users[idx].name, sizeof(g_ctx.users[idx].name), "%s", clean);
                        snprintf(g_ctx.users[idx].real_name, sizeof(g_ctx.users[idx].real_name), "%s", raw);
                        g_ctx.users[idx].is_admin = (idx == 0);
                    }
                }

                if (g_ctx.user_count == 0) {
                    MessageBoxA(hwnd, "Please enter at least one user name to use this computer.", "OOBE", MB_OK | MB_ICONEXCLAMATION);
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK OobePasswordProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                if (g_ctx.user_count > 0) {
                    char label[128];
                    snprintf(label, sizeof(label), "Setting password for user: %s", g_ctx.users[0].name);
                    SetDlgItemTextA(hwnd, IDC_PASS_USER_LABEL, label);
                }
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)(PSWIZB_BACK | PSWIZB_NEXT));
                return TRUE;
            } else if (pnmh->code == PSN_WIZNEXT) {
                char p1[128], p2[128];
                GetDlgItemTextA(hwnd, IDC_PASS_EDIT1, p1, sizeof(p1));
                GetDlgItemTextA(hwnd, IDC_PASS_EDIT2, p2, sizeof(p2));

                if (strcmp(p1, p2) != 0) {
                    MessageBoxA(hwnd, "The passwords do not match. Please re-enter your password.", "OOBE", MB_OK | MB_ICONEXCLAMATION);
                    SetWindowLongPtrA(hwnd, DWLP_MSGRESULT, -1);
                    return TRUE;
                }

                if (g_ctx.user_count > 0) {
                    snprintf(g_ctx.users[0].password, sizeof(g_ctx.users[0].password), "%s", p1);
                }
            }
            break;
        }
    }
    return FALSE;
}

static INT_PTR CALLBACK OobeFinishProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == PSN_SETACTIVE) {
                char summary[512];
                WCHAR summaryW[512];
                int offset = 0;
                offset += snprintf(summary + offset, sizeof(summary) - offset,
                    "Optional components summary:\r\n  • Installed: %d\r\n  • Skipped / Not selected: %d\r\n",
                    g_ctx.installed_count, g_ctx.skipped_count);
                if (g_ctx.declined_count > 0) {
                    offset += snprintf(summary + offset, sizeof(summary) - offset,
                        "  • Declined terms: %d\r\n", g_ctx.declined_count);
                }
                if (g_ctx.failed_count > 0) {
                    offset += snprintf(summary + offset, sizeof(summary) - offset,
                        "  • Failed: %d\r\n", g_ctx.failed_count);
                }
                MultiByteToWideChar(CP_UTF8, 0, summary, -1, summaryW, sizeof(summaryW) / sizeof(summaryW[0]));
                SetDlgItemTextW(hwnd, IDC_FINISH_SUMMARY, summaryW);
                SendMessageA(GetParent(hwnd), PSM_SETWIZBUTTONS, 0, (LPARAM)PSWIZB_FINISH);
                return TRUE;
            } else if (pnmh->code == PSN_WIZFINISH) {
                ProvisionUsers();
            }
            break;
        }
    }
    return FALSE;
}

static int CALLBACK OobeSheetCallback(HWND hwndDlg, UINT uMsg, LPARAM lParam)
{
    (void)lParam;
    if (uMsg == PSCB_INITIALIZED) {
        RECT rc;
        int w, h, x, y;
        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        if (screen_w < 800) screen_w = 1024;
        if (screen_h < 600) screen_h = 768;

        GetWindowRect(hwndDlg, &rc);
        w = rc.right - rc.left;
        h = rc.bottom - rc.top;
        x = (screen_w - w) / 2;
        y = (screen_h - h) / 2;
        if (x < 40) x = 40;
        if (y < 40) y = 40;
        SetWindowPos(hwndDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    }
    return 0;
}

static void ShowOobeWizard(HWND hParent)
{
    PROPSHEETPAGEA psp[7];
    PROPSHEETHEADERA psh;
    int page_count = 0;

    if (g_ctx.unattended && g_ctx.user_count > 0) {
        LogOobe("Unattended installation: provisioning configured users directly.");
        ProvisionUsers();
        return;
    }

    memset(psp, 0, sizeof(psp));

    #define ADD_OOBE_PAGE(id, proc) do { \
        psp[page_count].dwSize = sizeof(PROPSHEETPAGEA); \
        psp[page_count].dwFlags = PSP_USETITLE; \
        psp[page_count].hInstance = g_hInstance; \
        psp[page_count].pszTemplate = MAKEINTRESOURCEA(id); \
        psp[page_count].pfnDlgProc = proc; \
        psp[page_count].pszTitle = "Welcome"; \
        page_count++; \
    } while(0)

    ADD_OOBE_PAGE(IDD_OOBE_WELCOME,      OobeWelcomeProc);
    ADD_OOBE_PAGE(IDD_OOBE_UPDATES,      OobeUpdatesProc);
    ADD_OOBE_PAGE(IDD_OOBE_CONNECTIVITY, OobeConnectivityProc);
    ADD_OOBE_PAGE(IDD_OOBE_COMPONENTS,   OobeComponentsProc);
    ADD_OOBE_PAGE(IDD_OOBE_USERS,        OobeUsersProc);
    ADD_OOBE_PAGE(IDD_OOBE_PASSWORD,     OobePasswordProc);
    ADD_OOBE_PAGE(IDD_OOBE_FINISH,       OobeFinishProc);

    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERA);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_WIZARD | PSH_USECALLBACK;
    psh.pfnCallback = OobeSheetCallback;
    psh.hwndParent = hParent;
    psh.hInstance = g_hInstance;
    psh.pszCaption = "Welcome to Debian GNU/Linux with WineExplorer";
    psh.nPages = page_count;
    psh.nStartPage = 0;
    psh.ppsp = psp;

    PropertySheetA(&psh);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA wc;
    int screen_w, screen_h;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_hInstance = hInstance;
    InitCommonControls();

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.automatic_updates = true;

    LogOobe("=== WineExplorer OOBE (oobe.exe) Started ===");
    CheckUnattended();

    /* Register and create background window */
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = BackgroundWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "WexOobeBackground";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w < 800) screen_w = 1024;
    if (screen_h < 600) screen_h = 768;

    g_hBackground = CreateWindowExA(
        0, "WexOobeBackground", "Welcome to Debian GNU/Linux with WineExplorer",
        WS_POPUP | WS_VISIBLE, 0, 0, screen_w, screen_h,
        NULL, NULL, hInstance, NULL
    );

    UpdateWindow(g_hBackground);
    ShowOobeWizard(g_hBackground);

    DestroyWindow(g_hBackground);

    if (!g_ctx.provisioned) {
        LogOobe("OOBE wizard exited without completing user provisioning; preserving setup for retry.");
        return 1;
    }

    /* Signal completion to runner script */
    {
        FILE *fdone = fopen("Z:\\etc\\wineexplorer\\setup\\stage-oobe-done", "w");
        if (!fdone) fdone = fopen("/etc/wineexplorer/setup/stage-oobe-done", "w");
        if (fdone) {
            fprintf(fdone, "done\n");
            fclose(fdone);
        }
    }

    return 0;
}
