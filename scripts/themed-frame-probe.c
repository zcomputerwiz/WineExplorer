/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Verify themed frame shapes on an English WineExplorer test desktop.
 * Build: x86_64-w64-mingw32-gcc -o themed-frame-probe.exe themed-frame-probe.c -lgdi32
 * Open My Computer under Luna, run this probe, then apply Windows Classic and
 * run again with the argument "classic". Use an isolated display/prefix.
 */
#include <windows.h>
#include <stdio.h>

static int errors;

static void check_shape(HWND hwnd, const char *label, BOOL themed)
{
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    RECT box, window;
    int result = GetWindowRgn(hwnd, region);

    GetRgnBox(region, &box);
    GetWindowRect(hwnd, &window);
    printf("%s: region=%d box=%ld,%ld,%ld,%ld caption=%d corner=%d\n", label,
           result, box.left, box.top, box.right, box.bottom,
           PtInRegion(region, 20, 10), PtInRegion(region, 0, 0));
    if (themed && (result == ERROR || !PtInRegion(region, 20, 10) || PtInRegion(region, 0, 0) ||
                   box.right != window.right - window.left || box.bottom != window.bottom - window.top))
        errors++;
    if (!themed && result != ERROR) errors++;
    DeleteObject(region);
}

int main(int argc, char **argv)
{
    HWND hwnd = FindWindowW(NULL, L"My Computer");
    HRGN region, copy;
    RECT box, window;
    BOOL preserved;

    (void)argv;
    if (!hwnd) { fprintf(stderr, "Open My Computer first\n"); return 1; }
    if (argc > 1)
    {
        check_shape(hwnd, "Classic shape cleared", FALSE);
        return errors;
    }
    check_shape(hwnd, "Normal Luna", TRUE);
    ShowWindow(hwnd, SW_MAXIMIZE);
    Sleep(300);
    region = CreateRectRgn(0, 0, 0, 0);
    if (GetWindowRgn(hwnd, region) == ERROR) errors++;
    GetRgnBox(region, &box);
    GetWindowRect(hwnd, &window);
    printf("Maximized region: %ld x %ld\n", box.right, box.bottom);
    if (box.right != window.right - window.left || box.bottom != window.bottom - window.top) errors++;
    DeleteObject(region);
    ShowWindow(hwnd, SW_RESTORE);
    Sleep(300);
    check_shape(hwnd, "Restored Luna", TRUE);

    region = CreateRectRgn(10, 10, 300, 200);
    if (!SetWindowRgn(hwnd, region, TRUE)) { DeleteObject(region); return 1; }
    SetWindowPos(hwnd, NULL, 0, 0, 600, 450, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    copy = CreateRectRgn(0, 0, 0, 0);
    GetWindowRgn(hwnd, copy);
    region = CreateRectRgn(10, 10, 300, 200);
    preserved = EqualRgn(copy, region);
    printf("Application region preserved after resize/repaint: %d\n", preserved);
    if (!preserved) errors++;
    DeleteObject(copy);
    DeleteObject(region);
    SetWindowRgn(hwnd, NULL, TRUE);
    RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    Sleep(300);
    check_shape(hwnd, "Theme restored after app region clear", TRUE);
    return errors;
}
