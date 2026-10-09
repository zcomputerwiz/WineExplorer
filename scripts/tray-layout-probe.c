/* Run in the shell's Wine desktop. Reports to Z:\tmp\tray-layout-probe.txt.
 * Build: x86_64-w64-mingw32-gcc tray-layout-probe.c -o tray-layout-probe.exe -mwindows
 * Checks actual visible child-window bounds, independently of layout arithmetic. */
#include <windows.h>
#include <stdio.h>

static FILE *report;
static RECT tray_rect, icons[128], clock_rect, tasks_rect;
static unsigned int count;
static BOOL have_clock, have_tasks;
static int failures;

static BOOL CALLBACK inspect(HWND hwnd, LPARAM unused)
{
    WCHAR cls[128];
    RECT rect;
    (void)unused;
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetClassNameW(hwnd, cls, 128);
    GetWindowRect(hwnd, &rect);
    if (!wcscmp(cls, L"__wine_tray_icon"))
    {
        if (count < 128) icons[count++] = rect;
        else failures++;
        fprintf(report, "Icon: %ld,%ld to %ld,%ld\n", rect.left, rect.top, rect.right, rect.bottom);
    }
    else if (!wcscmp(cls, L"TrayClockWClass")) { clock_rect = rect; have_clock = TRUE; }
    else if (!wcscmp(cls, L"MSTaskListWClass")) { tasks_rect = rect; have_tasks = TRUE; }
    return TRUE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char *command, int show)
{
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    unsigned int i, j;
    RECT intersection;
    (void)instance; (void)previous; (void)command; (void)show;
    report = fopen("Z:\\tmp\\tray-layout-probe.txt", "w");
    if (!report) return 2;
    if (!tray || !IsWindowVisible(tray))
    { fputs("FAIL: no visible shell taskbar\n", report); fclose(report); return 2; }
    GetWindowRect(tray, &tray_rect);
    EnumChildWindows(tray, inspect, 0);
    for (i = 0; i < count; i++)
    {
        if (icons[i].left < tray_rect.left || icons[i].right > tray_rect.right ||
            icons[i].top < tray_rect.top || icons[i].bottom > tray_rect.bottom) failures++;
        if (have_clock && IntersectRect(&intersection, &icons[i], &clock_rect)) failures++;
        if (have_tasks && IntersectRect(&intersection, &icons[i], &tasks_rect)) failures++;
        for (j = 0; j < i; j++)
            if (IntersectRect(&intersection, &icons[i], &icons[j])) failures++;
    }
    fprintf(report, "Visible icons: %u\n%s: %d overlaps or out-of-bounds windows\n",
            count, failures ? "FAIL" : "PASS", failures);
    fclose(report);
    return failures != 0;
}
