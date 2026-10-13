/*
 * Network Connections Shell Extension (netshell.dll)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows Network Connections shell extension,
 * COM registration, connection launch utilities, and name validation.
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

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winreg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <ole2.h>

#include "wine/debug.h"
#include "resource.h"
#include "wexnet.h"
#include "wlan_dialog.h"
#include "module.h"

WINE_DEFAULT_DEBUG_CHANNEL(netshell);

static HINSTANCE g_hinst = NULL;

/* CLSID_NetworkConnections {7007ACC7-3202-11D1-AAD2-00805FC1270E} */
static const WCHAR wszCLSID_NetCon[] = L"{7007ACC7-3202-11D1-AAD2-00805FC1270E}";
/* CLSID_ConnectionTray {7007ACCF-3202-11D1-AAD2-00805FC1270E} */
static const WCHAR wszCLSID_NetTray[] = L"{7007ACCF-3202-11D1-AAD2-00805FC1270E}";

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hinst = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

HINSTANCE netshell_module(void) { return g_hinst; }

HRESULT WINAPI DllRegisterServer(void)
{
    HKEY hKey;
    WCHAR subkey[256];

    /* Register CLSID_NetworkConnections */
    wsprintfW(subkey, L"CLSID\\%s", wszCLSID_NetCon);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)L"Network Connections", sizeof(L"Network Connections"));
        RegSetValueExW(hKey, L"InfoTip", 0, REG_SZ, (const BYTE *)L"Connects to other computers, networks, and the Internet.",
                       sizeof(L"Connects to other computers, networks, and the Internet."));
        RegCloseKey(hKey);
    }

    /* Set InprocServer32 */
    wsprintfW(subkey, L"CLSID\\%s\\InProcServer32", wszCLSID_NetCon);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)L"netshell.dll", sizeof(L"netshell.dll"));
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE *)L"Apartment", sizeof(L"Apartment"));
        RegCloseKey(hKey);
    }

    /* Set DefaultIcon */
    wsprintfW(subkey, L"CLSID\\%s\\DefaultIcon", wszCLSID_NetCon);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)L"netshell.dll,0", sizeof(L"netshell.dll,0"));
        RegCloseKey(hKey);
    }

    /* Register in Control Panel Namespace */
    wsprintfW(subkey, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ControlPanel\\NameSpace\\%s", wszCLSID_NetCon);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)L"Network Connections", sizeof(L"Network Connections"));
        RegCloseKey(hKey);
    }

    wsprintfW(subkey, L"CLSID\\%s\\InProcServer32", wszCLSID_NetTray);
    if (!RegCreateKeyExW(HKEY_CLASSES_ROOT, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL))
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)L"netshell.dll", sizeof(L"netshell.dll"));
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE *)L"Apartment", sizeof(L"Apartment"));
        RegCloseKey(hKey);
    }
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\ShellServiceObjectDelayLoad",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL))
    {
        RegSetValueExW(hKey, L"Connections Tray", 0, REG_SZ, (const BYTE *)wszCLSID_NetTray, sizeof(wszCLSID_NetTray));
        RegCloseKey(hKey);
    }

    TRACE("netshell.dll registered successfully\n");
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR subkey[256];
    HKEY key;

    wsprintfW(subkey, L"CLSID\\%s", wszCLSID_NetTray);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, subkey);
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\ShellServiceObjectDelayLoad",
                       0, KEY_SET_VALUE, &key))
    {
        RegDeleteValueW(key, L"Connections Tray");
        RegCloseKey(key);
    }

    wsprintfW(subkey, L"CLSID\\%s", wszCLSID_NetCon);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, subkey);

    wsprintfW(subkey, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ControlPanel\\NameSpace\\%s", wszCLSID_NetCon);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, subkey);

    return S_OK;
}

/* Launch New Connection Wizard */
HRESULT WINAPI StartNCW(HWND hwndParent, DWORD dwFlags, PVOID pReserved)
{
    TRACE("(%p, 0x%lx, %p)\n", hwndParent, dwFlags, pReserved);
    wlan_dialog_show(hwndParent);
    return S_OK;
}

/* Validate connection name: no invalid file/URI characters and within bounds */
BOOL WINAPI NcIsValidConnectionName(LPCWSTR pszName)
{
    static const WCHAR invalid_chars[] = L"\\/:*?\"<>|";
    int len;

    if (!pszName || !pszName[0]) return FALSE;
    len = lstrlenW(pszName);
    if (len > 255) return FALSE;

    if (wcspbrk(pszName, invalid_chars) != NULL)
        return FALSE;

    return TRUE;
}

/* Free properties allocated by NetConnection queries */
VOID WINAPI NcFreeNetconProperties(PVOID pProps)
{
    if (pProps)
    {
        CoTaskMemFree(pProps);
    }
}

/* Launch connection configuration / status */
HRESULT WINAPI HrLaunchConnection(LPCWSTR pszName)
{
    TRACE("(%s)\n", wine_dbgstr_w(pszName));
    wlan_dialog_show(NULL);
    return S_OK;
}

HRESULT WINAPI HrLaunchConnectionEx(LPCWSTR pszName, DWORD dwFlags)
{
    TRACE("(%s, 0x%lx)\n", wine_dbgstr_w(pszName), dwFlags);
    return HrLaunchConnection(pszName);
}

/* Repair connection */
HRESULT WINAPI RepairConnection(HWND hwndParent)
{
    TRACE("(%p)\n", hwndParent);
    wlan_status_show(hwndParent);
    return S_OK;
}

/* Create desktop icon shortcut for connection */
HRESULT WINAPI HrCreateDesktopIcon(PVOID pPidl)
{
    TRACE("(%p)\n", pPidl);
    return S_OK;
}

/* Rename connection */
HRESULT WINAPI HrRenameConnection(PVOID pPidl, LPCWSTR pszNewName)
{
    TRACE("(%p, %s)\n", pPidl, wine_dbgstr_w(pszNewName));
    if (!NcIsValidConnectionName(pszNewName)) return E_INVALIDARG;
    return S_OK;
}

/* Dial-up network file invocation */
HRESULT WINAPI InvokeDunFile(LPCWSTR pszDunPath)
{
    TRACE("(%s)\n", wine_dbgstr_w(pszDunPath));
    return S_OK;
}
