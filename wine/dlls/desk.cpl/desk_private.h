/*
 * WineExplorer Desktop Environment - desk.cpl Private Definitions
 */

#ifndef __WINE_DESKCPL_PRIVATE_H
#define __WINE_DESKCPL_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"

#include "resource.h"
#include "preview.h"

extern INT_PTR CALLBACK theme_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
extern INT_PTR CALLBACK desktop_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
extern INT_PTR CALLBACK appearance_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
extern INT_PTR CALLBACK settings_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

#endif /* __WINE_DESKCPL_PRIVATE_H */
