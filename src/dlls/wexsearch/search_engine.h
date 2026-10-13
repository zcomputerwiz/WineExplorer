/*
 * WineExplorer Desktop Environment - file search engine (Search Companion, instant search)
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

#ifndef __WINE_EXPLORER_SEARCH_ENGINE_H
#define __WINE_EXPLORER_SEARCH_ENGINE_H

#include <windows.h>

#define SEARCH_MAX_ROOTS 32

enum { SEARCH_DATE_NONE, SEARCH_DATE_MODIFIED, SEARCH_DATE_CREATED, SEARCH_DATE_ACCESSED };

/* The criteria of Windows XP's "All files and folders" search */
typedef struct
{
    WCHAR names[MAX_PATH];        /* all or part of the file name; several separated by ';' or ','; * and ? allowed */
    WCHAR text[256];              /* a word or phrase in the file */
    WCHAR roots[SEARCH_MAX_ROOTS][MAX_PATH];   /* look in */
    int root_count;
    int date_kind;                /* SEARCH_DATE_* */
    FILETIME date_from, date_to;  /* inclusive */
    ULONGLONG size_min, size_max; /* bytes; size_max 0 = no limit */
    WCHAR extensions[1024];       /* type of file: ".txt;.log;" (empty = all files and folders) */
    BOOL system_folders, hidden, subfolders, case_sensitive;
} SEARCH_CRITERIA;

typedef struct
{
    WCHAR path[MAX_PATH];
    DWORD attributes;
    ULONGLONG size;
    FILETIME modified;
} SEARCH_RESULT;

/* Messages posted to the window that started the search.  wparam is the search id; stale ids are to be ignored
 * (but their lparam still freed). */
#define WM_SEARCH_RESULT  (WM_APP + 40)   /* lparam: SEARCH_RESULT *, free() it */
#define WM_SEARCH_FOLDER  (WM_APP + 41)   /* lparam: WCHAR * of the folder being searched, free() it */
#define WM_SEARCH_DONE    (WM_APP + 42)   /* lparam: number of results */

typedef struct search_job SEARCH_JOB;

void search_criteria_init(SEARCH_CRITERIA *c);   /* XP's defaults: subfolders and system folders, no hidden files */
SEARCH_JOB *search_start(const SEARCH_CRITERIA *c, HWND notify, UINT id);
void search_stop(SEARCH_JOB *job);   /* stops the search, waits for it and frees it */

/* "Local Hard Drives": the fixed drives, without the one that shows the whole Linux file system */
int search_local_drives(WCHAR roots[][MAX_PATH], int max);

#endif
