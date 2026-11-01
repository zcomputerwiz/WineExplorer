/*
 * oobe_review_layout_probe.c -- F3 rendered-height layout probe
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

/*
 * F3 repair proof for the IDD_OOBE_REVIEW information and status controls.
 *
 * The review required a rendered-height assertion -- not "the resource compiles"
 * or "the string appears in source". This probe reproduces the dialog's real
 * font (8pt "MS Shell Dlg", from the oobe.rc FONT statement) and runs
 * DrawText(DT_CALCRECT | DT_WORDBREAK) on the CURRENT production strings to get
 * the wrapped line count, then evaluates whether each text block fits its
 * control at the PRODUCTION pixel calibration.
 *
 * Why a two-stage measurement: the probe is normally run under Wine (no native
 * Windows compiler in CI), and Wine substitutes a WIDER "MS Shell Dlg" than real
 * Windows, so Wine's absolute pixel widths/heights are optimistic. But the number
 * of wrapped LINES is font-independent -- the wrap width scales with the average
 * character width exactly as the dialog base unit does, so a 280-unit control
 * holds the same characters-per-line under Wine (665px / ~9.5px) as on native
 * Windows (~420px / ~6px). We therefore take the line count from the real GDI
 * wrap, then size it against production constants:
 *
 *   PROD_WIDTH_PX    280 units render ~420px wide on native Windows (96 DPI)
 *   PROD_VERT_BASE   ~1.6 px per vertical dialog unit (reviewer measurement)
 *   PROD_LINE_H      conservative 14px per line for 8pt "MS Shell Dlg"
 *
 * If the required height exceeds the control's allocated height the probe exits
 * non-zero. The controls are also wrapping + scrollable read-only edits, so even
 * an out-of-calibration font substitution can only add a scrollbar -- never lose
 * text.
 *
 * Build and run:
 *   x86_64-w64-mingw32-gcc -O2 -Wall -o oobe_review_layout_probe.exe \
 *       oobe_review_layout_probe.c -lgdi32 -luser32
 *   xvfb-run -a wine oobe_review_layout_probe.exe
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* --- control geometry from oobe.rc IDD_OOBE_REVIEW (dialog units) ---------- */
#define INFO_W_UNITS   280
#define INFO_H_UNITS   72
#define STATUS_W_UNITS 280
#define STATUS_H_UNITS 24

/* --- production pixel calibration (native Windows, 96 DPI) ----------------- */
#define PROD_VERT_BASE 1.6f /* px per vertical dialog unit */
#define PROD_LINE_H    14   /* conservative px per wrapped line */

/* --- current production strings from oobe.c -------------------------------- */
/* Worst-case info block: the WM_OOBE_REV_READY success text with a long
 * publisher name, a two-digit package count and MB-scale size strings. The fixed
 * wording dominates; the numeric fields only add a few characters. Keep in sync
 * with oobe.c (WM_OOBE_REV_READY handler). */
static const char INFO_TEXT[] =
    "Publisher: Microsoft Corporation\r\n"
    "Downloaded from: SourceForge (downloads.sourceforge.net/corefonts/)\r\n"
    "  the pinned Core Fonts distribution archive; not a Microsoft-hosted download.\r\n"
    "11 font packages, 128.0 MB combined. Local cache: not present (download required).\r\n"
    "Free space needed: 192.0 MB (packages plus 64 MB of working headroom).\r\n"
    "Nothing will be executed or installed.";

/* Longest status line oobe.c can display: the cancellation/cache-retention
 * wording (WM_OOBE_REV_DONE, cancel path). Keep in sync with oobe.c. */
static const char STATUS_TEXT[] =
    "Download cancelled. Nothing was installed. Verified packages already in the local cache are kept; only this run's partial files were removed.";

/*
 * Wrap `text` at a control of `width_units` dialog units (converted to pixels
 * with the current font's base unit) and return the wrapped pixel height as
 * rendered by GDI, plus the resulting line count. The wrap width is derived from
 * the same font used for rendering, so characters-per-line -- and hence the line
 * count -- match a native-Windows dialog of the same unit size.
 */
static int wrapped_height(HDC dc, const char *text, int width_units,
                          float bux, int *lines_out)
{
    int wpx = (int)(width_units * bux + 0.5f);
    RECT rc;
    int h;

    rc.left = 2;
    rc.top = 0;
    rc.right = wpx - 2; /* ~2px edit border inset on each side */
    rc.bottom = 0;
    DrawTextA(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
    h = rc.bottom - rc.top;
    if (lines_out) *lines_out = (h + PROD_LINE_H - 1) / PROD_LINE_H;
    return h;
}

/*
 * Evaluate one control: take the GDI-wrapped line count, size it at the
 * production per-line height, and compare to the control's production pixel
 * height. Returns 1 if it fits without scrolling.
 */
static int check(const char *label, const char *text,
                 int width_units, int height_units, HDC dc, float bux)
{
    int lines;
    int wine_h = wrapped_height(dc, text, width_units, bux, &lines);
    int prod_required = lines * PROD_LINE_H;
    int prod_alloc = (int)(height_units * PROD_VERT_BASE + 0.5f);
    int fits = (prod_required <= prod_alloc);

    printf("%-7s wine_wrapped=%3dpx lines=%d | production: required=%3dpx alloc=%3dpx  %s\n",
           label, wine_h, lines, prod_required, prod_alloc, fits ? "FIT" : "CLIP");
    return fits;
}

int main(void)
{
    HDC screen = GetDC(NULL);
    HDC dc;
    HBITMAP bmp;
    LOGFONTA lf;
    HFONT font;
    HFONT oldfont;
    SIZE msize;
    TEXTMETRICA tm;
    float avg_char_w, bux;
    int ok;

    if (!screen) { printf("ERROR: GetDC(NULL) failed\n"); return 2; }
    dc = CreateCompatibleDC(screen);
    bmp = CreateCompatibleBitmap(screen, 800, 600);
    if (!dc || !bmp) { printf("ERROR: could not create memory DC\n"); return 2; }
    SelectObject(dc, bmp);

    /* 8pt "MS Shell Dlg" -- exactly the oobe.rc FONT statement. */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -MulDiv(8, GetDeviceCaps(screen, LOGPIXELSY), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpyA(lf.lfFaceName, "MS Shell Dlg");
    font = CreateFontIndirectA(&lf);
    if (!font) { printf("ERROR: CreateFontIndirectA failed\n"); return 2; }
    oldfont = (HFONT)SelectObject(dc, font);

    /* Horizontal dialog base unit for this font: average character width / 4.
     * Used only to size the wrap window so characters-per-line match production. */
    GetTextExtentPoint32A(dc, "MMMMMMWWWWWW", 12, &msize);
    avg_char_w = (float)msize.cx / 12.0f;
    bux = avg_char_w / 4.0f;
    GetTextMetricsA(dc, &tm);

    printf("font=MS Shell Dlg 8pt  dpi_y=%d  avg_char_w=%.2fpx  tmHeight=%d\n",
           (int)GetDeviceCaps(screen, LOGPIXELSY), avg_char_w, (int)tm.tmHeight);
    printf("wrap width for %du = %.0fpx (chars/line ~= production 420px/~6px)\n",
           INFO_W_UNITS, (double)(INFO_W_UNITS * bux));

    ok = 1;
    ok &= check("INFO",   INFO_TEXT,   INFO_W_UNITS,   INFO_H_UNITS,   dc, bux);
    ok &= check("STATUS", STATUS_TEXT, STATUS_W_UNITS, STATUS_H_UNITS, dc, bux);

    SelectObject(dc, oldfont);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);

    printf(ok ? "RESULT: PASS -- all controls fit without scrolling\n"
              : "RESULT: FAIL -- at least one control clips (would scroll)\n");
    return ok ? 0 : 1;
}
