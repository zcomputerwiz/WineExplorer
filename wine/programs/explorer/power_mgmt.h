/*
 * WineExplorer Desktop Environment - Power Management & Display Manager Integration
 *
 * Implements authentic "Shut Down Windows" dialog and Linux power management
 * (systemd-logind D-Bus, systemctl poweroff/reboot, session termination).
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

#ifndef __WINE_EXPLORER_POWER_MGMT_H
#define __WINE_EXPLORER_POWER_MGMT_H

#include <windows.h>

#define POWER_ACTION_SHUTDOWN 0
#define POWER_ACTION_RESTART  1
#define POWER_ACTION_LOGOFF   2
#define POWER_ACTION_STANDBY  3

/* Power Management API */
void power_mgmt_show_shutdown_dialog(HWND parent);
void power_mgmt_execute_action(int action);

#endif /* __WINE_EXPLORER_POWER_MGMT_H */
