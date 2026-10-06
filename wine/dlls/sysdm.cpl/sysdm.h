/*
 * WineExplorer Desktop Environment - sysdm.cpl Internal Header
 */

#ifndef __WINE_SYSDM_H
#define __WINE_SYSDM_H

#include <windows.h>
#include <commctrl.h>

#include "resource.h"

extern HINSTANCE g_hInstance;

INT_PTR CALLBACK general_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK compname_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK advanced_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

void show_performance_dialog(HWND parent);
void show_envvars_dialog(HWND parent);

#endif /* __WINE_SYSDM_H */
