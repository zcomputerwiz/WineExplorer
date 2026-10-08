/*
 * WineExplorer Desktop Environment - timedate.cpl Internal Header
 */

#ifndef __WINE_TIMEDATE_H
#define __WINE_TIMEDATE_H

#include <windows.h>
#include <commctrl.h>

#include "resource.h"

extern HINSTANCE g_hInstance;

void clock_register_class(HINSTANCE hinst);
void clock_unregister_class(HINSTANCE hinst);

INT_PTR CALLBACK dateandtime_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK timezone_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

BOOL uac_run_elevated(HWND owner, const WCHAR *program, const char *cmd);

#define CLM_STOPCLOCK  (WM_USER + 1)
#define CLM_STARTCLOCK (WM_USER + 2)
#define CLM_SETTIME    (WM_USER + 3)

#endif /* __WINE_TIMEDATE_H */
