/*
 * WineExplorer Desktop Environment - General System Information Page (sysdm.cpl)
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

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__i386__) || defined(__x86_64__)
#include <intrin.h>
#endif

#include "sysdm.h"

static BOOL query_hardware_cpuid_brand(WCHAR *out_str, size_t max_chars)
{
#if defined(__i386__) || defined(__x86_64__)
    int regs[4];
    char brand[49] = {0};
    char *p;

    __cpuid(regs, 0x80000000);
    if ((unsigned int)regs[0] >= 0x80000004)
    {
        __cpuid((int *)&brand[0], 0x80000002);
        __cpuid((int *)&brand[16], 0x80000003);
        __cpuid((int *)&brand[32], 0x80000004);
        brand[48] = '\0';

        /* Strip leading whitespace */
        p = brand;
        while (*p == ' ') p++;

        if (*p)
        {
            MultiByteToWideChar(CP_ACP, 0, p, -1, out_str, (int)max_chars);
            return TRUE;
        }
    }
#endif
    return FALSE;
}

INT_PTR CALLBACK general_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HKEY hKey;
        WCHAR buf[256];
        DWORD size, mhz = 0;
        MEMORYSTATUSEX mem;
        SYSTEM_INFO si;
        BOOL got_reg_owner = FALSE;
        BOOL got_reg_org = FALSE;
        BOOL got_reg_pid = FALSE;
        BOOL got_cpu = FALSE;

        /* Set authentic Windows banner bitmap */
        {
            HBITMAP hbmLogo = LoadBitmapW(g_hInstance, MAKEINTRESOURCEW(IDB_WINLOGO));
            if (hbmLogo)
            {
                SendDlgItemMessageW(hwnd, IDC_GEN_LOGO_STATIC, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbmLogo);
            }
        }

        /* Platform line: the Wine this shell is running on */
        {
            const char *(CDECL *pwine_get_version)(void) = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version");
            WCHAR ver[64] = L"";
            if (pwine_get_version) MultiByteToWideChar(CP_ACP, 0, pwine_get_version(), -1, ver, ARRAY_SIZE(ver));
            if (ver[0])
            {
                WCHAR line[96];
                swprintf(line, ARRAY_SIZE(line), L"Wine %s", ver);
                SetDlgItemTextW(hwnd, IDC_GEN_DE_LABEL, line);
            }
        }

        /* 1. OS Version and Registered Info from Registry Hive */
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"ProductName", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
                SetDlgItemTextW(hwnd, IDC_GEN_OS_LABEL, buf);

            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"RegisteredOwner", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
            {
                SetDlgItemTextW(hwnd, IDC_GEN_REG_USER, buf);
                got_reg_owner = TRUE;
            }

            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"RegisteredOrganization", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
            {
                SetDlgItemTextW(hwnd, IDC_GEN_REG_ORG, buf);
                got_reg_org = TRUE;
            }

            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"ProductId", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
            {
                SetDlgItemTextW(hwnd, IDC_GEN_REG_PID, buf);
                got_reg_pid = TRUE;
            }

            RegCloseKey(hKey);
        }

        /* Fallbacks matching Windows hive defaults if keys are missing */
        if (!got_reg_owner)
        {
            size = ARRAY_SIZE(buf);
            if (GetUserNameW(buf, &size) && size > 1)
                SetDlgItemTextW(hwnd, IDC_GEN_REG_USER, buf);
            else
                SetDlgItemTextW(hwnd, IDC_GEN_REG_USER, L"Administrator");
        }

        /* organization and product id are shown blank unless the registry has them */

        /* 2. Hardware CPU Detection (CPUID & Registry & Host) */
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, NULL, (BYTE*)buf, &size) == ERROR_SUCCESS && size > 2)
            {
                SetDlgItemTextW(hwnd, IDC_GEN_CPU_LABEL, buf);
                got_cpu = TRUE;
            }

            size = sizeof(mhz);
            RegQueryValueExW(hKey, L"~MHz", NULL, NULL, (BYTE*)&mhz, &size);

            RegCloseKey(hKey);
        }

        /* If registry ProcessorNameString was empty, query physical CPU directly via CPUID instruction */
        if (!got_cpu)
        {
            if (query_hardware_cpuid_brand(buf, ARRAY_SIZE(buf)))
            {
                SetDlgItemTextW(hwnd, IDC_GEN_CPU_LABEL, buf);
                got_cpu = TRUE;
            }
        }

        /* If still not resolved, query host environment or system architecture */
        if (!got_cpu)
        {
            GetSystemInfo(&si);
            if (GetEnvironmentVariableW(L"PROCESSOR_IDENTIFIER", buf, ARRAY_SIZE(buf)) > 0)
            {
                SetDlgItemTextW(hwnd, IDC_GEN_CPU_LABEL, buf);
            }
            else
            {
                swprintf(buf, 256, L"x86-64 Family (%u Processors)", si.dwNumberOfProcessors);
                SetDlgItemTextW(hwnd, IDC_GEN_CPU_LABEL, buf);
            }
        }

        /* 3. Host Physical Memory (RAM) via GlobalMemoryStatusEx */
        memset(&mem, 0, sizeof(mem));
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem))
        {
            double gb = (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
            if (gb >= 1.0)
                swprintf(buf, 256, L"%.2f GB of RAM", gb);
            else
                swprintf(buf, 256, L"%lu MB of RAM", (DWORD)(mem.ullTotalPhys / (1024 * 1024)));
            SetDlgItemTextW(hwnd, IDC_GEN_RAM_LABEL, buf);
        }

        return TRUE;
    }
    }
    return FALSE;
}
