/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <windows.h>
#include "wexres.h"
#include "taskbar_props.h"
#include "shellfolders.h"

BOOL shellfolder_on_desktop(int csidl)
{
    return shellfolder_on_desktop_style(csidl, taskbar_settings_get()->start_menu_style);
}

void shellfolder_set_on_desktop(int csidl, BOOL show)
{
    shellfolder_set_on_desktop_style(csidl, show, taskbar_settings_get()->start_menu_style);
}

int shellfolder_desktop_items(int *csidls, int max)
{
    return shellfolder_desktop_items_style(csidls, max, taskbar_settings_get()->start_menu_style);
}
