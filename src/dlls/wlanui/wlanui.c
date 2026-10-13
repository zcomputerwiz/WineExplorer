/*
 * Wireless LAN UI implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <windows.h>
#include <shellapi.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wlanui);

DWORD WINAPI WlanUIEditProfile(DWORD dwClientVersion, LPCWSTR wstrProfileName,
                               GUID *pInterfaceGuid, HWND hWnd, DWORD wlStartPage,
                               PVOID pReserved, PDWORD pWlanReasonCode)
{
    TRACE("(%lu, %s, %p, %p, %lu, %p, %p)\n",
          dwClientVersion, wine_dbgstr_w(wstrProfileName), pInterfaceGuid,
          hWnd, wlStartPage, pReserved, pWlanReasonCode);

    ShellExecuteW(hWnd, L"open", L"explorer.exe", L"/wlan", NULL, SW_SHOWNORMAL);
    return ERROR_SUCCESS;
}

DWORD WINAPI WLInvokeProfileUI(DWORD dwClientVersion, LPCWSTR wstrProfileName,
                               GUID *pInterfaceGuid, HWND hWnd, DWORD wlStartPage,
                               PVOID pReserved, PDWORD pWlanReasonCode)
{
    return WlanUIEditProfile(dwClientVersion, wstrProfileName, pInterfaceGuid,
                             hWnd, wlStartPage, pReserved, pWlanReasonCode);
}
