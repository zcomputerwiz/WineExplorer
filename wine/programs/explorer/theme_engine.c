/*
 * WineExplorer Desktop Environment - Theme Engine Implementation
 *
 * Implements complete theme loading and rendering compatible with Windows XP/2003
 * and earlier themes (.theme, .msstyles, system colors, wallpapers, desktop icons, sounds).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <mmsystem.h>
#include <uxtheme.h>

#include "wine/debug.h"
#include "theme_engine.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

static THEME_CONFIG current_theme;
static HWND main_window = NULL;

typedef HRESULT (WINAPI *pfnSetSystemVisualStyle)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD);

/* System color name to index mapping */
typedef struct _COLOR_MAP
{
    const WCHAR *name;
    int index;
} COLOR_MAP;

static const COLOR_MAP color_table[] =
{
    { L"Scrollbar",           COLOR_SCROLLBAR },
    { L"Background",          COLOR_BACKGROUND },
    { L"ActiveTitle",         COLOR_ACTIVECAPTION },
    { L"InactiveTitle",       COLOR_INACTIVECAPTION },
    { L"Menu",                COLOR_MENU },
    { L"Window",              COLOR_WINDOW },
    { L"WindowFrame",         COLOR_WINDOWFRAME },
    { L"MenuText",            COLOR_MENUTEXT },
    { L"WindowText",          COLOR_WINDOWTEXT },
    { L"TitleText",           COLOR_CAPTIONTEXT },
    { L"ActiveBorder",        COLOR_ACTIVEBORDER },
    { L"InactiveBorder",      COLOR_INACTIVEBORDER },
    { L"AppWorkspace",        COLOR_APPWORKSPACE },
    { L"Hilight",             COLOR_HIGHLIGHT },
    { L"HilightText",         COLOR_HIGHLIGHTTEXT },
    { L"ButtonFace",          COLOR_BTNFACE },
    { L"ButtonShadow",        COLOR_BTNSHADOW },
    { L"GrayText",            COLOR_GRAYTEXT },
    { L"ButtonText",          COLOR_BTNTEXT },
    { L"InactiveTitleText",   COLOR_INACTIVECAPTIONTEXT },
    { L"ButtonHilight",       COLOR_BTNHIGHLIGHT },
    { L"ButtonDkShadow",      COLOR_3DDKSHADOW },
    { L"ButtonLight",         COLOR_3DLIGHT },
    { L"InfoText",            COLOR_INFOTEXT },
    { L"InfoWindow",          COLOR_INFOBK },
    { L"HotTrackingColor",    COLOR_HOTLIGHT },
    { L"GradientActiveTitle", COLOR_GRADIENTACTIVECAPTION },
    { L"GradientInactiveTitle", COLOR_GRADIENTINACTIVECAPTION },
    { L"MenuHilight",         COLOR_MENUHILIGHT },
    { L"MenuBar",             COLOR_MENUBAR }
};

#define COLOR_TABLE_SIZE (sizeof(color_table) / sizeof(color_table[0]))

static void expand_theme_path(const WCHAR *src, WCHAR *dst, DWORD dst_len)
{
    WCHAR temp[MAX_PATH];
    WCHAR windir[MAX_PATH];
    WCHAR resdir[MAX_PATH];

    GetWindowsDirectoryW(windir, MAX_PATH);
    lstrcpyW(resdir, windir);
    lstrcatW(resdir, L"\\Resources");

    /* Handle common theme path variables */
    if (wcsstr(src, L"%ResourceDir%"))
    {
        const WCHAR *p = src + lstrlenW(L"%ResourceDir%");
        lstrcpyW(temp, resdir);
        lstrcatW(temp, p);
    }
    else if (wcsstr(src, L"%WinDir%"))
    {
        const WCHAR *p = src + lstrlenW(L"%WinDir%");
        lstrcpyW(temp, windir);
        if (*p != L'\\' && *p != L'/') lstrcatW(temp, L"\\");
        lstrcatW(temp, p);
    }
    else if (wcsstr(src, L"%SystemRoot%"))
    {
        const WCHAR *p = src + lstrlenW(L"%SystemRoot%");
        lstrcpyW(temp, windir);
        if (*p != L'\\' && *p != L'/') lstrcatW(temp, L"\\");
        lstrcatW(temp, p);
    }
    else
    {
        lstrcpynW(temp, src, MAX_PATH);
    }

    ExpandEnvironmentStringsW(temp, dst, dst_len);
}

static BOOL parse_rgb_string(const WCHAR *str, COLORREF *out_color)
{
    int r = 0, g = 0, b = 0;
    if (swscanf(str, L"%d %d %d", &r, &g, &b) == 3)
    {
        *out_color = RGB(r, g, b);
        return TRUE;
    }
    return FALSE;
}

static void parse_icon_entry(const WCHAR *val, WCHAR *out_path, int *out_idx)
{
    WCHAR expanded[MAX_PATH];
    WCHAR *comma;

    expand_theme_path(val, expanded, MAX_PATH);
    comma = wcsrchr(expanded, L',');
    if (comma)
    {
        *comma = L'\0';
        *out_idx = _wtoi(comma + 1);
    }
    else
    {
        *out_idx = 0;
    }
    lstrcpynW(out_path, expanded, MAX_PATH);
}

BOOL theme_load_file(const WCHAR *path, THEME_CONFIG *cfg)
{
    WCHAR buffer[MAX_PATH];
    unsigned int i;

    if (!path || !cfg) return FALSE;
    memset(cfg, 0, sizeof(*cfg));
    lstrcpynW(cfg->theme_path, path, MAX_PATH);

    /* Theme Display Name */
    GetPrivateProfileStringW(L"Theme", L"DisplayName", L"Classic Theme",
                             cfg->display_name, sizeof(cfg->display_name)/sizeof(WCHAR), path);

    /* [VisualStyles] */
    GetPrivateProfileStringW(L"VisualStyles", L"Path", L"",
                             buffer, MAX_PATH, path);
    if (buffer[0])
    {
        expand_theme_path(buffer, cfg->msstyles_path, MAX_PATH);
        GetPrivateProfileStringW(L"VisualStyles", L"ColorStyle", L"NormalColor",
                                 cfg->color_style, sizeof(cfg->color_style)/sizeof(WCHAR), path);
        GetPrivateProfileStringW(L"VisualStyles", L"Size", L"NormalSize",
                                 cfg->size_name, sizeof(cfg->size_name)/sizeof(WCHAR), path);
        cfg->has_visual_styles = TRUE;
        cfg->start_menu_style = THEME_STARTMENU_TWOCOLUMN;
    }
    else
    {
        cfg->has_visual_styles = FALSE;
        cfg->start_menu_style = THEME_STARTMENU_CLASSIC;
    }

    /* [Control Panel\Desktop] */
    GetPrivateProfileStringW(L"Control Panel\\Desktop", L"Wallpaper", L"",
                             buffer, MAX_PATH, path);
    expand_theme_path(buffer, cfg->wallpaper_path, MAX_PATH);
    cfg->tile_wallpaper = GetPrivateProfileIntW(L"Control Panel\\Desktop", L"TileWallpaper", 0, path);
    cfg->wallpaper_style = GetPrivateProfileIntW(L"Control Panel\\Desktop", L"WallpaperStyle",
                                                cfg->tile_wallpaper ? WALLPAPER_STYLE_TILE : WALLPAPER_STYLE_STRETCH, path);

    /* [Control Panel\Colors] */
    cfg->num_colors = 0;
    for (i = 0; i < COLOR_TABLE_SIZE; i++)
    {
        WCHAR color_val[64];
        GetPrivateProfileStringW(L"Control Panel\\Colors", color_table[i].name, L"",
                                 color_val, sizeof(color_val)/sizeof(WCHAR), path);
        if (color_val[0])
        {
            COLORREF cr;
            if (parse_rgb_string(color_val, &cr))
            {
                cfg->color_indices[cfg->num_colors] = color_table[i].index;
                cfg->color_values[cfg->num_colors] = cr;
                cfg->num_colors++;
            }
        }
    }
    cfg->has_custom_colors = (cfg->num_colors > 0);

    /* Desktop Icons */
    GetPrivateProfileStringW(L"CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\DefaultIcon",
                             L"DefaultValue", L"", buffer, MAX_PATH, path);
    if (buffer[0]) parse_icon_entry(buffer, cfg->icons.my_computer, &cfg->icons.my_computer_idx);

    GetPrivateProfileStringW(L"CLSID\\{450D8FBA-AD25-11D0-98A8-0800361B1103}\\DefaultIcon",
                             L"DefaultValue", L"", buffer, MAX_PATH, path);
    if (buffer[0]) parse_icon_entry(buffer, cfg->icons.my_documents, &cfg->icons.my_documents_idx);

    GetPrivateProfileStringW(L"CLSID\\{208D2C60-3AEA-1069-A2D7-08002B30309D}\\DefaultIcon",
                             L"DefaultValue", L"", buffer, MAX_PATH, path);
    if (buffer[0]) parse_icon_entry(buffer, cfg->icons.network_places, &cfg->icons.network_places_idx);

    GetPrivateProfileStringW(L"CLSID\\{645FF040-5081-101B-9F08-00AA002F954E}\\DefaultIcon",
                             L"full", L"", buffer, MAX_PATH, path);
    if (buffer[0]) parse_icon_entry(buffer, cfg->icons.recycle_full, &cfg->icons.recycle_full_idx);

    GetPrivateProfileStringW(L"CLSID\\{645FF040-5081-101B-9F08-00AA002F954E}\\DefaultIcon",
                             L"empty", L"", buffer, MAX_PATH, path);
    if (buffer[0]) parse_icon_entry(buffer, cfg->icons.recycle_empty, &cfg->icons.recycle_empty_idx);

    /* Sound events */
    GetPrivateProfileStringW(L"AppEvents\\Schemes\\Apps\\.Default\\WindowsLogon\\.Current",
                             L"DefaultValue", L"", buffer, MAX_PATH, path);
    expand_theme_path(buffer, cfg->sounds.logon, MAX_PATH);

    GetPrivateProfileStringW(L"AppEvents\\Schemes\\Apps\\.Default\\WindowsLogoff\\.Current",
                             L"DefaultValue", L"", buffer, MAX_PATH, path);
    expand_theme_path(buffer, cfg->sounds.logoff, MAX_PATH);

    GetPrivateProfileStringW(L"AppEvents\\Schemes\\Apps\\.Default\\SystemStart\\.Current",
                             L"DefaultValue", L"", buffer, MAX_PATH, path);
    expand_theme_path(buffer, cfg->sounds.startup, MAX_PATH);

    GetPrivateProfileStringW(L"AppEvents\\Schemes\\Apps\\.Default\\SystemExit\\.Current",
                             L"DefaultValue", L"", buffer, MAX_PATH, path);
    expand_theme_path(buffer, cfg->sounds.shutdown, MAX_PATH);

    TRACE("Loaded theme: %s (visual styles: %d, colors: %d, wallpaper: %s)\n",
          debugstr_w(cfg->display_name), cfg->has_visual_styles, cfg->num_colors, debugstr_w(cfg->wallpaper_path));

    return TRUE;
}

BOOL theme_apply(THEME_CONFIG *cfg)
{
    HMODULE huxtheme;
    pfnSetSystemVisualStyle pSetSystemVisualStyle;
    HKEY hkey;

    if (!cfg) return FALSE;

    /* 1. Apply visual style if requested and available */
    huxtheme = LoadLibraryW(L"uxtheme.dll");
    if (huxtheme)
    {
        pSetSystemVisualStyle = (pfnSetSystemVisualStyle)GetProcAddress(huxtheme, (LPCSTR)65);
        if (!pSetSystemVisualStyle)
            pSetSystemVisualStyle = (pfnSetSystemVisualStyle)GetProcAddress(huxtheme, "SetSystemVisualStyle");

        if (pSetSystemVisualStyle)
        {
            if (cfg->has_visual_styles && cfg->msstyles_path[0])
            {
                TRACE("Setting visual style: %s (%s, %s)\n",
                      debugstr_w(cfg->msstyles_path), debugstr_w(cfg->color_style), debugstr_w(cfg->size_name));
                pSetSystemVisualStyle(cfg->msstyles_path, cfg->color_style, cfg->size_name, 0);
            }
            else
            {
                TRACE("Disabling visual styles (Classic NT mode)\n");
                pSetSystemVisualStyle(NULL, NULL, NULL, 0);
            }
        }
        FreeLibrary(huxtheme);
    }

    /* 2. Apply system colors */
    if (cfg->has_custom_colors && cfg->num_colors > 0)
    {
        TRACE("Applying %d system colors\n", cfg->num_colors);
        SetSysColors(cfg->num_colors, cfg->color_indices, cfg->color_values);
    }

    /* 3. Apply wallpaper and registry style */
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS)
    {
        WCHAR style_str[16];
        WCHAR tile_str[16];

        swprintf(style_str, 16, L"%d", cfg->wallpaper_style);
        swprintf(tile_str, 16, L"%d", cfg->tile_wallpaper ? 1 : 0);

        RegSetValueExW(hkey, L"WallpaperStyle", 0, REG_SZ, (const BYTE*)style_str, (lstrlenW(style_str)+1)*sizeof(WCHAR));
        RegSetValueExW(hkey, L"TileWallpaper", 0, REG_SZ, (const BYTE*)tile_str, (lstrlenW(tile_str)+1)*sizeof(WCHAR));
        RegCloseKey(hkey);
    }

    if (cfg->wallpaper_path[0])
    {
        TRACE("Applying wallpaper: %s\n", debugstr_w(cfg->wallpaper_path));
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)cfg->wallpaper_path, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }

    /* 4. Update current theme state */
    memcpy(&current_theme, cfg, sizeof(THEME_CONFIG));

    /* 5. Broadcast changes to shell and all top-level windows */
    PostMessageW(HWND_BROADCAST, WM_SYSCOLORCHANGE, 0, 0);
    PostMessageW(HWND_BROADCAST, WM_THEMECHANGED, 0, 0);

    return TRUE;
}

THEME_CONFIG *theme_get_current(void)
{
    return &current_theme;
}

BOOL theme_is_visual_styles_active(void)
{
    return IsThemeActive() && current_theme.has_visual_styles;
}

void theme_play_sound(const WCHAR *event_name)
{
    const WCHAR *sound_file = NULL;

    if (!event_name) return;

    if (!wcscmp(event_name, L"Logon"))
        sound_file = current_theme.sounds.logon;
    else if (!wcscmp(event_name, L"Logoff"))
        sound_file = current_theme.sounds.logoff;
    else if (!wcscmp(event_name, L"Startup"))
        sound_file = current_theme.sounds.startup;
    else if (!wcscmp(event_name, L"Shutdown"))
        sound_file = current_theme.sounds.shutdown;

    if (sound_file && sound_file[0])
    {
        PlaySoundW(sound_file, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    }
}

void theme_init(HWND hwnd_main)
{
    WCHAR default_theme[MAX_PATH];
    WCHAR windir[MAX_PATH];

    main_window = hwnd_main;
    memset(&current_theme, 0, sizeof(current_theme));

    /* Set default Server 2003 Classic parameters */
    current_theme.start_menu_style = THEME_STARTMENU_TWOCOLUMN;
    current_theme.has_visual_styles = FALSE;

    /* Check if a theme is configured in registry */
    GetWindowsDirectoryW(windir, MAX_PATH);
    swprintf(default_theme, MAX_PATH, L"%s\\Resources\\Themes\\Classic.theme", windir);

    if (PathFileExistsW(default_theme))
    {
        THEME_CONFIG cfg;
        if (theme_load_file(default_theme, &cfg))
            theme_apply(&cfg);
    }
    else
    {
        /* Authentic Windows Server 2003 Classic color defaults */
        static const int def_indices[] = {
            COLOR_BACKGROUND,
            COLOR_BTNFACE,
            COLOR_BTNHIGHLIGHT,
            COLOR_BTNSHADOW,
            COLOR_3DDKSHADOW,
            COLOR_BTNTEXT,
            COLOR_WINDOW,
            COLOR_WINDOWTEXT,
            COLOR_ACTIVECAPTION,
            COLOR_CAPTIONTEXT,
            COLOR_INACTIVECAPTION,
            COLOR_INACTIVECAPTIONTEXT
        };
        static const COLORREF def_colors[] = {
            RGB(58, 110, 165),   /* Server 2003 Teal Desktop */
            RGB(212, 208, 200),  /* Standard Classic 3D Face */
            RGB(255, 255, 255),  /* Hilight */
            RGB(128, 128, 128),  /* Shadow */
            RGB(64, 64, 64),     /* Dark Shadow */
            RGB(0, 0, 0),        /* Button Text */
            RGB(255, 255, 255),  /* Window Background */
            RGB(0, 0, 0),        /* Window Text */
            RGB(10, 36, 106),    /* Active Caption Blue */
            RGB(255, 255, 255),  /* Active Caption Text */
            RGB(128, 128, 128),  /* Inactive Caption */
            RGB(212, 208, 200)   /* Inactive Caption Text */
        };

        SetSysColors(sizeof(def_indices)/sizeof(def_indices[0]), def_indices, def_colors);
    }
}

void theme_cleanup(void)
{
    if (current_theme.ht_taskbar) CloseThemeData(current_theme.ht_taskbar);
    if (current_theme.ht_taskband) CloseThemeData(current_theme.ht_taskband);
    if (current_theme.ht_startbutton) CloseThemeData(current_theme.ht_startbutton);
    if (current_theme.ht_tray) CloseThemeData(current_theme.ht_tray);
    if (current_theme.ht_clock) CloseThemeData(current_theme.ht_clock);
    memset(&current_theme, 0, sizeof(current_theme));
}

/* Authentic Classic 3D Bevel & Control Drawing Helpers */

void theme_draw_classic_bevel(HDC hdc, const RECT *rc, UINT edge, UINT flags)
{
    DrawEdge(hdc, (LPRECT)rc, edge, flags);
}

void theme_draw_sunken_well(HDC hdc, const RECT *rc)
{
    /* Classic 2-pixel sunken well (used in Server 2003 taskbar notification area and clock) */
    RECT r = *rc;
    DrawEdge(hdc, &r, BDR_SUNKENOUTER, BF_RECT | BF_ADJUST);
    DrawEdge(hdc, &r, BDR_SUNKENINNER, BF_RECT);
}

void theme_draw_raised_button(HDC hdc, const RECT *rc, BOOL pressed, BOOL focused)
{
    RECT r = *rc;
    HBRUSH face_brush = GetSysColorBrush(COLOR_BTNFACE);

    FillRect(hdc, &r, face_brush);

    if (pressed)
    {
        /* Sunken button edge */
        DrawEdge(hdc, &r, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
    }
    else
    {
        /* Raised 3D button edge */
        DrawEdge(hdc, &r, EDGE_RAISED, BF_RECT | BF_ADJUST);
    }

    if (focused)
    {
        InflateRect(&r, -1, -1);
        DrawFocusRect(hdc, &r);
    }
}
