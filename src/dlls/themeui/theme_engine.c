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
#include "wextheme.h"
#include "wexres.h"

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

BOOL theme_resolve_path(const WCHAR *in_path, WCHAR *out_path, DWORD out_len)
{
    WCHAR windir[MAX_PATH];
    WCHAR candidate[MAX_PATH];
    DWORD attrs;

    if (!in_path || !*in_path || !out_path || !out_len) return FALSE;

    /* Check if already a full/valid path */
    attrs = GetFileAttributesW(in_path);
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        GetFullPathNameW(in_path, out_len, out_path, NULL);
        return TRUE;
    }

    GetWindowsDirectoryW(windir, MAX_PATH);

    /* 1. %WinDir%\Resources\Themes\<Name>\<Name>.theme */
    swprintf(candidate, MAX_PATH, L"%ls\\Resources\\Themes\\%ls\\%ls.theme", windir, in_path, in_path);
    attrs = GetFileAttributesW(candidate);
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        lstrcpynW(out_path, candidate, out_len);
        return TRUE;
    }

    /* 2. %WinDir%\Resources\Themes\<Name>.theme */
    swprintf(candidate, MAX_PATH, L"%ls\\Resources\\Themes\\%ls.theme", windir, in_path);
    attrs = GetFileAttributesW(candidate);
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        lstrcpynW(out_path, candidate, out_len);
        return TRUE;
    }

    /* 3. If in_path already contains .theme: %WinDir%\Resources\Themes\<in_path> */
    swprintf(candidate, MAX_PATH, L"%ls\\Resources\\Themes\\%ls", windir, in_path);
    attrs = GetFileAttributesW(candidate);
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        lstrcpynW(out_path, candidate, out_len);
        return TRUE;
    }

    /* 4. Check user themes in %APPDATA%\Microsoft\Windows\Themes */
    {
        WCHAR appdata[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata)))
        {
            swprintf(candidate, MAX_PATH, L"%ls\\Microsoft\\Windows\\Themes\\%ls\\%ls.theme", appdata, in_path, in_path);
            attrs = GetFileAttributesW(candidate);
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                lstrcpynW(out_path, candidate, out_len);
                return TRUE;
            }

            swprintf(candidate, MAX_PATH, L"%ls\\Microsoft\\Windows\\Themes\\%ls.theme", appdata, in_path);
            attrs = GetFileAttributesW(candidate);
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                lstrcpynW(out_path, candidate, out_len);
                return TRUE;
            }

            swprintf(candidate, MAX_PATH, L"%ls\\Microsoft\\Windows\\Themes\\%ls", appdata, in_path);
            attrs = GetFileAttributesW(candidate);
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                lstrcpynW(out_path, candidate, out_len);
                return TRUE;
            }
        }
    }

    return FALSE;
}

BOOL theme_load_file(const WCHAR *path, THEME_CONFIG *cfg)
{
    WCHAR resolved[MAX_PATH];
    WCHAR buffer[MAX_PATH];
    unsigned int i;

    if (!path || !cfg) return FALSE;
    if (theme_resolve_path(path, resolved, MAX_PATH))
        path = resolved;
    else
    {
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        {
            if (GetFullPathNameW(path, MAX_PATH, resolved, NULL))
                path = resolved;
        }
    }

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
    BOOL has_cursors = FALSE;

    if (!sections) return;
    GetPrivateProfileSectionNamesW(sections, 32768, path);
    for (sec = sections; *sec; sec += lstrlenW(sec) + 1)
    {
        if (!wcsnicmp(sec, L"AppEvents\\", 10))
            apply_registry_section(path, sec, L"", TRUE);
        else if (!wcsnicmp(sec, L"CLSID\\", 6))
            apply_registry_section(path, sec, L"Software\\Classes", TRUE);
        else if (!wcsicmp(sec, L"Control Panel\\Cursors"))
        {
            apply_registry_section(path, sec, L"", TRUE);
            has_cursors = TRUE;
        }
        else if (!wcsicmp(sec, L"Control Panel\\Colors"))
            apply_registry_section(path, sec, L"", FALSE);
    }
    if (!has_cursors)
    {
        HKEY key;
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", 0, KEY_SET_VALUE, &key))
        {
            static const WCHAR * const cursor_names[] = {
                L"Arrow", L"Help", L"AppStarting", L"Wait", L"Crosshair", L"IBeam",
                L"NWPen", L"No", L"SizeNS", L"SizeWE", L"SizeNWSE", L"SizeNESW",
                L"SizeAll", L"UpArrow", L"Hand", L""
            };
            int i;
            for (i = 0; i < ARRAY_SIZE(cursor_names); i++)
                RegSetValueExW(key, cursor_names[i], 0, REG_SZ, (const BYTE *)L"", sizeof(WCHAR));
            RegCloseKey(key);
        }
    }
    HeapFree(GetProcessHeap(), 0, sections);
}

/* DWM frame style and colorization from the theme file (DWM P13) */
static void apply_dwm_settings(const WCHAR *path)
{
    WCHAR value[64];
    DWORD frame_style = 0;
    HKEY key;

    /* [WineExplorer] FrameStyle: "aero"/"1" = Vista/7 glass; "flat8"/"2" = opaque accent; else modern. */
    GetPrivateProfileStringW(L"WineExplorer", L"FrameStyle", L"", value, sizeof(value)/sizeof(WCHAR), path);
    if (!wcsicmp(value, L"aero") || !wcscmp(value, L"1")) frame_style = 1;
    else if (!wcsicmp(value, L"flat8") || !wcscmp(value, L"2")) frame_style = 2;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL))
    {
        RegSetValueExW(key, L"DwmFrameStyle", 0, REG_DWORD, (const BYTE *)&frame_style, sizeof(frame_style));
        RegCloseKey(key);
    }

    /* [VisualStyles] ColorizationColor: hex like "0X6B74B8FC"; leave the registry alone if missing or unparsable */
    GetPrivateProfileStringW(L"VisualStyles", L"ColorizationColor", L"", value, sizeof(value)/sizeof(WCHAR), path);
    if (value[0])
    {
        const WCHAR *start = value;
        DWORD color;
        WCHAR *end;

        if (value[0] == L'0' && (value[1] == L'x' || value[1] == L'X')) start += 2;
        color = wcstoul(start, &end, 16);
        if (end != start && *end == 0 &&
            !RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL))
        {
            RegSetValueExW(key, L"ColorizationColor", 0, REG_DWORD, (const BYTE *)&color, sizeof(color));
            RegCloseKey(key);
        }
    }
}

BOOL theme_apply(THEME_CONFIG *cfg)
{
    HMODULE huxtheme;
    HKEY hkey;

    if (!cfg) return FALSE;

    /* Reject an unavailable style before changing the active theme registry or
     * shell resources. Display Properties must not silently apply Classic. */
    if (cfg->has_visual_styles && cfg->msstyles_path[0])
    {
        HANDLE file = NULL;
        HRESULT hr = E_FAIL;
        huxtheme = LoadLibraryW(L"uxtheme.dll");
        if (huxtheme)
        {
            pfnOpenThemeFile open_file = (pfnOpenThemeFile)GetProcAddress(huxtheme, (LPCSTR)2);
            pfnCloseThemeFile close_file = (pfnCloseThemeFile)GetProcAddress(huxtheme, (LPCSTR)3);
            if (open_file && close_file)
            {
                hr = open_file(cfg->msstyles_path, cfg->color_style, cfg->size_name, &file, 0);
                if (SUCCEEDED(hr)) close_file(file);
            }
            FreeLibrary(huxtheme);
        }
        if (FAILED(hr)) return FALSE;
    }

    /* Remember the theme first (Windows keeps the active theme in HKCU\\...\\Themes) */
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, themes_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, NULL))
    {
        RegSetValueExW(hkey, L"CurrentTheme", 0, REG_SZ, (const BYTE *)cfg->theme_path, (lstrlenW(cfg->theme_path) + 1) * sizeof(WCHAR));
        RegCloseKey(hkey);
    }

    /* DWM settings before WM_THEMECHANGED, so windows see the new values when they repaint */
    apply_dwm_settings(cfg->theme_path);

    /* shell32's icons follow the theme's icon pack; written before WM_THEMECHANGED goes out, which reloads them */
    shellres_reset();
    shellres_sync_shell_icons();

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
    SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));

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

void theme_get_current(THEME_CONFIG *config)
{
    *config = current_theme;
}

BOOL theme_apply_file(const WCHAR *path)
{
    THEME_CONFIG config;
    WCHAR resolved[MAX_PATH];
    DWORD attrs;
    HWND tray;

    if (!path || !*path) return FALSE;
    if (theme_resolve_path(path, resolved, MAX_PATH))
        path = resolved;

    attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return FALSE;
    if (!theme_load_file(path, &config) || !theme_apply(&config)) return FALSE;
    /* Reload the shell after all registry sections and wallpaper are applied. */
    if ((tray = FindWindowW(L"Shell_TrayWnd", NULL))) PostMessageW(tray, WM_THEMECHANGED, 0, 0);
    return TRUE;
}

void theme_set_start_menu_style(int style)
{
    current_theme.start_menu_style = style;
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

void theme_refresh_wallpaper(void)
{
        DWORD size; WCHAR text[16];
        /* the registry, not SPI_GETDESKWALLPAPER: user32 keeps this process's value when another process sets it */
        size = MAX_PATH * sizeof(WCHAR);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"Wallpaper", RRF_RT_REG_SZ, NULL,
                         current_theme.wallpaper_path, &size))
            SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,current_theme.wallpaper_path,0);
        size=sizeof(text);
        if(!RegGetValueW(HKEY_CURRENT_USER,L"Control Panel\\Desktop",L"WallpaperStyle",RRF_RT_REG_SZ,NULL,text,&size)) current_theme.wallpaper_style=wcstol(text,NULL,10);
        size=sizeof(text);
        if(!RegGetValueW(HKEY_CURRENT_USER,L"Control Panel\\Desktop",L"TileWallpaper",RRF_RT_REG_SZ,NULL,text,&size)) current_theme.tile_wallpaper=wcstol(text,NULL,10)!=0;
}
