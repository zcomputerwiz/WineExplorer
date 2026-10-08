/*
 * WineExplorer Desktop Environment - Date and Time Properties (timedate.cpl)
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

#ifndef __WINE_TIMEDATE_RESOURCE_H
#define __WINE_TIMEDATE_RESOURCE_H

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <commctrl.h>

#define IDS_CPL_NAME        1
#define IDS_CPL_INFO        2

#define ICO_MAIN            100
#define IDB_WORLD           101

/* Dialogs */
#define IDD_DATEANDTIME     1001
#define IDD_TIMEZONE        1002

/* Controls: Common */
#define IDC_STATIC          -1

/* Date and Time Page */
#define IDC_MONTHCAL        2001
#define IDC_CLOCK_CTRL      2002
#define IDC_TIME_EDIT       2003
#define IDC_TIME_SPIN       2004

/* Time Zone Page */
#define IDC_TIMEZONE_LIST   2101
#define IDC_TIMEZONE_AUTO   2102
#define IDC_TIMEZONE_MAP    2103


#endif /* __WINE_TIMEDATE_RESOURCE_H */
