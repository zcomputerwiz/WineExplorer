/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * lab: SetCursorPos(x, y), to move the pointer of the lab display out of the way before a window is shown (a window shown under the pointer
 * is sent WM_NCHITTEST). Build: x86_64-w64-mingw32-gcc -O2 -o setcursor.exe setcursor.c -luser32
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    POINT pt = {0, 0};

    if (argc < 3) return 2;
    printf("SetCursorPos: %d\n", SetCursorPos(atoi(argv[1]), atoi(argv[2])));
    GetCursorPos(&pt);
    printf("cursor now %ld,%ld\n", pt.x, pt.y);
    return 0;
}
