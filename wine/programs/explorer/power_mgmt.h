/*
 * WineExplorer Desktop Environment - Power Management & Display Manager Integration
 *
 * Implements authentic "Shut Down Windows" dialog and Linux power management
 * (systemd-logind D-Bus, systemctl poweroff/reboot, session termination).
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
