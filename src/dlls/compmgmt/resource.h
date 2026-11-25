/*
 * Computer Management - resource identifiers
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

#ifndef __COMPMGMT_RESOURCE_H
#define __COMPMGMT_RESOURCE_H

#define IDM_MAIN                100

#define IDS_APPNAME             1
#define IDS_ROOT                2
#define IDS_COL_NAME            3
#define IDS_COL_DESCRIPTION     4
#define IDS_NO_DEVMGR           5
#define IDS_ABOUT_TEXT          6
#define IDS_NOT_AVAILABLE       7
/* the nodes, in the order of the node table: name, description, and for the grayed ones the reason */
#define IDS_NODE_BASE           100
#define IDS_DESC_BASE           200
#define IDS_REASON_BASE         300

#define IDM_REFRESH             1000
#define IDM_PROPERTIES          1001
#define IDM_EXPORT              1002
#define IDM_HELP_TOPICS         1003
#define IDM_ABOUT               1004
#define IDM_EXIT                1005
#define IDM_VIEW_LARGE          1010
#define IDM_VIEW_SMALL          1011
#define IDM_VIEW_LIST           1012
#define IDM_VIEW_DETAIL         1013
#define IDM_DM_BY_TYPE          1020
#define IDM_DM_BY_CONNECTION    1021
#define IDM_DM_RES_BY_TYPE      1022
#define IDM_DM_RES_BY_CONNECTION 1023
#define IDM_DM_SHOW_HIDDEN      1024
#define IDM_DM_SCAN             1025

/* the Services node */
#define IDM_SVC_START           1030
#define IDM_SVC_STOP            1031
#define IDM_SVC_PAUSE           1032
#define IDM_SVC_RESUME          1033
#define IDM_SVC_RESTART         1034
#define IDM_SVC_PROPERTIES      1035

#define IDD_SVC_GENERAL         200
#define IDD_SVC_LOGON           201
#define IDD_SVC_RECOVERY        202
#define IDD_SVC_DEPENDENCIES    203

#define IDC_SVC_NAME            300
#define IDC_SVC_DISPLAY         301
#define IDC_SVC_DESCRIPTION     302
#define IDC_SVC_PATH            303
#define IDC_SVC_STARTUP         304
#define IDC_SVC_STATUS          305
#define IDC_SVC_START           306
#define IDC_SVC_STOP            307
#define IDC_SVC_PAUSE           308
#define IDC_SVC_RESUME          309
#define IDC_SVC_ACCOUNT         310
#define IDC_SVC_DEPENDS_ON      311
#define IDC_SVC_DEPENDENTS      312
#define IDC_SVC_NOTE            313

#define IDS_COL_STATUS          20
#define IDS_COL_STARTUP         21
#define IDS_COL_LOGON           22
#define IDS_STATUS_STARTED      23
#define IDS_STATUS_PAUSED       24
#define IDS_STARTUP_AUTO        25
#define IDS_STARTUP_MANUAL      26
#define IDS_STARTUP_DISABLED    27
#define IDS_ASK_ELEVATE         28
#define IDS_ELEVATE_TITLE       29
#define IDS_SVC_PROPS_TITLE     30
#define IDS_SVC_ERROR           31
#define IDS_SVC_TIMEOUT         32
#define IDS_RECOVERY_NOTE       33
#define IDS_LOGON_NOTE          34
#define IDS_SVC_NO_DESCRIPTION  35
#define IDS_STARTUP_BOOT        36
#define IDS_STARTUP_SYSTEM      37

#define IDI_COMPMGMT            1

#endif
