/*
 * WineExplorer Desktop Environment - Mouse & Keyboard Control Panel (main.cpl)
 *
 * Resource Definitions
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

#ifndef __WINE_MAIN_CPL_RESOURCE_H
#define __WINE_MAIN_CPL_RESOURCE_H

#include <windows.h>
#include <commctrl.h>

#define IDC_STATIC                      -1

/* Applet and Control Icons */
#define IDI_MOUSE                       100
#define IDI_KEYBD                       200
#define IDI_PRINT                       300
#define IDI_FONTS                       400
#define IDI_ADM                         500

#define IDI_PTSPEED                     104
#define IDI_DELAY                       105
#define IDI_REPEAT                      106
#define IDI_SNAPDEF                     108
#define IDI_SGLCLICK                    109
#define IDI_DBLCLICK                    110

#define IDI_PTTRAILS                    600
#define IDI_PTVANISH                    601
#define IDI_PTSONAR                     602
#define IDI_WHEEL                       603

#define ICON_FOLDER_CLOSED              605
#define ICON_FOLDER_OPEN                606
#define ICON_CLICKLOCK                  607

/* Bitmaps */
#define IDB_MOUSE                       100

/* String IDs */
#define IDS_MOUSE_TITLE                 100
#define IDS_MOUSE_EXPLAIN               101
#define IDS_KEYBD_TITLE                 102
#define IDS_KEYBD_EXPLAIN               103
#define IDS_PRINT_TITLE                 104
#define IDS_PRINT_EXPLAIN               105
#define IDS_FONTS_TITLE                 106
#define IDS_FONTS_EXPLAIN               107
#define IDS_ADM_TITLE                   108
#define IDS_ADM_EXPLAIN                 109

#define IDS_NONE                        200
#define IDS_SCHEME_DEFAULT              201
#define IDS_SCHEME_INVERTED             202
#define IDS_SCHEME_BLACK                203
#define IDS_SCHEME_STANDARD             204

#define IDS_CURSOR_NORMAL               210
#define IDS_CURSOR_HELP                 211
#define IDS_CURSOR_APPSTARTING          212
#define IDS_CURSOR_WAIT                 213
#define IDS_CURSOR_CROSS                214
#define IDS_CURSOR_IBEAM                215
#define IDS_CURSOR_NWPEN                216
#define IDS_CURSOR_NO                   217
#define IDS_CURSOR_SIZENS               218
#define IDS_CURSOR_SIZEWE               219
#define IDS_CURSOR_SIZENWSE             220
#define IDS_CURSOR_SIZENESW             221
#define IDS_CURSOR_SIZEALL              222
#define IDS_CURSOR_UPARROW              223
#define IDS_CURSOR_HAND                 224

/* Dialog IDs */
#define DLG_MOUSE_BUTTONS               100
#define DLG_MOUSE_POINTER               101
#define DLG_MOUSE_MOTION                103
#define DLG_MOUSE_WHEEL                 108
#define DLG_HARDWARE                    106
#define DLG_KEYBD_SPEED                 104
#define IDD_CLICKLOCK_SETTINGS_DLG      116
#define DLG_MOUSE_POINTER_SCHEMESAVE    99

/* Mouse Buttons Page Controls */
#define IDBTN_BUTTONSWAP                101
#define MOUSE_MOUSEBMP                  103
#define MOUSE_CLICKSCROLL               105
#define MOUSE_DBLCLK_TEST_AREA          106
#define IDCK_CLICKLOCK                  114
#define IDBTN_CLICKLOCK_SETTINGS        115
#define IDC_CLICKLOCK_SETTINGS_TXT      117
#define IDC_CLICKLOCK_SETTINGS_LEFT_TXT 118
#define IDC_CLICKLOCK_SETTINGS_RIGHT_TXT 119
#define IDT_CLICKLOCK_TIME_SETTINGS     120
#define IDC_DBLCLICK_TEXT               124
#define IDC_CLICKLOCK_TEXT              125

/* Mouse Pointers Page Controls */
#define ID_SCHEMECOMBO                  113
#define ID_SAVESCHEME                   111
#define ID_REMOVESCHEME                 112
#define ID_PREVIEW                      110
#define ID_CURSORLIST                   101
#define ID_CURSORSHADOW                 114
#define ID_DEFAULT                      103
#define ID_BROWSE                       102
#define ID_SCHEMEFILENAME               300

/* Mouse Motion (Pointer Options) Page Controls */
#define MOUSE_SPEEDBMP                  106
#define MOUSE_SPEEDSCROLL               101
#define MOUSE_ENHANCED_MOTION           116
#define MOUSE_SNAPDEF                   109
#define MOUSE_PTRSNAPDEF                110
#define MOUSE_TRAILS                    103
#define MOUSE_PTRTRAIL                  105
#define MOUSE_TRAILSCROLL               104
#define MOUSE_TRAILSCROLLTXT1           107
#define MOUSE_TRAILSCROLLTXT2           108
#define MOUSE_VANISH                    114
#define MOUSE_PTRVANISH                 112
#define MOUSE_SONAR                     115
#define MOUSE_PTRSONAR                  113

/* Mouse Wheel Page Controls */
#define IDBMP_SCROLL                    101
#define IDRAD_SCROLL_LINES              103
#define IDRAD_SCROLL_PAGE               104
#define IDE_BUDDY_SCROLL_LINES          105
#define IDC_SPIN_SCROLL_LINES           106

/* Keyboard Speed Page Controls */
#define KDELAY_SCROLL                   201
#define KSPEED_SCROLL                   202
#define KREPEAT_EDIT                    203
#define KBLINK_SCROLL                   204

/* Hardware Page Controls */
#define IDC_DEV_LIST                    301
#define IDC_DEV_PROPERTIES              302
#define IDC_DEV_DESC                    303

#endif /* __WINE_MAIN_CPL_RESOURCE_H */
