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

#define IDI_COMPMGMT            1

#endif
