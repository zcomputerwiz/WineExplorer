/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#ifndef WEX_BROWSE_H
#define WEX_BROWSE_H
#include <windows.h>
BOOL WINAPI WexBrowseOpen(const WCHAR *root, const WCHAR *selection, BOOL explorer_mode, int show);
#endif
