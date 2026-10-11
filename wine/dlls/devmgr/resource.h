/*
 * Device Manager resources
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

#include <windef.h>
#include <winuser.h>
#include <commctrl.h>

/* Icons and bitmaps use the ids of Windows XP's devmgr.dll (xpres_devmgr.dll has the genuine ones); the class icons
 * use setupapi.dll's ids. */
#define IDI_SIGNED              40
#define IDI_BLANK               41
#define IDI_DEVMGR              201
#define IDI_RESOURCES           202
#define IDI_BATTERY             2002   /* batt.dll 2 on Windows */
#define IDI_MEDIA               3004   /* mmsys.cpl 3004 on Windows */
#define IDB_SYSDM16             347
#define IDB_TOOLBAR             358    /* ours: the whole toolbar, see devmgr.c */

/* menus */
#define IDM_MAIN                100
#define IDM_CONTEXT             101
#define IDM_OPTIONS             1000
#define IDM_EXIT                1001
#define IDM_UPDATE              1010
#define IDM_ENABLE              1011
#define IDM_DISABLE             1012
#define IDM_UNINSTALL           1013
#define IDM_SCAN                1014
#define IDM_ADD_LEGACY          1015
#define IDM_PRINT               1016
#define IDM_PROPERTIES          1017
#define IDM_HELP                1018
#define IDM_BY_TYPE             1020
#define IDM_BY_CONNECTION       1021
#define IDM_RES_BY_TYPE         1022
#define IDM_RES_BY_CONNECTION   1023
#define IDM_SHOW_HIDDEN         1024
#define IDM_CUSTOMIZE           1025
#define IDM_HELP_TOPICS         1030
#define IDM_ABOUT               1031
#define IDM_BACK                1040
#define IDM_FORWARD             1041
#define IDM_CONSOLE_TREE        1042
#define IDM_ACTION_PANE         1043

/* dialogs */
#define IDD_GENERAL             300
#define IDD_DRIVER              301
#define IDD_DETAILS             302
#define IDD_RESOURCES           303
#define IDD_DRIVER_FILES        304

#define IDC_DEVICE_ICON                1100
#define IDC_NAME                1101
#define IDC_TYPE                1102
#define IDC_MANUFACTURER        1103
#define IDC_LOCATION            1104
#define IDC_STATUS              1105
#define IDC_TROUBLESHOOT        1106
#define IDC_USAGE               1107
#define IDC_USAGE_LABEL         1108
#define IDC_PROVIDER            1110
#define IDC_DATE                1111
#define IDC_VERSION             1112
#define IDC_SIGNER              1113
#define IDC_DRIVER_DETAILS      1114
#define IDC_UPDATE_DRIVER       1115
#define IDC_ROLLBACK            1116
#define IDC_UNINSTALL           1117
#define IDC_DATE_LABEL          1118
#define IDC_VERSION_LABEL       1119
#define IDC_PROPERTY            1120
#define IDC_VALUES              1121
#define IDC_RESOURCE_LIST       1130
#define IDC_BASED_ON            1131
#define IDC_BASED_ON_LABEL      1132
#define IDC_AUTOMATIC           1133
#define IDC_CHANGE_SETTING      1134
#define IDC_CONFLICTS           1135
#define IDC_FILES               1140
#define IDC_FILE_PROVIDER       1141
#define IDC_FILE_VERSION        1142
#define IDC_FILE_COPYRIGHT      1143
#define IDC_FILE_SIGNER         1144

/* strings */
#define IDS_APPNAME             2000
#define IDS_WORKING             2001
#define IDS_WORKING_HINT        2002
#define IDS_NO_DRIVER           2003
#define IDS_NO_DRIVER_HINT      2004
#define IDS_REINSTALL           2005
#define IDS_TROUBLESHOOT        2006
#define IDS_USE_DEVICE          2007
#define IDS_UNKNOWN             2008
#define IDS_NOT_AVAILABLE       2009
#define IDS_NOT_SIGNED          2010
#define IDS_NO_FILES            2011
#define IDS_PROPERTIES_TITLE    2012
#define IDS_LOCATION_PCI        2013
#define IDS_LOCATION_ON         2014
#define IDS_LOCATION_USB        2015
#define IDS_STANDARD_DEVICES    2016
#define IDS_PROVIDER_LINUX      2017
#define IDS_NO_CONFLICTS        2018
#define IDS_RES_TYPE            2019
#define IDS_RES_SETTING         2020
#define IDS_RES_MEMORY          2021
#define IDS_RES_IO              2022
#define IDS_RES_IRQ             2023
#define IDS_RES_DMA             2024
#define IDS_RESTYPE_DMA         2025
#define IDS_RESTYPE_IO          2026
#define IDS_RESTYPE_IRQ         2027
#define IDS_RESTYPE_MEMORY      2028
#define IDS_NO_HOST             2029
#define IDS_PROVIDER_UNKNOWN    2030
#define IDS_BASIC_CONFIG        2031
#define IDS_DISPLAY_ABOUT       2032
#define IDS_STD_DISK            2033
#define IDS_STD_CDROM           2034
#define IDS_STD_MONITOR         2035
#define IDS_STD_KEYBOARD        2036
#define IDS_STD_COMPUTER        2037
#define IDS_STD_NET             2038
#define IDS_MICROSOFT           2039
#define IDS_BUILTIN_SIGNER      2040

#define IDS_PROP_INSTANCE       2100   /* the Details tab's list, in order */
#define IDS_PROP_HARDWARE_IDS   2101
#define IDS_PROP_COMPATIBLE_IDS 2102
#define IDS_PROP_MATCHING_ID    2103
#define IDS_PROP_SERVICE        2104
#define IDS_PROP_ENUMERATOR     2105
#define IDS_PROP_CLASS          2106
#define IDS_PROP_CLASS_GUID     2107
#define IDS_PROP_DRIVER_KEY     2108
#define IDS_PROP_FIRMWARE       2109
#define IDS_PROP_PDO            2110
#define IDS_PROP_COUNT          11

#define IDS_HINT_BASE           2200   /* + menu id - 1000: status bar text of the menu items */
#define IDS_LABEL_BASE          2300   /* + menu id - 1000: the Action menu items and the toolbar tips */

#define IDS_CLASS_BASE          2400   /* + CLASS_*: class names */
#define IDS_DEV_BASE            2500   /* + DEVNAME_*: names of devices that Windows names generically */
