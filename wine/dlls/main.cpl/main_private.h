/*
 * WineExplorer Desktop Environment - main.cpl Private Definitions
 */

#ifndef __WINE_MAIN_CPL_PRIVATE_H
#define __WINE_MAIN_CPL_PRIVATE_H

#include <windows.h>
#include <commctrl.h>
#include <cpl.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>

#include "resource.h"

extern HINSTANCE g_hinst;

LONG mouse_applet(HWND parent);
LONG keyboard_applet(HWND parent);

INT_PTR CALLBACK button_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK pointer_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK motion_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK wheel_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK mouse_hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

INT_PTR CALLBACK keyboard_speed_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK keyboard_hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

INT_PTR CALLBACK clicklock_settings_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

#endif /* __WINE_MAIN_CPL_PRIVATE_H */
