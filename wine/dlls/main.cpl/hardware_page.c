/*
 * WineExplorer Desktop Environment - Hardware Page (Shared for Mouse & Keyboard)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdio.h>
#include <string.h>

#include "main_private.h"

#define MAX_INPUT_DEVICES 32

struct input_device
{
    WCHAR name[128];
    char  phys[96];
    char  handlers[96];
};

/* Parse the kernel's input device list; pointing devices have a "mouse" handler, keyboards a "kbd" handler
 * together with the key/LED/repeat event capabilities (EV=120013) that separate them from power buttons. */
static int read_input_devices(BOOL want_mouse, struct input_device *out, int max)
{
    FILE *f = fopen("/proc/bus/input/devices", "r");
    char line[512];
    char name[128] = "", phys[96] = "", handlers[96] = "", ev[32] = "";
    int count = 0;
    BOOL in_block = FALSE;

    if (!f) return 0;

    for (;;)
    {
        BOOL eof = !fgets(line, sizeof(line), f);
        line[strcspn(line, "\r\n")] = 0;

        if (eof || !line[0])
        {
            if (in_block && count < max)
            {
                BOOL is_mouse = strstr(handlers, "mouse") != NULL;
                BOOL is_kbd = strstr(handlers, "kbd") != NULL && !strcmp(ev, "120013");
                if ((want_mouse && is_mouse) || (!want_mouse && is_kbd))
                {
                    MultiByteToWideChar(CP_UTF8, 0, name, -1, out[count].name, ARRAY_SIZE(out[count].name));
                    lstrcpynA(out[count].phys, phys, sizeof(out[count].phys));
                    lstrcpynA(out[count].handlers, handlers, sizeof(out[count].handlers));
                    count++;
                }
            }
            name[0] = phys[0] = handlers[0] = ev[0] = 0;
            in_block = FALSE;
            if (eof) break;
            continue;
        }

        in_block = TRUE;
        if (!strncmp(line, "N: Name=\"", 9))
        {
            lstrcpynA(name, line + 9, sizeof(name));
            if (name[0] && name[strlen(name) - 1] == '"') name[strlen(name) - 1] = 0;
        }
        else if (!strncmp(line, "P: Phys=", 8)) lstrcpynA(phys, line + 8, sizeof(phys));
        else if (!strncmp(line, "H: Handlers=", 12)) lstrcpynA(handlers, line + 12, sizeof(handlers));
        else if (!strncmp(line, "B: EV=", 6)) lstrcpynA(ev, line + 6, sizeof(ev));
    }
    fclose(f);
    return count;
}

static struct input_device g_mouse_devs[MAX_INPUT_DEVICES], g_kbd_devs[MAX_INPUT_DEVICES];
static int g_mouse_count, g_kbd_count;

static void init_hardware_listview(HWND hwnd, BOOL is_mouse)
{
    HWND hList = GetDlgItem(hwnd, IDC_DEV_LIST);
    struct input_device *devs = is_mouse ? g_mouse_devs : g_kbd_devs;
    int *count = is_mouse ? &g_mouse_count : &g_kbd_count;
    LVCOLUMNW lvc;
    LVITEMW lvi;
    int i;

    SendMessageW(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    memset(&lvc, 0, sizeof(lvc));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.cx = 140;
    lvc.pszText = (LPWSTR)(is_mouse ? L"Pointing Device" : L"Keyboard Device");
    SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

    lvc.cx = 90;
    lvc.pszText = (LPWSTR)L"Status";
    SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

    *count = read_input_devices(is_mouse, devs, MAX_INPUT_DEVICES);

    for (i = 0; i < *count; i++)
    {
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.pszText = devs[i].name;
        SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 1;
        lvi.pszText = (LPWSTR)L"Working";   /* the kernel only lists devices it has bound a driver to */
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }

    if (*count)
        SendMessageW(hList, LVM_SETITEMSTATE, 0, MAKELPARAM(LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED));
}

static int selected_index(HWND hwnd)
{
    return (int)SendDlgItemMessageW(hwnd, IDC_DEV_LIST, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

static void describe_selection(HWND hwnd, BOOL is_mouse)
{
    struct input_device *devs = is_mouse ? g_mouse_devs : g_kbd_devs;
    int count = is_mouse ? g_mouse_count : g_kbd_count;
    int sel = selected_index(hwnd);
    WCHAR text[320], phys[96], handlers[96];

    if (sel < 0 || sel >= count)
    {
        SetDlgItemTextW(hwnd, IDC_DEV_DESC, count ? L"" : L"No devices were reported by the host input subsystem.");
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, devs[sel].phys, -1, phys, ARRAY_SIZE(phys));
    MultiByteToWideChar(CP_UTF8, 0, devs[sel].handlers, -1, handlers, ARRAY_SIZE(handlers));
    swprintf(text, ARRAY_SIZE(text), L"Location: %s\nHandlers: %s", phys[0] ? phys : L"(not reported)", handlers);
    SetDlgItemTextW(hwnd, IDC_DEV_DESC, text);
}

static INT_PTR hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL is_mouse)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        init_hardware_listview(hwnd, is_mouse);
        describe_selection(hwnd, is_mouse);
        return TRUE;

    case WM_NOTIFY:
        if (((NMHDR *)lparam)->idFrom == IDC_DEV_LIST && ((NMHDR *)lparam)->code == LVN_ITEMCHANGED)
            describe_selection(hwnd, is_mouse);
        break;

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_DEV_PROPERTIES)
        {
            struct input_device *devs = is_mouse ? g_mouse_devs : g_kbd_devs;
            int sel = selected_index(hwnd);
            if (sel >= 0 && sel < (is_mouse ? g_mouse_count : g_kbd_count))
            {
                WCHAR text[320], phys[96], handlers[96];
                MultiByteToWideChar(CP_UTF8, 0, devs[sel].phys, -1, phys, ARRAY_SIZE(phys));
                MultiByteToWideChar(CP_UTF8, 0, devs[sel].handlers, -1, handlers, ARRAY_SIZE(handlers));
                swprintf(text, ARRAY_SIZE(text), L"Device: %s\nLocation: %s\nKernel handlers: %s",
                         devs[sel].name, phys[0] ? phys : L"(not reported)", handlers);
                MessageBoxW(hwnd, text, L"Device Properties", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK mouse_hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return hardware_page_proc(hwnd, msg, wparam, lparam, TRUE);
}

INT_PTR CALLBACK keyboard_hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return hardware_page_proc(hwnd, msg, wparam, lparam, FALSE);
}
