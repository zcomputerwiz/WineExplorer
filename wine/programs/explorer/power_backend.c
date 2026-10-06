/*
 * WineExplorer Desktop Environment - Power & Battery Backend Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements Linux sysfs /sys/class/power_supply parsing and Win32
 * GetSystemPowerStatus fallback for authentic battery metering and power schemes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include "wine/debug.h"
#include "power_backend.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

static UINT g_active_scheme = POWER_SCHEME_DESK;
static POWER_STATUS_INFO g_cached_status = {0};
static DWORD g_last_check = 0;

/* Read integer from sysfs file */
static BOOL read_sysfs_int(const char *path, int *out_val)
{
    FILE *f = fopen(path, "r");
    if (!f) return FALSE;
    if (fscanf(f, "%d", out_val) == 1)
    {
        fclose(f);
        return TRUE;
    }
    fclose(f);
    return FALSE;
}

/* Read string from sysfs file */
static BOOL read_sysfs_str(const char *path, char *out_buf, int max_len)
{
    FILE *f = fopen(path, "r");
    if (!f) return FALSE;
    if (fgets(out_buf, max_len, f))
    {
        char *nl = strchr(out_buf, '\n');
        if (nl) *nl = '\0';
        fclose(f);
        return TRUE;
    }
    fclose(f);
    return FALSE;
}

static BOOL query_linux_sysfs(POWER_STATUS_INFO *info)
{
    const char *ac_names[] = { "AC", "ACAD", "ADP0", "ADP1" };
    int capacity = 0;
    char status[64] = {0};
    int energy_now = 0, power_now = 0;
    int ac_online = 0;
    BOOL bat_found = FALSE;
    BOOL ac_found = FALSE;
    int i;
    size_t j;

    /* Check BAT0 through BAT4 */
    for (i = 0; i < 5; i++)
    {
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/capacity", i);
        if (read_sysfs_int(path, &capacity))
        {
            bat_found = TRUE;
            snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/status", i);
            read_sysfs_str(path, status, sizeof(status));

            snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/energy_now", i);
            if (!read_sysfs_int(path, &energy_now))
            {
                snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/charge_now", i);
                read_sysfs_int(path, &energy_now);
            }

            snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/power_now", i);
            if (!read_sysfs_int(path, &power_now))
            {
                snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/current_now", i);
                read_sysfs_int(path, &power_now);
            }
            break;
        }
    }

    /* Check AC power adapter */
    for (j = 0; j < sizeof(ac_names)/sizeof(ac_names[0]); j++)
    {
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/online", ac_names[j]);
        if (read_sysfs_int(path, &ac_online))
        {
            ac_found = TRUE;
            break;
        }
    }

    if (!bat_found && !ac_found) return FALSE;

    info->has_battery = bat_found;
    info->ac_online = ac_found ? (ac_online == 1) : TRUE;
    info->battery_percent = min(100, max(0, capacity));
    info->is_charging = (strncmp(status, "Char", 4) == 0);

    if (bat_found && !info->ac_online && power_now > 0 && energy_now > 0)
    {
        info->time_remaining_min = (int)(((long long)energy_now * 60) / power_now);
    }
    else
    {
        info->time_remaining_min = -1;
    }

    return TRUE;
}

void power_backend_init(void)
{
    /* Read active power scheme from registry */
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\PowerCfg", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        WCHAR str[16];
        DWORD str_size = sizeof(str);
        if (RegQueryValueExW(hKey, L"CurrentPowerPolicy", NULL, NULL, (LPBYTE)str, &str_size) == ERROR_SUCCESS)
        {
            g_active_scheme = _wtoi(str);
        }
        RegCloseKey(hKey);
    }

    power_backend_get_status(&g_cached_status);
}

BOOL power_backend_get_status(POWER_STATUS_INFO *info)
{
    DWORD now = GetTickCount();

    if (!info) return FALSE;

    if (now - g_last_check > 1000 || g_last_check == 0)
    {
        g_last_check = now;

        /* 1. Try zero-fork Linux kernel sysfs */
        if (!query_linux_sysfs(&g_cached_status))
        {
            /* 2. Fall back to Win32 GetSystemPowerStatus */
            SYSTEM_POWER_STATUS sps;
            if (GetSystemPowerStatus(&sps))
            {
                g_cached_status.has_battery = (sps.BatteryFlag != BATTERY_FLAG_NO_BATTERY &&
                                               sps.BatteryFlag != BATTERY_FLAG_UNKNOWN);
                g_cached_status.ac_online = (sps.ACLineStatus == 1);
                g_cached_status.is_charging = (sps.BatteryFlag & BATTERY_FLAG_CHARGING) != 0;
                g_cached_status.battery_percent = (sps.BatteryLifePercent <= 100) ? sps.BatteryLifePercent : 100;
                g_cached_status.time_remaining_min = (sps.BatteryLifeTime != (DWORD)-1) ? (sps.BatteryLifeTime / 60) : -1;
            }
            else
            {
                /* Default to desktop AC mode */
                g_cached_status.has_battery = FALSE;
                g_cached_status.ac_online = TRUE;
                g_cached_status.battery_percent = 100;
                g_cached_status.time_remaining_min = -1;
            }
        }
    }

    g_cached_status.active_scheme = g_active_scheme;
    *info = g_cached_status;
    return TRUE;
}

void power_backend_set_scheme(UINT scheme_id)
{
    HKEY hKey;

    if (scheme_id > POWER_SCHEME_MAXBATTERY)
        scheme_id = POWER_SCHEME_DESK;

    g_active_scheme = scheme_id;
    g_cached_status.active_scheme = scheme_id;

    /* Persist scheme to registry */
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\PowerCfg", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        WCHAR str[16];
        wsprintfW(str, L"%u", scheme_id);
        RegSetValueExW(hKey, L"CurrentPowerPolicy", 0, REG_SZ, (const BYTE *)str, (lstrlenW(str) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
    }
}

UINT power_backend_get_scheme(void)
{
    return g_active_scheme;
}
