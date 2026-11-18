/*
 * gpedit: the policies the editor offers
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* See policies.h. The survey behind the status column: docs/GROUP_POLICY.md, "Local policy". */

#include <stddef.h>

#include "policies.h"

#define POLICIES_ROOT "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\"
#define EXPLORER      POLICIES_ROOT "Explorer"
#define SYSTEM        POLICIES_ROOT "System"
#define COMDLG32      POLICIES_ROOT "Comdlg32"

#define FLAG( id, scope, status, category, name, explain, key, value ) \
    { id, scope, POLICY_FLAG, status, category, name, explain, key, value, 1, 0, NULL, NULL }
#define NUMBER( id, scope, status, category, name, explain, key, value ) \
    { id, scope, POLICY_NUMBER, status, category, name, explain, key, value, 1, 0, NULL, NULL }
#define GRAYED( id, scope, category, name, explain, key, value, reason ) \
    { id, scope, POLICY_FLAG, POLICY_NOT_APPLICABLE, category, name, explain, key, value, 1, 0, reason, NULL }

#define EXPLORER_CAT  "Windows Components\\Windows Explorer"
#define START_CAT     "Start Menu and Taskbar"
#define DESKTOP_CAT   "Desktop"
#define PANEL_CAT     "Control Panel"
#define SYSTEM_CAT    "System"
#define SECURITY_CAT  "Security Options"
#define GROUPPOLICY_CAT "System\\Group Policy"
#define LOGON_CAT       "System\\Logon"
#define WINDOWS_SYSTEM  "Software\\Policies\\Microsoft\\Windows\\System"
#define WINLOGON_POLICY "Software\\Policies\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon"

const struct policy policies[] =
{
    /* ---- the ones a component of this tree acts on ---- */
    NUMBER( "user:NoDrives", POLICY_USER, POLICY_APPLIES, EXPLORER_CAT, "Hide these drives in My Computer",
            "Hides the chosen drive letters in My Computer. A bit mask: drive A is bit 0, B bit 1, and so on. The "
            "drives stay reachable by typing their path.", EXPLORER, "NoDrives" ),
    NUMBER( "computer:NoDrives", POLICY_COMPUTER, POLICY_APPLIES, EXPLORER_CAT, "Hide these drives in My Computer",
            "Hides the chosen drive letters in My Computer for every user. A bit mask: drive A is bit 0, B bit 1, and "
            "so on. Together with the user setting, the drives hidden by either are hidden.", EXPLORER, "NoDrives" ),
    FLAG( "user:NoTrayItemsDisplay", POLICY_USER, POLICY_APPLIES, START_CAT, "Hide the notification area",
          "Removes the notification area (the system tray) from the taskbar and keeps programs from placing icons "
          "there.", EXPLORER, "NoTrayItemsDisplay" ),
    FLAG( "computer:NoTrayItemsDisplay", POLICY_COMPUTER, POLICY_APPLIES, START_CAT, "Hide the notification area",
          "Removes the notification area (the system tray) from the taskbar for every user.", EXPLORER,
          "NoTrayItemsDisplay" ),
    FLAG( "user:NoRecentDocsHistory", POLICY_USER, POLICY_APPLIES, START_CAT, "Do not keep a history of recently opened documents",
          "Programs that open documents through the shell no longer add them to the Recent Documents list.", EXPLORER,
          "NoRecentDocsHistory" ),
    FLAG( "computer:NoRecentDocsHistory", POLICY_COMPUTER, POLICY_APPLIES, START_CAT,
          "Do not keep a history of recently opened documents",
          "No document opened by any user is added to the Recent Documents list.", EXPLORER, "NoRecentDocsHistory" ),
    FLAG( "user:NoPlacesBar", POLICY_USER, POLICY_APPLIES, EXPLORER_CAT, "Hide the Places Bar in Open and Save dialogs",
          "Removes the bar of shortcuts at the left of the common Open and Save dialogs.", COMDLG32, "NoPlacesBar" ),
    { "computer:ConsentPromptBehaviorAdmin", POLICY_COMPUTER, POLICY_NUMBER, POLICY_APPLIES, SECURITY_CAT,
      "Behavior of the elevation prompt for administrators",
      "How an administrator is asked before a program runs with full rights. 2: always ask for consent; 5: ask "
      "for consent for changes to the computer (the default). Other values are not supported here.", SYSTEM,
      "ConsentPromptBehaviorAdmin", 5, 0, NULL, "2,5" },
    FLAG( "computer:PromptOnSecureDesktop", POLICY_COMPUTER, POLICY_APPLIES, SECURITY_CAT,
          "Show the elevation prompt on the secure desktop",
          "Enabled (the default): the prompt appears on the secure desktop, where other programs cannot reach it. "
          "Disabled: it appears on the user's own desktop.", SYSTEM, "PromptOnSecureDesktop" ),

    /* ---- shell restrictions, carried out by the shell programs (wexshell restrictions.c, startmenu_view.c, the folder
     window in wexbrowse, msgina at logoff, shell32's Open With dialog) ---- */
    FLAG( "user:NoRun", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove Run from the Start menu",
          "Removes the Run command from the Start menu.", EXPLORER, "NoRun" ),
    FLAG( "user:NoClose", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove Shut Down from the Start menu",
          "Removes the commands that shut down or restart the computer from the Start menu.", EXPLORER, "NoClose" ),
    FLAG( "user:NoLogoff", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove Log Off from the Start menu",
          "Removes the Log Off command from the Start menu.", EXPLORER, "NoLogoff" ),
    FLAG( "user:NoControlPanel", POLICY_USER, POLICY_APPLIES, PANEL_CAT, "Hide Control Panel",
          "Control Panel can no longer be opened from the shell.", EXPLORER, "NoControlPanel" ),
    FLAG( "user:NoSetFolders", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove Control Panel and Printers from the Settings menu",
          "Removes these folders from the Settings menu.", EXPLORER, "NoSetFolders" ),
    FLAG( "user:NoSetTaskbar", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove Taskbar and Start Menu from the Settings menu",
          "Users can no longer open the properties of the taskbar and the Start menu.", EXPLORER, "NoSetTaskbar" ),
    FLAG( "user:NoChangeStartMenu", POLICY_USER, POLICY_APPLIES, START_CAT, "Prevent changes to the Start menu",
          "Users cannot drag items on the Start menu or change its contents.", EXPLORER, "NoChangeStartMenu" ),
    FLAG( "user:NoFind", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove Search from the Start menu",
          "Removes the Search command from the Start menu and the shell's Search action.", EXPLORER, "NoFind" ),
    FLAG( "user:NoFolderOptions", POLICY_USER, POLICY_APPLIES, EXPLORER_CAT, "Remove Folder Options",
          "Removes the Folder Options command from the Tools menu of Windows Explorer.", EXPLORER, "NoFolderOptions" ),
    FLAG( "user:NoViewContextMenu", POLICY_USER, POLICY_APPLIES, EXPLORER_CAT, "Remove the shortcut menu of folders",
          "Right-clicking in a folder's background no longer shows a menu.", EXPLORER, "NoViewContextMenu" ),
    FLAG( "user:NoFileAssociate", POLICY_USER, POLICY_APPLIES, EXPLORER_CAT, "Do not allow changing file associations",
          "Removes the commands that change which program opens a file type.", EXPLORER, "NoFileAssociate" ),
    FLAG( "user:NoNetHood", POLICY_USER, POLICY_APPLIES, DESKTOP_CAT, "Hide My Network Places",
          "Removes My Network Places from the desktop and from Windows Explorer.", EXPLORER, "NoNetHood" ),
    FLAG( "user:NoRecentDocsMenu", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove Recent Documents from the Start menu",
          "Removes the Recent Documents menu from the Start menu.", EXPLORER, "NoRecentDocsMenu" ),
    FLAG( "user:ClearRecentDocsOnExit", POLICY_USER, POLICY_APPLIES, START_CAT, "Clear the history of recent documents at logoff",
          "The Recent Documents list is emptied when the user logs off.", EXPLORER, "ClearRecentDocsOnExit" ),
    FLAG( "user:NoSMHelp", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove Help from the Start menu",
          "Removes the Help and Support command from the Start menu.", EXPLORER, "NoSMHelp" ),
    FLAG( "user:NoSMMyDocs", POLICY_USER, POLICY_APPLIES, START_CAT, "Remove My Documents from the Start menu",
          "Removes the My Documents shortcut from the Start menu.", EXPLORER, "NoSMMyDocs" ),
    FLAG( "user:NoWinKeys", POLICY_USER, POLICY_APPLIES, EXPLORER_CAT, "Turn off the Windows key shortcuts",
          "The keyboard shortcuts that use the Windows logo key do nothing.", EXPLORER, "NoWinKeys" ),

    /* ---- when policy is refreshed (wex-gpupdate.sh: --background, --logon; docs/GROUP_POLICY.md, "The refresh interval" and
     "Policy before the desktop"). Windows keeps the interval and its random part in one setting; here they are two. ---- */
    NUMBER( "computer:GroupPolicyRefreshTime", POLICY_COMPUTER, POLICY_APPLIES, GROUPPOLICY_CAT,
            "Set Group Policy refresh interval for computers: minutes between refreshes",
            "How often policy is refreshed in the background, in minutes (0 to 44640; 90 when not configured). The timer of "
            "this system looks every five minutes, so a time below five minutes means every five. The refresh at start, "
            "at logon and the requests of gpupdate are not limited by it.", WINDOWS_SYSTEM, "GroupPolicyRefreshTime" ),
    NUMBER( "computer:GroupPolicyRefreshTimeOffset", POLICY_COMPUTER, POLICY_APPLIES, GROUPPOLICY_CAT,
            "Set Group Policy refresh interval for computers: random time added, in minutes",
            "A random time from zero to this many minutes (0 to 1440; 30 when not configured) is added to the interval, so "
            "that computers do not all ask the domain at the same moment.", WINDOWS_SYSTEM, "GroupPolicyRefreshTimeOffset" ),
    FLAG( "computer:DisableBkGndGroupPolicy", POLICY_COMPUTER, POLICY_APPLIES, GROUPPOLICY_CAT,
          "Turn off background refresh of Group Policy",
          "Enabled: policy is refreshed at start, at logon and when gpupdate asks, and not on the timer; the refresh "
          "interval is then ignored.", SYSTEM, "DisableBkGndGroupPolicy" ),
    FLAG( "computer:SyncForegroundPolicy", POLICY_COMPUTER, POLICY_APPLIES, LOGON_CAT,
          "Always wait for the network at computer startup and logon",
          "Enabled: a user's policy is applied before the desktop at every logon. Not configured: only at a user's first "
          "logon on this computer, as on Windows; at later logons the session refreshes policy once its shell is up. "
          "(The wait is limited to a minute.)", WINLOGON_POLICY, "SyncForegroundPolicy" ),

    /* ---- nothing here reads them: shown grayed out ---- */
    GRAYED( "user:NoDesktop", POLICY_USER, DESKTOP_CAT, "Hide all icons on the desktop",
            "Removes the icons and shortcuts from the desktop.", EXPLORER, "NoDesktop",
            "the shell of this system draws its own desktop and does not read this value" ),
    GRAYED( "user:DisableTaskMgr", POLICY_USER, SYSTEM_CAT, "Remove Task Manager",
            "Users cannot start Task Manager.", SYSTEM, "DisableTaskMgr", "no Task Manager here reads this value" ),
    GRAYED( "user:DisableRegistryTools", POLICY_USER, SYSTEM_CAT, "Prevent access to registry editing tools",
            "Users cannot run the Registry Editor.", SYSTEM, "DisableRegistryTools",
            "the registry editor does not read this value" ),
    GRAYED( "user:DisableLockWorkstation", POLICY_USER, SYSTEM_CAT, "Remove Lock Computer",
            "Removes the Lock Computer command.", SYSTEM, "DisableLockWorkstation", "nothing reads this value" ),
    GRAYED( "user:NoDispCPL", POLICY_USER, PANEL_CAT, "Disable Display in Control Panel",
            "Display Properties cannot be opened.", SYSTEM, "NoDispCPL", "Display Properties does not read this value" ),
    GRAYED( "computer:DontDisplayLastUserName", POLICY_COMPUTER, SECURITY_CAT, "Do not display the last user name at logon",
            "The logon screen does not offer the name of the last user.", SYSTEM, "DontDisplayLastUserName",
            "the logon screen does not read this value" ),
    GRAYED( "computer:GpNetworkStartTimeoutPolicyValue", POLICY_COMPUTER, GROUPPOLICY_CAT,
            "Specify startup policy processing wait time",
            "How long computer start waits for the network before it processes policy.", WINDOWS_SYSTEM,
            "GpNetworkStartTimeoutPolicyValue",
            "there is no policy processing at computer start to wait in: the first refresh runs two minutes after start" ),
    GRAYED( "computer:SlowlinkDefaultToAsync", POLICY_COMPUTER, GROUPPOLICY_CAT,
            "Change Group Policy processing to run asynchronously when a slow network connection is detected",
            "On a slow connection, policy does not hold up the logon.", WINDOWS_SYSTEM, "SlowlinkDefaultToAsync",
            "this system does not detect slow links: Samba fetches the policy, and only a first logon waits for it" ),
    GRAYED( "computer:GroupPolicyMinTransferRate", POLICY_COMPUTER, GROUPPOLICY_CAT,
            "Configure Group Policy slow link detection",
            "The speed below which a connection to the domain counts as slow.", WINDOWS_SYSTEM, "GroupPolicyMinTransferRate",
            "this system does not measure the connection to the domain" ),
    GRAYED( "computer:ProcessTSUserLogonAsync", POLICY_COMPUTER, GROUPPOLICY_CAT,
            "Allow asynchronous user Group Policy processing when logging on through Remote Desktop Services",
            "User policy does not hold up a Remote Desktop logon.", WINDOWS_SYSTEM, "ProcessTSUserLogonAsync",
            "this system has no Remote Desktop Services sessions" ),
};

const unsigned int policy_count = sizeof(policies) / sizeof(policies[0]);

/* equal without regard to case (ASCII: registry names of policies are) */
static int same_name( const char *a, const char *b )
{
    for (; *a && *b; a++, b++)
    {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return *a == *b;
}

const struct policy *policy_find( enum policy_scope scope, const char *key, const char *value )
{
    unsigned int i;

    for (i = 0; i < policy_count; i++)
        if (policies[i].scope == scope && same_name( policies[i].key, key ) && same_name( policies[i].value, value ))
            return &policies[i];
    return NULL;
}
