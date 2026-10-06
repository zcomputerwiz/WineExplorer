/*
 * WineExplorer Desktop Environment - Date and Time Properties (timedate.cpl)
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
