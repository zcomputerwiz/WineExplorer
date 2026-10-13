/*
 * WineExplorer Desktop Environment - Theme Engine
 *
 * Implements complete theme loading and rendering compatible with Windows XP/2003
 * and earlier themes (.theme, .msstyles, system colors, wallpapers, desktop icons, sounds).
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

#ifndef __WINE_EXPLORER_THEME_ENGINE_H
#define __WINE_EXPLORER_THEME_ENGINE_H

#include <windows.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>

#define THEME_STARTMENU_CLASSIC   0  /* Authentic Server 2003 / 2000 cascading single-column */
#define THEME_STARTMENU_TWOCOLUMN 1  /* Windows XP / 2003 two-column style */
#define THEME_STARTMENU_MODERN    2  /* Open-Shell inspired modern style with search */

#define WALLPAPER_STYLE_CENTER   0
#define WALLPAPER_STYLE_TILE     1
#define WALLPAPER_STYLE_STRETCH  2
#define WALLPAPER_STYLE_FIT      6
#define WALLPAPER_STYLE_FILL    10

#define NUM_SYS_COLORS 31

typedef struct _THEME_SOUNDS
{
    WCHAR logon[MAX_PATH];
    WCHAR logoff[MAX_PATH];
    WCHAR startup[MAX_PATH];
    WCHAR shutdown[MAX_PATH];
    WCHAR recycle[MAX_PATH];
    WCHAR navigate[MAX_PATH];
    WCHAR asterisk[MAX_PATH];
    WCHAR exclamation[MAX_PATH];
    WCHAR critical_stop[MAX_PATH];
} THEME_SOUNDS;

typedef struct _THEME_CONFIG
{
    WCHAR display_name[128];
    WCHAR theme_path[MAX_PATH];

    /* [VisualStyles] */
    BOOL  has_visual_styles;
    WCHAR msstyles_path[MAX_PATH];
    WCHAR color_style[64];
    WCHAR size_name[64];

    /* [Control Panel\Desktop] */
    WCHAR wallpaper_path[MAX_PATH];
    BOOL  tile_wallpaper;
    int   wallpaper_style;

    /* [Control Panel\Colors] */
    BOOL     has_custom_colors;
    int      color_indices[NUM_SYS_COLORS];
    COLORREF color_values[NUM_SYS_COLORS];
    int      num_colors;

    /* Sounds */
    THEME_SOUNDS sounds;

    /* Desktop Icons */

    /* Start Menu & Taskbar Style */
    int start_menu_style;

    /* Runtime Theme handles */
    HTHEME ht_taskbar;
    HTHEME ht_taskband;
    HTHEME ht_startbutton;
    HTHEME ht_tray;
    HTHEME ht_clock;
} THEME_CONFIG;

/* Theme Engine API */
void theme_init(HWND hwnd_main);
void theme_reload(void);
void theme_sync_colors(void);
void theme_cleanup(void);

BOOL theme_load_file(const WCHAR *path, THEME_CONFIG *out_config);
BOOL theme_apply(THEME_CONFIG *config);
/* Copy current settings; callers never receive writable DLL state. */
void theme_get_current(THEME_CONFIG *config);
void theme_set_start_menu_style(int style);
void theme_refresh_wallpaper(void);
BOOL theme_apply_file(const WCHAR *path);

BOOL theme_is_visual_styles_active(void);
int theme_taskbar_first_row(void);   /* height of the first row of the taskbar */
int theme_taskbar_row_pitch(void);   /* every further row */
HTHEME theme_open(HWND hwnd, const WCHAR *classes); /* NULL when Windows Classic */
void theme_play_sound(const WCHAR *event_name);

/* Drawing helpers */
void theme_draw_classic_bevel(HDC hdc, const RECT *rc, UINT edge, UINT flags);
void theme_draw_sunken_well(HDC hdc, const RECT *rc);
void theme_draw_raised_button(HDC hdc, const RECT *rc, BOOL pressed, BOOL focused);

#endif /* __WINE_EXPLORER_THEME_ENGINE_H */
