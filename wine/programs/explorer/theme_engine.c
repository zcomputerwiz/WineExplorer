/*
 * WineExplorer Desktop Environment - Theme Engine Implementation
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
void theme_cleanup(void);
static HWND main_window = NULL;

/* uxtheme's unnamed exports, as used by themeui.dll on Windows */
typedef HRESULT (WINAPI *pfnOpenThemeFile)(LPCWSTR, LPCWSTR, LPCWSTR, HANDLE *, DWORD);
typedef HRESULT (WINAPI *pfnCloseThemeFile)(HANDLE);
typedef HRESULT (WINAPI *pfnApplyTheme)(HANDLE, char *, HWND);

static const WCHAR themes_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes";


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

BOOL theme_load_file(const WCHAR *path, THEME_CONFIG *cfg)
{
    WCHAR buffer[MAX_PATH];
    unsigned int i;

    if (!path || !cfg) return FALSE;
    memset(cfg, 0, sizeof(*cfg));
    lstrcpynW(cfg->theme_path, path, MAX_PATH);

    /* Theme Display Name */
    GetPrivateProfileStringW(L"Theme", L"DisplayName", L"", buffer, MAX_PATH, path);
    if (buffer[0] != '@' || FAILED(SHLoadIndirectString(buffer, cfg->display_name, ARRAY_SIZE(cfg->display_name), NULL)))
        lstrcpynW(cfg->display_name, buffer[0] ? buffer : PathFindFileNameW(path), ARRAY_SIZE(cfg->display_name));

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

/* Write every "section = value" pair of the theme's registry-like sections under HKCU.  Theme files carry
 * AppEvents (sounds), CLSID (desktop icons) and Control Panel (cursors, desktop) data that Windows imports as-is. */
static void apply_registry_section(const WCHAR *path, const WCHAR *section, const WCHAR *root, BOOL expand_values)
{
    WCHAR *names = HeapAlloc(GetProcessHeap(), 0, 8192 * sizeof(WCHAR));
    WCHAR full[MAX_PATH], value[MAX_PATH], exp[MAX_PATH];
    const WCHAR *n;
    HKEY key;

    if (!names) return;
    if (root[0]) swprintf(full, MAX_PATH, L"%s\\%s", root, section);
    else lstrcpynW(full, section, MAX_PATH);
    if (GetPrivateProfileStringW(section, NULL, L"", names, 8192, path) &&
        !RegCreateKeyExW(HKEY_CURRENT_USER, full, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL))
    {
        for (n = names; *n; n += lstrlenW(n) + 1)
        {
            const WCHAR *name = n;
            GetPrivateProfileStringW(section, n, L"", value, MAX_PATH, path);
            if (!wcscmp(n, L"DefaultValue")) name = L"";
            else if (!wcsicmp(n, L"DefaultValue.MUI") || wcsstr(n, L".MUI")) continue;
            if (expand_values)
            {
                expand_theme_path(value, exp, MAX_PATH);
                RegSetValueExW(key, name, 0, REG_EXPAND_SZ, (const BYTE *)exp, (lstrlenW(exp) + 1) * sizeof(WCHAR));
            }
            else
                RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (lstrlenW(value) + 1) * sizeof(WCHAR));
        }
        RegCloseKey(key);
    }
    HeapFree(GetProcessHeap(), 0, names);
}

static void apply_registry_sections(const WCHAR *path)
{
    WCHAR *sections = HeapAlloc(GetProcessHeap(), 0, 32768 * sizeof(WCHAR));
    const WCHAR *sec;

    if (!sections) return;
    GetPrivateProfileSectionNamesW(sections, 32768, path);
    for (sec = sections; *sec; sec += lstrlenW(sec) + 1)
    {
        if (!wcsnicmp(sec, L"AppEvents\\", 10))
            apply_registry_section(path, sec, L"", TRUE);
        else if (!wcsnicmp(sec, L"CLSID\\", 6))
            apply_registry_section(path, sec, L"Software\\Classes", TRUE);
        else if (!wcsicmp(sec, L"Control Panel\\Cursors"))
            apply_registry_section(path, sec, L"", TRUE);
    }
    HeapFree(GetProcessHeap(), 0, sections);
}

BOOL theme_apply(THEME_CONFIG *cfg)
{
    HMODULE huxtheme;
    HKEY hkey;

    if (!cfg) return FALSE;

    /* Remember the theme first (Windows keeps the active theme in HKCU\\...\\Themes) */
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, themes_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, NULL))
    {
        RegSetValueExW(hkey, L"CurrentTheme", 0, REG_SZ, (const BYTE *)cfg->theme_path, (lstrlenW(cfg->theme_path) + 1) * sizeof(WCHAR));
        RegCloseKey(hkey);
    }

    /* 1. Visual style: open the msstyles file and make it the active style (uxtheme also applies the style's own
     *    colors, fonts and sizes and broadcasts WM_THEMECHANGED).  No style = Windows Classic. */
    huxtheme = LoadLibraryW(L"uxtheme.dll");
    if (huxtheme)
    {
        pfnOpenThemeFile pOpen = (pfnOpenThemeFile)GetProcAddress(huxtheme, (LPCSTR)2);
        pfnCloseThemeFile pClose = (pfnCloseThemeFile)GetProcAddress(huxtheme, (LPCSTR)3);
        pfnApplyTheme pApply = (pfnApplyTheme)GetProcAddress(huxtheme, (LPCSTR)4);

        if (pOpen && pClose && pApply)
        {
            HANDLE file = NULL;
            if (cfg->has_visual_styles && cfg->msstyles_path[0])
            {
                HRESULT hr = pOpen(cfg->msstyles_path, cfg->color_style, cfg->size_name, &file, 0);
                if (SUCCEEDED(hr))
                {
                    pApply(file, NULL, NULL);
                    pClose(file);
                }
                else
                {
                    WARN("could not open visual style %s: %#lx\n", debugstr_w(cfg->msstyles_path), hr);
                    cfg->has_visual_styles = FALSE;
                    pApply(NULL, NULL, NULL);
                }
            }
            else
                pApply(NULL, NULL, NULL);
        }
        FreeLibrary(huxtheme);
    }

    /* 2. System colors from the theme (the visual style supplies its own; Classic needs these) */
    if (cfg->has_custom_colors && cfg->num_colors > 0 && !cfg->has_visual_styles)
        SetSysColors(cfg->num_colors, cfg->color_indices, cfg->color_values);

    /* 3. Sounds, desktop icons and cursors */
    apply_registry_sections(cfg->theme_path);

    /* 4. Wallpaper */
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
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, cfg->wallpaper_path[0] ? (void *)cfg->wallpaper_path : (void *)L"",
                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

    memcpy(&current_theme, cfg, sizeof(THEME_CONFIG));
    return TRUE;
}

THEME_CONFIG *theme_get_current(void)
{
    return &current_theme;
}

BOOL theme_is_visual_styles_active(void)
{
    return IsThemeActive();
}

/* Taskbar rows as measured on Windows XP: the first row is 30 pixels with a visual style and 28 for Windows Classic,
 * every further row adds 30 (Classic 26) */
int theme_taskbar_first_row(void)
{
    return IsThemeActive() ? 30 : 28;
}

int theme_taskbar_row_pitch(void)
{
    return IsThemeActive() ? 30 : 26;
}

HTHEME theme_open(HWND hwnd, const WCHAR *classes)
{
    return IsThemeActive() ? OpenThemeData(hwnd, classes) : NULL;
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

/* The system colors are cached by every process; another process (explorer /theme) changed them in the registry.
 * Without a visual style the colors of the registry are the ones to show: take them over in this process. */
void theme_sync_colors(void)
{
    static const struct { const WCHAR *name; int index; } colors[] =
    {
        { L"Scrollbar", COLOR_SCROLLBAR }, { L"Background", COLOR_BACKGROUND }, { L"ActiveTitle", COLOR_ACTIVECAPTION },
        { L"InactiveTitle", COLOR_INACTIVECAPTION }, { L"Menu", COLOR_MENU }, { L"Window", COLOR_WINDOW },
        { L"WindowFrame", COLOR_WINDOWFRAME }, { L"MenuText", COLOR_MENUTEXT }, { L"WindowText", COLOR_WINDOWTEXT },
        { L"TitleText", COLOR_CAPTIONTEXT }, { L"ActiveBorder", COLOR_ACTIVEBORDER }, { L"InactiveBorder", COLOR_INACTIVEBORDER },
        { L"AppWorkSpace", COLOR_APPWORKSPACE }, { L"Hilight", COLOR_HIGHLIGHT }, { L"HilightText", COLOR_HIGHLIGHTTEXT },
        { L"ButtonFace", COLOR_BTNFACE }, { L"ButtonShadow", COLOR_BTNSHADOW }, { L"GrayText", COLOR_GRAYTEXT },
        { L"ButtonText", COLOR_BTNTEXT }, { L"InactiveTitleText", COLOR_INACTIVECAPTIONTEXT }, { L"ButtonHilight", COLOR_BTNHIGHLIGHT },
        { L"ButtonDkShadow", COLOR_3DDKSHADOW }, { L"ButtonLight", COLOR_3DLIGHT }, { L"InfoText", COLOR_INFOTEXT },
        { L"InfoWindow", COLOR_INFOBK }, 
        { L"HotTrackingColor", COLOR_HOTLIGHT }, { L"GradientActiveTitle", COLOR_GRADIENTACTIVECAPTION },
        { L"GradientInactiveTitle", COLOR_GRADIENTINACTIVECAPTION }, { L"MenuHilight", COLOR_MENUHILIGHT },
        { L"MenuBar", COLOR_MENUBAR },
    };
    static BOOL syncing;
    INT indices[ARRAY_SIZE(colors)];
    COLORREF values[ARRAY_SIZE(colors)];
    unsigned int i, n = 0;
    HKEY key;

    if (syncing || IsThemeActive()) return;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Colors", 0, KEY_READ, &key)) return;
    for (i = 0; i < ARRAY_SIZE(colors); i++)
    {
        WCHAR text[32];
        DWORD size = sizeof(text), r, g, b;
        WCHAR *end;

        if (!lstrcmpW(colors[i].name, L"ButtonAlternateFace")) continue;
        if (RegQueryValueExW(key, colors[i].name, NULL, NULL, (BYTE *)text, &size)) continue;
        r = wcstoul(text, &end, 10);
        g = wcstoul(end, &end, 10);
        b = wcstoul(end, &end, 10);
        indices[n] = colors[i].index;
        values[n++] = RGB(r, g, b);
    }
    RegCloseKey(key);
    if (!n) return;
    syncing = TRUE;
    SetSysColors(n, indices, values);
    syncing = FALSE;
}

/* read the active theme back from the registry, so the shell knows the wallpaper, icons and Start menu style */
void theme_reload(void)
{
    WCHAR path[MAX_PATH];
    DWORD size = sizeof(path);
    HKEY key;

    theme_cleanup();
    current_theme.start_menu_style = THEME_STARTMENU_CLASSIC;

    path[0] = 0;
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, themes_key, 0, KEY_QUERY_VALUE, &key))
    {
        if (RegQueryValueExW(key, L"CurrentTheme", NULL, NULL, (BYTE *)path, &size)) path[0] = 0;
        RegCloseKey(key);
    }
    if (path[0] && PathFileExistsW(path))
        theme_load_file(path, &current_theme);
    if (!IsThemeActive())
    {
        current_theme.has_visual_styles = FALSE;
        current_theme.start_menu_style = THEME_STARTMENU_CLASSIC;
    }
}

void theme_init(HWND hwnd_main)
{
    main_window = hwnd_main;
    /* uxtheme already activated the visual style in this process */
    theme_reload();
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
