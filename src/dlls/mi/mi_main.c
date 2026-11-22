/*
 * mi.dll: the entry points and the function table of the Management Infrastructure client API
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdarg.h>
#include "mi_priv.h"

void mi_trace(const char *fmt, ...)
{
    char buf[512], path[MAX_PATH];
    va_list ap;
    DWORD n;
    HANDLE h;

    n = GetEnvironmentVariableA("MI_TRACE", path, sizeof(path));
    if (!n || n >= sizeof(path)) return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    buf[n++] = '\n';
    h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, buf, n, &n, NULL);
    CloseHandle(h);
}

static const MI_ClientFT_V1 client_ft =
{
    &mi_tbl_MI_ApplicationFT,
    &mi_tbl_MI_SessionFT,
    &mi_tbl_MI_OperationFT,
    &mi_tbl_MI_HostedProviderFT,
    &mi_tbl_MI_SerializerFT,
    &mi_tbl_MI_DeserializerFT,
    &mi_tbl_MI_SubscriptionDeliveryOptionsFT,
    &mi_tbl_MI_DestinationOptionsFT,
    &mi_tbl_MI_OperationOptionsFT,
    &mi_tbl_MI_UtilitiesFT,
};

const MI_ClientFT_V1 *mi_clientFT_V1 = &client_ft;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
    return TRUE;
}
