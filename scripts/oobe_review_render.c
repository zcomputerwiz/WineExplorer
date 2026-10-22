/*
 * oobe_review_render.c -- F3 live-dialog render harness (test driver PE)
 *
 * Purpose: exercise the REAL IDD_OOBE_REVIEW dialog resource from oobe.rc under
 * Wine, with byte-identical production strings, and drive its controls
 * (scroll + Stop/cancel) so a screenshot shows readable text, long-term
 * scrolling, and the full cancellation wording. This is NOT the shipping
 * OobeReviewProc -- it is an isolated driver that links the same compiled
 * resource and reproduces the exact control geometry/strings the production
 * code sets (see oobe.c WM_OOBE_REV_READY / WM_OOBE_REV_DONE). The wire
 * transport itself is covered by scripts/test_oobe_bridge.py; here we prove the
 * rendered layout and control behaviour at two resolutions.
 *
 * Build (on the VM, mingw cross-compiler):
 *   x86_64-w64-mingw32-windres -I <oobe-src> -i oobe.rc -O coff -o render.res
 *   x86_64-w64-mingw32-gcc -O2 -Wall -I <oobe-src> \
 *       oobe_review_render.c render.res -o render.exe -luser32 -lgdi32
 * Run (bounded; the PE self-exits via timers, external `timeout` is a backstop):
 *   DISPLAY=:N WINEPREFIX=<fresh> WINEARCH=win64 timeout 30 wine render.exe
 */

#include <windows.h>
#include <stdio.h>
#include "resource.h"

/* --- byte-identical production strings (oobe.c) --------------------------- */

/* WM_OOBE_REV_READY success, not-cached path. Concrete worst case: the CoreFonts
 * row -- publisher "Microsoft Corporation", 11 packages, MB-scale sizes. The
 * fixed wording dominates; the numeric fields add only a few characters. Must
 * stay in sync with oobe.c (WM_OOBE_REV_READY handler) and the layout probe. */
static const char INFO_TEXT[] =
    "Publisher: Microsoft Corporation\r\n"
    "Downloaded from: SourceForge (downloads.sourceforge.net/corefonts/)\r\n"
    "  the pinned Core Fonts distribution archive; not a Microsoft-hosted download.\r\n"
    "11 font packages, 128.0 MB combined. Local cache: not present (download required).\r\n"
    "Free space needed: 192.0 MB (packages plus 64 MB of working headroom).\r\n"
    "Nothing will be executed or installed.";

/* WM_OOBE_REV_READY, not-cached status line (oobe.c ~line 678). */
static const char STATUS_NOTCACHED[] =
    "Click \"Download packages to review\" to authorize downloading the font packages for review. Nothing downloads before you do.";

/* WM_OOBE_REV_DONE, ERROR_CANCELLED status line (oobe.c ~line 714) -- the longest
 * production status string; it must fit/wrap in the 280x24-unit status control. */
static const char STATUS_CANCELLED[] =
    "Download cancelled. Nothing was installed. Verified packages already in the local cache are kept; only this run's partial files were removed.";

/* Long synthetic terms block (>10 lines) so IDC_REV_TERMS overflows its 280x88-unit
 * control and a vertical scrollbar is required -- proves long-term scrolling. */
static const char TERMS_TEXT[] =
    "MICROSOFT CORE FONTS FOR THE WEB -- LICENSE (SYNTHETIC, FOR LAYOUT ONLY)\r\n"
    "\r\n"
    "1. Grant of License. This license grants you a limited, non-exclusive,\r\n"
    "   non-transferable right to use the font files for internal review.\r\n"
    "2. Restrictions. You may not modify, redistribute, or create derivative\r\n"
    "   works from the font files without prior written permission.\r\n"
    "3. Review only. These packages are provided for license review; nothing in\r\n"
    "   this dialog installs, executes, or registers the fonts on the system.\r\n"
    "4. No warranty. The fonts are provided \"as is\", without warranty of any\r\n"
    "   kind, express or implied, including merchantability and fitness for a\r\n"
    "   particular purpose.\r\n"
    "5. Limitation of liability. In no event shall the copyright holder be\r\n"
    "   liable for any claim, damages, or other liability arising from the use\r\n"
    "   of these font files.\r\n"
    "6. Termination. This license terminates automatically if you fail to\r\n"
    "   comply with its terms.\r\n"
    "7. Governing law. This license is governed by the laws of the State of\r\n"
    "   Washington, without regard to conflict-of-law principles.\r\n"
    "8. Entire agreement. This license constitutes the entire agreement between\r\n"
    "   the parties and supersedes all prior or contemporaneous understandings.\r\n"
    "9. Severability. If any provision is held unenforceable, the remainder of\r\n"
    "   this license remains in full force and effect.\r\n"
    "10. Export. You must comply with all applicable export control laws.\r\n"
    "\r\n"
    "(End of synthetic terms block -- 24 lines total to force a scrollbar.)";

/* --- timers ---------------------------------------------------------------- */
#define TIMER_SCROLL  1   /* scroll IDC_REV_TERMS to its end (proves scrolling)  */
#define TIMER_STOP    2   /* drive the Stop button -> cancellation wording       */
#define TIMER_CLOSE   3   /* close so the PE exits on its own                    */

/* Phase marker: the host screenshot script polls C:\f3phase (== <prefix>/drive_c/
 * f3phase) to sync each capture against a UI state, decoupled from wine startup
 * latency on a loaded VM. Also echoed to stdout for the run log. */
static void write_phase(int phase)
{
    FILE *f = fopen("C:\\f3phase", "w");
    if (f) { fprintf(f, "%d", phase); fclose(f); }
    printf("PHASE %d\n", phase);
}

static INT_PTR CALLBACK RenderDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextA(hwnd, IDC_REV_INFO, INFO_TEXT);
        SetDlgItemTextA(hwnd, IDC_REV_STATUS, STATUS_NOTCACHED);
        SetDlgItemTextA(hwnd, IDC_REV_TERMS, TERMS_TEXT);
        /* Review-only row: the install-accept control is hidden (oobe.c line 628). */
        ShowWindow(GetDlgItem(hwnd, IDC_REV_AGREE), SW_HIDE);
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), TRUE);
        write_phase(1);   /* initial state: info + not-cached status + terms top */
        SetTimer(hwnd, TIMER_SCROLL, 4000, NULL);
        SetTimer(hwnd, TIMER_STOP,   8000, NULL);
        SetTimer(hwnd, TIMER_CLOSE, 12000, NULL);
        return TRUE;

    case WM_TIMER:
        if (wp == TIMER_SCROLL) {
            HWND t = GetDlgItem(hwnd, IDC_REV_TERMS);
            SendMessageA(t, EM_SETSEL, 0, -1);      /* select all -> caret at end */
            SendMessageA(t, EM_SCROLLCARET, 0, 0);  /* scroll the view to the end */
            write_phase(2);   /* terms scrolled to its end (bottom visible)       */
            KillTimer(hwnd, TIMER_SCROLL);
        } else if (wp == TIMER_STOP) {
            /* Drive Stop exactly as a click would; the WM_COMMAND handler below
             * applies the production ERROR_CANCELLED UI effect and marks phase 3. */
            PostMessageA(hwnd, WM_COMMAND,
                         MAKEWPARAM(IDC_BTN_REV_CANCEL, BN_CLICKED), 0);
        } else if (wp == TIMER_CLOSE) {
            EndDialog(hwnd, IDOK);
        }
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BTN_REV_CANCEL) {
            /* Production cancel path (oobe.c WM_OOBE_REV_DONE lines 705-716):
             * disable Stop, re-enable Download, show the full cancellation wording. */
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_CANCEL), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REV_DOWNLOAD), TRUE);
            SetDlgItemTextA(hwnd, IDC_REV_STATUS, STATUS_CANCELLED);
            write_phase(3);   /* Stop driven -> full cancellation status shown    */
            return TRUE;
        }
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            EndDialog(hwnd, LOWORD(wp));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    (void)hPrev; (void)cmd; (void)show;
    DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_OOBE_REVIEW), NULL, RenderDlgProc, 0);
    return 0;
}
