/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Run inside the opt-in Wayland shell. Reports geometry and activation.
 */
#include <windows.h>
#include <stdio.h>
static HWND windows[3];
static FILE *report;
static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        RECT rect;
        HBRUSH brush = CreateSolidBrush(hwnd == windows[0] ? RGB(255,0,0) :
                                       hwnd == windows[1] ? RGB(0,0,255) : RGB(0,255,0));
        HDC dc = BeginPaint(hwnd, &paint);
        GetClientRect(hwnd, &rect);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
int main(void)
{
    WNDCLASSW cls = {0};
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL), notepad;
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process;
    WCHAR command[] = L"notepad.exe";
    RECT work, rect;
    MSG message;
    DWORD end, next;
    unsigned int index;
    report = fopen("Z:\\tmp\\wayland-shell-probe.txt", "w");
    if (!report) return 1;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    GetWindowRect(tray, &rect);
    fprintf(report, "WORK %ld %ld %ld %ld\nTRAY %ld %ld %ld %ld\n",
            work.left,work.top,work.right,work.bottom,rect.left,rect.top,rect.right,rect.bottom);
    cls.lpfnWndProc = proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"WineWaylandShellProbe";
    RegisterClassW(&cls);
    for (index = 0; index < 3; ++index)
    {
        windows[index] = CreateWindowExW(index == 2 ? WS_EX_NOACTIVATE : 0, cls.lpszClassName,
            index == 0 ? L"Activation red" : index == 1 ? L"Activation blue" : L"Noactivate green",
            WS_OVERLAPPEDWINDOW, index == 2 ? 600 : 100 + index * 100, 100 + index * 60,
            300,230,NULL,NULL,cls.hInstance,NULL);
        ShowWindow(windows[index], index == 2 ? SW_SHOWNOACTIVATE : SW_SHOW);
    }
    if (!FindWindowW(L"Notepad",NULL) && CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process))
    { CloseHandle(process.hThread); CloseHandle(process.hProcess); }
    end = GetTickCount() + 600000;
    next = GetTickCount() + 3000;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if ((LONG)(GetTickCount() - next) >= 0)
        {
            WCHAR title[128];
            HWND active = GetForegroundWindow();
            GetWindowTextW(active,title,128);
            fprintf(report,"FOCUS %p %ls\n",active,title);
            notepad = FindWindowW(L"Notepad",NULL);
            if (notepad) { GetWindowRect(notepad,&rect); fprintf(report,"NOTEPAD %ld %ld %ld %ld minimized=%d\n",rect.left,rect.top,rect.right,rect.bottom,IsIconic(notepad)); }
            fflush(report);
            next = GetTickCount() + 1000;
        }
        Sleep(10);
    }
    for (index=0; index<3; ++index) DestroyWindow(windows[index]);
    fclose(report);
    return 0;
}
