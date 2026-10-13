/*
 * View as a slide show: Windows XP's full screen slide show of the pictures of a folder
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
#include <windows.h>
#include <shlwapi.h>
#include <gdiplus.h>

#include "slideshow.h"
#include "module.h"


#define SLIDESHOW_CLASS L"WineExplorerSlideShow"
#define SLIDE_SECONDS   5          /* Windows XP's pace */

struct slideshow
{
    WCHAR **files;
    int count, current;
    GpImage *image;
};

static BOOL is_picture(const WCHAR *name)
{
    static const WCHAR *exts[] = { L".bmp", L".dib", L".jpg", L".jpeg", L".jpe", L".gif", L".png", L".tif", L".tiff" };
    const WCHAR *ext = PathFindExtensionW(name);
    UINT i;

    for (i = 0; i < ARRAY_SIZE(exts); i++) if (!wcsicmp(ext, exts[i])) return TRUE;
    return FALSE;
}

static int __cdecl compare_names(const void *a, const void *b)
{
    return StrCmpLogicalW(*(const WCHAR **)a, *(const WCHAR **)b);
}

/* the folder's pictures, in the order Explorer shows names */
static void load_files(struct slideshow *show, const WCHAR *dir)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;
    WCHAR **files;

    swprintf(pattern, ARRAY_SIZE(pattern), L"%s\\*", dir);
    if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) return;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !is_picture(fd.cFileName)) continue;
        if (!(files = realloc(show->files, (show->count + 1) * sizeof(*files)))) break;
        show->files = files;
        swprintf(path, ARRAY_SIZE(path), L"%s\\%s", dir, fd.cFileName);
        show->files[show->count++] = wcsdup(path);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    if (show->count) qsort(show->files, show->count, sizeof(*show->files), compare_names);
}

static void show_picture(HWND hwnd, struct slideshow *show, int index)
{
    if (!show->count) return;
    show->current = (index % show->count + show->count) % show->count;
    if (show->image) GdipDisposeImage(show->image);
    show->image = NULL;
    GdipLoadImageFromFile(show->files[show->current], &show->image);
    InvalidateRect(hwnd, NULL, TRUE);
    SetTimer(hwnd, 1, SLIDE_SECONDS * 1000, NULL);
}

/* the picture as large as fits the screen, never larger than it is, on black */
static void paint(HWND hwnd, struct slideshow *show)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    UINT cx, cy, w, h;
    GpGraphics *graphics;

    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, GetStockObject(BLACK_BRUSH));
    if (show->image && !GdipGetImageWidth(show->image, &cx) && !GdipGetImageHeight(show->image, &cy) && cx && cy &&
        !GdipCreateFromHDC(hdc, &graphics))
    {
        w = cx; h = cy;
        if (w > (UINT)rc.right || h > (UINT)rc.bottom)
        {
            if ((ULONGLONG)cx * rc.bottom > (ULONGLONG)cy * rc.right) { w = rc.right; h = (UINT)((ULONGLONG)cy * rc.right / cx); }
            else { h = rc.bottom; w = (UINT)((ULONGLONG)cx * rc.bottom / cy); }
        }
        GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);
        GdipDrawImageRectI(graphics, show->image, (rc.right - w) / 2, (rc.bottom - h) / 2, w, h);
        GdipDeleteGraphics(graphics);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK slideshow_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct slideshow *show = (struct slideshow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    int i;

    switch (msg)
    {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lparam)->lpCreateParams);
        return 0;
    case WM_PAINT:
        paint(hwnd, show);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        show_picture(hwnd, show, show->current + 1);
        return 0;
    /* Windows XP: a click or the arrows go on or back, Esc ends it */
    case WM_LBUTTONUP:
        show_picture(hwnd, show, show->current + 1);
        return 0;
    case WM_RBUTTONUP:
        show_picture(hwnd, show, show->current - 1);
        return 0;
    case WM_KEYDOWN:
        switch (wparam)
        {
        case VK_ESCAPE: DestroyWindow(hwnd); break;
        case VK_LEFT: case VK_UP: case VK_PRIOR: case VK_BACK: show_picture(hwnd, show, show->current - 1); break;
        case VK_RIGHT: case VK_DOWN: case VK_NEXT: case VK_SPACE: case VK_RETURN: show_picture(hwnd, show, show->current + 1); break;
        case VK_HOME: show_picture(hwnd, show, 0); break;
        case VK_END: show_picture(hwnd, show, show->count - 1); break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (show->image) GdipDisposeImage(show->image);
        for (i = 0; i < show->count; i++) free(show->files[i]);
        free(show->files);
        free(show);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void start_gdiplus(void)
{
    static ULONG_PTR token;

    if (!token)
    {
        struct GdiplusStartupInput input = { 1 };
        GdiplusStartup(&token, &input, NULL);
    }
}

HBITMAP picture_preview(const WCHAR *path, int max_width, int max_height, UINT *width, UINT *height)
{
    BITMAPINFO info = { { sizeof(BITMAPINFOHEADER) } };
    HBITMAP bitmap = NULL;
    GpGraphics *graphics;
    GpImage *image;
    UINT w, h;
    HDC hdc;
    void *bits;

    if (!is_picture(path)) return NULL;
    start_gdiplus();
    if (GdipLoadImageFromFile(path, &image)) return NULL;
    if (!GdipGetImageWidth(image, width) && !GdipGetImageHeight(image, height) && *width && *height)
    {
        w = *width; h = *height;
        if ((int)w > max_width || (int)h > max_height)
        {
            if ((ULONGLONG)w * max_height > (ULONGLONG)h * max_width) { h = max(1, (UINT)((ULONGLONG)h * max_width / w)); w = max_width; }
            else { w = max(1, (UINT)((ULONGLONG)w * max_height / h)); h = max_height; }
        }
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -(int)h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        hdc = CreateCompatibleDC(NULL);
        if ((bitmap = CreateDIBSection(hdc, &info, DIB_RGB_COLORS, &bits, NULL, 0)))
        {
            HBITMAP old = SelectObject(hdc, bitmap);
            if (!GdipCreateFromHDC(hdc, &graphics))
            {
                GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);
                GdipDrawImageRectI(graphics, image, 0, 0, w, h);
                GdipDeleteGraphics(graphics);
            }
            SelectObject(hdc, old);
        }
        DeleteDC(hdc);
    }
    GdipDisposeImage(image);
    return bitmap;
}

/* The slide show of a folder's pictures, starting at first (a path, or NULL for the first picture) */
void slideshow_start(const WCHAR *folder, const WCHAR *first)
{
    struct slideshow *show;
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    int i, start = 0;

    start_gdiplus();
    if (!(show = calloc(1, sizeof(*show)))) return;
    load_files(show, folder);
    if (!show->count) { free(show); return; }
    for (i = 0; first && i < show->count; i++) if (!wcsicmp(show->files[i], first)) start = i;

    wc.lpfnWndProc = slideshow_proc;
    wc.hInstance = explorer_hInstance;
    wc.lpszClassName = SLIDESHOW_CLASS;
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOPMOST, SLIDESHOW_CLASS, L"Slide Show", WS_POPUP | WS_VISIBLE,
                           0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL,
                           wc.hInstance, show);
    if (!hwnd) { free(show); return; }
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    show_picture(hwnd, show, start);
}
