#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root-side controller for the repeatable, lab-only guest system-role workflow.
set -Eeuo pipefail

M=${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}
R=/opt/wex-rt2/build/wow64
OUT=/tmp/gsr-out
die() { echo "FAIL: $*" >&2; exit 1; }
own() {
    local path=$1
    [[ -e $path ]] || die "missing $path"
    path=$(realpath -e -- "$path") || die "cannot resolve $1"
    while [[ $path != / ]]; do
        [[ $(stat -c %u -- "$path") == 0 ]] || die "not root-owned: $path"
        path=$(dirname -- "$path")
    done
}
profile() { install -d -m 2775 -o "$1" -g wexsys "$M/drive_c/users/$1/AppData/Local/Temp" "$M/drive_c/users/$1/AppData/Roaming"; }

sysrole_up() {
    [[ -x /tmp/gsr/personalize-prefix.sh ]] || die "personalization helper was not staged"
    bash /tmp/gsr/guest-rt2.sh
    bash /tmp/gsr/guest-sysrole-provision.sh
    bash /tmp/gsr/guest-sysrole-user.sh
    for u in wextest wexadm; do
        python3 /tmp/gsr/guest-personalize.py "$u"
        profile "$u"
    done
    chmod o+x /home/debian
    touch /var/lib/wineexplorer/.wex-lab-owned
}
sysrole_status() {
    [[ -f /var/lib/wineexplorer/.wex-lab-owned ]] || die "foreign or unmarked machine root"
    getent group wexsys >/dev/null || die "wexsys group missing"
    own /usr/local/libexec/wineexplorer/wex-sysrun
    [[ $(stat -c '%a %U:%G' /usr/local/libexec/wineexplorer/wex-sysrun) == '2755 root:wexsys' ]] || die "launcher mode/group wrong"
    own /etc/wineexplorer/sysrun.conf
    while IFS='=' read -r key value; do
        case $key in loader|ntdll|wineserver|machine_root) own "$value";; esac
    done < /etc/wineexplorer/sysrun.conf
    grep -Fq '"Helper"="C:\\windows\\system32\\consent.exe"' "$M/system.reg" || die "Helper value missing"
    for u in wextest wexadm; do
        id "$u" >/dev/null || die "user missing: $u"
        [[ -d "$M/drive_c/users/$u/AppData/Local/Temp" ]] || die "profile missing: $u"
    done
    df -Pk / | awk 'NR==2 { if ($4 < 8388608) exit 1 }' || die "less than 8 GB free"
    bash /tmp/gsr/guest-sysrole-ids.sh
}
sysrole_reset() {
    [[ ! -e $M || -f /var/lib/wineexplorer/.wex-lab-owned ]] || die "foreign or unmarked machine root"
    pkill -x wineserver 2>/dev/null || true
    userdel -r wextest 2>/dev/null || true
    userdel -r wexadm 2>/dev/null || true
    groupdel wexsys 2>/dev/null || true
    rm -f /etc/wineexplorer/sysrun.conf /usr/local/libexec/wineexplorer/wex-sysrun
    rmdir /usr/local/libexec/wineexplorer /etc/wineexplorer 2>/dev/null || true
    rm -rf /opt/wex-rt2 /var/lib/wineexplorer /etc/skel/.wine
}
broker() {
    local action=${1:-} flag=${2:-}
    [[ $action == install || $action == remove ]] || { action=$flag; flag=${1:-}; }
    [[ $flag == --lab ]] || die 'LAB ONLY: broker install/remove requires explicit --lab'
    case $action in
        install) echo 'WARNING: installing the compile-time test elevation broker (LAB ONLY)'; bash /tmp/gsr/guest-broker-install.sh;;
        remove) rm -f "$M/drive_c/windows/system32/wexelevate_test.exe" "$M/drive_c/windows/system32/wexelevate_stub.exe" /tmp/wexelev/approve.all; [[ ! -f $M/system.reg.bak-broker ]] || cp -p "$M/system.reg.bak-broker" "$M/system.reg";;
        *) die 'usage: guest broker --lab install|remove';;
    esac
}
debug_env() { python3 -I /tmp/gsr/guest-env-debug.py "${1:-off}"; }
elev() { (( $# )) || die 'usage: guest elev PROGRAM [ARGS...]'; bash /tmp/gsr/guest-elev-run.sh "$@"; }
install_app() {
    local file=$1 label=${2:-install}; shift 2
    [[ -f /tmp/gsr-out/$file ]] || die "installer was not staged: $file"
    df -Pk / | awk 'NR==2 { if ($4 < 8388608) exit 1 }' || die "less than 8 GB free; refusing installer"
    rm -f "$M/drive_c/windows/Installer/"*.msi
    case ${file##*.} in
        [mM][sS][iI]) bash /tmp/gsr/guest-elev-run.sh msiexec.exe /i "Z:\\tmp\\gsr-out\\$file" /qn /norestart /l*v "Z:\\tmp\\gsr-out\\$label-elev.log";;
        *) bash /tmp/gsr/guest-elev-run.sh "Z:\\tmp\\gsr-out\\$file" >"$OUT/$label-elev.log" 2>&1;;
    esac
}
case ${1:-} in
    sysrole) case ${2:-} in up) sysrole_up;; status) sysrole_status;; reset) sysrole_reset;; *) die 'usage: guest sysrole up|status|reset';; esac;;
    broker) shift; broker "$@";;
    elev) shift 1; elev "$@";;
    install) shift; install_app "$@";;
    debug) shift; debug_env "${1:-off}";;
    *) die 'unknown guest operation';;
esac
