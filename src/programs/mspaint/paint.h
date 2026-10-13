/*
 * WineExplorer Desktop Environment - paint.h
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

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDI_APP             100
#define IDR_MAINMENU        101
#define ID_ACCEL            102

#define IDM_FILE_NEW        1001
#define IDM_FILE_OPEN       1002
#define IDM_FILE_SAVE       1003
#define IDM_FILE_SAVEAS     1004
#define IDM_FILE_EXIT       1005

#define IDM_EDIT_UNDO       1010
#define IDM_EDIT_CLEAR      1011

#define IDM_HELP_ABOUT      1020

#define IDC_TOOL_PENCIL     2001
#define IDC_TOOL_BRUSH      2002
#define IDC_TOOL_ERASER     2003
#define IDC_TOOL_LINE       2004
#define IDC_TOOL_RECT       2005
#define IDC_TOOL_FILLEDRECT 2006
#define IDC_TOOL_ELLIPSE    2007
#define IDC_TOOL_FILLEDELLIPSE 2008
#define IDC_TOOL_FILL       2009

#define TOOL_PENCIL         0
#define TOOL_BRUSH          1
#define TOOL_ERASER         2
#define TOOL_LINE           3
#define TOOL_RECT           4
#define TOOL_FILLEDRECT     5
#define TOOL_ELLIPSE        6
#define TOOL_FILLEDELLIPSE  7
#define TOOL_FILL           8
#define NUM_TOOLS           9
