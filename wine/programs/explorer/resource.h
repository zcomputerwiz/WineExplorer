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
#define IDM_ITEM_PROPERTIES      2025

#endif

