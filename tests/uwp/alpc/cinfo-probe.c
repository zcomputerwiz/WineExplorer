/* Black-box probe on Windows 11: the thread's Win32ClientInfo block (TEB+0x800, 62 pointer-sized slots) and Win32ThreadInfo
 * (TEB+0x78) before and after creating a window, after NtUserInitThreadCoreMessagingIocp2 and after Drain2. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef ULONG_PTR (WINAPI *drain2_t)(HWND, void *, void *, void *);

#define NSLOT 62
static ULONG_PTR snap[8][NSLOT + 1];
static const char *names[8];
static int ns;

static void take(const char *name)
{
    BYTE *teb = (BYTE *)NtCurrentTeb();
    memcpy(snap[ns], teb + 0x800, NSLOT * sizeof(ULONG_PTR));
    snap[ns][NSLOT] = *(ULONG_PTR *)(teb + 0x78);
    names[ns++] = name;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

int main(void)
{
    HMODULE win32u = LoadLibraryA("win32u.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    drain2_t pDrain2 = (void *)GetProcAddress(win32u, "NtUserDrainThreadCoreMessagingCompletions2");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out = 0;
    int i, k;

    LoadLibraryA("user32.dll");
    take("start");
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"CinfoProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"CinfoProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    take("window");
    {
        MSG msg;
        PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
        take("peek");
    }
    pInit2(hwnd, &out, 0, 0);
    take("init2");
    pDrain2(hwnd, NULL, NULL, NULL);
    take("drain2");
    PostMessageW(hwnd, 0x60, 5, 4);
    take("post");
    for (k = 1; k < ns; k++)
        for (i = 0; i <= NSLOT; i++)
            if (snap[k][i] != snap[k - 1][i])
                printf("%s -> %s: slot %d (%s): %#Ix -> %#Ix\n", names[k - 1], names[k], i, i == NSLOT ? "Win32ThreadInfo" : "ClientInfo",
                       snap[k - 1][i], snap[k][i]);
    printf("final non-zero ClientInfo slots:\n");
    for (i = 0; i < NSLOT; i++) if (snap[ns - 1][i]) printf("  slot %d = %#Ix\n", i, snap[ns - 1][i]);
    return 0;
}
