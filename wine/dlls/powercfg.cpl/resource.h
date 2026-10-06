/*
 * Wine powercfg.cpl resource definitions
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 */

#ifndef __WINE_POWERCFG_RESOURCE_H
#define __WINE_POWERCFG_RESOURCE_H

#define IDS_CPL_NAME               1
#define IDS_CPL_INFO               2

#define IDI_CPL_POWER              100
#define IDI_BATT                   101
#define IDI_PLUG                   102
#define IDI_HIBERNATE              103

#define IDD_PAGE_POWERSCHEMES      201
#define IDD_PAGE_POWERMETER        202
#define IDD_PAGE_ADVANCED          203
#define IDD_PAGE_HIBERNATE         204

/* Power Schemes Controls */
#define IDC_SCHEME_LIST            1001
#define IDC_SCHEME_SAVEAS          1002
#define IDC_SCHEME_DELETE          1003
#define IDC_MONITOR_AC             1004
#define IDC_MONITOR_DC             1005
#define IDC_DISK_AC                1006
#define IDC_DISK_DC                1007
#define IDC_STANDBY_AC             1008
#define IDC_STANDBY_DC             1009
#define IDC_HIBERNATE_AC           1010
#define IDC_HIBERNATE_DC           1011

/* Power Meter Controls */
#define IDC_METER_SOURCE           1101
#define IDC_METER_PROGRESS         1102
#define IDC_METER_PERCENT          1103
#define IDC_METER_DETAILS          1104
#define IDC_METER_ICON             1105

/* Advanced Controls */
#define IDC_ADV_SYSTRAY_ICON       1201
#define IDC_ADV_PASSWORD           1202
#define IDC_ADV_PWRBTN             1203
#define IDC_ADV_LID                1204

/* Hibernate Controls */
#define IDC_HIB_ENABLE             1301
#define IDC_HIB_REQUIRED_SPACE     1302
#define IDC_HIB_FREE_SPACE         1303

#endif /* __WINE_POWERCFG_RESOURCE_H */
