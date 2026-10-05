/*
 * WineExplorer Desktop Environment - Desktop View Surface
 *
 * Implements complete feature parity with Windows Desktop:
 * - Icon grid layout, auto-arrange, align-to-grid, registry position persistence
 * - Namespace objects: My Computer, My Documents, Recycle Bin, Network Places
 * - Physical directory enumeration from CSIDL_DESKTOPDIRECTORY & CSIDL_COMMON_DESKTOPDIRECTORY
 * - Selection marquee (rubber-band selection box), multi-item selection, drag-and-drop
 * - Desktop context menu (Arrange, Line up, Refresh, New, Properties)
 * - Multi-mode wallpaper rendering engine (Center, Tile, Stretch, Fit, Fill)
 */

#ifndef __WINE_EXPLORER_DESKTOP_VIEW_H
#define __WINE_EXPLORER_DESKTOP_VIEW_H

#include <windows.h>
#include <shlobj.h>

typedef enum _DESKTOP_ITEM_TYPE
{
    DESKTOP_ITEM_NAMESPACE,
    DESKTOP_ITEM_FILE,
    DESKTOP_ITEM_FOLDER,
    DESKTOP_ITEM_SHORTCUT
} DESKTOP_ITEM_TYPE;

typedef struct _DESKTOP_ITEM
{
    DESKTOP_ITEM_TYPE type;
    WCHAR display_name[MAX_PATH];
    WCHAR target_path[MAX_PATH];
    WCHAR arguments[MAX_PATH];
    HICON icon;
    POINT pos;
    RECT  bounds;
    BOOL  selected;
    BOOL  focused;
    int   grid_x;
    int   grid_y;
} DESKTOP_ITEM;

typedef struct _DESKTOP_VIEW
{
    HWND hwnd;
    HWND parent_desktop;

    DESKTOP_ITEM **items;
    int item_count;
    int item_capacity;

    int grid_cx;
    int grid_cy;
    int icon_size;

    BOOL auto_arrange;
    BOOL align_to_grid;
    BOOL show_desktop_icons;

    /* Selection marquee */
    BOOL  marquee_active;
    POINT marquee_start;
    POINT marquee_current;
    RECT  marquee_rect;

    /* Dragging item */
    BOOL  drag_active;
    POINT drag_offset;
    int   dragged_index;

    /* Wallpaper image */
    HBITMAP hbm_wallpaper;
    int     wallpaper_width;
    int     wallpaper_height;
} DESKTOP_VIEW;

/* Desktop View API */
HWND desktop_view_create(HWND parent_desktop, int width, int height);
void desktop_view_destroy(void);

void desktop_view_refresh(void);
void desktop_view_auto_arrange(void);
void desktop_view_align_to_grid(void);
void desktop_view_load_wallpaper(const WCHAR *path);
void desktop_view_paint(HDC hdc, const RECT *clip_rect);

#endif /* __WINE_EXPLORER_DESKTOP_VIEW_H */
