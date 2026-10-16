/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Open-Shell Start menu skin loader and renderer for WineExplorer
 *
 * Portions based on Open-Shell-Menu / Classic Shell:
 *   Copyright (c) 2009-2017, Ivo Beltchev
 *   Copyright (c) 2017-2018, The Open-Shell Team
 *   SPDX-License-Identifier: MIT
 *
 * WineExplorer integration:
 *   Copyright 2026 WineExplorer Desktop Environment Project
 */

#ifndef __WEXSHELL_STARTSKIN_H
#define __WEXSHELL_STARTSKIN_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

enum start_skin_type
{
    STARTSKIN_TYPE_CLASSIC1 = 0, /* Single column Classic cascading menu */
    STARTSKIN_TYPE_CLASSIC2 = 1, /* Classic two-column menu */
    STARTSKIN_TYPE_WIN7     = 2, /* Windows 7 / modern two-column menu */
    STARTSKIN_TYPE_COUNT
};

enum start_skin_opacity
{
    STARTSKIN_OPACITY_SOLID,
    STARTSKIN_OPACITY_REGION,
    STARTSKIN_OPACITY_ALPHA,
    STARTSKIN_OPACITY_GLASS,
    STARTSKIN_OPACITY_FULLALPHA,
    STARTSKIN_OPACITY_FULLGLASS
};

enum start_skin_align
{
    STARTSKIN_ALIGN_LEFT,
    STARTSKIN_ALIGN_CENTER,
    STARTSKIN_ALIGN_RIGHT,
    STARTSKIN_ALIGN_NONE
};

enum start_skin_valign
{
    STARTSKIN_VALIGN_TOP,
    STARTSKIN_VALIGN_CENTER,
    STARTSKIN_VALIGN_BOTTOM,
    STARTSKIN_VALIGN_NONE
};

enum start_skin_icon_size
{
    STARTSKIN_ICON_SIZE_NONE  = 0,
    STARTSKIN_ICON_SIZE_SMALL = 16,
    STARTSKIN_ICON_SIZE_LARGE = 32
};

enum start_skin_option_type
{
    STARTSKIN_OPTION_BOOL = 0,
    STARTSKIN_OPTION_NUMBER,
    STARTSKIN_OPTION_STRING,
    STARTSKIN_OPTION_COLOR,
    STARTSKIN_OPTION_IMAGE,
    STARTSKIN_OPTION_GROUP
};

struct start_skin_bitmap
{
    HBITMAP hbmp;
    BOOL    is_32;    /* 32-bit ARGB with premultiplied alpha */
    int     width;
    int     height;
};

struct start_skin_item_settings
{
    HFONT                    font;
    BOOL                     own_font;
    COLORREF                 text_color[4]; /* 0: normal, 1: selected, 2: disabled, 3: disabled+selected */
    RECT                     text_padding;
    struct start_skin_bitmap bmp_selection;
    int                      sel_slices_x[6];
    int                      sel_slices_y[3];
    struct start_skin_bitmap bmp_separator;
    int                      sep_slices_x[3];
    int                      sep_height;
    RECT                     icon_padding;
};

struct start_skin_variation
{
    int   res_id;
    WCHAR label[64];
    WCHAR tip[128];
};

struct start_skin_option
{
    WCHAR                       name[64];
    enum start_skin_option_type type;
    WCHAR                       label[64];
    WCHAR                       tip[128];
    WCHAR                       condition[128];
    int                         group_id;
    BOOL                        def_value;
    BOOL                        value;
    WCHAR                       s_value[128];
};

struct start_skin
{
    HMODULE                  hmod;
    WCHAR                    name[64];
    WCHAR                    about[512];
    HICON                    about_icon;
    int                      version;
    enum start_skin_type     skin_type;
    int                      dpi;
    BOOL                     two_columns;
    BOOL                     main_thin_frame;

    /* Backgrounds */
    COLORREF                 main_background;
    COLORREF                 main2_background;
    struct start_skin_bitmap main_bitmap;
    int                      main_bitmap_slices_x[9];
    int                      main_bitmap_slices_y[6];
    enum start_skin_opacity  main_opacity;
    enum start_skin_opacity  main2_opacity;

    /* Margins & paddings */
    RECT                     main_padding;
    RECT                     main2_padding;
    enum start_skin_icon_size main_icon_size;
    enum start_skin_icon_size main2_icon_size;

    /* Item drawing styles */
    struct start_skin_item_settings main_item;
    struct start_skin_item_settings main2_item;

    /* Vertical column separator */
    struct start_skin_bitmap main_separator_v;
    int                      main_separator_width;
    int                      main_separator_slices_y[3];

    /* Classic vertical caption banner */
    COLORREF                 caption_background;
    COLORREF                 caption_text_color;
    HFONT                    caption_font;
    BOOL                     own_caption_font;
    struct start_skin_bitmap caption_bitmap;
    RECT                     caption_padding;
    int                      caption_width;

    /* User account tile & name */
    struct start_skin_bitmap user_bitmap;
    struct start_skin_bitmap user_mask;
    POINT                    user_frame_pos;
    POINT                    user_image_offset;
    int                      user_image_size;
    int                      user_image_alpha;
    RECT                     user_name_pos;
    enum start_skin_align    user_name_align;
    HFONT                    user_font;
    BOOL                     own_user_font;
    COLORREF                 user_text_color;

    /* Integrated search box */
    struct start_skin_bitmap search_background;
    int                      search_background_slices_x[9];
    int                      search_background_slices_y[9];
    RECT                     search_background_padding;
    RECT                     search_padding;
    BOOL                     search_frame;
    HFONT                    search_hint_font;
    BOOL                     own_search_hint_font;
    COLORREF                 search_text_color[2];
    COLORREF                 search_text_bg;

    /* Shutdown & Log Off buttons */
    struct start_skin_item_settings shutdown_btn;
    RECT                     shutdown_padding;
    struct start_skin_bitmap shutdown_bitmap;

    /* Submenus */
    struct start_skin_bitmap submenu_bitmap;
    int                      submenu_slices_x[6];
    int                      submenu_slices_y[3];
    COLORREF                 submenu_background;
    struct start_skin_item_settings submenu_item;
    RECT                     submenu_padding;
    int                      submenu_offset;
    BOOL                     submenu_thin_frame;

    /* Pager buttons & arrows */
    struct start_skin_bitmap submenu_pager;
    struct start_skin_bitmap submenu_pager_arrows;

    /* Metadata lists */
    struct start_skin_variation *variations;
    int                      variation_count;
    struct start_skin_option *options;
    int                      option_count;
};

/* Evaluates a boolean condition against an array of TRUE variable names.
 * Returns 1 for TRUE, 0 for FALSE, -1 for syntax error. */
int startskin_eval_condition(const WCHAR *condition, const WCHAR * const *values, int count);

/* Loads a skin DLL and parses its description and bitmaps.
 * Returns NULL on malformed/corrupt skin, missing required bitmaps, or file error. */
struct start_skin *startskin_load(const WCHAR *skin_path, const WCHAR *variation,
                                 const WCHAR *options_str, enum start_skin_type skin_type,
                                 int dpi);

/* Frees all resources associated with a loaded skin. */
void startskin_free(struct start_skin *skin);

/* 9-slice bitmap blitting helper */
void startskin_draw_slices(HDC hdc, const struct start_skin_bitmap *bmp,
                           const RECT *dst_rc, const RECT *src_rc,
                           const RECT *margins, BOOL alpha);

/* Draws main background slices (caption + main column, or 2 columns) */
void startskin_draw_bg_slices(HDC hdc, const struct start_skin_bitmap *bmp,
                              const RECT *dst_rc, const int *slices_x,
                              const int *slices_y, int col_idx, BOOL alpha);

/* Draws a 3-slice horizontal separator */
void startskin_draw_separator(HDC hdc, const struct start_skin_bitmap *bmp,
                              const RECT *dst_rc, const int *slices_x,
                              int height, BOOL alpha);

/* Draws a 9-slice item selection highlight */
void startskin_draw_selection(HDC hdc, const struct start_skin_bitmap *bmp,
                              const RECT *dst_rc, const int *slices_x,
                              const int *slices_y, BOOL alpha);

/* Enumerate available skins in standard system and user locations */
typedef void (*startskin_enum_cb)(const WCHAR *name, const WCHAR *path, BOOL is_7, void *param);
int startskin_enumerate(startskin_enum_cb callback, void *param);

#ifdef __cplusplus
}
#endif

#endif /* __WEXSHELL_STARTSKIN_H */
