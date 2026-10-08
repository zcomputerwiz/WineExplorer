/*
 * Wine sndvol32 resource definitions
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

#ifndef __WINE_SNDVOL32_RESOURCE_H
#define __WINE_SNDVOL32_RESOURCE_H

#define IDI_MIXER                 1
#define IDI_MUTE                  2
#define IDI_TRAY                  3
#define IDI_LSPEAKER              4
#define IDI_RSPEAKER              5

#define IDR_MIXERMENU             10
#define IDM_PROPERTIES            101
#define IDM_ADVANCED              102
#define IDM_EXIT                  103
#define IDM_HELPTOPICS            201
#define IDM_ABOUT                 202

#define IDD_MIXER_MAIN            500
#define IDD_TRAYMASTER            501

/* Controls */
#define IDC_MASTER_BALANCE        1001
#define IDC_MASTER_VOLUME         1002
#define IDC_MASTER_MUTE           1003

#define IDC_WAVE_BALANCE          1011
#define IDC_WAVE_VOLUME           1012
#define IDC_WAVE_MUTE             1013

#define IDC_SYNTH_BALANCE         1021
#define IDC_SYNTH_VOLUME          1022
#define IDC_SYNTH_MUTE            1023

#define IDC_CD_BALANCE            1031
#define IDC_CD_VOLUME             1032
#define IDC_CD_MUTE               1033

#define IDC_MIC_BALANCE           1041
#define IDC_MIC_VOLUME            1042
#define IDC_MIC_MUTE              1043

#define IDC_LINE_BALANCE          1051
#define IDC_LINE_VOLUME           1052
#define IDC_LINE_MUTE             1053

#define IDC_TRAY_VOLUME_LABEL     1101
#define IDC_TRAY_VOLUME_SLIDER    1102
#define IDC_TRAY_VOLUME_MUTE      1103

#endif /* __WINE_SNDVOL32_RESOURCE_H */
