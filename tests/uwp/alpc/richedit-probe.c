/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The RichEdit control Notepad (Windows App SDK build) ships in its package, loaded from the package directory, driven with the
 * messages Notepad sends it (measured with wintrace on Windows 11) and some more of the common ones. Runs on Windows and under Wine
 * (the comparison runner shows what differs): this tells whether Wine's environment (Direct2D, DirectWrite, text services, the
 * window and clipboard layers) is enough for the native control. Only public messages and the window class the app uses.
 *
 * Usage: richedit-probe [path to riched20.dll]; without a path the Notepad package directory is searched
 * (C:\Program Files\WindowsApps on Windows, %LOCALAPPDATA%\WineExplorer\WindowsApps under Wine).
 */
#include <windows.h>
#include <richedit.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void show_text(const char *what, HWND h)
{
    WCHAR buf[256] = L"";
    LRESULT n = SendMessageW(h, WM_GETTEXT, 256, (LPARAM)buf);
    int i;
    printf("%-44s len %Id text \"", what, n);
    for (i = 0; buf[i] && i < 60; i++) { if (buf[i] == '\r') printf("\\r"); else if (buf[i] == '\n') printf("\\n"); else if (buf[i] < 0x80) putchar(buf[i]); else printf("\\u%04x", buf[i]); }
    printf("\"\n");
}

static BOOL find_riched(WCHAR *out, DWORD len)
{
    const WCHAR *patterns[2];
    WCHAR local[MAX_PATH], pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    int i;

    /* a copy next to the program (the package directory is not readable for a program without package identity on Windows;
     * never in the repository) */
    GetModuleFileNameW(NULL, pat, MAX_PATH);
    *wcsrchr(pat, '\\') = 0;
    swprintf(out, len, L"%ls\\riched\\riched20.dll", pat);
    if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return TRUE;

    GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    patterns[0] = L"C:\\Program Files\\WindowsApps\\Microsoft.WindowsNotepad_*";
    swprintf(pat, MAX_PATH, L"%ls\\WineExplorer\\WindowsApps\\Microsoft.WindowsNotepad_*", local);
    patterns[1] = pat;
    for (i = 0; i < 2; i++)
    {
        HANDLE h = FindFirstFileW(patterns[i], &fd);
        WCHAR dir[MAX_PATH];
        if (h == INVALID_HANDLE_VALUE) continue;
        do
        {
            wcscpy(dir, patterns[i]);
            *wcsrchr(dir, '\\') = 0;
            swprintf(out, len, L"%ls\\%ls\\Notepad\\riched20.dll", dir, fd.cFileName);
            if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) { FindClose(h); return TRUE; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    WCHAR path[MAX_PATH];
    HMODULE mod;
    HWND parent, edit;
    WNDCLASSW cls;
    LRESULT r;
    CHARRANGE cr;
    TEXTRANGEW tr;
    GETTEXTLENGTHEX gtl;
    SETTEXTEX ste;
    CHARFORMAT2W cf;
    WCHAR buf[128];
    DWORD ver;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1) MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
    else if (!find_riched(path, MAX_PATH)) { printf("no riched20.dll of the Notepad package found\n"); return 1; }
    mod = LoadLibraryW(path);
    printf("LoadLibrary(package riched20): %s\n", mod ? "ok" : "failed");
    if (!mod) { printf("  error %lu\n", GetLastError()); return 1; }
    if (!GetClassInfoW(NULL, L"RichEditD2DPT", &cls)) { printf("class RichEditD2DPT: not registered (error %lu)\n", GetLastError()); return 1; }
    printf("class RichEditD2DPT: registered, style %#x, extra %d\n", cls.style, cls.cbWndExtra);

    parent = CreateWindowExW(0, L"STATIC", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, NULL, NULL, GetModuleHandleW(NULL), NULL);
    edit = CreateWindowExW(0, L"RichEditD2DPT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_NOHIDESEL,
                           0, 0, 780, 560, parent, NULL, GetModuleHandleW(NULL), NULL);
    printf("CreateWindow(RichEditD2DPT): %s\n", edit ? "ok" : "failed");
    if (!edit) { printf("  error %lu\n", GetLastError()); return 1; }
    ShowWindow(parent, SW_SHOW);
    ShowWindow(edit, SW_SHOW);
    UpdateWindow(parent);
    printf("IsWindowUnicode %d, GetClassName ", IsWindowUnicode(edit));
    { WCHAR cn[64]; GetClassNameW(edit, cn, 64); printf("%ls\n", cn); }

    /* the messages Notepad sends while it sets the control up */
    SendMessageW(edit, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    printf("EM_GETTEXTMODE: %#Ix\n", SendMessageW(edit, EM_GETTEXTMODE, 0, 0));
    r = SendMessageW(edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_UPDATE);
    printf("EM_SETEVENTMASK returns %#Ix, EM_GETEVENTMASK %#Ix\n", r, SendMessageW(edit, EM_GETEVENTMASK, 0, 0));
    r = SendMessageW(edit, EM_SETBKGNDCOLOR, 0, 0x272727);
    printf("EM_SETBKGNDCOLOR returns %#Ix\n", r);
    memset(&cf, 0, sizeof(cf)); cf.cbSize = sizeof(cf);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    printf("EM_GETCHARFORMAT(default): mask %#lx effects %#lx height %ld face \"%ls\" weight %d\n", cf.dwMask, cf.dwEffects, cf.yHeight, cf.szFaceName, cf.wWeight);
    cf.dwMask = CFM_COLOR | CFM_SIZE | CFM_FACE; cf.crTextColor = 0xffffff; cf.yHeight = 320;
    if (argc > 2) MultiByteToWideChar(CP_ACP, 0, argv[2], -1, cf.szFaceName, LF_FACESIZE);   /* a face for systems without Consolas */
    else wcscpy(cf.szFaceName, L"Consolas");
    r = SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    printf("EM_SETCHARFORMAT(all) returns %#Ix\n", r);
    memset(&cf, 0, sizeof(cf)); cf.cbSize = sizeof(cf);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    printf("after: height %ld face \"%ls\" color %#lx\n", cf.yHeight, cf.szFaceName, cf.crTextColor);
    printf("EM_SETUNDOLIMIT(100) returns %#Ix\n", SendMessageW(edit, EM_SETUNDOLIMIT, 100, 0));
    printf("EM_GETZOOM: ");
    { int num = -1, den = -1; r = SendMessageW(edit, EM_GETZOOM, (WPARAM)&num, (LPARAM)&den); printf("%#Ix %d/%d\n", r, num, den); }
    printf("EM_SETZOOM(3,2): %#Ix\n", SendMessageW(edit, EM_SETZOOM, 3, 2));
    { int num = -1, den = -1; SendMessageW(edit, EM_GETZOOM, (WPARAM)&num, (LPARAM)&den); printf("zoom now %d/%d\n", num, den); SendMessageW(edit, EM_SETZOOM, 0, 0); }
    printf("EM_SETEDITSTYLE(0xe14018): %#Ix\n", SendMessageW(edit, EM_SETEDITSTYLE, 0xe14018, 0xe14018));
    printf("EM_SETTYPOGRAPHYOPTIONS: %#Ix\n", SendMessageW(edit, EM_SETTYPOGRAPHYOPTIONS, TO_ADVANCEDTYPOGRAPHY, TO_ADVANCEDTYPOGRAPHY));
    printf("EM_GETTYPOGRAPHYOPTIONS: %#Ix\n", SendMessageW(edit, EM_GETTYPOGRAPHYOPTIONS, 0, 0));
    printf("EM_GETOLEINTERFACE: %s\n", SendMessageW(edit, EM_GETOLEINTERFACE, 0, (LPARAM)&r) ? "ok" : "no");
    printf("EM_AUTOURLDETECT(0): %#Ix\n", SendMessageW(edit, EM_AUTOURLDETECT, 0, 0));
    printf("EM_SETWORDWRAPMODE: %#Ix\n", SendMessageW(edit, EM_SETWORDWRAPMODE, WBF_WORDWRAP | WBF_WORDBREAK, 0));
    printf("EM_SHOWSCROLLBAR(vert): %#Ix\n", SendMessageW(edit, EM_SHOWSCROLLBAR, SB_VERT, 1));
    {
        RECT rc = {0, 0, 0, 0};
        SendMessageW(edit, EM_GETRECT, 0, (LPARAM)&rc);
        printf("EM_GETRECT: %ld,%ld-%ld,%ld\n", rc.left, rc.top, rc.right, rc.bottom);
        rc.left = 8; rc.top = 4; rc.right = 700; rc.bottom = 500;
        SendMessageW(edit, EM_SETRECT, 0, (LPARAM)&rc);
        memset(&rc, 0, sizeof(rc));
        SendMessageW(edit, EM_GETRECT, 0, (LPARAM)&rc);
        printf("after EM_SETRECT: %ld,%ld-%ld,%ld\n", rc.left, rc.top, rc.right, rc.bottom);
    }

    /* text */
    show_text("initial", edit);
    printf("EM_GETMODIFY initially %Id\n", SendMessageW(edit, EM_GETMODIFY, 0, 0));
    r = SendMessageW(edit, WM_SETTEXT, 0, (LPARAM)L"Hello\r\nworld \u00e9\u4e2d\U0001F600 end");
    printf("WM_SETTEXT returns %Id\n", r);
    show_text("after WM_SETTEXT", edit);
    OutputDebugStringA("MARK before EM_GETLINECOUNT\n");
    gtl.flags = GTL_DEFAULT; gtl.codepage = 1200;
    printf("EM_GETTEXTLENGTHEX(default): %Id, (numchars): ", SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0));
    gtl.flags = GTL_NUMCHARS; printf("%Id, (numbytes): ", SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0));
    gtl.flags = GTL_NUMBYTES; printf("%Id\n", SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0));
    printf("WM_GETTEXTLENGTH %Id, EM_GETLINECOUNT %Id, EM_LINELENGTH(0) %Id, EM_LINEINDEX(1) %Id\n", SendMessageW(edit, WM_GETTEXTLENGTH, 0, 0),
           SendMessageW(edit, EM_GETLINECOUNT, 0, 0), SendMessageW(edit, EM_LINELENGTH, 0, 0), SendMessageW(edit, EM_LINEINDEX, 1, 0));
    OutputDebugStringA("MARK after EM_GETLINECOUNT\n");
    printf("EM_GETMODIFY after set %Id\n", SendMessageW(edit, EM_GETMODIFY, 0, 0));
    SendMessageW(edit, EM_SETMODIFY, 0, 0);
    printf("EM_GETMODIFY after reset %Id\n", SendMessageW(edit, EM_GETMODIFY, 0, 0));

    r = SendMessageW(edit, EM_SETSEL, 2, 7);
    printf("EM_SETSEL(2,7) returns %Id; ", r);
    { DWORD s = 99, e = 99; SendMessageW(edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e); printf("EM_GETSEL %lu..%lu; ", s, e); }
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr); printf("EM_EXGETSEL %ld..%ld\n", cr.cpMin, cr.cpMax);
    memset(buf, 0, sizeof(buf));
    printf("EM_GETSELTEXT: %Id \"%ls\"\n", SendMessageW(edit, EM_GETSELTEXT, 0, (LPARAM)buf), buf);
    memset(buf, 0, sizeof(buf)); tr.chrg.cpMin = 0; tr.chrg.cpMax = 5; tr.lpstrText = buf;
    printf("EM_GETTEXTRANGE(0,5): %Id \"%ls\"\n", SendMessageW(edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr), buf);
    SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)L"XY");
    show_text("after EM_REPLACESEL", edit);
    printf("EM_CANUNDO %Id, EM_UNDO %Id, ", SendMessageW(edit, EM_CANUNDO, 0, 0), SendMessageW(edit, EM_UNDO, 0, 0));
    show_text("after undo", edit);
    printf("EM_CANREDO %Id, EM_REDO %Id, ", SendMessageW(edit, EM_CANREDO, 0, 0), SendMessageW(edit, EM_REDO, 0, 0));
    show_text("after redo", edit);
    printf("EM_EXLINEFROMCHAR(8) %Id, EM_LINEFROMCHAR(8) %Id\n", SendMessageW(edit, EM_EXLINEFROMCHAR, 0, 8), SendMessageW(edit, EM_LINEFROMCHAR, 8, 0));
    { WCHAR line[64] = {64}; memset(line + 1, 0, 62 * 2); line[0] = 60; printf("EM_GETLINE(0): %Id \"%ls\"\n", SendMessageW(edit, EM_GETLINE, 0, (LPARAM)line), line); }
    { FINDTEXTEXW ft; ft.chrg.cpMin = 0; ft.chrg.cpMax = -1; ft.lpstrText = L"world"; r = SendMessageW(edit, EM_FINDTEXTEXW, FR_DOWN, (LPARAM)&ft);
      printf("EM_FINDTEXTEXW(\"world\"): %Id (%ld..%ld)\n", r, ft.chrgText.cpMin, ft.chrgText.cpMax); }
    ste.flags = ST_DEFAULT; ste.codepage = 1200;
    printf("EM_SETTEXTEX(ST_DEFAULT): %Id; ", SendMessageW(edit, EM_SETTEXTEX, (WPARAM)&ste, (LPARAM)L"one\ntwo\r\nthree\rfour"));
    show_text("text", edit);
    printf("lines %Id\n", SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
    { GETTEXTEX gt = {0}; WCHAR out[64] = L""; gt.cb = sizeof(out); gt.flags = GT_USECRLF; gt.codepage = 1200;
      printf("EM_GETTEXTEX(GT_USECRLF): %Id ", SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)out));
      for (i = 0; out[i]; i++) { if (out[i] == '\r') printf("\\r"); else if (out[i] == '\n') printf("\\n"); else putchar(out[i]); } printf("\n"); }
    SendMessageW(edit, EM_SETSEL, 0, -1);
    SendMessageW(edit, WM_CLEAR, 0, 0);
    show_text("after WM_CLEAR of all", edit);

    /* typing through the message queue */
    SetFocus(edit);
    SendMessageW(edit, WM_CHAR, 'a', 0x1e0001); SendMessageW(edit, WM_CHAR, 'B', 0x300001); SendMessageW(edit, WM_CHAR, 0x4e2d, 1); SendMessageW(edit, WM_CHAR, '\r', 0x1c0001);
    SendMessageW(edit, WM_CHAR, 'z', 0x2c0001);
    show_text("after WM_CHAR a B U+4E2D Enter z", edit);
    printf("EM_GETLINECOUNT %Id\n", SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
    SendMessageW(edit, WM_KEYDOWN, VK_BACK, 0xe0001); SendMessageW(edit, WM_KEYUP, VK_BACK, 0xc00e0001);
    show_text("after backspace", edit);
    /* messages of the newer control that Notepad sends and that have no public header */
    { static const UINT priv[] = {0x53b, 0x540, 0x58d, 0x596, 0x597, 0x598, 0x5a0, 0x150c, 0x150d};
      for (i = 0; i < (int)(sizeof(priv) / sizeof(priv[0])); i++) printf("private message %#x (wp 1, lp 0) returns %Id\n", priv[i], SendMessageW(edit, priv[i], 1, 0)); }
    ver = (DWORD)SendMessageW(edit, EM_GETLANGOPTIONS, 0, 0);
    printf("EM_GETLANGOPTIONS %#lx, EM_SETLANGOPTIONS %Id\n", ver, SendMessageW(edit, EM_SETLANGOPTIONS, 0, IMF_DUALFONT));
    DestroyWindow(edit);
    DestroyWindow(parent);
    printf("done\n");
    return 0;
}
