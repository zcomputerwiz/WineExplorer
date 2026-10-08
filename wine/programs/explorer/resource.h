/*
 * Explorer resource definitions
 *
 * Copyright 2011 Jay Yang
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

#ifndef __WINE_EXPLORER_RESOURCE_H
#define __WINE_EXPLORER_RESOURCE_H

#define IDS_EXPLORER_TITLE 1
#define IDS_PATHBOX_LABEL 2
#define IDS_START_LABEL 3
#define IDS_RUN 4
#define IDS_EXIT_LABEL 5
#define IDS_EXIT_PROMPT 6
#define IDS_DESKTOP_TITLE 7

/* Dialogs */
#define IDD_TASKBARPROP       101
#define IDD_STARTMENUPROP     102

/* Taskbar Property Controls */
#define IDC_LOCKTASKBAR       1101
#define IDC_AUTOHIDETASKBAR   1102
#define IDC_ONTOPTASKBAR      1103
#define IDC_GROUPTASKBAR      1104
#define IDC_QUICKLAUNCH       1105
#define IDC_SHOWCLOCK         1106
#define IDC_HIDEINACTIVEICONS 1107
#define IDC_TASKBAR_PREVIEW   1108

/* Start Menu Property Controls */
#define IDC_START_CLASSIC     1201
#define IDC_START_TWOCOLUMN   1202
#define IDC_START_MODERN      1203
#define IDC_START_CUSTOMIZE   1204
#define IDC_START_PREVIEW     1205

/* Context Menus */
#define IDM_TASKBAR_CASCADE      2001
#define IDM_TASKBAR_TILE_H       2002
#define IDM_TASKBAR_TILE_V       2003
#define IDM_TASKBAR_SHOWDESKTOP  2004
#define IDM_TASKBAR_TASKMGR      2005
#define IDM_TASKBAR_LOCK         2006
#define IDM_TASKBAR_PROPERTIES   2007

#define IDM_STARTBTN_OPEN        2011
#define IDM_STARTBTN_EXPLORE     2012
#define IDM_STARTBTN_SEARCH      2013
#define IDM_STARTBTN_PROPERTIES  2014

#define IDM_ITEM_OPEN            2021
#define IDM_ITEM_EXPLORE         2022
#define IDM_ITEM_MANAGE          2023
#define IDM_ITEM_EMPTY_RECYCLE   2024
#define IDM_ITEM_DELETE          2025
#define IDM_ITEM_PROPERTIES      2026

/* Network Tray and Wireless Icons */
#define IDI_NET_BLANK            1001
#define IDI_NET_DISCONNECT       1002
#define IDI_NET_PLUGGED          1003
#define IDI_NET_RECV             1004
#define IDI_NET_XMIT             1005
#define IDI_NET_XMITRECV         1006
#define IDI_NET_SIG0             1007
#define IDI_NET_SIG1             1008
#define IDI_NET_SIG2             1009
#define IDI_NET_SIG3             1010
#define IDI_NET_SIG4             1011
#define IDI_NET_SIG5             1012
#define IDI_NET_WIRELESS         1013
#define IDB_WZC_LISTICONS        1014

/* Dialogs */
#define IDD_WZCQCFG              103
#define IDD_STATMON_GENERAL_LAN  104
#define IDD_STATMON_GENERAL_WLAN 107

/* Wireless Quick Config Controls */
#define IDC_WZCQCFG_LBL_INFO     1301
#define IDC_WZCQCFG_LBL_NETWORKS 1302
#define IDC_WZCQCFG_NETWORKS     1303
#define IDC_WZCQCFG_ICO_WARN     1304
#define IDC_WZCQCFG_LBL_NOWKINFO 1305
#define IDC_WZCQCFG_CHK_NOWK     1306
#define IDC_WZCQCFG_LBL_WKINFO   1307
#define IDC_WZCQCFG_LBL_WEPK     1308
#define IDC_WZCQCFG_WEPK         1309
#define IDC_WZCQCFG_LBL_WEPK2    1310
#define IDC_WZCQCFG_WEPK2        1311
#define IDC_WZCQCFG_CHK_SHOWPWD  1312
#define IDC_WZCQCFG_REFRESH      1313
#define IDC_WZCQCFG_ADVANCED     1314
#define IDC_WZCQCFG_CONNECT      1315

/* Status Monitor Controls */
#define IDC_STATMON_TAB          1401
#define IDC_TXT_SM_STATUS        1402
#define IDC_TXT_SM_DURATION      1403
#define IDC_TXT_SM_SPEED         1404
#define IDC_TXT_SM_SSID          1405
#define IDC_CTL_SM_SIGNAL        1406
#define IDC_TXT_SM_BYTES_TRANS   1407
#define IDC_TXT_SM_BYTES_RCVD    1408
#define IDC_TXT_SM_PKTS_TRANS    1409
#define IDC_TXT_SM_PKTS_RCVD     1410
#define IDC_TXT_SM_IP            1411
#define IDC_TXT_SM_SUBNET        1412
#define IDC_TXT_SM_GATEWAY       1413
#define IDC_TXT_SM_DHCP          1414
#define IDC_BTN_SM_PROPERTIES    1415
#define IDC_BTN_SM_DISABLE       1416
#define IDC_BTN_SM_REPAIR        1417
#define IDC_ICO_SM_ACTIVITY      1418
#define IDC_LBL_SM_STATUS        1421
#define IDC_LBL_SM_DUR           1422
#define IDC_LBL_SM_SPEED         1423
#define IDC_LBL_SM_SSID          1424
#define IDC_LBL_SM_SIGNAL        1425
#define IDC_LBL_SM_BYTES         1426
#define IDC_LBL_SM_PKTS          1427
#define IDC_LBL_SM_SENT          1428
#define IDC_LBL_SM_RCVD          1429
#define IDC_LBL_SM_DIVIDER       1430
#define IDC_LBL_SM_TYPE          1431
#define IDC_LBL_SM_IP            1432
#define IDC_LBL_SM_SUBNET        1433
#define IDC_LBL_SM_GW            1434
#define IDC_GRP_SM_CONN          1435
#define IDC_GRP_SM_ACT           1436
#define IDC_GRP_SM_SUPPORT       1437
#define IDC_ICO_SM_SUPPORT       1438
#define IDC_BTN_SM_DETAILS       1439
#define IDC_TXT_SM_SUPPORT_NOTE  1440
#define IDC_LBL_SM_DASH1         1441
#define IDC_LBL_SM_DASH2         1442

/* Volume & Power System Tray Icons */
#define IDI_VOLUME               1015
#define IDI_MUTE                 1016
#define IDI_BATTERYPLUG          1017
#define IDI_BATT                 1018
#define IDI_HOTPLUG              1019
#define IDI_START                1020
#define IDB_AVATAR               1026
#define IDB_LOGOFF               1027
#define IDB_SHUTDOWN             1028
#define IDB_STARTBTN             1029

/* Tray Volume Dialog */
#define IDD_TRAYMASTER           105
#define IDC_TRAY_VOLUME_SLIDER   1501
#define IDC_TRAY_VOLUME_MUTE     1502
#define IDC_TRAY_VOLUME_LABEL    1503

/* Volume Tray Context Menu */
#define IDM_VOL_OPEN_MIXER       2201
#define IDM_VOL_AUDIO_PROPS      2202

/* Power Tray Context Menu */
#define IDM_PWR_OPEN_METER       2301
#define IDM_PWR_PROPERTIES       2302
#define IDM_PWR_SCHEME_BASE      2310

/* Local Area Connection Icons */
#define IDI_NET_LAN              1021
#define IDI_NET_ADAPTER          1022
#define IDI_NET_CLIENT           1023
#define IDI_NET_SERVICE          1024
#define IDI_NET_PROTOCOL         1025

/* Local Area Connection Properties Dialog */
#define IDD_LAN_PROPERTIES       106
#define IDC_NETPROP_TAB          1601
#define IDC_NETPROP_ADAPTER_ICO  1602
#define IDC_NETPROP_ADAPTER_NAME 1603
#define IDC_NETPROP_CONFIGURE    1604
#define IDC_NETPROP_COMPLIST     1605
#define IDC_NETPROP_INSTALL      1606
#define IDC_NETPROP_UNINSTALL    1607
#define IDC_NETPROP_PROPERTIES   1608
#define IDC_NETPROP_DESCGROUP    1609
#define IDC_NETPROP_DESC         1610
#define IDC_NETPROP_SHOWICON     1611
#define IDC_NETPROP_NOTIFY       1612
#define IDC_NETPROP_FW_GROUP     1613
#define IDC_NETPROP_FIREWALL     1614
#define IDC_NETPROP_FW_LINK      1615
#define IDC_NETPROP_ICS_GROUP    1616
#define IDC_NETPROP_ICS_SHARE    1617
#define IDC_NETPROP_ICS_BEACON   1618
#define IDC_NETPROP_SETTINGS     1619

/* polkit / UAC-style authentication dialog */
#define IDD_UAC                  2400
#define IDC_UAC_BANNER           2401
#define IDC_UAC_PROGRAM          2402
#define IDC_UAC_PROMPT           2403
#define IDC_UAC_ACCOUNT          2404
#define IDC_UAC_PASSWORD         2405
#define IDC_UAC_USERTILE         2406
#define IDC_UAC_PW_LABEL         2407
#define IDC_UAC_ERROR            2408

#endif


