/*
 * WineExplorer Desktop Environment - Taskbar and Start Menu Properties Header
 */

#ifndef __WINE_EXPLORER_TASKBAR_PROPS_H
#define __WINE_EXPLORER_TASKBAR_PROPS_H

#include <windows.h>

void taskbar_props_show(HWND parent);

/* Active Taskbar Settings */
typedef struct _TASKBAR_SETTINGS
{
    BOOL lock_taskbar;
    BOOL autohide;
    BOOL always_on_top;
    BOOL group_buttons;
    BOOL show_quick_launch;
    BOOL show_clock;
    BOOL hide_inactive_icons;
    int  start_menu_style; /* 0: Classic, 1: Two-column XP, 2: Modern */
} TASKBAR_SETTINGS;

TASKBAR_SETTINGS *taskbar_settings_get(void);
void taskbar_settings_save(void);
void taskbar_settings_load(void);

#endif /* __WINE_EXPLORER_TASKBAR_PROPS_H */
