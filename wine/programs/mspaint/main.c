/*
 * Wine Paint - Authentic Win32 Paint Application
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

#include "paint.h"

#define CANVAS_WIDTH  640
#define CANVAS_HEIGHT 480
#define PALETTE_ROWS  2
#define PALETTE_COLS  14

static const COLORREF g_palette[PALETTE_ROWS * PALETTE_COLS] =
{
    /* Row 1 */
    RGB(0,0,0),       RGB(128,128,128), RGB(128,0,0),    RGB(128,128,0),
    RGB(0,128,0),     RGB(0,128,128),   RGB(0,0,128),     RGB(128,0,128),
    RGB(128,128,64),  RGB(0,64,64),     RGB(0,128,255),   RGB(0,64,128),
    RGB(64,0,255),    RGB(128,64,0),
    /* Row 2 */
    RGB(255,255,255), RGB(192,192,192), RGB(255,0,0),    RGB(255,255,0),
    RGB(0,255,0),     RGB(0,255,255),   RGB(0,0,255),     RGB(255,0,255),
    RGB(255,255,128), RGB(0,255,128),   RGB(128,255,255), RGB(128,128,255),
    RGB(255,0,128),   RGB(255,128,64)
};

static const WCHAR *g_tool_names[NUM_TOOLS] =
{
    L"Pencil", L"Brush", L"Eraser", L"Line",
    L"Rectangle", L"Filled Rect", L"Ellipse", L"Filled Ellipse", L"Fill"
};

static HINSTANCE g_hInst = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hCanvasWnd = NULL;
static HWND g_hToolboxWnd = NULL;
static HWND g_hPaletteWnd = NULL;
static HWND g_hStatusWnd = NULL;

static HDC g_hdc_canvas = NULL;
static HBITMAP g_hbm_canvas = NULL;
static HBITMAP g_hbm_undo = NULL;
static int g_canvas_w = CANVAS_WIDTH;
static int g_canvas_h = CANVAS_HEIGHT;

static int g_current_tool = TOOL_PENCIL;
static COLORREF g_fg_color = RGB(0, 0, 0);
static COLORREF g_bg_color = RGB(255, 255, 255);

static BOOL g_is_drawing = FALSE;
static POINT g_start_pt;
static POINT g_last_pt;
static POINT g_curr_pt;

static WCHAR g_current_file[MAX_PATH] = L"";

static void save_undo_state(void)
{
    HDC hdc_screen;
    HDC hdc_undo;
    HBITMAP old_undo;

    hdc_screen = GetDC(NULL);
    hdc_undo = CreateCompatibleDC(hdc_screen);
    old_undo = SelectObject(hdc_undo, g_hbm_undo);
    BitBlt(hdc_undo, 0, 0, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
    SelectObject(hdc_undo, old_undo);
    DeleteDC(hdc_undo);
    ReleaseDC(NULL, hdc_screen);
}

static void restore_undo_state(void)
{
    HDC hdc_screen;
    HDC hdc_undo;
    HBITMAP old_undo;

    hdc_screen = GetDC(NULL);
    hdc_undo = CreateCompatibleDC(hdc_screen);
    old_undo = SelectObject(hdc_undo, g_hbm_undo);
    BitBlt(g_hdc_canvas, 0, 0, g_canvas_w, g_canvas_h, hdc_undo, 0, 0, SRCCOPY);
    SelectObject(hdc_undo, old_undo);
    DeleteDC(hdc_undo);
    ReleaseDC(NULL, hdc_screen);

    InvalidateRect(g_hCanvasWnd, NULL, FALSE);
}

static void clear_canvas(void)
{
    RECT rc;
    HBRUSH bg_br;

    save_undo_state();
    rc.left = 0;
    rc.top = 0;
    rc.right = g_canvas_w;
    rc.bottom = g_canvas_h;
    bg_br = CreateSolidBrush(g_bg_color);
    FillRect(g_hdc_canvas, &rc, bg_br);
    DeleteObject(bg_br);
    InvalidateRect(g_hCanvasWnd, NULL, FALSE);
}

/* Flood Fill */
static void flood_fill_canvas(int start_x, int start_y, COLORREF fill_color)
{
    COLORREF target_color;
    HBRUSH br;
    HBRUSH old_br;

    if (start_x < 0 || start_x >= g_canvas_w || start_y < 0 || start_y >= g_canvas_h)
        return;

    target_color = GetPixel(g_hdc_canvas, start_x, start_y);
    if (target_color == fill_color) return;

    save_undo_state();
    br = CreateSolidBrush(fill_color);
    old_br = SelectObject(g_hdc_canvas, br);
    ExtFloodFill(g_hdc_canvas, start_x, start_y, target_color, FLOODFILLSURFACE);
    SelectObject(g_hdc_canvas, old_br);
    DeleteObject(br);
    InvalidateRect(g_hCanvasWnd, NULL, FALSE);
}

/* BMP Loading and Saving */
static BOOL save_bitmap(const WCHAR *filename)
{
    BITMAPINFO bi;
    BITMAPFILEHEADER bfh;
    DWORD bytes_written;
    DWORD image_size;
    BYTE *bits;
    HANDLE hFile;
    HDC hdc_screen;

    image_size = ((g_canvas_w * 3 + 3) & ~3) * g_canvas_h;
    bits = malloc(image_size);
    if (!bits) return FALSE;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_canvas_w;
    bi.bmiHeader.biHeight = g_canvas_h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    hdc_screen = GetDC(NULL);
    GetDIBits(hdc_screen, g_hbm_canvas, 0, g_canvas_h, bits, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc_screen);

    memset(&bfh, 0, sizeof(bfh));
    bfh.bfType = 0x4D42; /* 'BM' */
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + image_size;

    hFile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        free(bits);
        return FALSE;
    }

    WriteFile(hFile, &bfh, sizeof(bfh), &bytes_written, NULL);
    WriteFile(hFile, &bi.bmiHeader, sizeof(BITMAPINFOHEADER), &bytes_written, NULL);
    WriteFile(hFile, bits, image_size, &bytes_written, NULL);
    CloseHandle(hFile);

    free(bits);
    return TRUE;
}

static BOOL load_bitmap(const WCHAR *filename)
{
    HBITMAP hbm;
    HDC hdc_screen;
    HDC hdc_src;
    HBITMAP old_src;
    BITMAP bm;

    hbm = (HBITMAP)LoadImageW(NULL, filename, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!hbm) return FALSE;

    save_undo_state();
    GetObjectW(hbm, sizeof(bm), &bm);

    hdc_screen = GetDC(NULL);
    hdc_src = CreateCompatibleDC(hdc_screen);
    old_src = SelectObject(hdc_src, hbm);

    BitBlt(g_hdc_canvas, 0, 0, min(g_canvas_w, bm.bmWidth), min(g_canvas_h, bm.bmHeight), hdc_src, 0, 0, SRCCOPY);

    SelectObject(hdc_src, old_src);
    DeleteDC(hdc_src);
    DeleteObject(hbm);
    ReleaseDC(NULL, hdc_screen);

    InvalidateRect(g_hCanvasWnd, NULL, FALSE);
    return TRUE;
}

/* Canvas Window Procedure */
static LRESULT CALLBACK canvas_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        BitBlt(hdc, 0, 0, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);

        /* Draw rubberband preview if active */
        if (g_is_drawing && (g_current_tool >= TOOL_LINE && g_current_tool <= TOOL_FILLEDELLIPSE))
        {
            HPEN pen = CreatePen(PS_SOLID, 1, g_fg_color);
            HBRUSH br = (g_current_tool == TOOL_FILLEDRECT || g_current_tool == TOOL_FILLEDELLIPSE) ?
                        CreateSolidBrush(g_bg_color) : GetStockObject(NULL_BRUSH);
            HPEN old_pen = SelectObject(hdc, pen);
            HBRUSH old_br = SelectObject(hdc, br);

            if (g_current_tool == TOOL_LINE)
            {
                MoveToEx(hdc, g_start_pt.x, g_start_pt.y, NULL);
                LineTo(hdc, g_curr_pt.x, g_curr_pt.y);
            }
            else if (g_current_tool == TOOL_RECT || g_current_tool == TOOL_FILLEDRECT)
            {
                Rectangle(hdc, g_start_pt.x, g_start_pt.y, g_curr_pt.x, g_curr_pt.y);
            }
            else if (g_current_tool == TOOL_ELLIPSE || g_current_tool == TOOL_FILLEDELLIPSE)
            {
                Ellipse(hdc, g_start_pt.x, g_start_pt.y, g_curr_pt.x, g_curr_pt.y);
            }

            SelectObject(hdc, old_pen);
            SelectObject(hdc, old_br);
            DeleteObject(pen);
            if (g_current_tool == TOOL_FILLEDRECT || g_current_tool == TOOL_FILLEDELLIPSE)
                DeleteObject(br);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);

        SetCapture(hwnd);
        g_is_drawing = TRUE;
        g_start_pt.x = g_last_pt.x = g_curr_pt.x = x;
        g_start_pt.y = g_last_pt.y = g_curr_pt.y = y;

        save_undo_state();

        if (g_current_tool == TOOL_FILL)
        {
            g_is_drawing = FALSE;
            ReleaseCapture();
            flood_fill_canvas(x, y, g_fg_color);
            return 0;
        }

        if (g_current_tool == TOOL_PENCIL || g_current_tool == TOOL_BRUSH || g_current_tool == TOOL_ERASER)
        {
            COLORREF col = (g_current_tool == TOOL_ERASER) ? g_bg_color : g_fg_color;
            int size = (g_current_tool == TOOL_BRUSH) ? 3 : (g_current_tool == TOOL_ERASER ? 6 : 1);
            HPEN pen = CreatePen(PS_SOLID, size, col);
            HBRUSH br = CreateSolidBrush(col);
            HPEN old_pen = SelectObject(g_hdc_canvas, pen);
            HBRUSH old_br = SelectObject(g_hdc_canvas, br);

            if (size > 1)
                Ellipse(g_hdc_canvas, x - size/2, y - size/2, x + size/2 + 1, y + size/2 + 1);
            else
                SetPixel(g_hdc_canvas, x, y, col);

            SelectObject(g_hdc_canvas, old_pen);
            SelectObject(g_hdc_canvas, old_br);
            DeleteObject(pen);
            DeleteObject(br);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);
        WCHAR coord_text[64];

        swprintf(coord_text, 64, L"%d, %d", x, y);
        SendMessageW(g_hStatusWnd, SB_SETTEXTW, 1, (LPARAM)coord_text);

        if (g_is_drawing)
        {
            g_curr_pt.x = x;
            g_curr_pt.y = y;

            if (g_current_tool == TOOL_PENCIL || g_current_tool == TOOL_BRUSH || g_current_tool == TOOL_ERASER)
            {
                COLORREF col = (g_current_tool == TOOL_ERASER) ? g_bg_color : g_fg_color;
                int size = (g_current_tool == TOOL_BRUSH) ? 3 : (g_current_tool == TOOL_ERASER ? 6 : 1);
                HPEN pen = CreatePen(PS_SOLID, size, col);
                HPEN old_pen = SelectObject(g_hdc_canvas, pen);

                MoveToEx(g_hdc_canvas, g_last_pt.x, g_last_pt.y, NULL);
                LineTo(g_hdc_canvas, x, y);

                SelectObject(g_hdc_canvas, old_pen);
                DeleteObject(pen);

                g_last_pt.x = x;
                g_last_pt.y = y;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else
            {
                /* Rubberband refresh */
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);

        if (g_is_drawing)
        {
            g_is_drawing = FALSE;
            ReleaseCapture();

            if (g_current_tool >= TOOL_LINE && g_current_tool <= TOOL_FILLEDELLIPSE)
            {
                HPEN pen = CreatePen(PS_SOLID, 1, g_fg_color);
                HBRUSH br = (g_current_tool == TOOL_FILLEDRECT || g_current_tool == TOOL_FILLEDELLIPSE) ?
                            CreateSolidBrush(g_bg_color) : GetStockObject(NULL_BRUSH);
                HPEN old_pen = SelectObject(g_hdc_canvas, pen);
                HBRUSH old_br = SelectObject(g_hdc_canvas, br);

                if (g_current_tool == TOOL_LINE)
                {
                    MoveToEx(g_hdc_canvas, g_start_pt.x, g_start_pt.y, NULL);
                    LineTo(g_hdc_canvas, x, y);
                }
                else if (g_current_tool == TOOL_RECT || g_current_tool == TOOL_FILLEDRECT)
                {
                    Rectangle(g_hdc_canvas, g_start_pt.x, g_start_pt.y, x, y);
                }
                else if (g_current_tool == TOOL_ELLIPSE || g_current_tool == TOOL_FILLEDELLIPSE)
                {
                    Ellipse(g_hdc_canvas, g_start_pt.x, g_start_pt.y, x, y);
                }

                SelectObject(g_hdc_canvas, old_pen);
                SelectObject(g_hdc_canvas, old_br);
                DeleteObject(pen);
                if (g_current_tool == TOOL_FILLEDRECT || g_current_tool == TOOL_FILLEDELLIPSE)
                    DeleteObject(br);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* Palette Window Procedure */
static LRESULT CALLBACK palette_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        int r, c;

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        /* Active color preview boxes at left */
        {
            RECT bg_box = { 10, 10, 26, 26 };
            RECT fg_box = { 4, 4, 20, 20 };
            HBRUSH bg_br = CreateSolidBrush(g_bg_color);
            HBRUSH fg_br = CreateSolidBrush(g_fg_color);

            DrawEdge(hdc, &bg_box, EDGE_SUNKEN, BF_RECT);
            FillRect(hdc, &bg_box, bg_br);

            DrawEdge(hdc, &fg_box, EDGE_SUNKEN, BF_RECT);
            FillRect(hdc, &fg_box, fg_br);

            DeleteObject(bg_br);
            DeleteObject(fg_br);
        }

        /* 28 color swatches */
        for (r = 0; r < PALETTE_ROWS; r++)
        {
            for (c = 0; c < PALETTE_COLS; c++)
            {
                RECT box;
                HBRUSH br;
                int idx = r * PALETTE_COLS + c;

                box.left = 34 + c * 16;
                box.top = 4 + r * 16;
                box.right = box.left + 15;
                box.bottom = box.top + 15;

                DrawEdge(hdc, &box, EDGE_SUNKEN, BF_RECT);
                InflateRect(&box, -1, -1);
                br = CreateSolidBrush(g_palette[idx]);
                FillRect(hdc, &box, br);
                DeleteObject(br);
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);
        int c = (x - 34) / 16;
        int r = (y - 4) / 16;

        if (c >= 0 && c < PALETTE_COLS && r >= 0 && r < PALETTE_ROWS)
        {
            int idx = r * PALETTE_COLS + c;
            if (msg == WM_LBUTTONDOWN)
                g_fg_color = g_palette[idx];
            else
                g_bg_color = g_palette[idx];
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* Toolbox Window Procedure */
static LRESULT CALLBACK toolbox_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HFONT font, old_font;
        int i;

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        old_font = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);

        for (i = 0; i < NUM_TOOLS; i++)
        {
            int col = i % 2;
            int row = i / 2;
            RECT btn_rc;
            btn_rc.left = 4 + col * 32;
            btn_rc.top = 4 + row * 28;
            btn_rc.right = btn_rc.left + 30;
            btn_rc.bottom = btn_rc.top + 26;

            if (i == g_current_tool)
            {
                DrawEdge(hdc, &btn_rc, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
                FillRect(hdc, &btn_rc, GetSysColorBrush(COLOR_BTNHIGHLIGHT));
            }
            else
            {
                DrawEdge(hdc, &btn_rc, EDGE_RAISED, BF_RECT | BF_ADJUST);
                FillRect(hdc, &btn_rc, GetSysColorBrush(COLOR_BTNFACE));
            }

            SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
            DrawTextW(hdc, g_tool_names[i], -1, &btn_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, old_font);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);
        int col = (x - 4) / 32;
        int row = (y - 4) / 28;
        int idx = row * 2 + col;

        if (col >= 0 && col < 2 && idx >= 0 && idx < NUM_TOOLS)
        {
            g_current_tool = idx;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* Main Frame Procedure */
static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HDC hdc_screen;
        RECT rc;
        int stat_parts[] = { 350, -1 };

        /* Create status bar */
        g_hStatusWnd = CreateWindowExW(0, STATUSCLASSNAMEW, NULL,
                                       WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                       0, 0, 0, 0, hwnd, NULL, g_hInst, NULL);
        SendMessageW(g_hStatusWnd, SB_SETPARTS, 2, (LPARAM)stat_parts);
        SendMessageW(g_hStatusWnd, SB_SETTEXTW, 0, (LPARAM)L"For Help, click Help Topics on the Help Menu.");

        /* Create Toolbox Window */
        g_hToolboxWnd = CreateWindowExW(0, L"PaintToolbox", NULL,
                                        WS_CHILD | WS_VISIBLE,
                                        2, 2, 70, 200, hwnd, NULL, g_hInst, NULL);

        /* Create Palette Window */
        g_hPaletteWnd = CreateWindowExW(0, L"PaintPalette", NULL,
                                        WS_CHILD | WS_VISIBLE,
                                        72, 490, 480, 40, hwnd, NULL, g_hInst, NULL);

        /* Create Canvas Window */
        g_hCanvasWnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"PaintCanvas", NULL,
                                       WS_CHILD | WS_VISIBLE,
                                       74, 4, CANVAS_WIDTH, CANVAS_HEIGHT, hwnd, NULL, g_hInst, NULL);

        /* Initialize Memory DC & Bitmaps */
        hdc_screen = GetDC(NULL);
        g_hdc_canvas = CreateCompatibleDC(hdc_screen);
        g_hbm_canvas = CreateCompatibleBitmap(hdc_screen, CANVAS_WIDTH, CANVAS_HEIGHT);
        g_hbm_undo = CreateCompatibleBitmap(hdc_screen, CANVAS_WIDTH, CANVAS_HEIGHT);
        SelectObject(g_hdc_canvas, g_hbm_canvas);

        rc.left = 0; rc.top = 0; rc.right = CANVAS_WIDTH; rc.bottom = CANVAS_HEIGHT;
        FillRect(g_hdc_canvas, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

        ReleaseDC(NULL, hdc_screen);
        return 0;
    }

    case WM_SIZE:
    {
        int width = LOWORD(lparam);
        int height = HIWORD(lparam);
        int status_h = 22;

        SendMessageW(g_hStatusWnd, WM_SIZE, wparam, lparam);
        SetWindowPos(g_hPaletteWnd, NULL, 74, height - status_h - 42, width - 80, 40, SWP_NOZORDER);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDM_FILE_NEW:
            clear_canvas();
            lstrcpyW(g_current_file, L"");
            SetWindowTextW(hwnd, L"untitled - Paint");
            break;

        case IDM_FILE_OPEN:
        {
            OPENFILENAMEW ofn;
            WCHAR path[MAX_PATH] = L"";

            memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameW(&ofn))
            {
                if (load_bitmap(path))
                {
                    WCHAR title[MAX_PATH + 32];
                    lstrcpyW(g_current_file, path);
                    swprintf(title, ARRAY_SIZE(title), L"%s - Paint", wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path);
                    SetWindowTextW(hwnd, title);
                }
            }
            break;
        }

        case IDM_FILE_SAVE:
            if (g_current_file[0] != L'\0')
            {
                save_bitmap(g_current_file);
                break;
            }
            /* Fall through to Save As */

        case IDM_FILE_SAVEAS:
        {
            OPENFILENAMEW ofn;
            WCHAR path[MAX_PATH] = L"untitled.bmp";

            memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Bitmap Files (*.bmp)\0*.bmp\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"bmp";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

            if (GetSaveFileNameW(&ofn))
            {
                if (save_bitmap(path))
                {
                    WCHAR title[MAX_PATH + 32];
                    lstrcpyW(g_current_file, path);
                    swprintf(title, ARRAY_SIZE(title), L"%s - Paint", wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path);
                    SetWindowTextW(hwnd, title);
                }
            }
            break;
        }

        case IDM_EDIT_UNDO:
            restore_undo_state();
            break;

        case IDM_EDIT_CLEAR:
            clear_canvas();
            break;

        case IDM_HELP_ABOUT:
            ShellAboutW(hwnd, L"Paint", L"WineExplorer Paint",
                        LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP)));
            break;

        case IDM_FILE_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        DeleteDC(g_hdc_canvas);
        DeleteObject(g_hbm_canvas);
        DeleteObject(g_hbm_undo);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSW wc;
    MSG msg;
    HACCEL hAccel;

    g_hInst = hInstance;
    InitCommonControls();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = main_proc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = L"WinePaintApp";
    RegisterClassW(&wc);

    wc.lpfnWndProc = canvas_proc;
    wc.lpszMenuName = NULL;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_CROSS);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = L"PaintCanvas";
    RegisterClassW(&wc);

    wc.lpfnWndProc = toolbox_proc;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PaintToolbox";
    RegisterClassW(&wc);

    wc.lpfnWndProc = palette_proc;
    wc.lpszClassName = L"PaintPalette";
    RegisterClassW(&wc);

    g_hMainWnd = CreateWindowExW(0, L"WinePaintApp", L"untitled - Paint",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 740, 580,
                                 NULL, NULL, hInstance, NULL);

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(ID_ACCEL));

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (!TranslateAcceleratorW(g_hMainWnd, hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
