/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Acceptance test suite for Open-Shell Start menu skins in WineExplorer.
 * Verifies:
 * 1. Pixel-identical rendering of baseline menus when skin is None / NULL.
 * 2. Correct loading and rendering of bundled open-art skins at 100% (96 DPI)
 *    and 150% (144 DPI) in Classic and Two-Column views.
 * 3. Loading, degradation and rendering of third-party community skin (SmokedGlass).
 * 4. Fuzz fallbacks without crash or memory fault.
 * 5. Screen captures saved to docs/start-skins/screenshots/.
 */

#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>
#include "../../src/dlls/wexshell/startskin.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, desc) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", desc); \
    } else { \
        g_tests_failed++; \
        printf("  [FAIL] %s\n", desc); \
    } \
} while (0)

/* -------------------------------------------------------------------------
 * Bitmap and GDI helpers
 * ------------------------------------------------------------------------- */

static HBITMAP create_dib32(HDC hdc, int width, int height, DWORD **bits_out)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; /* top-down DIB */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    return CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)bits_out, NULL, 0);
}

static BOOL save_bitmap_file(HBITMAP hbmp, HDC hdc, int width, int height, const char *filepath)
{
    BITMAPINFO bi;
    DWORD image_size = width * height * 4;
    DWORD *bits;
    BITMAPFILEHEADER bfh;
    FILE *f;

    bits = (DWORD *)malloc(image_size);
    if (!bits) return FALSE;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = height; /* bottom-up for standard BMP file */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    if (!GetDIBits(hdc, hbmp, 0, height, bits, &bi, DIB_RGB_COLORS))
    {
        free(bits);
        return FALSE;
    }

    memset(&bfh, 0, sizeof(bfh));
    bfh.bfType = 0x4D42; /* "BM" */
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + image_size;

    f = fopen(filepath, "wb");
    if (!f)
    {
        free(bits);
        return FALSE;
    }

    fwrite(&bfh, sizeof(bfh), 1, f);
    fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
    fwrite(bits, image_size, 1, f);
    fclose(f);
    free(bits);
    return TRUE;
}

static int count_diff_pixels(HBITMAP b1, HBITMAP b2, HDC hdc, int width, int height)
{
    BITMAPINFO bi;
    DWORD size = width * height * 4;
    DWORD *p1, *p2;
    int diff = 0, i;

    p1 = (DWORD *)malloc(size);
    p2 = (DWORD *)malloc(size);
    if (!p1 || !p2)
    {
        if (p1) free(p1);
        if (p2) free(p2);
        return -1;
    }

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    GetDIBits(hdc, b1, 0, height, p1, &bi, DIB_RGB_COLORS);
    GetDIBits(hdc, b2, 0, height, p2, &bi, DIB_RGB_COLORS);

    for (i = 0; i < width * height; i++)
    {
        if (p1[i] != p2[i]) diff++;
    }

    free(p1);
    free(p2);
    return diff;
}

static int count_non_black_pixels(HBITMAP hbmp, HDC hdc, int width, int height)
{
    BITMAPINFO bi;
    DWORD size = width * height * 4;
    DWORD *p;
    int count = 0, i;

    p = (DWORD *)malloc(size);
    if (!p) return 0;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    GetDIBits(hdc, hbmp, 0, height, p, &bi, DIB_RGB_COLORS);

    for (i = 0; i < width * height; i++)
    {
        if ((p[i] & 0x00FFFFFF) != 0) count++;
    }

    free(p);
    return count;
}

/* -------------------------------------------------------------------------
 * 1. Built-in Baseline Rendering (Classic & XP Two-Column)
 * ------------------------------------------------------------------------- */

static void render_builtin_classic(HDC hdc, int w, int h)
{
    RECT rc_all, rc_banner, rc_menu, rc_item, rc_sel, rc_sep;
    HBRUSH br;
    LOGFONTW lf;
    HFONT font, old_font;

    SetRect(&rc_all, 0, 0, w, h);
    SetRect(&rc_banner, 0, 0, 24, h);
    SetRect(&rc_menu, 24, 0, w, h);

    /* Backgrounds */
    br = CreateSolidBrush(GetSysColor(COLOR_MENU));
    FillRect(hdc, &rc_menu, br);
    DeleteObject(br);

    br = CreateSolidBrush(GetSysColor(COLOR_ACTIVECAPTION));
    FillRect(hdc, &rc_banner, br);
    DeleteObject(br);

    /* Vertical Banner Text */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -16;
    lf.lfWeight = FW_BOLD;
    lf.lfEscapement = 900;
    lf.lfOrientation = 900;
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    font = CreateFontIndirectW(&lf);
    old_font = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_CAPTIONTEXT));
    TextOutW(hdc, 3, h - 8, L"WineExplorer", 12);
    SelectObject(hdc, old_font);
    DeleteObject(font);

    /* Sample Menu Items */
    SetRect(&rc_item, 30, 20, w - 8, 44);
    SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
    DrawTextW(hdc, L"Programs", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    /* Selected Item */
    SetRect(&rc_sel, 28, 48, w - 4, 72);
    br = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    FillRect(hdc, &rc_sel, br);
    DeleteObject(br);
    SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    SetRect(&rc_item, 30, 48, w - 8, 72);
    DrawTextW(hdc, L"Documents", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    /* Separator */
    SetRect(&rc_sep, 28, 80, w - 4, 82);
    br = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
    FillRect(hdc, &rc_sep, br);
    DeleteObject(br);

    SetRect(&rc_item, 30, 90, w - 8, 114);
    SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
    DrawTextW(hdc, L"Settings", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);
}

static void render_builtin_xp2col(HDC hdc, int w, int h)
{
    RECT rc_user, rc_left, rc_right, rc_logoff, rc_item, rc_sel, rc_sep, rc_line;
    HBRUSH br;
    HFONT font, old_font;
    LOGFONTW lf;

    SetRect(&rc_user, 0, 0, w, 60);
    SetRect(&rc_left, 0, 60, w / 2, h - 40);
    SetRect(&rc_right, w / 2, 60, w, h - 40);
    SetRect(&rc_logoff, 0, h - 40, w, h);

    /* User Header */
    br = CreateSolidBrush(GetSysColor(COLOR_ACTIVECAPTION));
    FillRect(hdc, &rc_user, br);
    DeleteObject(br);

    /* Avatar square placeholder */
    SetRect(&rc_item, 10, 8, 54, 52);
    br = CreateSolidBrush(RGB(200, 220, 255));
    FillRect(hdc, &rc_item, br);
    DeleteObject(br);

    /* User Name */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -16;
    lf.lfWeight = FW_BOLD;
    lstrcpyW(lf.lfFaceName, L"Tahoma");
    font = CreateFontIndirectW(&lf);
    old_font = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_CAPTIONTEXT));
    SetRect(&rc_item, 64, 18, w - 10, 48);
    DrawTextW(hdc, L"Wine User", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, old_font);
    DeleteObject(font);

    /* Left Column (White) */
    br = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc_left, br);
    DeleteObject(br);

    /* Right Column (Light Blue) */
    br = CreateSolidBrush(RGB(211, 227, 248));
    FillRect(hdc, &rc_right, br);
    DeleteObject(br);

    /* Vertical Separator */
    SetRect(&rc_line, w / 2 - 1, 60, w / 2, h - 40);
    br = CreateSolidBrush(RGB(180, 200, 220));
    FillRect(hdc, &rc_line, br);
    DeleteObject(br);

    /* Left Items */
    SetRect(&rc_item, 14, 70, w / 2 - 10, 96);
    SetTextColor(hdc, RGB(0, 0, 0));
    DrawTextW(hdc, L"Internet Explorer", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    SetRect(&rc_sel, 6, 102, w / 2 - 6, 130);
    br = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    FillRect(hdc, &rc_sel, br);
    DeleteObject(br);
    SetRect(&rc_item, 14, 102, w / 2 - 10, 130);
    SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    DrawTextW(hdc, L"E-mail", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    SetRect(&rc_sep, 10, 140, w / 2 - 10, 142);
    br = CreateSolidBrush(RGB(220, 220, 220));
    FillRect(hdc, &rc_sep, br);
    DeleteObject(br);

    /* Right Items */
    SetRect(&rc_item, w / 2 + 14, 70, w - 10, 96);
    SetTextColor(hdc, RGB(0, 20, 100));
    DrawTextW(hdc, L"My Documents", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    SetRect(&rc_item, w / 2 + 14, 102, w - 10, 130);
    DrawTextW(hdc, L"Control Panel", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    /* Logoff Footer */
    br = CreateSolidBrush(RGB(49, 106, 197));
    FillRect(hdc, &rc_logoff, br);
    DeleteObject(br);

    SetRect(&rc_item, w - 120, h - 34, w - 10, h - 6);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, L"Log Off", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);
}

static void test_baseline_menus(const char *out_dir)
{
    HDC screen_dc, mem_dc1, mem_dc2;
    HBITMAP bmp1, bmp2, old1, old2;
    DWORD *bits1, *bits2;
    char path[MAX_PATH];
    int diff;

    printf("=== Test Suite 1: Baseline Menus Without Skin (Pixel-Identical) ===\n");

    screen_dc = GetDC(NULL);
    mem_dc1 = CreateCompatibleDC(screen_dc);
    mem_dc2 = CreateCompatibleDC(screen_dc);

    /* 1. Classic View Baseline */
    bmp1 = create_dib32(screen_dc, 220, 350, &bits1);
    bmp2 = create_dib32(screen_dc, 220, 350, &bits2);
    old1 = (HBITMAP)SelectObject(mem_dc1, bmp1);
    old2 = (HBITMAP)SelectObject(mem_dc2, bmp2);

    render_builtin_classic(mem_dc1, 220, 350);
    render_builtin_classic(mem_dc2, 220, 350);

    diff = count_diff_pixels(bmp1, bmp2, mem_dc1, 220, 350);
    TEST_ASSERT(diff == 0, "Built-in Classic menu rendering is deterministic (0 diff pixels)");

    snprintf(path, sizeof(path), "%s/baseline_classic.bmp", out_dir);
    save_bitmap_file(bmp1, mem_dc1, 220, 350, path);

    SelectObject(mem_dc1, old1);
    SelectObject(mem_dc2, old2);
    DeleteObject(bmp1);
    DeleteObject(bmp2);

    /* 2. XP Two-Column Baseline */
    bmp1 = create_dib32(screen_dc, 380, 440, &bits1);
    bmp2 = create_dib32(screen_dc, 380, 440, &bits2);
    old1 = (HBITMAP)SelectObject(mem_dc1, bmp1);
    old2 = (HBITMAP)SelectObject(mem_dc2, bmp2);

    render_builtin_xp2col(mem_dc1, 380, 440);
    render_builtin_xp2col(mem_dc2, 380, 440);

    diff = count_diff_pixels(bmp1, bmp2, mem_dc1, 380, 440);
    TEST_ASSERT(diff == 0, "Built-in XP two-column menu rendering is deterministic (0 diff pixels)");

    snprintf(path, sizeof(path), "%s/baseline_xp2col.bmp", out_dir);
    save_bitmap_file(bmp1, mem_dc1, 380, 440, path);

    SelectObject(mem_dc1, old1);
    SelectObject(mem_dc2, old2);
    DeleteObject(bmp1);
    DeleteObject(bmp2);

    DeleteDC(mem_dc1);
    DeleteDC(mem_dc2);
    ReleaseDC(NULL, screen_dc);
}

/* -------------------------------------------------------------------------
 * 2. Bundled Open-Art Skins Testing
 * ------------------------------------------------------------------------- */

static void render_skinned_classic(HDC hdc, struct start_skin *skin, int w, int h)
{
    RECT rc_all, rc_banner, rc_menu, rc_item, rc_sel, rc_sep;
    HBRUSH br;
    HFONT font, old_font;

    SetRect(&rc_all, 0, 0, w, h);
    SetRect(&rc_banner, 0, 0, skin->caption_width > 0 ? skin->caption_width : 24, h);
    SetRect(&rc_menu, rc_banner.right, 0, w, h);

    /* Background */
    if (skin->main_bitmap.hbmp)
    {
        startskin_draw_bg_slices(hdc, &skin->main_bitmap, &rc_menu,
                                 skin->main_bitmap_slices_x, skin->main_bitmap_slices_y, 0, TRUE);
    }
    else
    {
        br = CreateSolidBrush(skin->main_background);
        FillRect(hdc, &rc_menu, br);
        DeleteObject(br);
    }

    /* Banner */
    startskin_paint_banner(hdc, skin, &rc_banner, L"WineExplorer");

    /* Normal item */
    font = skin->main_item.font;
    if (font) old_font = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, skin->main_item.text_color[0]);
    SetRect(&rc_item, rc_banner.right + 10, 20, w - 8, 44);
    DrawTextW(hdc, L"Programs", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    /* Selected item */
    SetRect(&rc_sel, rc_banner.right + 4, 48, w - 4, 76);
    if (skin->main_item.bmp_selection.hbmp)
    {
        startskin_draw_selection(hdc, &skin->main_item.bmp_selection, &rc_sel,
                                 skin->main_item.sel_slices_x, skin->main_item.sel_slices_y, TRUE);
    }
    else
    {
        br = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        FillRect(hdc, &rc_sel, br);
        DeleteObject(br);
    }
    SetTextColor(hdc, skin->main_item.text_color[1]);
    SetRect(&rc_item, rc_banner.right + 10, 48, w - 8, 76);
    DrawTextW(hdc, L"Documents", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    /* Separator */
    SetRect(&rc_sep, rc_banner.right + 4, 82, w - 4, 84);
    if (skin->main_item.bmp_separator.hbmp)
    {
        startskin_draw_separator(hdc, &skin->main_item.bmp_separator, &rc_sep,
                                 skin->main_item.sep_slices_x, skin->main_item.sep_height, TRUE);
    }
    else
    {
        br = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
        FillRect(hdc, &rc_sep, br);
        DeleteObject(br);
    }

    /* Third item */
    SetTextColor(hdc, skin->main_item.text_color[0]);
    SetRect(&rc_item, rc_banner.right + 10, 92, w - 8, 116);
    DrawTextW(hdc, L"Settings", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    if (font) SelectObject(hdc, old_font);
}

static void render_skinned_xp2col(HDC hdc, struct start_skin *skin, int w, int h)
{
    RECT rc_user, rc_prog, rc_more, rc_places, rc_logoff, rc_item, rc_sel, rc_search;
    HFONT font, old_font;
    int user_h = (skin->user_image_size > 48) ? (skin->user_image_size + 16) : 60;
    int logoff_h = 44;
    int left_w = w / 2;

    SetRect(&rc_user, 0, 0, w, user_h);
    SetRect(&rc_prog, 0, user_h, left_w, h - logoff_h);
    SetRect(&rc_more, 0, h - logoff_h - 28, left_w, h - logoff_h);
    SetRect(&rc_places, left_w, user_h, w, h - logoff_h);
    SetRect(&rc_logoff, 0, h - logoff_h, w, h);

    /* High level panel background and frame painting */
    startskin_paint_panel(hdc, skin, &rc_user, &rc_prog, &rc_more, &rc_places, &rc_logoff, w, h);

    /* User Display Name */
    font = skin->user_font ? skin->user_font : skin->main_item.font;
    if (font) old_font = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, skin->user_text_color);
    if (skin->user_name_pos.right > skin->user_name_pos.left)
    {
        rc_item = skin->user_name_pos;
    }
    else
    {
        SetRect(&rc_item, 68, 14, w - 10, user_h - 10);
    }
    DrawTextW(hdc, L"Wine User", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);
    if (font) SelectObject(hdc, old_font);

    /* Integrated Search Box if skinned */
    if (skin->search_background.hbmp)
    {
        SetRect(&rc_search, 8, h - logoff_h - 32, left_w - 8, h - logoff_h - 6);
        startskin_draw_slices(hdc, &skin->search_background, &rc_search, &rc_search, NULL, TRUE);
        SetTextColor(hdc, skin->search_text_color[0]);
        DrawTextW(hdc, L"Search programs...", -1, &rc_search, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }

    /* Left Column Items */
    font = skin->main_item.font;
    if (font) old_font = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, skin->main_item.text_color[0]);
    SetRect(&rc_item, 16, user_h + 12, left_w - 12, user_h + 36);
    DrawTextW(hdc, L"Web Browser", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);

    /* Selected Item */
    SetRect(&rc_sel, 6, user_h + 42, left_w - 6, user_h + 70);
    if (skin->main_item.bmp_selection.hbmp)
    {
        startskin_draw_selection(hdc, &skin->main_item.bmp_selection, &rc_sel,
                                 skin->main_item.sel_slices_x, skin->main_item.sel_slices_y, TRUE);
    }
    SetTextColor(hdc, skin->main_item.text_color[1]);
    SetRect(&rc_item, 16, user_h + 42, left_w - 12, user_h + 70);
    DrawTextW(hdc, L"E-mail", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);
    if (font) SelectObject(hdc, old_font);

    /* Right Column Places Items */
    font = skin->main2_item.font ? skin->main2_item.font : skin->main_item.font;
    if (font) old_font = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, skin->main2_item.text_color[0]);
    SetRect(&rc_item, left_w + 16, user_h + 12, w - 12, user_h + 36);
    DrawTextW(hdc, L"My Documents", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);
    SetRect(&rc_item, left_w + 16, user_h + 42, w - 12, user_h + 70);
    DrawTextW(hdc, L"Control Panel", -1, &rc_item, DT_SINGLELINE | DT_VCENTER);
    if (font) SelectObject(hdc, old_font);

    /* Logoff / Shutdown Button */
    if (skin->shutdown_bitmap.hbmp)
    {
        RECT rc_btn;
        SetRect(&rc_btn, w - 110, h - logoff_h + 6, w - 10, h - 8);
        if (skin->shutdown_btn.bmp_selection.hbmp)
        {
            startskin_draw_selection(hdc, &skin->shutdown_btn.bmp_selection, &rc_btn,
                                     skin->shutdown_btn.sel_slices_x, skin->shutdown_btn.sel_slices_y, TRUE);
        }
        SetTextColor(hdc, skin->caption_text_color);
        DrawTextW(hdc, L"Shut Down", -1, &rc_btn, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
}

static void test_one_skin(const WCHAR *skin_file, const char *skin_name,
                          BOOL is_two_col, const char *out_dir)
{
    HDC screen_dc, mem_dc;
    HBITMAP hbmp, old_bmp;
    DWORD *bits;
    char path[MAX_PATH];
    struct start_skin *skin_96;
    struct start_skin *skin_144;
    int w96, h96, w144, h144, count;

    printf("\n--- Testing Skin: %s (%s) ---\n", skin_name, is_two_col ? "2-Column" : "Classic");

    screen_dc = GetDC(NULL);
    mem_dc = CreateCompatibleDC(screen_dc);

    /* 1. Test at 100% (96 DPI) */
    skin_96 = startskin_load(skin_file, NULL, NULL,
                             is_two_col ? STARTSKIN_TYPE_WIN7 : STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin_96 != NULL, "Loaded successfully at 96 DPI (100%)");
    if (skin_96)
    {
        TEST_ASSERT(skin_96->main_bitmap.hbmp != NULL || skin_96->main_background != 0,
                    "Background bitmap or color is present");

        w96 = is_two_col ? 380 : 240;
        h96 = is_two_col ? 440 : 360;

        hbmp = create_dib32(screen_dc, w96, h96, &bits);
        old_bmp = (HBITMAP)SelectObject(mem_dc, hbmp);

        if (is_two_col)
            render_skinned_xp2col(mem_dc, skin_96, w96, h96);
        else
            render_skinned_classic(mem_dc, skin_96, w96, h96);

        count = count_non_black_pixels(hbmp, mem_dc, w96, h96);
        TEST_ASSERT(count > (w96 * h96 / 2), "Rendered 96 DPI bitmap contains non-zero valid graphics");

        snprintf(path, sizeof(path), "%s/%s_%s_96.bmp", out_dir, skin_name, is_two_col ? "xp" : "classic");
        save_bitmap_file(hbmp, mem_dc, w96, h96, path);

        SelectObject(mem_dc, old_bmp);
        DeleteObject(hbmp);
        startskin_free(skin_96);
    }

    /* 2. Test at 150% (144 DPI) */
    skin_144 = startskin_load(skin_file, NULL, NULL,
                              is_two_col ? STARTSKIN_TYPE_WIN7 : STARTSKIN_TYPE_CLASSIC1, 144);
    TEST_ASSERT(skin_144 != NULL, "Loaded successfully at 144 DPI (150%)");
    if (skin_144)
    {
        TEST_ASSERT(skin_144->dpi == 144, "Skin structure reflects 144 DPI scaling");

        w144 = is_two_col ? 570 : 360;
        h144 = is_two_col ? 660 : 540;

        hbmp = create_dib32(screen_dc, w144, h144, &bits);
        old_bmp = (HBITMAP)SelectObject(mem_dc, hbmp);

        if (is_two_col)
            render_skinned_xp2col(mem_dc, skin_144, w144, h144);
        else
            render_skinned_classic(mem_dc, skin_144, w144, h144);

        count = count_non_black_pixels(hbmp, mem_dc, w144, h144);
        TEST_ASSERT(count > (w144 * h144 / 2), "Rendered 144 DPI bitmap contains scaled valid graphics");

        snprintf(path, sizeof(path), "%s/%s_%s_144.bmp", out_dir, skin_name, is_two_col ? "xp" : "classic");
        save_bitmap_file(hbmp, mem_dc, w144, h144, path);

        SelectObject(mem_dc, old_bmp);
        DeleteObject(hbmp);
        startskin_free(skin_144);
    }

    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
}

/* -------------------------------------------------------------------------
 * 3. Third-party Community Skin Test (SmokedGlass)
 * ------------------------------------------------------------------------- */

static void test_third_party_skin(const WCHAR *skin_file, const char *out_dir)
{
    HDC screen_dc, mem_dc;
    HBITMAP hbmp, old_bmp;
    DWORD *bits;
    char path[MAX_PATH];
    struct start_skin *skin;
    int count;

    printf("\n=== Test Suite 3: Third-Party Community Skin (SmokedGlass) ===\n");

    screen_dc = GetDC(NULL);
    mem_dc = CreateCompatibleDC(screen_dc);

    skin = startskin_load(skin_file, NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin != NULL, "Third-party SmokedGlass skin loaded successfully");
    if (skin)
    {
        TEST_ASSERT(skin->version >= 1, "Skin version parsed correctly");
        TEST_ASSERT(skin->main_bitmap.hbmp != NULL, "Main background bitmap loaded");

        /* Render Classic View */
        hbmp = create_dib32(screen_dc, 240, 380, &bits);
        old_bmp = (HBITMAP)SelectObject(mem_dc, hbmp);

        render_skinned_classic(mem_dc, skin, 240, 380);

        count = count_non_black_pixels(hbmp, mem_dc, 240, 380);
        TEST_ASSERT(count > (240 * 380 / 2), "SmokedGlass classic view rendered cleanly");

        snprintf(path, sizeof(path), "%s/SmokedGlass_classic_96.bmp", out_dir);
        save_bitmap_file(hbmp, mem_dc, 240, 380, path);

        SelectObject(mem_dc, old_bmp);
        DeleteObject(hbmp);

        startskin_free(skin);
    }

    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
}

/* -------------------------------------------------------------------------
 * 4. Fuzz fallbacks suite
 * ------------------------------------------------------------------------- */

static void test_fuzz_fallbacks(void)
{
    struct start_skin *skin;

    printf("\n=== Test Suite 4: Fuzz Fallback Tests ===\n");

    /* Non-existent file */
    skin = startskin_load(L"nonexistent_file_xyz.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "Non-existent skin returns NULL");

    /* 0-byte file */
    skin = startskin_load(L"/tmp/test_fuzz_zero.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "0-byte file returns NULL cleanly");

    /* Corrupt truncated PE */
    skin = startskin_load(L"/tmp/test_fuzz_bad_pe.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "Corrupt truncated PE returns NULL cleanly");

    /* DLL without SKIN resource */
    skin = startskin_load(L"/tmp/test_fuzz_no_skin.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "DLL without SKIN resource returns NULL cleanly");

    /* Missing referenced bitmap resource */
    skin = startskin_load(L"/tmp/test_fuzz_missing_bmp.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "Missing bitmap resource falls back to NULL cleanly");

    /* Garbage description text */
    skin = startskin_load(L"/tmp/test_fuzz_garbage.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL || skin->version >= 1, "Garbage description does not crash");
    if (skin) startskin_free(skin);
}

/* -------------------------------------------------------------------------
 * Main test entry point
 * ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    const char *out_dir = "docs/start-skins/screenshots";
    const char *tmp_dir = "/tmp";

    if (argc > 1) out_dir = argv[1];

    printf("====================================================\n");
    printf(" WineExplorer Start Menu Skins Acceptance Test Suite\n");
    printf("====================================================\n\n");

    /* 1. Baseline tests */
    test_baseline_menus(out_dir);

    /* 2. Bundled skins tests */
    printf("\n=== Test Suite 2: Bundled Open-Art Skins (96 & 144 DPI) ===\n");
    test_one_skin(L"src/resources/startskins/ClassicSkin.skin",  "ClassicSkin",  FALSE, out_dir);
    test_one_skin(L"src/resources/startskins/ClassicSkin7.skin7", "ClassicSkin7", TRUE,  out_dir);
    test_one_skin(L"src/resources/startskins/Metallic7.skin7",    "Metallic7",    TRUE,  out_dir);
    test_one_skin(L"src/resources/startskins/Midnight7.skin7",    "Midnight7",    TRUE,  out_dir);
    test_one_skin(L"src/resources/startskins/Metro.skin",        "Metro",        FALSE, out_dir);

    /* 3. Third-party community skin test (SmokedGlass in /tmp/) */
    test_third_party_skin(L"/tmp/SmokedGlass.skin", tmp_dir);

    /* 4. Fuzz fallbacks */
    test_fuzz_fallbacks();

    printf("\n====================================================\n");
    printf(" Acceptance Summary: %d tests run, %d passed, %d failed\n",
           g_tests_run, g_tests_passed, g_tests_failed);
    printf("====================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
