#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs INSIDE the installed session 0 machine as root (sent there by installer/tests/session0.sh over the
# serial console). It checks session 0 on the running system and prints exactly one line per check,
# "ok NAME" or "not ok NAME: detail", then SESSION0-GUEST-DONE. It makes no PASS/FAIL decision of its own;
# the host script decides from these lines. It restarts session 0 once (the restart checks), so the host
# runs it in the background and polls for the done line.
#
#   session0-guest.sh [INSTALLED_USER]     (the installed user, default wineuser)
#
# SESSION0_KEEP=1 keeps the second test user (wexs0b) instead of removing it at the end.
set -uo pipefail

INSTALLED_USER=${1:-wineuser}
SECOND_USER=wexs0b
SOCK=/run/wineexplorer-session0/session0
CONF=/etc/wineexplorer/sysrun.conf
MAP=/etc/wineexplorer/accounts.map
USER_REG="/home/$INSTALLED_USER/.wine/user.reg"

# Windows key paths in single-quoted variables so no backslash is lost
HKLM_CV='HKLM\Software\Microsoft\Windows NT\CurrentVersion'
HKCU_TEST='HKCU\Software\WexS0Test'
HKCU_AFTER='HKCU\Software\WexS0After'

ok() { echo "ok $1"; }
notok() {
    local detail
    detail=$(printf '%s' "$2" | tr '\n' ' ' | cut -c1-200)
    echo "not ok $1: $detail"
}

# run a wine command as a user; $2 is the command after "timeout 90". The key paths arrive already quoted,
# so the double quotes survive into the user's shell and protect the space in "Windows NT".
wine_cmd() {
    su - "$1" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 90 $2" 2>&1
}

# session 0's journal for this boot (the server unit)
s0journal() {
    journalctl --no-pager -b 0 -u wex-session0.service 2>/dev/null
}

# a link line for a uid: "session N (server pid P, uid U) answers on its link"
link_line() {
    s0journal | grep -Eq "session [0-9]+ \(server pid [0-9]+, uid $1\) answers on its link"
}

UID_I=$(id -u "$INSTALLED_USER" 2>/dev/null)
if [ -z "$UID_I" ]; then
    notok user_linked "no such user $INSTALLED_USER"
    echo "SESSION0-GUEST-DONE"
    exit 0
fi

# 1. both session 0 units and the display manager are active
if [ "$(systemctl is-active wex-session0.service)" = active ] &&
   [ "$(systemctl is-active wex-session0-services.service)" = active ] &&
   [ "$(systemctl is-active lightdm.service)" = active ]; then
    ok units_active
else
    notok units_active "$(systemctl is-active wex-session0.service wex-session0-services.service lightdm.service | tr '\n' ' ')"
fi

# 2. the session 0 socket exists, is a socket, and is owned by wexsys
if [ -S "$SOCK" ] && [ "$(stat -c %U "$SOCK" 2>/dev/null)" = wexsys ]; then
    ok socket
else
    notok socket "$(ls -la "$SOCK" 2>&1 | head -1)"
fi

# 3. session 0's account runs exactly one wineserver --session0 and one services.exe
n_server=$(pgrep -u wexsys -f 'wineserver --session0' 2>/dev/null | wc -l)
n_svc=$(pgrep -u wexsys -f 'services\.exe' 2>/dev/null | wc -l)
if [ "$n_server" -eq 1 ] && [ "$n_svc" -eq 1 ]; then
    ok one_set
else
    notok one_set "wineserver --session0: $n_server, services.exe: $n_svc"
fi

# 4. the configuration names the socket and the session 0 account
if grep -q '^session0_socket=' "$CONF" 2>/dev/null && grep -q '^session0_user=wexsys$' "$CONF" 2>/dev/null; then
    ok configuration
else
    notok configuration "$(grep -E '^session0_(socket|user)=' "$CONF" 2>/dev/null | tr '\n' ' ')"
fi

# 5. the installed user has a running wineserver, and session 0's journal has a link line for its uid
if ! pgrep -u "$INSTALLED_USER" -x wineserver >/dev/null 2>&1; then
    notok user_linked "no wineserver for $INSTALLED_USER"
elif link_line "$UID_I"; then
    ok user_linked
else
    notok user_linked "no link line for uid $UID_I"
fi

# 6. the installed user reads HKLM through session 0
OUT=$(wine_cmd "$INSTALLED_USER" "wine reg query \"$HKLM_CV\" /v ProductName")
if grep -q 'ProductName' <<<"$OUT"; then
    ok hklm_read
else
    notok hklm_read "$(echo "$OUT" | tail -1)"
fi

# 7. an HKCU write of the installed user reaches user.reg within the save interval
wine_cmd "$INSTALLED_USER" "wine reg add \"$HKCU_TEST\" /v v /d 1 /f" >/dev/null
found=0
for i in $(seq 1 15); do
    if grep -q 'WexS0Test' "$USER_REG" 2>/dev/null; then found=1; break; fi
    sleep 1
done
if [ "$found" -eq 1 ]; then
    ok hkcu_saved
else
    notok hkcu_saved "WexS0Test not in user.reg within 15 s"
fi

# 8. a second, standard user: created if missing, in the accounts map, and reads HKLM
if ! id "$SECOND_USER" >/dev/null 2>&1; then
    useradd -m -s /bin/bash "$SECOND_USER" 2>/dev/null
fi
UID_B=$(id -u "$SECOND_USER" 2>/dev/null)
found=0
for i in $(seq 1 30); do
    if grep -q "^$SECOND_USER " "$MAP" 2>/dev/null; then found=1; break; fi
    sleep 1
done
if [ -z "$UID_B" ]; then
    notok second_user_reads "could not create $SECOND_USER"
elif [ "$found" -ne 1 ]; then
    notok second_user_reads "$SECOND_USER not in accounts.map within 30 s"
else
    OUT=$(wine_cmd "$SECOND_USER" "wine reg query \"$HKLM_CV\" /v ProductName")
    if grep -q 'ProductName' <<<"$OUT"; then
        ok second_user_reads
    else
        notok second_user_reads "$(echo "$OUT" | tail -1)"
    fi
fi

# 9. both users have a wineserver at once, and the second is linked to session 0
if [ -n "$UID_B" ]; then
    # the ping keeps the second user's server alive while both are checked
    wine_cmd "$SECOND_USER" "wine cmd /c ping -n 20 127.0.0.1" >/dev/null 2>&1 &
    sleep 5
    if pgrep -u "$INSTALLED_USER" -x wineserver >/dev/null 2>&1 &&
       pgrep -u "$SECOND_USER" -x wineserver >/dev/null 2>&1; then
        if link_line "$UID_B"; then
            ok two_sessions
        else
            notok two_sessions "no link line for uid $UID_B"
        fi
    else
        notok two_sessions "wineserver missing (installed: $(pgrep -u "$INSTALLED_USER" -x wineserver 2>/dev/null | wc -l), second: $(pgrep -u "$SECOND_USER" -x wineserver 2>/dev/null | wc -l))"
    fi
else
    notok two_sessions "no second user"
fi

# 10. a service started from the user's session runs in session 0, not in the session
wine_cmd "$INSTALLED_USER" "wine net start wuauserv" >/dev/null
sleep 3
n_sys=$(pgrep -u wexsys -f 'wuauserv' 2>/dev/null | wc -l)
n_user=$(pgrep -u "$INSTALLED_USER" -f 'wuauserv' 2>/dev/null | wc -l)
wine_cmd "$INSTALLED_USER" "wine net stop wuauserv" >/dev/null
if [ "$n_sys" -ge 1 ] && [ "$n_user" -eq 0 ]; then
    ok service_forwarded
elif [ -f /usr/local/share/wineexplorer/elevation.enable ] && [ "$n_sys" -eq 0 ] && [ "$n_user" -eq 0 ]; then
    # an image with enforced elevation: the user's token is limited, and session 0 refuses it the start, as
    # Windows refuses a standard user; the service runs nowhere
    ok service_forwarded
    echo "note service_start_by_the_user: refused (elevation is enforced)"
else
    notok service_forwarded "wuauserv as wexsys: $n_sys, as $INSTALLED_USER: $n_user"
fi

# 11. a fresh command of the second user prints no leaked session 0 line on its terminal
ended=0
for i in $(seq 1 40); do
    if ! pgrep -u "$SECOND_USER" -x wineserver >/dev/null 2>&1; then ended=1; break; fi
    sleep 1
done
if [ "$ended" -ne 1 ]; then
    notok quiet_terminal "$SECOND_USER's wineserver still running after 40 s"
else
    # the server the command starts prints at its own exit, a moment after the command returns, so the
    # stderr goes to a file that is read once that server has ended
    ERRFILE=/tmp/s0-quiet.err
    rm -f "$ERRFILE"
    su - "$SECOND_USER" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 90 wine cmd /c ver" 2>"$ERRFILE" >/dev/null
    for i in $(seq 1 10); do
        if ! pgrep -u "$SECOND_USER" -x wineserver >/dev/null 2>&1; then break; fi
        sleep 1
    done
    if grep -qE 'Session 0 socket|impersonating self' "$ERRFILE" 2>/dev/null; then
        notok quiet_terminal "$(grep -E 'Session 0 socket|impersonating self' "$ERRFILE" | head -1)"
    else
        ok quiet_terminal
    fi
    rm -f "$ERRFILE"
fi

# 12. restarting session 0 leaves one set of its programs and no process of the old set
OLD_PIDS=$(pgrep -u wexsys 2>/dev/null | sort)
systemctl restart wex-session0.service
active=0
for i in $(seq 1 30); do
    if [ "$(systemctl is-active wex-session0.service)" = active ] &&
       [ "$(systemctl is-active wex-session0-services.service)" = active ]; then
        active=1; break
    fi
    sleep 1
done
if [ "$active" -ne 1 ]; then
    notok restart_one_set "units not active after 30 s: $(systemctl is-active wex-session0.service wex-session0-services.service | tr '\n' ' ')"
else
    n_server=$(pgrep -u wexsys -f 'wineserver --session0' 2>/dev/null | wc -l)
    n_svc=$(pgrep -u wexsys -f 'services\.exe' 2>/dev/null | wc -l)
    old_alive=0
    for p in $OLD_PIDS; do
        if kill -0 "$p" 2>/dev/null; then old_alive=1; break; fi
    done
    if [ "$n_server" -eq 1 ] && [ "$n_svc" -eq 1 ] && [ "$old_alive" -eq 0 ]; then
        ok restart_one_set
    else
        notok restart_one_set "wineserver: $n_server, services.exe: $n_svc, old pids alive: $old_alive"
    fi
fi

# 13. the installed user's session links again under its own id
relinked=0
for i in $(seq 1 15); do
    if s0journal | grep -Eq "session [0-9]+ \(server pid [0-9]+, uid $UID_I\) linked again"; then
        relinked=1; break
    fi
    sleep 1
done
if [ "$relinked" -eq 1 ]; then
    ok restart_relinked
else
    notok restart_relinked "no 'linked again' line for uid $UID_I"
fi

# 14. the installed user reads HKLM again after the restart
OUT=$(wine_cmd "$INSTALLED_USER" "wine reg query \"$HKLM_CV\" /v ProductName")
if grep -q 'ProductName' <<<"$OUT"; then
    ok hklm_after_restart
else
    notok hklm_after_restart "$(echo "$OUT" | tail -1)"
fi

# 15. a new HKCU value of the installed user reaches user.reg within the save interval
wine_cmd "$INSTALLED_USER" "wine reg add \"$HKCU_AFTER\" /v v /d 1 /f" >/dev/null
found=0
for i in $(seq 1 15); do
    if grep -q 'WexS0After' "$USER_REG" 2>/dev/null; then found=1; break; fi
    sleep 1
done
if [ "$found" -eq 1 ]; then
    ok hkcu_after_restart
else
    notok hkcu_after_restart "WexS0After not in user.reg within 15 s"
fi

# 16. no unit is failed
FAILED=$(systemctl --failed --no-legend 2>/dev/null)
if [ -z "$FAILED" ]; then
    ok no_failed_units
else
    notok no_failed_units "$(echo "$FAILED" | head -3 | tr '\n' ' ')"
fi

# remove the second test user unless SESSION0_KEEP=1
if [ "${SESSION0_KEEP:-0}" != 1 ]; then
    pkill -u "$SECOND_USER" 2>/dev/null || true
    sleep 1
    userdel -r "$SECOND_USER" 2>/dev/null || true
fi

echo "SESSION0-GUEST-DONE"
