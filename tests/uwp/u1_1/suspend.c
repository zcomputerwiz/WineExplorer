/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * suspend.exe: the Wine runs of the suspend mode of the U1.1 view probe (application ViewSuspend). Wine has no process
 * lifetime manager; its CoreApplication suspends a view whose window is hidden and resumes it when the window is shown.
 * This finds the CoreWindow of the probe, waits until the probe has presented, minimizes the window, waits 8 s and
 * restores it. It never sends keys or clicks. */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    HWND window = NULL;
    int i;

    for (i = 0; i < 300 && !(window = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL )); i++) Sleep( 100 );
    if (!window) { printf( "no CoreWindow\n" ); return 1; }
    Sleep( 7000 );   /* the probe writes READY about 4.5 s after its window appears */
    printf( "minimize\n" );
    ShowWindow( window, SW_MINIMIZE );
    Sleep( 8000 );
    printf( "restore\n" );
    ShowWindow( window, SW_RESTORE );
    return 0;
}
