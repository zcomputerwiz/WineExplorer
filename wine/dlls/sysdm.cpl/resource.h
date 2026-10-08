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

#endif /* __WINE_SYSDM_RESOURCE_H */
