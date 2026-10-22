// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Drives the explorer tray balloon path: add an icon with a balloon, modify it, delete it.
// Exit codes: 0 done, 2 no tray window, 3 a Shell_NotifyIcon call failed.
#include <windows.h>
#include <shellapi.h>
#include <cstdio>

static int notify(DWORD message, NOTIFYICONDATAW *data, const char *name)
{
    if (Shell_NotifyIconW(message, data)) return 0;
    printf("Shell_NotifyIconW %s failed (error %lu)\n", name, GetLastError());
    return 3;
}

static void report(const char *path, const char *text)
{
    printf("%s\n", text);
    if (!path) return;
    FILE *file = fopen(path, "w");
    if (file) {fputs(text, file); fclose(file);}
}

int main(int argc, char **argv)
{
    const char *result_path = argc > 1 ? argv[1] : NULL;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"WexTrayProbe";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"probe", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);

    for (int i = 0; i < 300 && !FindWindowW(L"Shell_TrayWnd", NULL); ++i) Sleep(100);
    if (!FindWindowW(L"Shell_TrayWnd", NULL)) {report(result_path, "no Shell_TrayWnd"); return 2;}

    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    data.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    lstrcpyW(data.szTip, L"probe");
    lstrcpyW(data.szInfo, L"first balloon");
    lstrcpyW(data.szInfoTitle, L"title");
    data.dwInfoFlags = NIIF_INFO;
    int rc = notify(NIM_ADD, &data, "NIM_ADD");
    Sleep(1500);
    lstrcpyW(data.szInfo, L"second balloon");
    if (!rc) rc = notify(NIM_MODIFY, &data, "NIM_MODIFY");
    Sleep(1500);
    if (!rc) rc = notify(NIM_DELETE, &data, "NIM_DELETE");
    Sleep(500);
    report(result_path, rc ? "tray_balloon_probe=FAIL" : "tray_balloon_probe=DONE");
    return rc;
}
