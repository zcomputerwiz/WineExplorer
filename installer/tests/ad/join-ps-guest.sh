#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Runs INSIDE an installed session 0 machine as root (sent there by machine.sh with WEX_GUEST=join-ps-guest.sh and
# WEX_LAB_EXTRA=the folder with the dlls and the module). The domain join from PowerShell 7 (docs/POWERSHELL.md): the
# official release is downloaded in the guest, the WexComputer module and the dlls of the build under test come in
# /root/extra; join-ps.ps1 does the work. One line per check, "ok NAME" or "not ok NAME: detail", then AD-GUEST-DONE.
#
#   join-ps-guest.sh [INSTALLED_USER]     (default wineuser)
#
# /root/adminpw holds the lab domain's Administrator password. The elevated commands run the way the policy editor's
# do (consent dialog, answered by the host: machine.sh, host step "consent").
set -uo pipefail

INSTALLED_USER=${1:-wineuser}
ADDR=10.71.0.30
DC=10.71.0.10
LAB_MAC=52:54:00:71:00:30
DOM=ad.wexlab.example
export DEBIAN_FRONTEND=noninteractive
PROBE=/usr/local/share/wex-lab-probe/tierprobe64.exe
PW=$(cat /root/adminpw)

ok() { echo "ok $1"; }
notok() { echo "not ok $1: $(printf '%s' "$2" | tr '\n' ' ' | cut -c1-300)"; }
finish() { ip route del blackhole "$DC/32" 2>/dev/null; rm -f /root/adminpw /tmp/join-pw.txt; echo "AD-GUEST-DONE"; exit 0; }
wine_cmd() { su - "$1" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout ${3:-120} $2" 2>&1; }
host_step()
{
    local name=$1 i
    rm -f "/root/host-done-$name"
    echo "AD-GUEST-HOST-STEP $*"
    for i in $(seq 1 300); do [ -e "/root/host-done-$name" ] && break; sleep 5; done
    HOST_ANSWER=$(cat "/root/host-done-$name" 2>/dev/null)
    [ "$HOST_ANSWER" = ok ]
}
s0_reg() { (cd / && runuser -u wexsys -- env -i WINEPREFIX=/var/lib/wineexplorer/machine HOME=/var/lib/wineexplorer/machine \
    PATH=/usr/bin:/bin WINEDEBUG=-all /usr/local/bin/wine "$@" </dev/null 2>/dev/null); }

# the lab network and the engine's packages (the join itself is netdom's)
LAB_IF=$(ip -o link | grep -i "$LAB_MAC" | awk -F': ' '{ print $2 }')
[ -n "$LAB_IF" ] || { notok lab_network "no card $LAB_MAC"; finish; }
lab_up()
{
    command -v nmcli >/dev/null 2>&1 && nmcli device set "$LAB_IF" managed no >/dev/null 2>&1
    ip link set "$LAB_IF" up
    ip addr show dev "$LAB_IF" | grep -q "$ADDR/" || ip addr add "$ADDR/24" dev "$LAB_IF" 2>/dev/null
}
lab_up
rm -f /etc/resolv.conf
printf 'search %s\nnameserver %s\n' "$DOM" "$DC" > /etc/resolv.conf
for i in $(seq 1 30); do getent hosts "dc1.$DOM" >/dev/null 2>&1 && break; sleep 2; done
getent hosts "dc1.$DOM" >/dev/null 2>&1 && ok lab_network || { notok lab_network "the controller's name does not resolve"; finish; }
apt-get update > /root/ad-apt.log 2>&1
apt-get install -y samba winbind libnss-winbind libpam-winbind krb5-user dnsutils ldap-utils libsasl2-modules-gssapi-mit >> /root/ad-apt.log 2>&1
command -v net >/dev/null 2>&1 && ok packages_installed || { notok packages_installed "$(tail -3 /root/ad-apt.log)"; finish; }
# the installation may put the old name server back
lab_up
rm -f /etc/resolv.conf
printf 'search %s\nnameserver %s\n' "$DOM" "$DC" > /etc/resolv.conf
grep -q "dc1.$DOM" /etc/hosts || echo "$DC dc1.$DOM dc1" >> /etc/hosts

# 1. PowerShell 7 (the release from GitHub, checked against its published digest), the module and the dlls under test
PS_URL=https://github.com/PowerShell/PowerShell/releases/download/v7.7.0/PowerShell-7.7.0-win-x64.zip
PS_SHA=7c74e03b20a75b59715847439d046a6214b1863ee79b187872980f139bb4a05f
apt-get install -y unzip curl >> /root/ad-apt.log 2>&1
lab_up
rm -f /etc/resolv.conf
printf 'search %s\nnameserver %s\n' "$DOM" "$DC" > /etc/resolv.conf
# (the download goes over the machine's own network: the lab network has no route out)
if ! curl -fsSL --retry 3 -o /root/pwsh.zip "$PS_URL" 2>/root/pwsh-dl.err; then
    # the name server of the lab does not resolve github.com: use a public one for the download
    printf 'nameserver 1.1.1.1\n' > /etc/resolv.conf
    curl -fsSL --retry 3 -o /root/pwsh.zip "$PS_URL" 2>>/root/pwsh-dl.err
    printf 'search %s\nnameserver %s\n' "$DOM" "$DC" > /etc/resolv.conf
fi
if [ "$(sha256sum /root/pwsh.zip 2>/dev/null | cut -d' ' -f1)" = "$PS_SHA" ]; then ok pwsh_download
else notok pwsh_download "$(cat /root/pwsh-dl.err 2>&1)"; finish; fi
rm -rf /opt/pwsh7; mkdir -p /opt/pwsh7; unzip -q /root/pwsh.zip -d /opt/pwsh7 && chmod -R a+rX /opt/pwsh7
[ -f /opt/pwsh7/pwsh.exe ] && ok pwsh_unpacked || { notok pwsh_unpacked "no pwsh.exe"; finish; }
mkdir -p /opt/pwsh7/Modules; cp -a /root/extra/WexComputer /opt/pwsh7/Modules/WexComputer; chmod -R a+rX /opt/pwsh7/Modules
WINELIB=$(dirname "$(find /usr/local /opt -name wbemprox.dll -path '*x86_64-windows*' 2>/dev/null | head -1)")
WINELIB=${WINELIB%/x86_64-windows}
if [ -d "$WINELIB/x86_64-windows" ]; then
    for a in x86_64-windows i386-windows; do
        for d in mi wbemprox; do
            [ -f "$WINELIB/$a/$d.dll" ] && cp "$WINELIB/$a/$d.dll" "$WINELIB/$a/$d.dll.orig"
            install -m 0644 "/root/extra/dlls/$a/$d.dll" "$WINELIB/$a/$d.dll"
        done
    done
    # the prefixes of an installed machine hold fake dlls for what Wine had when they were made: mi.dll is new, so it
    # goes into each prefix's system folders too (a new image's prefix would hold the fake dll)
    for pf in /var/lib/wineexplorer/machine "/home/$INSTALLED_USER/.wine"; do
        [ -d "$pf/drive_c/windows/system32" ] || continue
        install -m 0644 /root/extra/dlls/x86_64-windows/mi.dll "$pf/drive_c/windows/system32/mi.dll"
        [ -d "$pf/drive_c/windows/syswow64" ] && install -m 0644 /root/extra/dlls/i386-windows/mi.dll "$pf/drive_c/windows/syswow64/mi.dll"
        echo "note mi_in_prefix: $pf"
    done
    ok dlls_installed
else notok dlls_installed "no Wine library folder found"; finish; fi
echo "note winelib: $WINELIB"

# 2. the service, elevation enforced, a new session for the user (as join-guest.sh)
[ -S /run/wex-domain-join.sock ] && ok socket_active || notok socket_active "$(ls -l /run/wex-domain-join.sock 2>&1)"
if [ -z "$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)" ]; then ok not_joined_before; else notok not_joined_before "$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)"; fi
s0_reg reg add 'HKLM\Software\Wine\Elevation' /v Helper /t REG_SZ /d 'C:\windows\system32\consent.exe' /f >/dev/null
sleep 5

# 3. the elevated batch: a console window for PowerShell (it needs one), join-ps.ps1 inside it
ORIG=$(hostname -s | tr 'a-z' 'A-Z')
comp_dn() { samba-tool computer show "$1" -H "ldap://dc1.$DOM" -U "Administrator%$PW" --attributes=distinguishedName 2>/dev/null | sed -n 's/^distinguishedName: //p'; }
rm -f /tmp/join-pw.txt /tmp/ps-stage-* /tmp/ps-continue-* /tmp/ps-join.out /tmp/join-ps.ps1
printf '%s\n' "$PW" > /root/join-pw.src
install -m 0600 -o "$INSTALLED_USER" /root/join-pw.src /tmp/join-pw.txt; rm -f /root/join-pw.src
install -m 0644 /root/extra/join-ps.ps1 /tmp/join-ps.ps1
: > /tmp/ps-join.out; chmod 666 /tmp/ps-join.out
cat > /tmp/join-ps.bat <<'BAT'
@echo off
start "pwsh" /wait Z:\opt\pwsh7\pwsh.exe -NoProfile -ExecutionPolicy Bypass -File Z:\tmp\join-ps.ps1
echo rc=%ERRORLEVEL% > Z:\tmp\ps-join.rc
BAT
sed -i 's/$/\r/' /tmp/join-ps.bat; chmod 644 /tmp/join-ps.bat
su - "$INSTALLED_USER" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 900 wine $PROBE runas cmd.exe '/c Z:\\tmp\\join-ps.bat'" > /tmp/join-ps-runas.out 2>&1 &
if ! host_step consent; then notok consent "$HOST_ANSWER"; finish; fi
wait_stage()   # NAME: the script's stage file, 4 minutes at most
{
    local i
    for i in $(seq 1 120); do [ -e "/tmp/ps-stage-$1" ] && return 0; grep -q '^=== done' /tmp/ps-join.out 2>/dev/null && return 1; sleep 2; done
    return 1
}
res() { tr -d '\r' < /tmp/ps-join.out | sed -n "s/^RESULT $1 //p" | head -1; }

# stage "joined": Add-Computer, the channel, the reset
if wait_stage joined; then
    sleep 1
    tr -d '\r' < /tmp/ps-join.out | sed 's/^/note ps: /' | cut -c1-300
    case "$(res autoload)" in "ok: "*Add-Computer*Remove-Computer*Test-ComputerSecureChannel*Reset-ComputerMachinePassword*) ok ps_module_autoload ;; *) notok ps_module_autoload "$(res autoload)" ;; esac
    case "$(res cim_before)" in *PartOfDomain=False*) ok ps_cim_before ;; *) notok ps_cim_before "$(res cim_before)" ;; esac
    echo "note ps_sc_before: $(res sc_before)"
    case "$(res add)" in "ok: "*) ok ps_add_computer ;; *) notok ps_add_computer "$(res add)" ;; esac
    net ads testjoin >/dev/null 2>&1 && ok ps_samba_testjoin || notok ps_samba_testjoin "$(net ads testjoin 2>&1)"
    det=$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)
    [ "$det" = "WEXLAB $DOM" ] && ok ps_join_detected || notok ps_join_detected "[$det]"
    case "$(comp_dn "$ORIG")" in *OU=WexMachines,*) ok ps_account_in_ou ;; *) notok ps_account_in_ou "[$(comp_dn "$ORIG")]" ;; esac
    case "$(res cim_joined)" in *PartOfDomain=True*Domain=$DOM*) ok ps_cim_joined ;; *) notok ps_cim_joined "$(res cim_joined)" ;; esac
    case "$(res sc_joined)" in "ok: True"*) ok ps_secure_channel ;; *) notok ps_secure_channel "$(res sc_joined)" ;; esac
    case "$(res add_again)" in "error: "*) ok ps_add_again_refused; echo "note ps_add_again: $(res add_again)" ;; *) notok ps_add_again_refused "$(res add_again)" ;; esac
    case "$(res reset_pw)" in "ok: "*) ok ps_reset_machine_password ;; *) notok ps_reset_machine_password "$(res reset_pw)" ;; esac
    case "$(res sc_after_reset)" in "ok: True"*) ok ps_secure_channel_after_reset ;; *) notok ps_secure_channel_after_reset "$(res sc_after_reset)" ;; esac
    wbinfo -t >/dev/null 2>&1 && ok ps_winbind_trust || notok ps_winbind_trust "$(wbinfo -t 2>&1)"
    touch /tmp/ps-continue-joined
else notok ps_stage_joined "the script did not reach the stage: $(tr -d '\r' < /tmp/ps-join.out | tail -5 | tr '\n' '|') runas: $(tr -d '\r' < /tmp/join-ps-runas.out | tail -3 | tr '\n' '|')"; finish; fi

# stage "renamed": Rename-Computer in the domain
if wait_stage renamed; then
    sleep 1
    case "$(res rename)" in "ok: "*) ok ps_rename_computer ;; *) notok ps_rename_computer "$(res rename)" ;; esac
    if [ "$(hostname -s)" = pslab1 ] && [ -n "$(comp_dn PSLAB1)" ] && [ -z "$(comp_dn "$ORIG")" ]; then ok ps_rename_account
    else notok ps_rename_account "host [$(hostname -s)] new [$(comp_dn PSLAB1)] old [$(comp_dn "$ORIG")]"; fi
    net ads testjoin >/dev/null 2>&1 && ok ps_rename_testjoin || notok ps_rename_testjoin "$(net ads testjoin 2>&1)"
    echo "note ps_cim_renamed (the running session keeps its start-up name): $(res cim_renamed)"
    echo "note ps_regname: $(s0_reg reg query 'HKLM\System\CurrentControlSet\Control\ComputerName\ActiveComputerName' 2>&1 | tr -d '\r' | tr '\n' ' ')"
    echo "note ps_state_tool: $(systemd-run --quiet --wait --collect --pipe -p User=wexsys -p WorkingDirectory=/ -p Environment=WINEPREFIX=/var/lib/wineexplorer/machine -p Environment=HOME=/var/lib/wineexplorer/machine -p Environment=WINEDEBUG=-all /usr/local/bin/wex-domain-state.sh 2>&1 | tr '\n' '|')"
    echo "note ps_regname_after: $(s0_reg reg query 'HKLM\System\CurrentControlSet\Control\ComputerName\ActiveComputerName' 2>&1 | tr -d '\r' | tr '\n' ' ')"
    touch /tmp/ps-continue-renamed
else notok ps_stage_renamed "the script did not reach the stage: $(tr -d '\r' < /tmp/ps-join.out | tail -5 | tr '\n' '|')"; fi

# the end: the name back, the removal
for i in $(seq 1 90); do grep -q '^=== done' /tmp/ps-join.out && break; sleep 2; done
tr -d '\r' < /tmp/ps-join.out | sed -n '/^STAGE renamed/,$p' | sed 's/^/note ps: /' | cut -c1-300
case "$(res rename_back)" in "ok: ReturnValue=0") ok ps_rename_back ;; *) notok ps_rename_back "$(res rename_back)" ;; esac
[ "$(hostname -s | tr 'a-z' 'A-Z')" = "$ORIG" ] && ok ps_rename_back_host || notok ps_rename_back_host "host [$(hostname -s)]"
case "$(res remove)" in "ok: "*) ok ps_remove_computer ;; *) notok ps_remove_computer "$(res remove)" ;; esac
[ -z "$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)" ] && ok ps_left_detected || notok ps_left_detected "$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)"
net ads testjoin >/dev/null 2>&1 && notok ps_left_testjoin "still joined" || ok ps_left_testjoin
case "$(res cim_after)" in *PartOfDomain=False*) ok ps_cim_after ;; *) notok ps_cim_after "$(res cim_after)" ;; esac
echo "note ps_sc_after: $(res sc_after)"
if journalctl --no-pager -b 0 2>/dev/null | grep -qF -- "$PW"; then notok ps_journal_has_no_password "the password is in the journal"; else ok ps_journal_has_no_password; fi
echo "note account_after_remove: $(comp_dn "$ORIG") $(comp_dn PSLAB1)"

s0_reg reg delete 'HKLM\Software\Wine\Elevation' /v Helper /f >/dev/null
finish
