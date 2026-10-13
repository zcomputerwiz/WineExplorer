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

#include "wexsearch.h"

/* the parts, in search_companion.c and search_results.c */
HWND search_results_create(HWND parent);
void search_results_clear(HWND results, BOOL searched);
void search_results_add(HWND results, SEARCH_RESULT *r);   /* takes r */
void search_results_done(HWND results);
int search_results_count(HWND results, BOOL selected);

#endif
