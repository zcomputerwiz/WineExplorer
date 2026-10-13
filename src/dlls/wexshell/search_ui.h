/*
 * WineExplorer Desktop Environment - Search Companion and Search Results of an Explorer window
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

#ifndef __WINE_EXPLORER_SEARCH_UI_H
#define __WINE_EXPLORER_SEARCH_UI_H

#include <windows.h>
#include "search_engine.h"

/* "Search Results", CLSID_FindFolder: what explorer.exe ::{E17D4FC0-...} and SHFindFiles open */
#define FIND_FOLDER_PATH L"::{E17D4FC0-5564-11D1-83F2-00A0C90DC849}"

/* Sent to the Explorer window when the search changes what it shows (wparam: SEARCHUI_*) */
#define WM_SEARCHUI_CHANGED (WM_APP + 50)
enum { SEARCHUI_RESULTS_SHOWN = 1, SEARCHUI_PANE_CLOSED, SEARCHUI_SEARCH_DONE };

typedef struct search_ui SEARCH_UI;

/* The pane on the left of an Explorer window.  scope: the folder to look in (NULL: Local Hard Drives); results:
 * show the Search Results list right away (opened as "Search Results") instead of the folder until the search */
SEARCH_UI *search_ui_create(HWND parent, const WCHAR *scope, BOOL results);
void search_ui_destroy(SEARCH_UI *ui);

/* Lays the pane and the results out in rect (the client area below the bars).  Returns TRUE when the results list
 * fills the right part, so that the folder view is to be hidden; rect is then empty, else it is what is left. */
BOOL search_ui_layout(SEARCH_UI *ui, RECT *rect);

BOOL search_ui_pane_visible(SEARCH_UI *ui);
BOOL search_ui_results_visible(SEARCH_UI *ui);
int search_ui_result_count(SEARCH_UI *ui, BOOL selected);
void search_ui_show_pane(SEARCH_UI *ui, BOOL show);
void search_ui_set_scope(SEARCH_UI *ui, const WCHAR *folder);
BOOL search_ui_translate(SEARCH_UI *ui, MSG *msg);   /* Tab, Enter: call from the message loop */

/* the parts, in search_companion.c and search_results.c */
HWND search_results_create(HWND parent);
void search_results_clear(HWND results, BOOL searched);
void search_results_add(HWND results, SEARCH_RESULT *r);   /* takes r */
void search_results_done(HWND results);
int search_results_count(HWND results, BOOL selected);

#endif
