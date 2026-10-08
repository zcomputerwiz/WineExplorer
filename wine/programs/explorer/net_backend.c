/*
 * WineExplorer Desktop Environment - Linux Host NetworkManager / Network Backend Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Reads interface state from /proc and /sys, addresses through iphlpapi, and drives
 * the host's NetworkManager through nmcli.  Nothing in here invents data: when the
 * host does not provide a value the field is left empty / zero and the UI hides it.
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

#include "wine/debug.h"
#include "net_backend.h"
#include "hostcmd.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define ACTIVE_SSID_REFRESH_MS 10000

static BOOL g_backend_initialized = FALSE;
static BOOL g_was_connected = FALSE;
static DWORD g_connection_start_tick = 0;
static char g_disabled_dev[32];      /* interface the user disabled from the UI */

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

/* Link quality of a wireless interface from /proc/net/wireless; -1 if not reported. */
static int read_wireless_quality(const char *iface)
{
    FILE *f = fopen("/proc/net/wireless", "r");
    char line[256];
    int result = -1;

    if (!f) return -1;
    while (fgets(line, sizeof(line), f))
    {
        char *colon = strchr(line, ':'), *p = line;
        int status, quality;
        if (!colon) continue;
        while (*p == ' ') p++;
        if ((size_t)(colon - p) != strlen(iface) || strncmp(p, iface, colon - p)) continue;
        if (sscanf(colon + 1, "%d %d", &status, &quality) == 2)
        {
            result = quality <= 70 ? quality * 100 / 70 : quality;  /* drivers report 0-70 or 0-100 */
            result = max(0, min(100, result));
        }
        break;
    }
    fclose(f);
    return result;
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

/* Pick the interface carrying the most traffic that is not loopback. */
static BOOL pick_interface(char *iface, size_t size, ULONGLONG *rx_b, ULONGLONG *rx_p, ULONGLONG *tx_b, ULONGLONG *tx_p)
{
    FILE *f = fopen("/proc/net/dev", "r");
    char line[512];
    BOOL found = FALSE;

    if (!f) return FALSE;
    while (fgets(line, sizeof(line), f))
    {
        char name[32] = "", *p = line, *colon = strchr(line, ':');
        ULONGLONG a, b, c, d;
        if (!colon) continue;
        while (*p == ' ') p++;
        lstrcpynA(name, p, min((int)sizeof(name), (int)(colon - p) + 1));
        if (!strcmp(name, "lo")) continue;
        if (sscanf(colon + 1, "%llu %llu %*u %*u %*u %*u %*u %*u %llu %llu", &a, &b, &c, &d) < 4) continue;
        if (!found || b + d > *rx_p + *tx_p)
        {
            lstrcpynA(iface, name, (int)size);
            *rx_b = a; *rx_p = b; *tx_b = c; *tx_p = d;
            found = TRUE;
        }
    }
    fclose(f);
    return found;
}

/* ------------------------------------------------------------------ nmcli (active SSID, refreshed asynchronously) */

static WCHAR g_active_ssid[MAX_SSID_LEN];
static HANDLE g_ssid_proc, g_ssid_pipe;
static DWORD g_ssid_last_start;

static void poll_active_ssid(const char *iface)
{
    char buf[1024];

    if (g_ssid_proc)
    {
        if (WaitForSingleObject(g_ssid_proc, 0) != WAIT_OBJECT_0) return;
        {
            DWORD got = 0;
            char *line, *save = NULL;
            memset(buf, 0, sizeof(buf));
            ReadFile(g_ssid_pipe, buf, sizeof(buf) - 1, &got, NULL);
            g_active_ssid[0] = 0;
            for (line = strtok_s(buf, "\r\n", &save); line; line = strtok_s(NULL, "\r\n", &save))
            {
                /* -t output is "yes:<ssid>" for the active network; escaped colons are "\:" */
                if (strncmp(line, "yes:", 4)) continue;
                MultiByteToWideChar(CP_UTF8, 0, line + 4, -1, g_active_ssid, MAX_SSID_LEN);
                break;
            }
        }
        CloseHandle(g_ssid_pipe); CloseHandle(g_ssid_proc);
        g_ssid_proc = g_ssid_pipe = NULL;
        return;
    }

    if (GetTickCount() - g_ssid_last_start < ACTIVE_SSID_REFRESH_MS && g_ssid_last_start) return;
    {
        const char *argv[] = { "nmcli", "-t", "-f", "ACTIVE,SSID", "dev", "wifi", "list", "ifname", iface, NULL };
        g_ssid_last_start = GetTickCount();
        g_ssid_proc = hostcmd_spawn(argv, &g_ssid_pipe);
    }
}

/* ------------------------------------------------------------------ public API */

void net_backend_init(void)
{
    if (g_backend_initialized) return;
    g_backend_initialized = TRUE;
}

/* Query active network connection details and adapter metrics */
BOOL net_backend_get_status(NET_STATUS_INFO *info)
{
    char iface[32] = "", tmp[160];
    ULONGLONG rx_b = 0, rx_p = 0, tx_b = 0, tx_p = 0;
    IP_ADAPTER_INFO adapters[16];
    ULONG buf_len = sizeof(adapters);
    BOOL is_wifi, link_up;

    if (!info) return FALSE;
    memset(info, 0, sizeof(*info));
    info->is_enabled = TRUE;

    if (!pick_interface(iface, sizeof(iface), &rx_b, &rx_p, &tx_b, &tx_p))
    {
        /* No network interface at all: nothing to show */
        info->has_adapter = FALSE;
        info->conn_type = NET_TYPE_DISCONNECTED;
        g_was_connected = FALSE;
        return TRUE;
    }

    info->has_adapter = TRUE;
    MultiByteToWideChar(CP_UTF8, 0, iface, -1, info->active_device, ARRAY_SIZE(info->active_device));

    {
        char path[96];
        snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", iface);
        is_wifi = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }
    info->conn_type = is_wifi ? NET_TYPE_WIRELESS : NET_TYPE_ETHERNET;

    link_up = read_sysfs_line(iface, "operstate", tmp, sizeof(tmp)) && !strcmp(tmp, "up");
    info->is_enabled = strcmp(iface, g_disabled_dev) != 0;
    info->is_connected = link_up && info->is_enabled;

    if (pci_description(iface, tmp, sizeof(tmp)))
        MultiByteToWideChar(CP_UTF8, 0, tmp, -1, info->adapter_name, ARRAY_SIZE(info->adapter_name));
    else
        MultiByteToWideChar(CP_UTF8, 0, iface, -1, info->adapter_name, ARRAY_SIZE(info->adapter_name));

    lstrcpyW(info->active_ssid, is_wifi ? L"Wireless Network Connection" : L"Local Area Connection");

    if (read_sysfs_line(iface, "speed", tmp, sizeof(tmp)) && atoi(tmp) > 0)
        info->speed_mbps = atoi(tmp);                    /* Ethernet; wireless drivers do not export it */

    if (is_wifi)
    {
        int q = read_wireless_quality(iface);
        if (info->is_connected)
        {
            poll_active_ssid(iface);
            if (g_active_ssid[0]) lstrcpynW(info->active_ssid, g_active_ssid, MAX_SSID_LEN);
        }
        if (q >= 0) { info->signal_percent = q; info->signal_bars = signal_to_bars(q); }
    }

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

/* Scan for available Wi-Fi access points using nmcli.  Returns 0 networks when nmcli is missing or no radio exists. */
int net_backend_scan_wireless(WLAN_NETWORK_ENTRY *entries, int max_entries)
{
    static const char *const scan_cmd[] = { "nmcli", "-t", "-e", "yes", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                                            "dev", "wifi", "list", "--rescan", "auto", NULL };
    char buffer[16384];
    char *line, *save = NULL;
    int count = 0;

    if (!entries || max_entries <= 0) return 0;
    memset(entries, 0, sizeof(WLAN_NETWORK_ENTRY) * max_entries);

    if (!hostcmd_capture(scan_cmd, buffer, sizeof(buffer), 8000)) return 0;

    for (line = strtok_s(buffer, "\r\n", &save); line && count < max_entries; line = strtok_s(NULL, "\r\n", &save))
    {
        /* IN-USE:SSID:SIGNAL:SECURITY with ':' and '\' escaped by a backslash inside fields */
        char fields[4][128] = {{0}};
        int nf = 0, n = 0;
        char *p;
        WCHAR w_ssid[MAX_SSID_LEN];
        int sig, i, dup = -1;

        for (p = line; *p && nf < 4; p++)
        {
            if (*p == '\\' && p[1]) { if (n < 127) fields[nf][n++] = *++p; else p++; }
            else if (*p == ':' && nf < 3) { nf++; n = 0; }
            else if (n < 127) fields[nf][n++] = *p;
        }
        if (!fields[1][0]) continue;                       /* hidden network */

        MultiByteToWideChar(CP_UTF8, 0, fields[1], -1, w_ssid, MAX_SSID_LEN);
        sig = max(0, min(100, atoi(fields[2])));

        for (i = 0; i < count; i++)
            if (!wcscmp(entries[i].ssid, w_ssid)) { dup = i; break; }

        if (dup >= 0)
        {
            if (sig > entries[dup].signal_percent)
            {
                entries[dup].signal_percent = sig;
                entries[dup].signal_bars = signal_to_bars(sig);
            }
            if (fields[0][0] == '*') entries[dup].is_connected = TRUE;
            continue;
        }

        lstrcpyW(entries[count].ssid, w_ssid);
        entries[count].signal_percent = sig;
        entries[count].signal_bars = signal_to_bars(sig);
        entries[count].is_connected = (fields[0][0] == '*');
        if (!fields[3][0] || !strcmp(fields[3], "--"))
        {
            lstrcpyW(entries[count].security, L"Open (Unsecured)");
        }
        else
        {
            MultiByteToWideChar(CP_UTF8, 0, fields[3], -1, entries[count].security, MAX_SECURITY_LEN);
            entries[count].is_secure = TRUE;
        }
        count++;
    }
    return count;
}

/* Connect to a wireless network by SSID and optional passphrase.
 * ponytail: the passphrase is an nmcli argument and so visible to `ps` while nmcli runs; move to a
 * NetworkManager D-Bus secret agent if that matters. */
BOOL net_backend_connect_wireless(const WCHAR *ssid, const WCHAR *passphrase)
{
    char a_ssid[MAX_SSID_LEN * 4] = "", a_pwd[256] = "";
    const char *argv[10];
    int n = 0;

    if (!ssid || !ssid[0]) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, ssid, -1, a_ssid, sizeof(a_ssid), NULL, NULL);

    argv[n++] = "nmcli"; argv[n++] = "dev"; argv[n++] = "wifi"; argv[n++] = "connect"; argv[n++] = a_ssid;
    if (passphrase && passphrase[0])
    {
        WideCharToMultiByte(CP_UTF8, 0, passphrase, -1, a_pwd, sizeof(a_pwd), NULL, NULL);
        argv[n++] = "password"; argv[n++] = a_pwd;
    }
    argv[n] = NULL;

    hostcmd_run_async(argv);
    SecureZeroMemory(a_pwd, sizeof(a_pwd));
    TRACE("Connecting to Wi-Fi SSID %s\n", debugstr_a(a_ssid));
    return TRUE;
}

/* First device of the given nmcli TYPE ("wifi", "ethernet"); empty if none. */
static BOOL find_nm_device(const char *type, char *dev, size_t size)
{
    static const char *const cmd[] = { "nmcli", "-t", "-f", "DEVICE,TYPE", "dev", NULL };
    char buf[2048], *line, *save = NULL;

    dev[0] = 0;
    if (!hostcmd_capture(cmd, buf, sizeof(buf), 3000)) return FALSE;
    for (line = strtok_s(buf, "\r\n", &save); line; line = strtok_s(NULL, "\r\n", &save))
    {
        char *colon = strchr(line, ':');
        if (!colon || strcmp(colon + 1, type)) continue;
        *colon = 0;
        lstrcpynA(dev, line, (int)size);
        return TRUE;
    }
    return FALSE;
}

BOOL net_backend_disconnect(void)
{
    char dev[32];
    const char *argv[] = { "nmcli", "dev", "disconnect", dev, NULL };

    if (!find_nm_device("wifi", dev, sizeof(dev))) return FALSE;
    hostcmd_run_async(argv);
    return TRUE;
}

/* "Disable"/"Enable" act on the active interface only (never on all networking). */
BOOL net_backend_set_enabled(BOOL enable)
{
    NET_STATUS_INFO st;
    char dev[32];
    const char *argv[] = { "nmcli", "dev", enable ? "connect" : "disconnect", dev, NULL };

    net_backend_get_status(&st);
    if (!st.has_adapter) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, st.active_device, -1, dev, sizeof(dev), NULL, NULL);
    hostcmd_run_async(argv);
    if (enable) g_disabled_dev[0] = 0; else lstrcpynA(g_disabled_dev, dev, sizeof(g_disabled_dev));
    return TRUE;
}

/* Repair connection: re-applies the device's NetworkManager configuration */
BOOL net_backend_repair(void)
{
    NET_STATUS_INFO st;
    char dev[32];
    const char *argv[] = { "nmcli", "dev", "reapply", dev, NULL };

    net_backend_get_status(&st);
    if (!st.has_adapter) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, st.active_device, -1, dev, sizeof(dev), NULL, NULL);
    hostcmd_run_async(argv);
    return TRUE;
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
