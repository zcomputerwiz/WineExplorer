/*
 * WineExplorer Desktop Environment - Start menu settings, pinned programs and most frequently used programs
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

#ifndef __WINE_EXPLORER_STARTSETTINGS_H
#define __WINE_EXPLORER_STARTSETTINGS_H

#include <windows.h>

#define START_HIDE  0   /* "Don't display this item" */
#define START_LINK  1   /* "Display as a link" (or simply shown) */
#define START_MENU  2   /* "Display as a menu" */

/* The "Customize Start Menu" options; the value names are the ones of Windows XP under
 * HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced */
typedef struct _START_SETTINGS
{
    BOOL large_icons;      /* Start_LargeMFUIcons */
    int  min_mfu;          /* Start_MinMFU: number of programs on the Start menu */
    BOOL show_internet;    /* ShowInternet / ShowEmail of the StartPanel are not stored by Windows XP in Advanced */
    BOOL show_email;
    int  my_computer;      /* Start_ShowMyComputer */
    int  my_docs;          /* Start_ShowMyDocs */
    int  my_music;         /* Start_ShowMyMusic */
    int  my_pics;          /* Start_ShowMyPics */
    int  net_places;       /* Start_ShowNetPlaces (0 or 1) */
    int  control_panel;    /* Start_ShowControlPanel */
    int  connect_to;       /* Start_ShowNetConn */
    int  printers;         /* Start_ShowPrinters (0 or 1) */
    int  help;             /* Start_ShowHelp */
    int  search;           /* Start_ShowSearch */
    int  run;              /* Start_ShowRun */
    int  program_access;   /* Start_ShowSetProgramAccessAndDefaults */
    BOOL recent_docs;      /* Start_ShowRecentDocs */
    BOOL notify_new_apps;  /* Start_NotifyNewApps */
    BOOL hover_open;       /* the submenus open when the mouse pauses on them (MenuShowDelay != 0) */
} START_SETTINGS;

void startsettings_load(START_SETTINGS *s);
void startsettings_save(const START_SETTINGS *s);

/* a program of the left column of the Start panel */
typedef struct _START_PROGRAM
{
    WCHAR path[MAX_PATH];   /* what to run (executable or shortcut) */
    WCHAR title[96];
    BOOL pinned;
} START_PROGRAM;

/* Pinned programs (always shown, in order) */
int  startprograms_pinned(START_PROGRAM *out, int max);
BOOL startprograms_is_pinned(const WCHAR *path);
void startprograms_pin(const WCHAR *path, const WCHAR *title);
void startprograms_unpin(const WCHAR *path);

/* The most frequently used programs, best first, without pinned and removed ones */
int  startprograms_mfu(START_PROGRAM *out, int max);
void startprograms_count_launch(const WCHAR *path);
void startprograms_remove_from_list(const WCHAR *path);
void startprograms_clear_list(void);

/* a readable name for a program: shortcut name, else the file description, else the file name */
void startprograms_title(const WCHAR *path, WCHAR *title, int len);

#endif
