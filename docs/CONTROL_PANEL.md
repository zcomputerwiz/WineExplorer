# Control Panel

Goal: the Control Panel of Windows XP, with applets that work. An applet that makes no sense in Wine on Linux is left
out; an option inside an applet that cannot work is shown grayed out (disabled), as Windows does for options a policy
or the hardware rules out. References: `references/screenshots/control_panel/` (XP VM), ReactOS `dll/cpl/*` (may be

## The folder
Windows XP opens Control Panel (`control.exe`, Start menu, My Computer's "Control Panel" link) as an Explorer folder
window, `::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\::{21EC2020-3AEA-1069-A2DD-08002B30309D}`, in Category View
("Pick a category") with a "Switch to Classic View" task. Stock Wine's `control.exe` opens its own applet window.

1. `control.exe` with no applet opens the Control Panel folder in Explorer (classic view first: shell32's folder).
2. Category View: the nine categories and their task pages, drawn natively in our Explorer window like the task pane.

## Applets (XP Professional, classic view)
| XP item | Here | Plan |
|---|---|---|
| Accessibility Options (`access.cpl`) | missing | port ReactOS `access`; what Wine does not implement (StickyKeys, FilterKeys, ToggleKeys, MouseKeys, SoundSentry, SerialKeys) grayed |
| Add Hardware (`hdwwiz.cpl`) | - | left out: Linux finds hardware itself |
| Add or Remove Programs (`appwiz.cpl`) | Wine's ("Add/Remove Programs") | XP name; XP look later (left band: Change or Remove / Add New / Windows Components / Program Access and Defaults) |
| Administrative Tools | missing | folder of the tools that exist (Data Sources (ODBC)); later |
| Automatic Updates (`wuaucpl.cpl`) | - | left out: the distribution updates the system |
| Date and Time (`timedate.cpl`) | ours | done |
| Display (`desk.cpl`) | ours | done |
| Folder Options | missing | port from ReactOS shell32 (`folder_options`, General / View / File Types); Offline Files tab grayed |
| Fonts | missing | the Fonts folder (`C:\windows\Fonts`) |
| Game Controllers (`joy.cpl`) | Wine's | keep |
| Internet Options (`inetcpl.cpl`) | Wine's ("Internet Settings") | XP name |
| Keyboard, Mouse (`main.cpl`) | ours | done |
| Network Connections (`ncpa.cpl`) | ours | done |
| Network Setup Wizard | - | left out: NetworkManager |
| Phone and Modem Options (`telephon.cpl`) | - | left out: no TAPI devices |
| Power Options (`powercfg.cpl`) | ours | done |
| Printers and Faxes | Wine's Printers folder | check it lists the CUPS printers |
| Regional and Language Options (`intl.cpl`) | ReactOS port | formats and location use Wine user locale overrides; non-Unicode system locale, code-page installation, and input-method controls are disabled; Languages details remain future work |
| Scanners and Cameras | - | left out for now (SANE later) |
| Scheduled Tasks | - | later (Wine's task scheduler) |
| Security Center, Windows Firewall | - | left out: not Windows' firewall or antivirus |
| Sounds and Audio Devices (`mmsys.cpl`) | ours | done |
| Speech (`sapi.cpl`) | - | later, if Wine's SAPI speaks |
| System (`sysdm.cpl`) | ours | done |
| Taskbar and Start Menu | in Explorer | Control Panel item opening the taskbar properties |
| User Accounts (`nusrmgr.cpl`) | missing | XP's task pages: name and picture work, passwords through the Linux account (polkit) or grayed |
| Wireless Network Setup Wizard | - | left out: Network Connections handles wireless |

## Validation (2026-10-09)

The applet, kernelbase locale override, Control Panel launcher, Explorer, and wineserver build successfully.
The applet icon matches the ReactOS resource and is cataloged in the asset manifest.
