/*
 * WineExplorer Desktop Environment - Linux Host NetworkManager / Network Backend Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Clean-room implementation for pairing Wine Explorer with Linux NetworkManager (nmcli)
 * and Win32 iphlpapi network status.
 */

#ifndef __WINE_NET_BACKEND_H
#define __WINE_NET_BACKEND_H

#include <windows.h>

#define MAX_SSID_LEN 64
#define MAX_BSSID_LEN 32
#define MAX_SECURITY_LEN 64
#define MAX_NETWORKS 64

typedef enum {
    NET_TYPE_DISCONNECTED = 0,
    NET_TYPE_WIRELESS,
    NET_TYPE_ETHERNET
} NET_CONNECTION_TYPE;

typedef struct _WLAN_NETWORK_ENTRY {
    WCHAR ssid[MAX_SSID_LEN];
    char  bssid[MAX_BSSID_LEN];
    int   signal_percent;      /* 0 - 100 */
    int   signal_bars;         /* 0 - 5 */
    WCHAR security[MAX_SECURITY_LEN]; /* e.g. L"WPA2", L"WPA3", L"Open" */
    BOOL  is_secure;
    BOOL  is_connected;
    BOOL  is_adhoc;
} WLAN_NETWORK_ENTRY;

typedef struct _NET_STATUS_INFO {
    NET_CONNECTION_TYPE conn_type;
    BOOL  is_connected;
    BOOL  is_enabled;
    WCHAR active_ssid[MAX_SSID_LEN];
    WCHAR active_device[32];
    WCHAR adapter_name[128];
    WCHAR ip_address[32];
    WCHAR subnet_mask[32];
    WCHAR default_gateway[32];
    int   signal_percent;      /* 0 - 100 */
    int   signal_bars;         /* 0 - 5 */
    DWORD speed_mbps;          /* e.g. 54, 150, 1000 */
    DWORD duration_seconds;
    ULONGLONG bytes_sent;
    ULONGLONG bytes_received;
    ULONGLONG packets_sent;
    ULONGLONG packets_received;
} NET_STATUS_INFO;

void net_backend_init(void);
BOOL net_backend_get_status(NET_STATUS_INFO *info);
int  net_backend_scan_wireless(WLAN_NETWORK_ENTRY *entries, int max_entries);
BOOL net_backend_connect_wireless(const WCHAR *ssid, const WCHAR *passphrase);
BOOL net_backend_disconnect(void);
BOOL net_backend_set_enabled(BOOL enable);
BOOL net_backend_repair(void);
BOOL net_backend_get_show_tray_icon(void);
void net_backend_set_show_tray_icon(BOOL show);

#endif /* __WINE_NET_BACKEND_H */
