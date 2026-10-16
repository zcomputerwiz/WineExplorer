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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "startskin.h"

#define MAX_OP_STACK  32
#define MAX_VAL_STACK 32
#define MAX_COND_VARS 64

enum op_type
{
    OP_AND,
    OP_OR,
    OP_NOT,
    OP_PAR /* '(' */
};

/* Forward declarations */
static void free_item_settings(struct start_skin_item_settings *item);
static void free_skin_bitmap(struct start_skin_bitmap *bmp);

/* -------------------------------------------------------------------------
 * Boolean condition evaluation
 * ------------------------------------------------------------------------- */

static BOOL apply_operator(BOOL *val_stack, int *vsp, enum op_type op)
{
    switch (op)
    {
    case OP_AND:
        if (*vsp < 2) return FALSE;
        (*vsp)--;
        val_stack[*vsp - 1] = val_stack[*vsp - 1] && val_stack[*vsp];
        return TRUE;
    case OP_OR:
        if (*vsp < 2) return FALSE;
        (*vsp)--;
        val_stack[*vsp - 1] = val_stack[*vsp - 1] || val_stack[*vsp];
        return TRUE;
    case OP_NOT:
        if (*vsp < 1) return FALSE;
        val_stack[*vsp - 1] = !val_stack[*vsp - 1];
        return TRUE;
    default:
        return FALSE;
    }
}

int startskin_eval_condition(const WCHAR *condition, const WCHAR * const *values, int count)
{
    WCHAR token[128];
    enum op_type op_stack[MAX_OP_STACK];
    int osp = 0;
    BOOL val_stack[MAX_VAL_STACK];
    int vsp = 0;

    if (!condition || !*condition)
        return 1; /* Empty condition defaults to true */

    while (*condition)
    {
        while (*condition == L' ' || *condition == L'\t')
            condition++;
        if (!*condition)
            break;

        if (*condition == L'(')
        {
            if (osp >= MAX_OP_STACK) return -1;
            op_stack[osp++] = OP_PAR;
            condition++;
            continue;
        }

        if (*condition == L')')
        {
            BOOL found = FALSE;
            while (osp > 0)
            {
                osp--;
                if (op_stack[osp] == OP_PAR)
                {
                    found = TRUE;
                    break;
                }
                if (!apply_operator(val_stack, &vsp, op_stack[osp]))
                    return -1;
            }
            if (!found) return -1; /* Unmatched ')' */
            condition++;
            continue;
        }

        /* Extract token */
        {
            const WCHAR *end = condition;
            int len;

            while (*end && *end != L' ' && *end != L'\t' && *end != L'(' && *end != L')')
                end++;

            len = (int)(end - condition);
            if (len >= (int)(sizeof(token) / sizeof(token[0])))
                return -1;

            wcsncpy(token, condition, len);
            token[len] = L'\0';
            condition = end;
        }

        if (_wcsicmp(token, L"and") == 0 || _wcsicmp(token, L"or") == 0)
        {
            while (osp > 0 && op_stack[osp - 1] != OP_PAR)
            {
                osp--;
                if (!apply_operator(val_stack, &vsp, op_stack[osp]))
                    return -1;
            }
            if (osp >= MAX_OP_STACK) return -1;
            op_stack[osp++] = (token[0] == L'a' || token[0] == L'A') ? OP_AND : OP_OR;
        }
        else if (_wcsicmp(token, L"not") == 0)
        {
            while (osp > 0 && op_stack[osp - 1] == OP_NOT)
            {
                osp--;
                if (!apply_operator(val_stack, &vsp, op_stack[osp]))
                    return -1;
            }
            if (osp >= MAX_OP_STACK) return -1;
            op_stack[osp++] = OP_NOT;
        }
        else
        {
            BOOL val = FALSE;
            int i;

            if (vsp >= MAX_VAL_STACK) return -1;

            if (_wcsicmp(token, L"true") == 0 || _wcsicmp(token, L"1") == 0)
            {
                val = TRUE;
            }
            else if (_wcsicmp(token, L"false") == 0 || _wcsicmp(token, L"0") == 0)
            {
                val = FALSE;
            }
            else
            {
                for (i = 0; i < count; i++)
                {
                    if (values[i] && _wcsicmp(token, values[i]) == 0)
                    {
                        val = TRUE;
                        break;
                    }
                }
            }
            val_stack[vsp++] = val;
        }
    }

    while (osp > 0)
    {
        osp--;
        if (op_stack[osp] == OP_PAR) return -1; /* Unclosed '(' */
        if (!apply_operator(val_stack, &vsp, op_stack[osp]))
            return -1;
    }

    if (vsp != 1) return -1;
    return val_stack[0] ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Text parsing and line handling
 * ------------------------------------------------------------------------- */

struct skin_parser
{
    WCHAR **lines;
    int     line_count;
    int     line_capacity;
    WCHAR  *raw_buffer;
};

static void parser_init(struct skin_parser *parser)
{
    memset(parser, 0, sizeof(*parser));
}

static void parser_free(struct skin_parser *parser)
{
    int i;
    if (parser->lines)
    {
        for (i = 0; i < parser->line_count; i++)
            free(parser->lines[i]);
        free(parser->lines);
    }
    if (parser->raw_buffer)
        free(parser->raw_buffer);
    memset(parser, 0, sizeof(*parser));
}

static BOOL parser_add_line(struct skin_parser *parser, const WCHAR *line)
{
    WCHAR *copy;
    if (parser->line_count >= parser->line_capacity)
    {
        int new_cap = parser->line_capacity ? parser->line_capacity * 2 : 256;
        WCHAR **new_lines = (WCHAR **)realloc(parser->lines, new_cap * sizeof(WCHAR *));
        if (!new_lines) return FALSE;
        parser->lines = new_lines;
        parser->line_capacity = new_cap;
    }
    copy = _wcsdup(line);
    if (!copy) return FALSE;
    parser->lines[parser->line_count++] = copy;
    return TRUE;
}

static BOOL parser_load_text_buf(struct skin_parser *parser, const unsigned char *buf, int size)
{
    int wlen = 0;
    WCHAR *wbuf = NULL;
    WCHAR *p, *line_start;

    if (!buf || size < 2)
        return FALSE;

    /* UTF-16LE BOM check */
    if (buf[0] == 0xFF && buf[1] == 0xFE)
    {
        wlen = (size - 2) / sizeof(WCHAR);
        wbuf = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
        if (!wbuf) return FALSE;
        memcpy(wbuf, buf + 2, wlen * sizeof(WCHAR));
        wbuf[wlen] = L'\0';
    }
    /* UTF-8 BOM check */
    else if (size >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
    {
        wlen = MultiByteToWideChar(CP_UTF8, 0, (const char *)(buf + 3), size - 3, NULL, 0);
        wbuf = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
        if (!wbuf) return FALSE;
        MultiByteToWideChar(CP_UTF8, 0, (const char *)(buf + 3), size - 3, wbuf, wlen);
        wbuf[wlen] = L'\0';
    }
    else
    {
        /* Try UTF-8 first, fallback to ACP */
        wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)buf, size, NULL, 0);
        if (wlen > 0)
        {
            wbuf = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
            if (!wbuf) return FALSE;
            MultiByteToWideChar(CP_UTF8, 0, (const char *)buf, size, wbuf, wlen);
            wbuf[wlen] = L'\0';
        }
        else
        {
            wlen = MultiByteToWideChar(CP_ACP, 0, (const char *)buf, size, NULL, 0);
            wbuf = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
            if (!wbuf) return FALSE;
            MultiByteToWideChar(CP_ACP, 0, (const char *)buf, size, wbuf, wlen);
            wbuf[wlen] = L'\0';
        }
    }

    /* Split buffer into lines */
    p = wbuf;
    while (*p)
    {
        WCHAR *line_end;

        /* Skip leading whitespace */
        while (*p == L' ' || *p == L'\t')
            p++;

        if (*p == L'\r' || *p == L'\n')
        {
            p++;
            continue;
        }

        line_start = p;
        while (*p && *p != L'\r' && *p != L'\n')
            p++;

        line_end = p;
        while (line_end > line_start && (*(line_end - 1) == L' ' || *(line_end - 1) == L'\t'))
            line_end--;

        if (line_end > line_start && *line_start != L';')
        {
            WCHAR saved = *line_end;
            *line_end = L'\0';
            parser_add_line(parser, line_start);
            *line_end = saved;
        }

        while (*p == L'\r' || *p == L'\n')
            p++;
    }

    free(wbuf);
    return TRUE;
}

static BOOL parser_load_resource(struct skin_parser *parser, HMODULE hmod, int res_id)
{
    HRSRC hrsrc;
    HGLOBAL hglob;
    const unsigned char *data;
    DWORD size;

    if (!hmod) return FALSE;
    hrsrc = FindResourceW(hmod, MAKEINTRESOURCEW(res_id), L"SKIN");
    if (!hrsrc) return FALSE;

    hglob = LoadResource(hmod, hrsrc);
    if (!hglob) return FALSE;

    data = (const unsigned char *)LockResource(hglob);
    size = SizeofResource(hmod, hrsrc);
    if (!data || size == 0) return FALSE;

    return parser_load_text_buf(parser, data, (int)size);
}

static const WCHAR *parser_find_setting(const struct skin_parser *parser, const WCHAR *name)
{
    int i;
    size_t name_len = wcslen(name);

    /* Search from bottom to top so later lines override earlier ones */
    for (i = parser->line_count - 1; i >= 0; i--)
    {
        const WCHAR *line = parser->lines[i];
        if (_wcsnicmp(line, name, name_len) == 0)
        {
            const WCHAR *p = line + name_len;
            while (*p == L' ' || *p == L'\t')
                p++;
            if (*p != L'=')
                continue;
            p++;
            while (*p == L' ' || *p == L'\t')
                p++;
            return p;
        }
    }
    return NULL;
}

static void parser_filter_conditions(struct skin_parser *parser, const WCHAR * const *values, int count)
{
    BOOL enabled = TRUE;
    int i, write_idx = 0;

    for (i = 0; i < parser->line_count; i++)
    {
        WCHAR *line = parser->lines[i];
        if (line[0] == L'[')
        {
            WCHAR *end = wcschr(line, L']');
            if (end)
            {
                *end = L'\0';
                if (startskin_eval_condition(line + 1, values, count) == 1)
                    enabled = TRUE;
                else
                    enabled = FALSE;
                *end = L']';
            }
            free(line);
            continue;
        }

        if (enabled)
        {
            parser->lines[write_idx++] = line;
        }
        else
        {
            free(line);
        }
    }
    parser->line_count = write_idx;
}

/* -------------------------------------------------------------------------
 * Value and property parsers
 * ------------------------------------------------------------------------- */

static int scale_skin_element(int val, int scale_pct, int dpi)
{
    int ddpi = dpi - 96;
    if (ddpi <= 0 || scale_pct <= 0)
        return val;
    if (val < 0)
        return (val * (9600 + ddpi * scale_pct) - 4800) / 9600;
    else
        return (val * (9600 + ddpi * scale_pct) + 4800) / 9600;
}

static const struct
{
    const WCHAR *name;
    int          sys_idx;
} g_system_colors[] = {
    { L"SystemScrollbar",          COLOR_SCROLLBAR },
    { L"SystemBackground",         COLOR_BACKGROUND },
    { L"SystemActiveCaption",      COLOR_ACTIVECAPTION },
    { L"SystemInactiveCaption",    COLOR_INACTIVECAPTION },
    { L"SystemMenu",               COLOR_MENU },
    { L"SystemWindow",             COLOR_WINDOW },
    { L"SystemWindowFrame",        COLOR_WINDOWFRAME },
    { L"SystemMenuText",           COLOR_MENUTEXT },
    { L"SystemWindowText",         COLOR_WINDOWTEXT },
    { L"SystemCaptionText",        COLOR_CAPTIONTEXT },
    { L"SystemActiveBorder",       COLOR_ACTIVEBORDER },
    { L"SystemInactiveBorder",     COLOR_INACTIVEBORDER },
    { L"SystemAppWorkspace",       COLOR_APPWORKSPACE },
    { L"SystemHighlight",          COLOR_HIGHLIGHT },
    { L"SystemHighlightText",      COLOR_HIGHLIGHTTEXT },
    { L"SystemBtnFace",            COLOR_BTNFACE },
    { L"SystemBtnShadow",          COLOR_BTNSHADOW },
    { L"SystemGrayText",           COLOR_GRAYTEXT },
    { L"SystemBtnText",            COLOR_BTNTEXT },
    { L"SystemInactiveCaptionText", COLOR_INACTIVECAPTIONTEXT },
    { L"SystemBtnHighlight",       COLOR_BTNHIGHLIGHT },
    { L"System3DDKShadow",         COLOR_3DDKSHADOW },
    { L"System3DLight",            COLOR_3DLIGHT },
    { L"SystemInfoText",           COLOR_INFOTEXT },
    { L"SystemInfoBk",             COLOR_INFOBK }
};

static COLORREF parse_color(const WCHAR *str, COLORREF default_color)
{
    size_t i;
    if (!str || !*str)
        return default_color;

    if (str[0] == L'#')
    {
        unsigned long val = wcstoul(str + 1, NULL, 16);
        return RGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
    }

    if (str[0] == L'$')
    {
        const WCHAR *name = str + 1;
        for (i = 0; i < sizeof(g_system_colors) / sizeof(g_system_colors[0]); i++)
        {
            if (_wcsicmp(name, g_system_colors[i].name) == 0)
                return GetSysColor(g_system_colors[i].sys_idx);
        }
    }

    return default_color;
}

static int parse_numbers(const WCHAR *str, int *out, int max_count, int dpi)
{
    int count = 0;
    const WCHAR *p = str;

    if (!str || !out) return 0;

    while (*p && count < max_count)
    {
        WCHAR token[64];
        int tlen = 0;
        int val = 0;
        int scale = 0;

        while (*p == L' ' || *p == L'\t' || *p == L',')
            p++;
        if (!*p) break;

        while (*p && *p != L',' && *p != L' ' && *p != L'\t' && tlen < 63)
            token[tlen++] = *p++;
        token[tlen] = L'\0';

        if (token[0] == L'#')
        {
            unsigned long num = wcstoul(token + 1, NULL, 16);
            val = (int)RGB((num >> 16) & 0xFF, (num >> 8) & 0xFF, num & 0xFF);
        }
        else if (token[0] == L'$')
        {
            val = (int)parse_color(token, RGB(0, 0, 0));
        }
        else
        {
            val = _wtoi(token);
        }

        /* Check for trailing scale percentage (e.g., 100%) in subsequent token or within */
        if (*p == L',')
        {
            const WCHAR *lookahead = p + 1;
            while (*lookahead == L' ' || *lookahead == L'\t')
                lookahead++;
            if (*lookahead >= L'0' && *lookahead <= L'9')
            {
                const WCHAR *pct = lookahead;
                while (*pct >= L'0' && *pct <= L'9')
                    pct++;
                if (*pct == L'%')
                {
                    scale = _wtoi(lookahead);
                    p = pct + 1;
                }
            }
        }

        if (scale > 0)
            val = scale_skin_element(val, scale, dpi);

        out[count++] = val;
    }
    return count;
}

static HFONT parse_font(const WCHAR *str, const WCHAR *def_face, int def_size, int def_weight, int dpi)
{
    WCHAR face[64];
    int size = def_size;
    int weight = def_weight;
    BOOL italic = FALSE;
    int scale = 100;

    wcsncpy(face, def_face ? def_face : L"Segoe UI", 63);
    face[63] = L'\0';

    if (str && *str)
    {
        const WCHAR *p = str;
        WCHAR token[64];
        int tlen;

        /* Font name */
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'"')
        {
            p++;
            tlen = 0;
            while (*p && *p != L'"' && tlen < 63)
                face[tlen++] = *p++;
            face[tlen] = L'\0';
            if (*p == L'"') p++;
        }
        else
        {
            tlen = 0;
            while (*p && *p != L',' && *p != L' ' && *p != L'\t' && tlen < 63)
                face[tlen++] = *p++;
            face[tlen] = L'\0';
        }

        while (*p == L' ' || *p == L'\t' || *p == L',') p++;

        /* Weight / style */
        if (*p)
        {
            tlen = 0;
            while (*p && *p != L',' && *p != L' ' && *p != L'\t' && tlen < 63)
                token[tlen++] = *p++;
            token[tlen] = L'\0';

            if (_wcsicmp(token, L"bold") == 0)
                weight = FW_BOLD;
            else if (_wcsicmp(token, L"italic") == 0)
                italic = TRUE;
            else if (_wcsicmp(token, L"bold_italic") == 0)
            {
                weight = FW_BOLD;
                italic = TRUE;
            }
            else
                weight = FW_NORMAL;
        }

        while (*p == L' ' || *p == L'\t' || *p == L',') p++;

        /* Size */
        if (*p)
        {
            tlen = 0;
            while (*p && *p != L',' && *p != L' ' && *p != L'\t' && tlen < 63)
                token[tlen++] = *p++;
            token[tlen] = L'\0';
            size = _wtoi(token);
        }

        while (*p == L' ' || *p == L'\t' || *p == L',') p++;

        /* Scale */
        if (*p)
        {
            scale = _wtoi(p);
            if (scale <= 0) scale = 100;
        }
    }

    if (dpi > 96 && scale > 0)
        size = scale_skin_element(size, scale, dpi);

    return CreateFontW(-MulDiv(size, dpi, 72), 0, 0, 0, weight,
                       italic, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       face);
}

/* -------------------------------------------------------------------------
 * Bitmap loading and 32-bit alpha detection
 * ------------------------------------------------------------------------- */

static BOOL load_skin_bitmap(HMODULE hmod, int res_id, struct start_skin_bitmap *out_bmp)
{
    HBITMAP hbmp = NULL;
    BITMAP bm;
    int n, i;
    unsigned int *pixels;
    BOOL has_alpha = FALSE;

    memset(out_bmp, 0, sizeof(*out_bmp));
    if (!hmod || res_id <= 0)
        return FALSE;

    /* First try standard LoadImage with LR_CREATEDIBSECTION */
    hbmp = (HBITMAP)LoadImageW(hmod, MAKEINTRESOURCEW(res_id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

    /* Fallback: manual load from RT_BITMAP resource */
    if (!hbmp)
    {
        HRSRC hrsrc = FindResourceW(hmod, MAKEINTRESOURCEW(res_id), (LPCWSTR)RT_BITMAP);
        if (hrsrc)
        {
            HGLOBAL hglob = LoadResource(hmod, hrsrc);
            if (hglob)
            {
                const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)LockResource(hglob);
                DWORD rsize = SizeofResource(hmod, hrsrc);
                if (bih && rsize >= sizeof(BITMAPINFOHEADER))
                {
                    HDC hdc = CreateCompatibleDC(NULL);
                    void *bits = NULL;
                    int colors = bih->biClrUsed;
                    const BYTE *src_pixels;

                    if (colors == 0 && bih->biBitCount <= 8)
                        colors = 1 << bih->biBitCount;

                    src_pixels = (const BYTE *)bih + bih->biSize + colors * sizeof(RGBQUAD);
                    hbmp = CreateDIBSection(hdc, (const BITMAPINFO *)bih, DIB_RGB_COLORS, &bits, NULL, 0);
                    if (hbmp && bits)
                    {
                        DWORD copy_bytes = bih->biSizeImage;
                        if (copy_bytes == 0)
                        {
                            int stride = ((bih->biWidth * bih->biBitCount + 31) & ~31) / 8;
                            copy_bytes = stride * abs(bih->biHeight);
                        }
                        if ((DWORD)(src_pixels - (const BYTE *)bih) + copy_bytes <= rsize)
                            memcpy(bits, src_pixels, copy_bytes);
                    }
                    DeleteDC(hdc);
                }
            }
        }
    }

    if (!hbmp)
        return FALSE;

    if (!GetObjectW(hbmp, sizeof(bm), &bm) || !bm.bmBits)
    {
        DeleteObject(hbmp);
        return FALSE;
    }

    out_bmp->hbmp   = hbmp;
    out_bmp->width  = bm.bmWidth;
    out_bmp->height = bm.bmHeight;
    out_bmp->is_32  = FALSE;

    if (bm.bmBitsPixel < 32)
        return TRUE;

    /* Detect 32-bit ARGB with non-zero alpha channel */
    n = bm.bmWidth * bm.bmHeight;
    pixels = (unsigned int *)bm.bmBits;

    for (i = 0; i < n; i++)
    {
        if ((pixels[i] & 0xFF000000) != 0)
        {
            has_alpha = TRUE;
            break;
        }
    }

    if (has_alpha)
    {
        out_bmp->is_32 = TRUE;
        /* Premultiply alpha */
        for (i = 0; i < n; i++)
        {
            unsigned int p = pixels[i];
            int a = (p >> 24) & 0xFF;
            int r = (p >> 16) & 0xFF;
            int g = (p >> 8) & 0xFF;
            int b = p & 0xFF;

            r = (r * a) / 255;
            g = (g * a) / 255;
            b = (b * a) / 255;
            pixels[i] = ((unsigned int)a << 24) | ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
        }
    }

    return TRUE;
}

/* -------------------------------------------------------------------------
 * Skin loading & orchestration
 * ------------------------------------------------------------------------- */

struct start_skin *startskin_load(const WCHAR *skin_path, const WCHAR *variation,
                                 const WCHAR *options_str, enum start_skin_type skin_type,
                                 int dpi)
{
    struct start_skin *skin;
    struct skin_parser parser;
    HMODULE hmod;
    const WCHAR *str;
    const WCHAR *cond_values[MAX_COND_VARS];
    int cond_count = 0;
    int res_main_bmp = 0;
    WCHAR dpi_token[32];

    if (!skin_path || !*skin_path)
        return NULL;

    hmod = LoadLibraryExW(skin_path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!hmod)
        return NULL;

    parser_init(&parser);

    /* Open-Shell Win7 skins use resource 2 if Win7 style is selected, otherwise 1 */
    if (!parser_load_resource(&parser, hmod, (skin_type == STARTSKIN_TYPE_WIN7) ? 2 : 1))
    {
        if (!parser_load_resource(&parser, hmod, 1))
        {
            FreeLibrary(hmod);
            return NULL;
        }
    }

    skin = (struct start_skin *)calloc(1, sizeof(*skin));
    if (!skin)
    {
        parser_free(&parser);
        FreeLibrary(hmod);
        return NULL;
    }

    skin->hmod = hmod;
    skin->skin_type = skin_type;
    skin->dpi = (dpi > 0) ? dpi : 96;
    skin->two_columns = (skin_type == STARTSKIN_TYPE_CLASSIC2 || skin_type == STARTSKIN_TYPE_WIN7);

    /* Extract skin name from path */
    {
        const WCHAR *p = wcsrchr(skin_path, L'\\');
        const WCHAR *dot;
        if (!p) p = wcsrchr(skin_path, L'/');
        p = p ? p + 1 : skin_path;
        wcsncpy(skin->name, p, 63);
        skin->name[63] = L'\0';
        dot = wcsrchr(skin->name, L'.');
        if (dot) *(WCHAR *)dot = L'\0';
    }

    /* Parse variations */
    {
        int var_idx = 1;
        while (var_idx <= 32)
        {
            WCHAR var_key[32];
            swprintf(var_key, 32, L"Variation%d", var_idx);
            str = parser_find_setting(&parser, var_key);
            if (!str) break;

            skin->variations = (struct start_skin_variation *)realloc(
                skin->variations, (skin->variation_count + 1) * sizeof(struct start_skin_variation));
            if (skin->variations)
            {
                struct start_skin_variation *v = &skin->variations[skin->variation_count];
                WCHAR token[128];
                const WCHAR *p = str;
                int id = 0;

                while (*p == L' ' || *p == L'\t') p++;
                id = _wtoi(p);
                while (*p && *p != L',') p++;
                if (*p == L',') p++;
                while (*p == L' ' || *p == L'\t') p++;

                wcsncpy(token, p, 127);
                token[127] = L'\0';
                {
                    WCHAR *hash = wcschr(token, L'#');
                    if (hash)
                    {
                        *hash = L'\0';
                        wcsncpy(v->tip, hash + 1, 127);
                    }
                    else
                    {
                        v->tip[0] = L'\0';
                    }
                }
                v->res_id = id;
                wcsncpy(v->label, token, 63);
                v->label[63] = L'\0';
                skin->variation_count++;
            }
            var_idx++;
        }
    }

    /* Apply variation if requested */
    if (variation && *variation && skin->variation_count > 0)
    {
        int i;
        for (i = 0; i < skin->variation_count; i++)
        {
            if (_wcsicmp(variation, skin->variations[i].label) == 0)
            {
                struct skin_parser var_parser;
                parser_init(&var_parser);
                if (parser_load_resource(&var_parser, hmod, skin->variations[i].res_id))
                {
                    /* Append variation lines after base lines with [TRUE] delimiter (so variation overrides base) */
                    int j;
                    int old_base_count = parser.line_count;
                    int old_var_count = var_parser.line_count;
                    WCHAR **merged = (WCHAR **)malloc(
                        (old_base_count + 1 + old_var_count) * sizeof(WCHAR *));
                    if (merged)
                    {
                        for (j = 0; j < old_base_count; j++)
                            merged[j] = parser.lines[j];
                        merged[old_base_count] = _wcsdup(L"[TRUE]");
                        for (j = 0; j < old_var_count; j++)
                            merged[old_base_count + 1 + j] = var_parser.lines[j];

                        free(parser.lines);
                        free(var_parser.lines);
                        var_parser.lines = NULL;
                        var_parser.line_count = 0;

                        parser.lines = merged;
                        parser.line_count = old_base_count + 1 + old_var_count;
                        parser.line_capacity = parser.line_count;
                    }
                }
                parser_free(&var_parser);
                break;
            }
        }
    }

    /* Set up condition variables */
    if (skin->two_columns)
        cond_values[cond_count++] = L"TWO_COLUMNS";
    if (skin->dpi >= 144)
        cond_values[cond_count++] = L"HIGH_DPI";

    swprintf(dpi_token, 32, L"%d_DPI", skin->dpi);
    cond_values[cond_count++] = dpi_token;
    cond_values[cond_count++] = L"SEARCHBOX";

    /* Parse user options */
    if (options_str && *options_str)
    {
        const WCHAR *p = options_str;
        while (*p && cond_count < MAX_COND_VARS - 1)
        {
            WCHAR opt[64];
            int olen = 0;
            while (*p == L' ' || *p == L'|' || *p == L';') p++;
            if (!*p) break;

            while (*p && *p != L'=' && *p != L'|' && *p != L';' && olen < 63)
                opt[olen++] = *p++;
            opt[olen] = L'\0';

            if (*p == L'=')
            {
                p++;
                if (*p == L'1' || *p == L't' || *p == L'T')
                    cond_values[cond_count++] = _wcsdup(opt);
                while (*p && *p != L'|' && *p != L';') p++;
            }
            else
            {
                cond_values[cond_count++] = _wcsdup(opt);
            }
        }
    }

    /* Filter sections by evaluating conditions */
    parser_filter_conditions(&parser, cond_values, cond_count);

    /* General properties */
    str = parser_find_setting(&parser, L"About");
    if (str)
    {
        wcsncpy(skin->about, str, 511);
        skin->about[511] = L'\0';
    }

    str = parser_find_setting(&parser, L"Version");
    skin->version = str ? _wtoi(str) : 1;

    str = parser_find_setting(&parser, L"Main_thin_frame");
    skin->main_thin_frame = str ? (_wtoi(str) != 0) : FALSE;

    /* Backgrounds & colors */
    skin->main_background = parse_color(
        parser_find_setting(&parser, L"Main_background"), GetSysColor(COLOR_MENU));
    skin->main2_background = parse_color(
        parser_find_setting(&parser, L"Main2_background"), skin->main_background);

    /* Main background bitmap */
    str = parser_find_setting(&parser, L"Main_bitmap");
    if (str && _wtoi(str) > 0)
    {
        res_main_bmp = _wtoi(str);
        if (!load_skin_bitmap(hmod, res_main_bmp, &skin->main_bitmap))
        {
            /* Clean fallback on missing or malformed required bitmap */
            startskin_free(skin);
            parser_free(&parser);
            return NULL;
        }

        /* Slices */
        str = parser_find_setting(&parser, L"Main_bitmap_slices_X");
        if (str)
            parse_numbers(str, skin->main_bitmap_slices_x, 9, skin->dpi);
        str = parser_find_setting(&parser, L"Main_bitmap_slices_Y");
        if (str)
            parse_numbers(str, skin->main_bitmap_slices_y, 6, skin->dpi);
    }

    /* Margins / Paddings */
    str = parser_find_setting(&parser, L"Main_padding");
    if (str)
        parse_numbers(str, (int *)&skin->main_padding, 4, skin->dpi);
    str = parser_find_setting(&parser, L"Main2_padding");
    if (str)
        parse_numbers(str, (int *)&skin->main2_padding, 4, skin->dpi);

    /* Fonts and item text colors */
    skin->main_item.font = parse_font(
        parser_find_setting(&parser, L"Main_font"), L"Segoe UI", 9, FW_NORMAL, skin->dpi);
    skin->main_item.own_font = TRUE;

    skin->main2_item.font = parse_font(
        parser_find_setting(&parser, L"Main2_font"), L"Segoe UI", 9, FW_NORMAL, skin->dpi);
    skin->main2_item.own_font = TRUE;

    /* Colors: normal, selected, disabled, disabled+selected */
    str = parser_find_setting(&parser, L"Main_text_color");
    if (str)
    {
        COLORREF cols[4] = {0};
        int n = parse_numbers(str, (int *)cols, 4, skin->dpi);
        skin->main_item.text_color[0] = (n > 0) ? cols[0] : GetSysColor(COLOR_MENUTEXT);
        skin->main_item.text_color[1] = (n > 1) ? cols[1] : GetSysColor(COLOR_HIGHLIGHTTEXT);
        skin->main_item.text_color[2] = (n > 2) ? cols[2] : GetSysColor(COLOR_GRAYTEXT);
        skin->main_item.text_color[3] = (n > 3) ? cols[3] : GetSysColor(COLOR_GRAYTEXT);
    }
    else
    {
        skin->main_item.text_color[0] = GetSysColor(COLOR_MENUTEXT);
        skin->main_item.text_color[1] = GetSysColor(COLOR_HIGHLIGHTTEXT);
        skin->main_item.text_color[2] = GetSysColor(COLOR_GRAYTEXT);
        skin->main_item.text_color[3] = GetSysColor(COLOR_GRAYTEXT);
    }

    str = parser_find_setting(&parser, L"Main2_text_color");
    if (str)
    {
        COLORREF cols[4] = {0};
        int n = parse_numbers(str, (int *)cols, 4, skin->dpi);
        skin->main2_item.text_color[0] = (n > 0) ? cols[0] : skin->main_item.text_color[0];
        skin->main2_item.text_color[1] = (n > 1) ? cols[1] : skin->main_item.text_color[1];
        skin->main2_item.text_color[2] = (n > 2) ? cols[2] : skin->main_item.text_color[2];
        skin->main2_item.text_color[3] = (n > 3) ? cols[3] : skin->main_item.text_color[3];
    }
    else
    {
        memcpy(skin->main2_item.text_color, skin->main_item.text_color, sizeof(skin->main2_item.text_color));
    }

    /* Selection bitmaps */
    str = parser_find_setting(&parser, L"Main_selection");
    if (str && _wtoi(str) > 0)
    {
        load_skin_bitmap(hmod, _wtoi(str), &skin->main_item.bmp_selection);
        str = parser_find_setting(&parser, L"Main_selection_slices_X");
        if (str) parse_numbers(str, skin->main_item.sel_slices_x, 6, skin->dpi);
        str = parser_find_setting(&parser, L"Main_selection_slices_Y");
        if (str) parse_numbers(str, skin->main_item.sel_slices_y, 3, skin->dpi);
    }

    str = parser_find_setting(&parser, L"Main2_selection");
    if (str && _wtoi(str) > 0)
    {
        load_skin_bitmap(hmod, _wtoi(str), &skin->main2_item.bmp_selection);
        str = parser_find_setting(&parser, L"Main2_selection_slices_X");
        if (str) parse_numbers(str, skin->main2_item.sel_slices_x, 6, skin->dpi);
        str = parser_find_setting(&parser, L"Main2_selection_slices_Y");
        if (str) parse_numbers(str, skin->main2_item.sel_slices_y, 3, skin->dpi);
    }

    /* Separators */
    str = parser_find_setting(&parser, L"Main_separator");
    if (str && _wtoi(str) > 0)
    {
        load_skin_bitmap(hmod, _wtoi(str), &skin->main_item.bmp_separator);
        str = parser_find_setting(&parser, L"Main_separator_slices_X");
        if (str) parse_numbers(str, skin->main_item.sep_slices_x, 3, skin->dpi);
    }

    str = parser_find_setting(&parser, L"Main2_separator");
    if (str && _wtoi(str) > 0)
    {
        load_skin_bitmap(hmod, _wtoi(str), &skin->main2_item.bmp_separator);
        str = parser_find_setting(&parser, L"Main2_separator_slices_X");
        if (str) parse_numbers(str, skin->main2_item.sep_slices_x, 3, skin->dpi);
    }

    /* Vertical column separator */
    str = parser_find_setting(&parser, L"Main_separatorV");
    if (str && _wtoi(str) > 0)
    {
        load_skin_bitmap(hmod, _wtoi(str), &skin->main_separator_v);
        str = parser_find_setting(&parser, L"Main_separator_slices_Y");
        if (str) parse_numbers(str, skin->main_separator_slices_y, 3, skin->dpi);
    }

    /* Caption properties */
    skin->caption_font = parse_font(
        parser_find_setting(&parser, L"Caption_font"), L"Segoe UI", 18, FW_NORMAL, skin->dpi);
    skin->own_caption_font = TRUE;
    skin->caption_text_color = parse_color(
        parser_find_setting(&parser, L"Caption_text_color"), RGB(255, 255, 255));
    skin->caption_background = parse_color(
        parser_find_setting(&parser, L"Caption_background"), skin->main_background);

    str = parser_find_setting(&parser, L"Caption_bitmap");
    if (str && _wtoi(str) > 0)
        load_skin_bitmap(hmod, _wtoi(str), &skin->caption_bitmap);

    str = parser_find_setting(&parser, L"Caption_padding");
    if (str)
        parse_numbers(str, (int *)&skin->caption_padding, 4, skin->dpi);

    /* User account tile & avatar */
    str = parser_find_setting(&parser, L"User_bitmap");
    if (str && _wtoi(str) > 0)
        load_skin_bitmap(hmod, _wtoi(str), &skin->user_bitmap);

    str = parser_find_setting(&parser, L"User_mask");
    if (str && _wtoi(str) > 0)
        load_skin_bitmap(hmod, _wtoi(str), &skin->user_mask);

    str = parser_find_setting(&parser, L"User_image_size");
    skin->user_image_size = str ? _wtoi(str) : (skin_type == STARTSKIN_TYPE_WIN7 ? 48 : 0);

    str = parser_find_setting(&parser, L"User_frame_position");
    if (str)
        parse_numbers(str, (int *)&skin->user_frame_pos, 2, skin->dpi);

    str = parser_find_setting(&parser, L"User_image_offset");
    if (str)
        parse_numbers(str, (int *)&skin->user_image_offset, 2, skin->dpi);

    str = parser_find_setting(&parser, L"User_name_position");
    if (str)
        parse_numbers(str, (int *)&skin->user_name_pos, 4, skin->dpi);

    skin->user_font = parse_font(
        parser_find_setting(&parser, L"User_font"), L"Segoe UI", 16, FW_BOLD, skin->dpi);
    skin->own_user_font = TRUE;

    skin->user_text_color = parse_color(
        parser_find_setting(&parser, L"User_text_color"), GetSysColor(COLOR_MENUTEXT));

    /* Search box */
    str = parser_find_setting(&parser, L"Search_background");
    if (str && _wtoi(str) > 0)
    {
        load_skin_bitmap(hmod, _wtoi(str), &skin->search_background);
        str = parser_find_setting(&parser, L"Search_background_slices_X");
        if (str) parse_numbers(str, skin->search_background_slices_x, 9, skin->dpi);
        str = parser_find_setting(&parser, L"Search_background_slices_Y");
        if (str) parse_numbers(str, skin->search_background_slices_y, 9, skin->dpi);
    }

    str = parser_find_setting(&parser, L"Search_padding");
    if (str)
        parse_numbers(str, (int *)&skin->search_padding, 4, skin->dpi);

    skin->search_hint_font = parse_font(
        parser_find_setting(&parser, L"Search_hint_font"), L"Segoe UI", 9, FW_NORMAL, skin->dpi);
    skin->own_search_hint_font = TRUE;

    skin->search_text_color[0] = parse_color(
        parser_find_setting(&parser, L"Search_text_color"), GetSysColor(COLOR_WINDOWTEXT));
    skin->search_text_bg = parse_color(
        parser_find_setting(&parser, L"Search_text_background"), GetSysColor(COLOR_WINDOW));

    /* Shutdown / Logoff buttons */
    str = parser_find_setting(&parser, L"Shutdown_bitmap");
    if (str && _wtoi(str) > 0)
        load_skin_bitmap(hmod, _wtoi(str), &skin->shutdown_bitmap);

    str = parser_find_setting(&parser, L"Shutdown_padding");
    if (str)
        parse_numbers(str, (int *)&skin->shutdown_padding, 4, skin->dpi);

    /* Submenus */
    str = parser_find_setting(&parser, L"Submenu_bitmap");
    if (str && _wtoi(str) > 0)
    {
        load_skin_bitmap(hmod, _wtoi(str), &skin->submenu_bitmap);
        str = parser_find_setting(&parser, L"Submenu_bitmap_slices_X");
        if (str) parse_numbers(str, skin->submenu_slices_x, 6, skin->dpi);
        str = parser_find_setting(&parser, L"Submenu_bitmap_slices_Y");
        if (str) parse_numbers(str, skin->submenu_slices_y, 3, skin->dpi);
    }
    skin->submenu_background = parse_color(
        parser_find_setting(&parser, L"Submenu_background"), skin->main_background);

    skin->submenu_item.font = parse_font(
        parser_find_setting(&parser, L"Submenu_font"), L"Segoe UI", 9, FW_NORMAL, skin->dpi);
    skin->submenu_item.own_font = TRUE;

    str = parser_find_setting(&parser, L"Submenu_selection");
    if (str && _wtoi(str) > 0)
    {
        load_skin_bitmap(hmod, _wtoi(str), &skin->submenu_item.bmp_selection);
        str = parser_find_setting(&parser, L"Submenu_selection_slices_X");
        if (str) parse_numbers(str, skin->submenu_item.sel_slices_x, 6, skin->dpi);
        str = parser_find_setting(&parser, L"Submenu_selection_slices_Y");
        if (str) parse_numbers(str, skin->submenu_item.sel_slices_y, 3, skin->dpi);
    }

    str = parser_find_setting(&parser, L"Submenu_padding");
    if (str)
        parse_numbers(str, (int *)&skin->submenu_padding, 4, skin->dpi);

    parser_free(&parser);
    return skin;
}

static void free_skin_bitmap(struct start_skin_bitmap *bmp)
{
    if (bmp->hbmp)
    {
        DeleteObject(bmp->hbmp);
        bmp->hbmp = NULL;
    }
}

static void free_item_settings(struct start_skin_item_settings *item)
{
    if (item->own_font && item->font)
    {
        DeleteObject(item->font);
        item->font = NULL;
    }
    free_skin_bitmap(&item->bmp_selection);
    free_skin_bitmap(&item->bmp_separator);
}

void startskin_free(struct start_skin *skin)
{
    if (!skin) return;

    free_skin_bitmap(&skin->main_bitmap);
    free_skin_bitmap(&skin->main_separator_v);
    free_skin_bitmap(&skin->caption_bitmap);
    free_skin_bitmap(&skin->user_bitmap);
    free_skin_bitmap(&skin->user_mask);
    free_skin_bitmap(&skin->search_background);
    free_skin_bitmap(&skin->shutdown_bitmap);
    free_skin_bitmap(&skin->submenu_bitmap);
    free_skin_bitmap(&skin->submenu_pager);
    free_skin_bitmap(&skin->submenu_pager_arrows);

    free_item_settings(&skin->main_item);
    free_item_settings(&skin->main2_item);
    free_item_settings(&skin->shutdown_btn);
    free_item_settings(&skin->submenu_item);

    if (skin->own_caption_font && skin->caption_font)
        DeleteObject(skin->caption_font);
    if (skin->own_user_font && skin->user_font)
        DeleteObject(skin->user_font);
    if (skin->own_search_hint_font && skin->search_hint_font)
        DeleteObject(skin->search_hint_font);

    if (skin->variations)
        free(skin->variations);
    if (skin->options)
        free(skin->options);

    if (skin->hmod)
        FreeLibrary(skin->hmod);

    free(skin);
}

/* -------------------------------------------------------------------------
 * Drawing routines
 * ------------------------------------------------------------------------- */

static void blit_chunk(HDC dst, int dx, int dy, int dw, int dh,
                       HDC src, int sx, int sy, int sw, int sh, BOOL alpha)
{
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;

    if (alpha)
    {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(dst, dx, dy, dw, dh, src, sx, sy, sw, sh, bf);
    }
    else if (dw == sw && dh == sh)
    {
        BitBlt(dst, dx, dy, dw, dh, src, sx, sy, SRCCOPY);
    }
    else
    {
        StretchBlt(dst, dx, dy, dw, dh, src, sx, sy, sw, sh, SRCCOPY);
    }
}

void startskin_draw_slices(HDC hdc, const struct start_skin_bitmap *bmp,
                           const RECT *dst_rc, const RECT *src_rc,
                           const RECT *margins, BOOL alpha)
{
    HDC mem;
    HBITMAP prev;
    int x0a, x1a, x2a, x3a;
    int x0b, x1b, x2b, x3b;
    int y0a, y1a, y2a, y3a;
    int y0b, y1b, y2b, y3b;

    if (!hdc || !bmp || !bmp->hbmp || !dst_rc || !src_rc || !margins)
        return;

    x0a = dst_rc->left;
    x1a = dst_rc->left + margins->left;
    if (x1a > dst_rc->right) x1a = dst_rc->right;
    x2a = dst_rc->right - margins->right;
    if (x2a < dst_rc->left) x2a = dst_rc->left;
    if (x1a > x2a) x1a = x2a = (x1a + x2a) / 2;
    x3a = dst_rc->right;

    x0b = src_rc->left;
    x1b = src_rc->left + margins->left;
    x2b = src_rc->right - margins->right;
    x3b = src_rc->right;

    y0a = dst_rc->top;
    y1a = dst_rc->top + margins->top;
    if (y1a > dst_rc->bottom) y1a = dst_rc->bottom;
    y2a = dst_rc->bottom - margins->bottom;
    if (y2a < dst_rc->top) y2a = dst_rc->top;
    if (y1a > y2a) y1a = y2a = (y1a + y2a) / 2;
    y3a = dst_rc->bottom;

    y0b = src_rc->top;
    y1b = src_rc->top + margins->top;
    y2b = src_rc->bottom - margins->bottom;
    y3b = src_rc->bottom;

    mem = CreateCompatibleDC(hdc);
    prev = (HBITMAP)SelectObject(mem, bmp->hbmp);
    SetStretchBltMode(hdc, COLORONCOLOR);

    /* Row 1 */
    blit_chunk(hdc, x0a, y0a, x1a - x0a, y1a - y0a, mem, x0b, y0b, x1b - x0b, y1b - y0b, alpha && bmp->is_32);
    blit_chunk(hdc, x1a, y0a, x2a - x1a, y1a - y0a, mem, x1b, y0b, x2b - x1b, y1b - y0b, alpha && bmp->is_32);
    blit_chunk(hdc, x2a, y0a, x3a - x2a, y1a - y0a, mem, x2b, y0b, x3b - x2b, y1b - y0b, alpha && bmp->is_32);

    /* Row 2 */
    blit_chunk(hdc, x0a, y1a, x1a - x0a, y2a - y1a, mem, x0b, y1b, x1b - x0b, y2b - y1b, alpha && bmp->is_32);
    blit_chunk(hdc, x1a, y1a, x2a - x1a, y2a - y1a, mem, x1b, y1b, x2b - x1b, y2b - y1b, alpha && bmp->is_32);
    blit_chunk(hdc, x2a, y1a, x3a - x2a, y2a - y1a, mem, x2b, y1b, x3b - x2b, y2b - y1b, alpha && bmp->is_32);

    /* Row 3 */
    blit_chunk(hdc, x0a, y2a, x1a - x0a, y3a - y2a, mem, x0b, y2b, x1b - x0b, y3b - y2b, alpha && bmp->is_32);
    blit_chunk(hdc, x1a, y2a, x2a - x1a, y3a - y2a, mem, x1b, y2b, x2b - x1b, y3b - y2b, alpha && bmp->is_32);
    blit_chunk(hdc, x2a, y2a, x3a - x2a, y3a - y2a, mem, x2b, y2b, x3b - x2b, y3b - y2b, alpha && bmp->is_32);

    SelectObject(mem, prev);
    DeleteDC(mem);
}

void startskin_draw_bg_slices(HDC hdc, const struct start_skin_bitmap *bmp,
                              const RECT *dst_rc, const int *slices_x,
                              const int *slices_y, int col_idx, BOOL alpha)
{
    RECT src_rc, margins;
    int base_x;

    if (!bmp || !bmp->hbmp || !dst_rc || !slices_x || !slices_y)
        return;

    base_x = (col_idx == 0) ? 0 : (slices_x[0] + slices_x[1] + slices_x[2]);
    src_rc.left   = base_x;
    src_rc.top    = 0;
    src_rc.right  = base_x + slices_x[col_idx * 3 + 0] + slices_x[col_idx * 3 + 1] + slices_x[col_idx * 3 + 2];
    src_rc.bottom = slices_y[0] + slices_y[1] + slices_y[2];

    margins.left   = slices_x[col_idx * 3 + 0];
    margins.right  = slices_x[col_idx * 3 + 2];
    margins.top    = slices_y[0];
    margins.bottom = slices_y[2];

    startskin_draw_slices(hdc, bmp, dst_rc, &src_rc, &margins, alpha);
}

void startskin_draw_separator(HDC hdc, const struct start_skin_bitmap *bmp,
                              const RECT *dst_rc, const int *slices_x,
                              int height, BOOL alpha)
{
    RECT src_rc, margins, target;

    if (!bmp || !bmp->hbmp || !dst_rc || !slices_x)
        return;

    target = *dst_rc;
    if (height > 0)
    {
        target.top = dst_rc->top + (dst_rc->bottom - dst_rc->top - height) / 2;
        target.bottom = target.top + height;
    }

    src_rc.left   = 0;
    src_rc.top    = 0;
    src_rc.right  = slices_x[0] + slices_x[1] + slices_x[2];
    src_rc.bottom = bmp->height;

    margins.left   = slices_x[0];
    margins.right  = slices_x[2];
    margins.top    = 0;
    margins.bottom = 0;

    startskin_draw_slices(hdc, bmp, &target, &src_rc, &margins, alpha);
}

void startskin_draw_selection(HDC hdc, const struct start_skin_bitmap *bmp,
                              const RECT *dst_rc, const int *slices_x,
                              const int *slices_y, BOOL alpha)
{
    RECT src_rc, margins;

    if (!bmp || !bmp->hbmp || !dst_rc || !slices_x || !slices_y)
        return;

    src_rc.left   = 0;
    src_rc.top    = 0;
    src_rc.right  = slices_x[0] + slices_x[1] + slices_x[2];
    src_rc.bottom = slices_y[0] + slices_y[1] + slices_y[2];

    margins.left   = slices_x[0];
    margins.right  = slices_x[2];
    margins.top    = slices_y[0];
    margins.bottom = slices_y[2];

    startskin_draw_slices(hdc, bmp, dst_rc, &src_rc, &margins, alpha);
}

/* -------------------------------------------------------------------------
 * Skin enumeration
 * ------------------------------------------------------------------------- */

static void enum_dir_skins(const WCHAR *dir, startskin_enum_cb cb, void *param)
{
    WIN32_FIND_DATAW fd;
    HANDLE hfind;
    WCHAR pattern[MAX_PATH];

    swprintf(pattern, MAX_PATH, L"%s\\*.*", dir);
    hfind = FindFirstFileW(pattern, &fd);
    if (hfind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            const WCHAR *ext = wcsrchr(fd.cFileName, L'.');
            if (ext)
            {
                BOOL is_skin  = (_wcsicmp(ext, L".skin") == 0);
                BOOL is_skin7 = (_wcsicmp(ext, L".skin7") == 0);

                if (is_skin || is_skin7)
                {
                    WCHAR full_path[MAX_PATH];
                    WCHAR name[64];
                    swprintf(full_path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
                    wcsncpy(name, fd.cFileName, 63);
                    name[63] = L'\0';
                    {
                        WCHAR *dot = wcsrchr(name, L'.');
                        if (dot) *dot = L'\0';
                    }
                    cb(name, full_path, is_skin7, param);
                }
            }
        }
    } while (FindNextFileW(hfind, &fd));

    FindClose(hfind);
}

int startskin_enumerate(startskin_enum_cb callback, void *param)
{
    WCHAR appdata[MAX_PATH];

    if (!callback) return 0;

    /* 1. System directory: /usr/local/share/wineexplorer/startskins or install dir */
    enum_dir_skins(L"C:\\usr\\local\\share\\wineexplorer\\startskins", callback, param);
    enum_dir_skins(L"Z:\\usr\\local\\share\\wineexplorer\\startskins", callback, param);

    /* 2. User directory: %APPDATA%\WineExplorer\StartSkins */
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK)
    {
        WCHAR user_skins[MAX_PATH];
        swprintf(user_skins, MAX_PATH, L"%s\\WineExplorer\\StartSkins", appdata);
        enum_dir_skins(user_skins, callback, param);
    }

    return 0;
}
