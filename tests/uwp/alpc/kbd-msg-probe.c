/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows and Wine), message level only (nothing is injected into the real input stream, nothing leaves the probe's own
 * window): what a window sees for posted keyboard messages and the thread's keyboard state.
 *   1. the messages retrieved for a posted WM_KEYDOWN/WM_KEYUP pair of VK_A, with TranslateMessage (the WM_CHAR it makes), plain and
 *      with the thread's keyboard state set to Shift down and to Caps Lock on (SetKeyboardState);
 *   2. what GetKeyState / GetKeyboardState report after SetKeyboardState;
 *   3. the order of a WH_GETMESSAGE hook, a WH_KEYBOARD hook and the window procedure for one posted key message;
 *   4. MapVirtualKey / VkKeyScan / ToUnicode for a few keys on the active layout (a US layout is assumed for the expected output).
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HWND hwnd;
static char seq[512];

static void add(const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(seq);
    va_start(ap, fmt);
    vsnprintf(seq + n, sizeof(seq) - n, fmt, ap);
    va_end(ap);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN || m == WM_KEYUP || m == WM_CHAR || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP || m == WM_DEADCHAR)
        add(" win[%#x wp %#Ix lp %#Ix]", m, w, l & 0xc0ff0000 ? (l & 0xc0ff0000) : l);
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK kbd_hook(int code, WPARAM w, LPARAM l)
{
    if (code >= 0) add(" kbdhook[code %d vk %#Ix flags %#Ix]", code, w, (l >> 24) & 0xc0);
    return CallNextHookEx(NULL, code, w, l);
}

static LRESULT CALLBACK getmsg_hook(int code, WPARAM w, LPARAM l)
{
    MSG *m = (MSG *)l;
    if (code >= 0 && (m->message == WM_KEYDOWN || m->message == WM_KEYUP)) add(" getmsg[%s %#x]", w == PM_REMOVE ? "remove" : "noremove", m->message);
    return CallNextHookEx(NULL, code, w, l);
}

static void pump(const char *title)
{
    MSG msg;
    int guard = 0;

    seq[0] = 0;
    while (guard++ < 50 && PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE))
    {
        add(" got[%#x wp %#Ix]", msg.message, msg.wParam);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    printf("%-52s%s\n", title, seq);
}

static void post_a(void)
{
    PostMessageW(hwnd, WM_KEYDOWN, 'A', (LPARAM)(1 | (0x1e << 16)));
    PostMessageW(hwnd, WM_KEYUP, 'A', (LPARAM)(1 | (0x1e << 16) | (1u << 30) | (1u << 31)));
}

int main(void)
{
    WNDCLASSW wc = {0};
    BYTE state[256];
    HHOOK h1, h2;
    WCHAR buf[4];
    int i, n;

    setvbuf(stdout, NULL, _IONBF, 0);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"KbdMsgProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"KbdMsgProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);

    printf("== 1. posted VK_A down/up through TranslateMessage\n");
    memset(state, 0, sizeof(state));
    SetKeyboardState(state);
    post_a(); pump("plain");
    memset(state, 0, sizeof(state)); state[VK_SHIFT] = 0x80; state[VK_LSHIFT] = 0x80; SetKeyboardState(state);
    post_a(); pump("Shift down in the thread's state");
    memset(state, 0, sizeof(state)); state[VK_CAPITAL] = 1; SetKeyboardState(state);
    post_a(); pump("Caps Lock on in the thread's state");
    memset(state, 0, sizeof(state)); state[VK_CONTROL] = 0x80; state[VK_LCONTROL] = 0x80; SetKeyboardState(state);
    post_a(); pump("Ctrl down in the thread's state");
    memset(state, 0, sizeof(state)); SetKeyboardState(state);

    printf("== 2. key state after SetKeyboardState\n");
    memset(state, 0, sizeof(state)); state[VK_SHIFT] = 0x80; state[VK_CAPITAL] = 1; state['A'] = 0x80;
    SetKeyboardState(state);
    printf("GetKeyState(VK_SHIFT) %#x, (VK_CAPITAL) %#x, ('A') %#x\n", (unsigned)(GetKeyState(VK_SHIFT) & 0xffff), (unsigned)(GetKeyState(VK_CAPITAL) & 0xffff), (unsigned)(GetKeyState('A') & 0xffff));
    memset(state, 0xee, sizeof(state));
    GetKeyboardState(state);
    printf("GetKeyboardState: VK_SHIFT %#x, VK_CAPITAL %#x, 'A' %#x, VK_LSHIFT %#x\n", state[VK_SHIFT], state[VK_CAPITAL], state['A'], state[VK_LSHIFT]);
    memset(state, 0, sizeof(state)); SetKeyboardState(state);

    printf("== 3. hook order for one posted key message (retrieved with PM_REMOVE)\n");
    h1 = SetWindowsHookExW(WH_KEYBOARD, kbd_hook, NULL, GetCurrentThreadId());
    h2 = SetWindowsHookExW(WH_GETMESSAGE, getmsg_hook, NULL, GetCurrentThreadId());
    post_a(); pump("WH_GETMESSAGE + WH_KEYBOARD hooks");
    UnhookWindowsHookEx(h1); UnhookWindowsHookEx(h2);

    printf("== 4. layout queries (US layout assumed)\n");
    printf("MapVirtualKeyW(VK_A, VK_TO_VSC) %#x, (0x1e, VSC_TO_VK) %#x, (VK_A, VK_TO_CHAR) %#x\n", MapVirtualKeyW('A', MAPVK_VK_TO_VSC), MapVirtualKeyW(0x1e, MAPVK_VSC_TO_VK), MapVirtualKeyW('A', MAPVK_VK_TO_CHAR));
    printf("VkKeyScanW('a') %#x, ('A') %#x, ('1') %#x, ('!') %#x\n", VkKeyScanW('a') & 0xffff, VkKeyScanW('A') & 0xffff, VkKeyScanW('1') & 0xffff, VkKeyScanW('!') & 0xffff);
    memset(state, 0, sizeof(state));
    n = ToUnicode('A', 0x1e, state, buf, 4, 0);
    printf("ToUnicode(VK_A, no modifiers): %d chars, first %#x\n", n, n > 0 ? buf[0] : 0);
    state[VK_SHIFT] = 0x80;
    n = ToUnicode('A', 0x1e, state, buf, 4, 0);
    printf("ToUnicode(VK_A, Shift): %d chars, first %#x\n", n, n > 0 ? buf[0] : 0);
    state[VK_SHIFT] = 0; state[VK_CONTROL] = 0x80;
    n = ToUnicode('A', 0x1e, state, buf, 4, 0);
    printf("ToUnicode(VK_A, Ctrl): %d chars, first %#x\n", n, n > 0 ? buf[0] : 0);
    (void)i;
    DestroyWindow(hwnd);
    return 0;
}
