/*
 * WineExplorer Desktop Environment - Network Backend Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Connection state, Wi-Fi scanning and connecting go through NetworkManager's D-Bus API (wexhost); traffic
 * counters come from /proc, addresses from iphlpapi.  Nothing in here invents data: when the host does not
 * provide a value the field is left empty / zero and the UI hides it.
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
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <iphlpapi.h>
#include <wexhost.h>

#include "wine/debug.h"
#include "wexnet.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

static BOOL g_was_connected = FALSE;
static DWORD g_connection_start_tick = 0;
static char g_sae_only[MAX_NETWORKS][MAX_SSID_LEN];   /* SSIDs of the last scan that need WPA3 (SAE) */
static int g_sae_count;

/* ------------------------------------------------------------------ sysfs / procfs */

static int signal_to_bars(int percent)
{
    if (percent <= 0) return 0;
    if (percent < 25) return 1;
    if (percent < 50) return 2;
    if (percent < 70) return 3;
    if (percent < 85) return 4;
    return 5;
}

static BOOL read_sysfs_line(const char *iface, const char *file, char *out, size_t size)
{
    char path[160];
    FILE *f;

    snprintf(path, sizeof(path), "/sys/class/net/%s/%s", iface, file);
    if (!(f = fopen(path, "r"))) return FALSE;
    out[0] = 0;
    if (fgets(out, (int)size, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
    return out[0] != 0;
}

/* Hardware description from the PCI ids database, e.g. "Intel Corporation 82540EM Gigabit Ethernet Controller". */
static BOOL pci_description(const char *iface, char *out, size_t size)
{
    static const char *const dbs[] = { "/usr/share/misc/pci.ids", "/usr/share/hwdata/pci.ids" };
    char vendor[16], device[16], line[256];
    unsigned vid, did;
    char vname[128] = "";
    FILE *f = NULL;
    size_t i;

    if (!read_sysfs_line(iface, "device/vendor", vendor, sizeof(vendor)) ||
        !read_sysfs_line(iface, "device/device", device, sizeof(device)) ||
        sscanf(vendor, "%x", &vid) != 1 || sscanf(device, "%x", &did) != 1)
        return FALSE;

    for (i = 0; i < ARRAY_SIZE(dbs) && !f; i++) f = fopen(dbs[i], "r");
    if (!f) return FALSE;

    while (fgets(line, sizeof(line), f))
    {
        unsigned id;
        char name[128];
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || !line[0]) continue;
        if (line[0] != '\t')
        {
            if (vname[0]) break;                         /* passed our vendor without finding the device */
            if (sscanf(line, "%x %127[^\n]", &id, name) == 2 && id == vid) lstrcpynA(vname, name, sizeof(vname));
        }
        else if (vname[0] && line[1] != '\t')
        {
            if (sscanf(line + 1, "%x %127[^\n]", &id, name) == 2 && id == did)
            {
                snprintf(out, size, "%s %s", vname, name);
                fclose(f);
                return TRUE;
            }
        }
    }
    fclose(f);
    return FALSE;
}

/* Counters of one interface from /proc/net/dev; with no name, the busiest non-loopback interface (name returned). */
static BOOL read_interface_counters(char *iface, size_t size, ULONGLONG *rx_b, ULONGLONG *rx_p,
                                    ULONGLONG *tx_b, ULONGLONG *tx_p)
{
    FILE *f = fopen("/proc/net/dev", "r");
    char line[512];
    BOOL found = FALSE;
    BOOL want_named = iface[0] != 0;

    if (!f) return FALSE;
    while (fgets(line, sizeof(line), f))
    {
        char name[32] = "", *p = line, *colon = strchr(line, ':');
        ULONGLONG a, b, c, d;
        if (!colon) continue;
        while (*p == ' ') p++;
        lstrcpynA(name, p, min((int)sizeof(name), (int)(colon - p) + 1));
        if (!strcmp(name, "lo")) continue;
        if (want_named && strcmp(name, iface)) continue;
        if (sscanf(colon + 1, "%llu %llu %*u %*u %*u %*u %*u %*u %llu %llu", &a, &b, &c, &d) < 4) continue;
        if (!found || b + d > *rx_p + *tx_p)
        {
            if (!want_named) lstrcpynA(iface, name, (int)size);
            *rx_b = a; *rx_p = b; *tx_b = c; *tx_p = d;
            found = TRUE;
        }
    }
    fclose(f);
    return found;
}

/* ------------------------------------------------------------------ public API */

void net_backend_init(void)
{
}

BOOL net_backend_get_status(NET_STATUS_INFO *info)
{
    char iface[32] = "", tmp[160];
    ULONGLONG rx_b = 0, rx_p = 0, tx_b = 0, tx_p = 0;
    IP_ADAPTER_INFO adapters[16];
    ULONG buf_len = sizeof(adapters);
    WEX_NET_STATUS nm;
    BOOL have_nm;

    if (!info) return FALSE;
    memset(info, 0, sizeof(*info));
    info->is_enabled = TRUE;

    /* NetworkManager is the authority on what is connected; without it fall back to what the kernel shows */
    have_nm = !WexNetGetStatus(&nm) && nm.present;
    if (have_nm) lstrcpynA(iface, nm.ifname, sizeof(iface));
    if (!read_interface_counters(iface, sizeof(iface), &rx_b, &rx_p, &tx_b, &tx_p))
    {
        info->has_adapter = FALSE;
        info->conn_type = NET_TYPE_DISCONNECTED;
        g_was_connected = FALSE;
        return TRUE;
    }

    info->has_adapter = TRUE;
    MultiByteToWideChar(CP_UTF8, 0, iface, -1, info->active_device, ARRAY_SIZE(info->active_device));

    if (have_nm)
    {
        info->conn_type = (nm.type == WEX_NET_WIFI) ? NET_TYPE_WIRELESS : NET_TYPE_ETHERNET;
        info->is_connected = nm.connected;
        info->is_enabled = nm.connected || nm.state >= 30;       /* 20 = unavailable (e.g. radio off) */
        info->speed_mbps = nm.speed_mbps;
        if (nm.type == WEX_NET_WIFI)
        {
            info->signal_percent = nm.signal;
            info->signal_bars = signal_to_bars(nm.signal);
            if (nm.ssid[0]) MultiByteToWideChar(CP_UTF8, 0, nm.ssid, -1, info->active_ssid, MAX_SSID_LEN);
        }
    }
    else
    {
        char path[96];
        snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", iface);
        info->conn_type = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? NET_TYPE_WIRELESS : NET_TYPE_ETHERNET;
        info->is_connected = read_sysfs_line(iface, "operstate", tmp, sizeof(tmp)) && !strcmp(tmp, "up");
        if (read_sysfs_line(iface, "speed", tmp, sizeof(tmp)) && atoi(tmp) > 0) info->speed_mbps = atoi(tmp);
    }

    if (!info->active_ssid[0])
        lstrcpyW(info->active_ssid, info->conn_type == NET_TYPE_WIRELESS ? L"Wireless Network Connection" : L"Local Area Connection");

    if (pci_description(iface, tmp, sizeof(tmp)))
        MultiByteToWideChar(CP_UTF8, 0, tmp, -1, info->adapter_name, ARRAY_SIZE(info->adapter_name));
    else
        MultiByteToWideChar(CP_UTF8, 0, iface, -1, info->adapter_name, ARRAY_SIZE(info->adapter_name));

    info->bytes_received = rx_b;
    info->bytes_sent = tx_b;
    info->packets_received = rx_p;
    info->packets_sent = tx_p;

    if (info->is_connected && GetAdaptersInfo(adapters, &buf_len) == ERROR_SUCCESS)
    {
        PIP_ADAPTER_INFO p;
        for (p = adapters; p; p = p->Next)
        {
            const char *ip = p->IpAddressList.IpAddress.String;
            if (!ip[0] || !strcmp(ip, "0.0.0.0") || !strcmp(ip, "127.0.0.1")) continue;
            MultiByteToWideChar(CP_ACP, 0, ip, -1, info->ip_address, ARRAY_SIZE(info->ip_address));
            MultiByteToWideChar(CP_ACP, 0, p->IpAddressList.IpMask.String, -1, info->subnet_mask, ARRAY_SIZE(info->subnet_mask));
            MultiByteToWideChar(CP_ACP, 0, p->GatewayList.IpAddress.String, -1, info->default_gateway, ARRAY_SIZE(info->default_gateway));
            break;
        }
    }

    /* Duration = time since this process first saw the link up */
    if (info->is_connected && !g_was_connected) g_connection_start_tick = GetTickCount();
    g_was_connected = info->is_connected;
    if (info->is_connected) info->duration_seconds = (GetTickCount() - g_connection_start_tick) / 1000;

    return TRUE;
}

/* Visible Wi-Fi networks (empty when there is no Wi-Fi device or NetworkManager). */
int net_backend_scan_wireless(WLAN_NETWORK_ENTRY *entries, int max_entries)
{
    WEX_WIFI_AP aps[MAX_NETWORKS];
    DWORD count = 0, i;
    int n = 0;

    if (!entries || max_entries <= 0) return 0;
    memset(entries, 0, sizeof(WLAN_NETWORK_ENTRY) * max_entries);
    g_sae_count = 0;

    if (WexNetScanWifi(aps, ARRAY_SIZE(aps), &count)) return 0;

    for (i = 0; i < count && n < max_entries; i++)
    {
        WLAN_NETWORK_ENTRY *e = &entries[n++];
        MultiByteToWideChar(CP_UTF8, 0, aps[i].ssid, -1, e->ssid, MAX_SSID_LEN);
        e->signal_percent = min(100, aps[i].strength);
        e->signal_bars = signal_to_bars(e->signal_percent);
        e->is_connected = aps[i].connected;
        e->is_secure = aps[i].secured;
        if (aps[i].secured)
            MultiByteToWideChar(CP_UTF8, 0, aps[i].security, -1, e->security, MAX_SECURITY_LEN);
        else
            lstrcpyW(e->security, L"Open (Unsecured)");
        if (aps[i].sae_only && g_sae_count < MAX_NETWORKS)
            lstrcpynA(g_sae_only[g_sae_count++], aps[i].ssid, MAX_SSID_LEN);
    }
    return n;
}

BOOL net_backend_connect_wireless(const WCHAR *ssid, const WCHAR *passphrase)
{
    char a_ssid[MAX_SSID_LEN * 4] = "", a_pwd[256] = "";
    BOOL sae = FALSE, ok;
    int i;

    if (!ssid || !ssid[0]) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, ssid, -1, a_ssid, sizeof(a_ssid), NULL, NULL);
    if (passphrase && passphrase[0])
        WideCharToMultiByte(CP_UTF8, 0, passphrase, -1, a_pwd, sizeof(a_pwd), NULL, NULL);
    for (i = 0; i < g_sae_count; i++) if (!strcmp(g_sae_only[i], a_ssid)) sae = TRUE;

    ok = !WexNetConnectWifi(a_ssid, a_pwd, sae);
    SecureZeroMemory(a_pwd, sizeof(a_pwd));
    TRACE("Connecting to Wi-Fi SSID %s: %d\n", debugstr_a(a_ssid), ok);
    return ok;
}

BOOL net_backend_disconnect(void)
{
    return !WexNetDisconnect();
}

/* "Disable"/"Enable" act on the active device only (never on all networking). */
BOOL net_backend_set_enabled(BOOL enable)
{
    return enable ? !WexNetEnable() : !WexNetDisconnect();
}

/* Repair: drop the connection and let NetworkManager bring it up again */
BOOL net_backend_repair(void)
{
    WexNetDisconnect();
    return !WexNetEnable();
}

static BOOL read_show_icon(const WCHAR *key, BOOL *value)
{
    HKEY hKey;
    DWORD val, size = sizeof(val);
    BOOL ok = FALSE;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return FALSE;
    if (RegQueryValueExW(hKey, L"ShowIcon", NULL, NULL, (BYTE *)&val, &size) == ERROR_SUCCESS)
    {
        *value = (val != 0);
        ok = TRUE;
    }
    RegCloseKey(hKey);
    return ok;
}

/* Show icon in notification area registry setting */
BOOL net_backend_get_show_tray_icon(void)
{
    BOOL show;

    if (read_show_icon(L"Software\\Microsoft\\Windows\\CurrentVersion\\Network\\Connections", &show)) return show;
    return TRUE;
}

void net_backend_set_show_tray_icon(BOOL show)
{
    HKEY hKey;
    DWORD val = show ? 1 : 0;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Network\\Connections",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, L"ShowIcon", 0, REG_DWORD, (BYTE *)&val, sizeof(val));
        RegCloseKey(hKey);
    }
}
