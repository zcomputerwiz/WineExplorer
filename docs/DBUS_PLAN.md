# Plan: talk to Linux through D-Bus, not command lines

Status (2026-10-08): **implemented in `wexhost`:** timedate1 + polkit agent, login1 (power actions, with a
Windows-style end-of-session query first), UPower (battery/AC), hostname1 (computer name/description), AccountsService
(display name), NetworkManager (status, Wi-Fi scan/connect, disconnect/enable). `nmcli`/`hostcmd.c` are gone.
Still to do: PipeWire native volume events (volume works through winepulse), StatusNotifierItem, notifications,
D-Bus signals instead of polling (nettray/powertray/volumetray still poll every 0.5-1.5 s), native Wayland displays.
Display Properties now uses native XRandR through `wexhost` on X11 / supported Xwayland sessions; see DISPLAY.md.
Original intent follows.  Today's backends (`hostcmd.c` running `nmcli`,
`wpctl`/`pactl`/`amixer`, `systemctl`, `sudo date`, `timedatectl`) are a stopgap. They are shell-free (argv only), but
they parse text, poll, and force our own password handling. Other Linux desktops use D-Bus services and polkit.

## Target interfaces
| Area | Service / interface | Used by |
|---|---|---|
| Date, time, zone, NTP | `org.freedesktop.timedate1` (`SetTime`, `SetTimezone`, `SetNTP`, `ListTimezones`, properties `Timezone`, `NTP`, `NTPSynchronized`) | timedate.cpl, tray clock |
| Elevation | polkit: services check `org.freedesktop.PolicyKit1`; **we register an authentication agent** (`org.freedesktop.PolicyKit1.AuthenticationAgent`, `RegisterAuthenticationAgent`) that shows the UAC dialog | all applets |
| Network | `org.freedesktop.NetworkManager` (devices, `Device.Wireless` `GetAllAccessPoints`/`RequestScan`, `Settings`, `ActivateConnection`, `AddAndActivateConnection`), signals for state/strength | nettray, wlan dialogs, ncpa.cpl |
| Power actions | `org.freedesktop.login1.Manager` (`PowerOff`, `Reboot`, `Suspend`, `CanPowerOff` ...), inhibitors | shutdown dialog |
| Battery / AC | `org.freedesktop.UPower` (+ `DisplayDevice`) | powertray, powercfg.cpl |
| Volume | PipeWire native API or PulseAudio protocol (libpulse); change events via subscription | volumetray, sndvol32, mmsys.cpl |
| Computer name | `org.freedesktop.hostname1` (`SetStaticHostname`, `SetPrettyHostname`) | sysdm.cpl Computer Name, `setup.exe` |
| Locale / keyboard | `org.freedesktop.locale1` (`SetLocale`, `SetX11Keyboard`): `WexLocaleSet` | `setup.exe` Regional page |
| Accounts | `org.freedesktop.Accounts` (`CreateUser`, `SetPassword` with an in-process SHA-512 crypt hash, `SetXSession`, `SetAutomaticLogin`): `WexUserCreate`, `WexRootPasswordSet`, `WexUserSetSession` | `setup.exe`, `oobe.exe` |
| User name / picture | `org.freedesktop.Accounts` (AccountsService) | Start menu user tile |
| Tray icons from Linux apps | `org.kde.StatusNotifierWatcher` / StatusNotifierItem | systray |
| Notifications | `org.freedesktop.Notifications` | balloon tips |
| Displays | X11: XRandR (stock Wine already); Wayland/gamescope: output-management protocols | desk.cpl |

## Architecture
PE code cannot link libdbus. Wine precedent: `dlls/mountmgr.sys/dbus.c` and `dlls/winebth.sys` load libdbus at
run time on the Unix side. Plan:
1. A Unix-side library in the explorer stack (loaded through Wine's unixlib mechanism) with a small typed API
   per service (`nm_*`, `timedate_*`, `login1_*`, `upower_*`, `accounts_*`, `polkit_agent_*`). It `dlopen`s
   libdbus-1 (or GIO) so a missing service or library just reports "unavailable" - the UI hides that feature.
2. A single event thread on the Unix side delivers D-Bus signals to the PE side as window messages (replaces the
   500 ms polling timers).
3. The polkit agent runs in explorer: polkit calls us over D-Bus, we show the UAC dialog (`timedate.cpl/uac.c`
   moves into explorer or a shared dll), return the result. Applets then simply call the service
   (e.g. `timedate1.SetTime`) and polkit does the rest - no `sudo`, no password in our process tree beyond the
   agent dialog.
4. Keep `hostcmd.c` only as an explicit fallback where a service is absent.

Order of work: (1) Unix lib skeleton + timedate1 + polkit agent (finishes the Date/Time applet properly),
(2) login1 + UPower (shutdown, battery), (3) NetworkManager, (4) PipeWire/Pulse volume, (5) hostname1/Accounts,
(6) StatusNotifierItem.

## Reference sources (cloned under `references/linux-de/`, gitignored)
| Repo | Use it for |
|---|---|
| `gnome-control-center` | timedate1 (`panels/datetime/`), NetworkManager (`panels/network/`), power (`panels/power/`), polkit-permission handling, hostname1/Accounts panels |
| `policykit-gnome` | smallest complete polkit authentication agent (listener + dialog): model for our UAC agent |
| `lxqt-policykit` | second, independent agent implementation (Qt) |
| `cinnamon-control-center`, `cinnamon-settings-daemon` | Windows-like panel layout and the D-Bus calls behind each panel (power, media keys, color) |
| `lxqt-config` | simpler settings panels (time, keyboard, mouse) |

Other useful, not cloned: `systemd` D-Bus API docs (timedate1/login1/hostname1 introspection XML),
NetworkManager `libnm` docs/examples, `libpolkit-agent` docs, `upower` introspection, PipeWire docs.
Clone more with `git clone --depth 1 <url> references/linux-de/<name>` when needed.
