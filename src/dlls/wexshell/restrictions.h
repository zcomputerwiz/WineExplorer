/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The shell restrictions of Group Policy (docs/GROUP_POLICY.md, "Local policy"): the values under
 * Software\Microsoft\Windows\CurrentVersion\Policies\Explorer, in the user's hive and in the machine's.
 */

#ifndef WEXSHELL_RESTRICTIONS_H
#define WEXSHELL_RESTRICTIONS_H

#include <windows.h>

/* TRUE when the value NAME ("NoRun", "NoClose", ...) of Policies\Explorer is not 0 for the user or for the machine */
BOOL shell_restricted( const WCHAR *name );

#endif
