/*
 * WineExplorer Desktop Environment - System Properties (sysdm.cpl)
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

#ifndef __WINE_SYSDM_RESOURCE_H
#define __WINE_SYSDM_RESOURCE_H

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <commctrl.h>

#define IDS_CPL_NAME        1
#define IDS_CPL_INFO        2
#define IDS_CPL_TITLE       3

#define ICO_MAIN            100
#define IDB_WINLOGO         101

/* Dialogs */
#define IDD_GENERAL         1001
#define IDD_COMPNAME        1002
#define IDD_HARDWARE        1003
#define IDD_ADVANCED        1004
#define IDD_PERFORMANCE     1005
#define IDD_ENVVARS         1006
#define IDD_COMPNAME_CHANGE 1007
#define IDD_STARTREC        1008
#define IDD_COMPNAME_MORE   1009
#define IDD_JOIN_CREDS      1010
#define IDD_JOIN_ERROR      1011
/* the Network Identification Wizard */
#define IDD_NETID_WELCOME   1020
#define IDD_NETID_USAGE     1021
#define IDD_NETID_KIND      1022
#define IDD_NETID_INFO      1023
#define IDD_NETID_ACCOUNT   1024
#define IDD_NETID_COMPDOM   1025
#define IDD_NETID_WORKGROUP 1026
#define IDD_NETID_DONE      1027

/* Startup and Recovery strings */
#define IDS_STARTREC_NO_DUMP     11
#define IDS_STARTREC_SMALL_DUMP  12
#define IDS_STARTREC_KERNEL_DUMP 13
#define IDS_STARTREC_FULL_DUMP   14

/* General Page Controls */
#define IDC_GEN_OS_LABEL       2001
#define IDC_GEN_VER_LABEL      2002
#define IDC_GEN_DE_LABEL       2003
#define IDC_GEN_REG_USER       2004
#define IDC_GEN_REG_ORG        2005
#define IDC_GEN_REG_PID        2006
#define IDC_GEN_CPU_LABEL      2007
#define IDC_GEN_RAM_LABEL      2008
#define IDC_GEN_LOGO_STATIC    2009

/* Computer Name Controls */
#define IDC_COMP_DESC          2101
#define IDC_COMP_NAME_LABEL    2102
#define IDC_COMP_WORKGROUP     2103
#define IDC_COMP_CHANGE_BTN    2104
#define IDC_COMPCHG_NAME       2105
#define IDC_COMPCHG_DOMAIN_RB  2106
#define IDC_COMPCHG_DOMAIN     2107
#define IDC_COMPCHG_WG_RB      2108
#define IDC_COMPCHG_WG         2109
#define IDC_COMPCHG_FULL       2110
#define IDC_COMPCHG_MORE       2111
#define IDC_COMP_MEMBER_LABEL  2112
#define IDC_COMP_NETID_BTN     2113
#define IDC_MORE_SUFFIX        2120
#define IDC_MORE_CHECK         2121
#define IDC_MORE_NETBIOS       2122
#define IDC_CRED_TEXT          2130
#define IDC_CRED_USER          2131
#define IDC_CRED_PASS          2132
#define IDC_CRED_DOMAIN        2133
#define IDC_ERR_TEXT           2140
#define IDC_ERR_DETAILS_BTN    2141
#define IDC_ERR_DETAILS        2142
#define IDC_ERR_ICON           2143
#define IDC_NID_TITLE          2150
#define IDC_NID_BUSINESS       2151
#define IDC_NID_HOME           2152
#define IDC_NID_DOMAIN_NET     2153
#define IDC_NID_NO_DOMAIN_NET  2154
#define IDC_NID_USER           2155
#define IDC_NID_PASS           2156
#define IDC_NID_DOMAIN         2157
#define IDC_NID_COMP_TEXT      2158
#define IDC_NID_COMP_NAME      2159
#define IDC_NID_COMP_DOMAIN    2160
#define IDC_NID_WORKGROUP      2161
#define IDC_NID_DONE_TEXT      2162
#define IDC_NID_B1             2163
#define IDC_NID_B2             2164
#define IDC_NID_B3             2165
#define IDC_NID_B4             2166
#define IDC_NID_B5             2167

/* Hardware Controls */
#define IDC_HW_DEVMGR_BTN      2201
#define IDC_HW_PROFILES_BTN    2202

/* Advanced Controls */
#define IDC_ADV_PERF_BTN       2301
#define IDC_ADV_PROFILE_BTN    2302
#define IDC_ADV_STARTUP_BTN    2303
#define IDC_ADV_ENVVAR_BTN     2304

/* Performance Controls */
#define IDC_PERF_RADIO_AUTO    2401
#define IDC_PERF_RADIO_BESTAPP 2402
#define IDC_PERF_RADIO_BESTPERF 2403
#define IDC_PERF_RADIO_CUSTOM  2404
#define IDC_PERF_TREE          2405

/* Environment Variables Controls */
#define IDC_ENV_USER_LIST      2501
#define IDC_ENV_USER_NEW       2502
#define IDC_ENV_USER_EDIT      2503
#define IDC_ENV_USER_DEL       2504
#define IDC_ENV_SYS_LIST       2505
#define IDC_ENV_SYS_NEW        2506
#define IDC_ENV_SYS_EDIT       2507
#define IDC_ENV_SYS_DEL        2508
/* New/Edit User (System) Variable */
#define IDD_ENVVAR_EDIT        1030
#define IDC_ENVE_NAME          2511
#define IDC_ENVE_VALUE         2512
#define IDC_ENV_USER_GROUP     2513

/* Startup and Recovery Controls (after ReactOS's sysdm IDD_STARTUPRECOVERY) */
#define IDC_STRECOSCOMBO       2601
#define IDC_STRECLIST          2602
#define IDC_STRRECLISTEDIT     2603
#define IDC_STRRECLISTUPDWN    2604
#define IDC_STRRECREC          2605
#define IDC_STRRECRECEDIT      2606
#define IDC_STRRECRECUPDWN     2607
#define IDC_STRRECEDIT         2608
#define IDC_STRRECWRITEEVENT   2609
#define IDC_STRRECSENDALERT    2610
#define IDC_STRRECRESTART      2611
#define IDC_STRRECDEBUGCOMBO   2612
#define IDC_STRRECDUMPFILE     2613
#define IDC_STRRECOVERWRITE    2614
#define IDC_STRRECLISTSEC      2615
#define IDC_STRRECRECSEC       2616
#define IDC_STRRECDUMPLABEL    2617

#endif /* __WINE_SYSDM_RESOURCE_H */
