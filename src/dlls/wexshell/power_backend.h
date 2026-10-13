/*
 * WineExplorer Desktop Environment - Power & Battery Backend Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Provides abstraction for Linux sysfs /sys/class/power_supply, UPower,
 * and Win32 GetSystemPowerStatus.
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

#ifndef __WINE_EXPLORER_POWER_BACKEND_H
#define __WINE_EXPLORER_POWER_BACKEND_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    POWER_SCHEME_DESK = 0,      /* Home/Office Desk */
    POWER_SCHEME_LAPTOP,        /* Portable/Laptop */
    POWER_SCHEME_PRESENTATION,  /* Presentation */
    POWER_SCHEME_ALWAYSON,      /* Always On */
    POWER_SCHEME_MINIMAL,       /* Minimal Power Management */
    POWER_SCHEME_MAXBATTERY     /* Max Battery */
} POWER_SCHEME_ID;

typedef struct
{
    BOOL has_battery;
    BOOL ac_online;
    BOOL is_charging;
    int battery_percent;       /* 0 to 100 */
    int time_remaining_min;    /* Minutes left or -1 if calculating/AC */
    UINT active_scheme;
} POWER_STATUS_INFO;

/* Initialize power monitoring */
void power_backend_init(void);

/* Query live battery and AC power status */
BOOL power_backend_get_status(POWER_STATUS_INFO *info);

/* Set active power scheme */
void power_backend_set_scheme(UINT scheme_id);

/* Get active power scheme */
UINT power_backend_get_scheme(void);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_EXPLORER_POWER_BACKEND_H */
