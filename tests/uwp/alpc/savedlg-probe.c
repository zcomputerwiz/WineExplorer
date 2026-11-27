/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Wine lab): shows the common item Save dialog from a plain program (no owner, or an owner window of its own with
 * SAVEDLG_OWNER set), with the calls Notepad makes before Show, so a screenshot can tell whether the dialog paints outside Notepad.
 * The dialog closes when the process is killed.
 */
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    static const COMDLG_FILTERSPEC types[2] = {{L"Text documents (*.txt)", L"*.txt"}, {L"All files (*.*)", L"*.*"}};
    IFileSaveDialog *dlg;
    HWND owner = NULL;
    FILEOPENDIALOGOPTIONS opts = 0;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (getenv("SAVEDLG_OWNER"))
    {
        owner = CreateWindowExW(0, L"STATIC", L"owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 800, 600, NULL, NULL, NULL, NULL);
        UpdateWindow(owner);
    }
    hr = CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileSaveDialog, (void **)&dlg);
    printf("create hr %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IFileSaveDialog_SetFileName(dlg, L"hello.txt");
    IFileSaveDialog_SetTitle(dlg, L"Save as");
    IFileSaveDialog_SetDefaultExtension(dlg, L"txt");
    IFileSaveDialog_SetFileTypes(dlg, 2, types);
    IFileSaveDialog_GetOptions(dlg, &opts);
    IFileSaveDialog_SetOptions(dlg, 0x884a);
    hr = IFileSaveDialog_Show(dlg, owner);
    printf("show hr %#lx\n", hr);
    IFileSaveDialog_Release(dlg);
    return 0;
}
