/*
 * WineExplorer Desktop Environment - Display Properties (desk.cpl) Resources
 */

#ifndef __WINE_DESKCPL_RESOURCE_H
#define __WINE_DESKCPL_RESOURCE_H

#include <stddef.h>
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <commctrl.h>

#define IDS_CPL_NAME        1
#define IDS_CPL_INFO        2

#define ICO_MAIN            100

/* Dialog IDs */
#define IDD_DESKTOP         1000
#define IDD_THEMES          1001
#define IDD_BACKGROUND      1002
#define IDD_APPEARANCE      1003
#define IDD_SETTINGS        1004
#define IDD_ADVAPPEARANCE   1005
#define IDD_CUSTDESKTOP     1006

/* Control IDs: Common */
#define IDC_STATIC          -1

/* Themes Tab */
#define IDC_THEME_LIST      2100
#define IDC_THEME_SAVEAS    2101
#define IDC_THEME_DELETE    2102
#define IDC_THEME_PREVIEW   2103

/* Desktop / Background Tab */
#define IDC_BG_PREVIEW      2200
#define IDC_BG_LIST         2201
#define IDC_BG_BROWSE       2202
#define IDC_BG_POSITION     2203
#define IDC_BG_COLOR        2204
#define IDC_BG_CUSTDESK     2205

/* Customize Desktop Dialog */
#define IDC_CUST_MYDOCS     2210
#define IDC_CUST_MYCOMP     2211
#define IDC_CUST_MYNET      2212
#define IDC_CUST_RECYCLE    2213
#define IDC_CUST_CHANGEICON 2214
#define IDC_CUST_DEFAULTICON 2215

/* Appearance Tab */
#define IDC_APP_PREVIEW     2300
#define IDC_APP_STYLE       2301
#define IDC_APP_COLORSCHEME 2302
#define IDC_APP_FONTSIZE    2303
#define IDC_APP_EFFECTS     2304
#define IDC_APP_ADVANCED    2305

/* Advanced Appearance Dialog */
#define IDC_ADV_PREVIEW     2310
#define IDC_ADV_ELEMENT     2311
#define IDC_ADV_SIZE        2312
#define IDC_ADV_COLOR1      2313
#define IDC_ADV_COLOR2      2314
#define IDC_ADV_FONT        2315
#define IDC_ADV_FONTSIZE    2316
#define IDC_ADV_FONTBOLD    2317
#define IDC_ADV_FONTITALIC  2318

/* Settings Tab */
#define IDC_SET_MONITOR_IMG 2400
#define IDC_SET_DISPLAY_LBL 2401
#define IDC_SET_RES_SLIDER  2402
#define IDC_SET_RES_TEXT    2403
#define IDC_SET_BPP_COMBO   2404
#define IDC_SET_ADVANCED    2405

/* Original Virtual Desktop Dialog Controls */
#define IDC_VIRTUAL_DESKTOP         2000
#define IDC_DISPLAY_SETTINGS_LIST   2001
#define IDC_DISPLAY_SETTINGS_RESET  2002
#define IDC_DISPLAY_SETTINGS_APPLY  2003
#define IDC_EMULATE_MODESET         2004

#endif /* __WINE_DESKCPL_RESOURCE_H */
