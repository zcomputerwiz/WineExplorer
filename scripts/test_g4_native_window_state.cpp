// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Read-only state of the original Electron fixture in the same private prefix.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static BOOL CALLBACK inspect(HWND hwnd, LPARAM parameter)
{
    char title[256];
    GetWindowTextA(hwnd, title, sizeof(title));
    if (_stricmp(title, "wineexplorer-g4-diagnostic")) return TRUE;
    RECT rect = {};
    if (!GetWindowRect(hwnd, &rect)) return TRUE;
    ++*(unsigned int *)parameter;
    printf("NATIVE_WINDOW fixed=%llu visible=%u x=%ld y=%ld width=%ld height=%ld\n",
           (unsigned long long)(ULONG_PTR)GetPropW(hwnd, L"__wine_wayland_render_scale"),
           IsWindowVisible(hwnd), rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    return TRUE;
}

int main()
{
    unsigned int matches = 0;
    EnumWindows(inspect, (LPARAM)&matches);
    return matches == 1 ? 0 : 1;
}
