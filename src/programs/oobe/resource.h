/*
 * OOBE Wizard resource header
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

#ifndef WEX_OOBE_RESOURCE_H
#define WEX_OOBE_RESOURCE_H

#define IDD_OOBE_WELCOME        101
#define IDD_OOBE_UPDATES        102
#define IDD_OOBE_CONNECTIVITY   103
#define IDD_OOBE_COMPONENTS     104
#define IDD_OOBE_USERS          105
#define IDD_OOBE_PASSWORD       106
#define IDD_OOBE_FINISH         107
#define IDD_OOBE_TERMS          108
#define IDD_OOBE_REVIEW         109

#define IDC_STATIC              -1

#define IDC_RADIO_UPDATES_ON    1010
#define IDC_RADIO_UPDATES_OFF   1011

#define IDC_NET_STATUS          1020

#define IDC_COMP_LIST           1050
#define IDC_COMP_DESC           1051
#define IDC_COMP_SIZE           1052
#define IDC_BTN_TERMS           1053
#define IDC_COMP_STATUS         1055

#define IDC_TERMS_TEXT          1060
#define IDC_TERMS_AGREE         1061

/* Review-only embedded terms dialog (disabled components such as CoreFonts). */
#define IDC_REV_INFO            1080   /* vendor / source / size / cache state */
#define IDC_REV_TERMS           1081   /* rendered terms text */
#define IDC_REV_STATUS          1082   /* progress and result line */
#define IDC_BTN_REV_DOWNLOAD    1083   /* authorizes the download, before it starts */
#define IDC_BTN_REV_CANCEL      1084   /* stops a running download */
#define IDC_REV_AGREE           1085   /* install acceptance; hidden for review-only rows */

#define IDC_USER1               1031
#define IDC_USER2               1032
#define IDC_USER3               1033
#define IDC_USER4               1034
#define IDC_USER5               1035

#define IDC_PASS_USER_LABEL     1040
#define IDC_PASS_EDIT1          1041
#define IDC_PASS_EDIT2          1042

#define IDC_FINISH_SUMMARY      1070

#endif /* WEX_OOBE_RESOURCE_H */
