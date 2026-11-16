#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs INSIDE an installed session 0 machine as root (sent there by installer/tests/ad/machine.sh). It joins
# the machine to the lab domain and checks Group Policy end to end (docs/GROUP_POLICY.md): the join is told to
# Windows programs, and the domain's registry policy reaches them. One line per check, "ok NAME" or
# "not ok NAME: detail" ("note NAME: text" for a finding that is not a check), then AD-GUEST-DONE; the host
# script decides.
#
#   machine-guest.sh [INSTALLED_USER]     (default wineuser)
#
# /root/adminpw holds the lab domain's Administrator password (put there by the host script).
set -uo pipefail

INSTALLED_USER=${1:-wineuser}
ADDR=10.71.0.30
DC=10.71.0.10
LAB_MAC=52:54:00:71:00:30
export DEBIAN_FRONTEND=noninteractive

HKLM_DOMAIN='HKLM\Software\WineExplorer\Domain'
HKLM_POLICY='HKLM\Software\Policies\WexLab'

ok() { echo "ok $1"; }
notok() { echo "not ok $1: $(printf '%s' "$2" | tr '\n' ' ' | cut -c1-240)"; }
# the lab password of the domain user does not stay on the machine, and the dropped route of the offline
# test is gone, however the run ends
finish() { ip route del blackhole "$DC/32" 2>/dev/null; rm -f /root/wexuser1pw /root/wexadmin1pw; echo "AD-GUEST-DONE"; exit 0; }
wine_cmd() { su - "$1" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 90 $2" 2>&1; }
value() { wine_cmd "$INSTALLED_USER" "wine reg query \"$1\" /v $2" | tr -d '\r' | awk -v n="$2" '$1 == n { print $NF }'; }

# 1. not joined: the detection says nothing and Windows programs see no domain
before=$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)
if [ -z "$before" ] && [ -z "$(value "$HKLM_DOMAIN" FlatName)" ]; then ok not_joined_before
else notok not_joined_before "detection [$before] registry [$(value "$HKLM_DOMAIN" FlatName)]"; fi

# the lab network: the card with the lab address, names from the controller
LAB_IF=$(ip -o link | grep -i "$LAB_MAC" | awk -F': ' '{ print $2 }')
if [ -z "$LAB_IF" ]; then notok lab_network "no card $LAB_MAC"; finish; fi
lab_up()
{
    # the installed system's network service would try DHCP on the card and take the address away again
    command -v nmcli >/dev/null 2>&1 && nmcli device set "$LAB_IF" managed no >/dev/null 2>&1
    ip link set "$LAB_IF" up
    ip addr show dev "$LAB_IF" | grep -q "$ADDR/" || ip addr add "$ADDR/24" dev "$LAB_IF" 2>/dev/null
}
lab_up
rm -f /etc/resolv.conf
printf 'search ad.wexlab.example\nnameserver %s\n' "$DC" > /etc/resolv.conf
found=
for i in $(seq 1 30); do
    if getent hosts dc1.ad.wexlab.example >/dev/null 2>&1; then found=1; break; fi
    sleep 2
done
if [ -n "$found" ]; then ok lab_network; else notok lab_network "the controller's name does not resolve"; finish; fi

# 2. Samba and the join
apt-get update > /root/ad-apt.log 2>&1
apt-get install -y samba winbind krb5-user >> /root/ad-apt.log 2>&1
if ! command -v net >/dev/null 2>&1 || ! command -v samba-gpupdate >/dev/null 2>&1; then
    notok samba_installed "$(tail -3 /root/ad-apt.log)"; finish
fi
ok samba_installed
cat > /etc/samba/smb.conf <<EOF
[global]
    workgroup = WEXLAB
    realm = AD.WEXLAB.EXAMPLE
    security = ads
    kerberos method = secrets and keytab
    idmap config * : backend = tdb
    idmap config * : range = 10000-19999
    idmap config WEXLAB : backend = rid
    idmap config WEXLAB : range = 20000-99999
    winbind use default domain = no
    winbind offline logon = yes
    template shell = /bin/bash
    template homedir = /home/%D/%U
EOF
cat > /etc/krb5.conf <<EOF
[libdefaults]
    default_realm = AD.WEXLAB.EXAMPLE
    dns_lookup_realm = false
    dns_lookup_kdc = true

[realms]
    AD.WEXLAB.EXAMPLE = {
        kdc = dc1.ad.wexlab.example
        admin_server = dc1.ad.wexlab.example
    }
EOF
# the package installation, or the network service of the installed system, may have put the old name server
# back: the controller is the name server again, and its name is known without one
lab_up
rm -f /etc/resolv.conf
printf 'search ad.wexlab.example\nnameserver %s\n' "$DC" > /etc/resolv.conf
grep -q dc1.ad.wexlab.example /etc/hosts || echo "$DC dc1.ad.wexlab.example dc1" >> /etc/hosts
grep -q "$ADDR" /etc/hosts || echo "$ADDR $(hostname -s).ad.wexlab.example $(hostname -s)" >> /etc/hosts
# into the lab's OU when the controller has it (gpo-lab.sh fixtures): the OU's object then applies after the
# domain's (docs/GPO_LAB.md)
# (a search before the join finds nothing: the join is tried into the OU, and plainly when the OU is not there)
OU_ARG=createcomputer=WexMachines
join=$(net ads join $OU_ARG -S dc1.ad.wexlab.example -U "Administrator%$(cat /root/adminpw)" 2>&1) || {
    OU_ARG=
    join="$join | $(net ads join -S dc1.ad.wexlab.example -U "Administrator%$(cat /root/adminpw)" 2>&1)"
}
# an account left from an earlier run keeps its place at a join: move it into the OU (when there is one), and
# take the account's real place from the directory
LDAP_ARGS=(-H ldap://dc1.ad.wexlab.example -U "Administrator%$(cat /root/adminpw)")
samba-tool computer move "$(hostname -s)" 'OU=WexMachines,DC=ad,DC=wexlab,DC=example' "${LDAP_ARGS[@]}" >/dev/null 2>&1 || true
ACCOUNT_DN=$(samba-tool computer show "$(hostname -s)" "${LDAP_ARGS[@]}" --attributes=distinguishedName 2>/dev/null |
    sed -n 's/^distinguishedName: //p')
case "$ACCOUNT_DN" in *OU=WexMachines,*) OU_ARG=in-ou ;; *) OU_ARG= ;; esac
echo "note join_container: ${ACCOUNT_DN:-unknown}"
systemctl restart winbind 2>/dev/null
if net ads testjoin >/dev/null 2>&1; then ok joined
else
    notok joined "$join | addr: $(ip -4 -o addr show dev "$LAB_IF" | awk '{ print $4 }') | ping: $(ping -c 1 -W 2 "$DC" 2>&1 | tail -1) | host: $(hostname -f 2>&1)"
    finish
fi

# 3. the join is told to Windows programs when session 0's services start
detected=$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)
systemctl restart wex-session0-services.service
sleep 5
flat=$(value "$HKLM_DOMAIN" FlatName)
name=$(value "$HKLM_DOMAIN" Name)
if [ "$detected" = "WEXLAB ad.wexlab.example" ] && [ "$flat" = WEXLAB ] && [ "$name" = ad.wexlab.example ]; then ok join_told_to_programs
else notok join_told_to_programs "detection [$detected] FlatName [$flat] Name [$name]"; fi

# 4. the domain's registry policy reaches a user's program
gp=$(systemctl start wex-gpupdate.service 2>&1; journalctl --no-pager -b 0 -u wex-gpupdate.service 2>/dev/null | tail -6)
limit=$(value "$HKLM_POLICY" Limit)
if [ "$limit" = 0x7 ] || [ "$limit" = 0x9 ]; then ok domain_policy_applied
else notok domain_policy_applied "Limit [$limit]; $gp"; fi

# 5. Can the user change it? Session 0 refuses the write only while elevation is enforced, which an installed
# system does not do by default (every user's token is unrestricted there). Reported as a note, not a check:
# the answer follows from that setting, not from Group Policy. Then the timer.
if wine_cmd "$INSTALLED_USER" "wine reg add \"$HKLM_POLICY\" /v Limit /t REG_DWORD /d 1 /f" >/dev/null 2>&1 &&
   [ "$(value "$HKLM_POLICY" Limit)" = 0x1 ]; then
    echo "note user_write_to_machine_policy: taken (elevation is not enforced on this system)"
    systemctl start wex-gpupdate.service 2>/dev/null
    if [ "$(value "$HKLM_POLICY" Limit)" = 0x7 ] || [ "$(value "$HKLM_POLICY" Limit)" = 0x9 ]; then ok refresh_puts_policy_back
    else notok refresh_puts_policy_back "Limit [$(value "$HKLM_POLICY" Limit)] after a refresh"; fi
else echo "note user_write_to_machine_policy: refused"; fi
if [ "$(systemctl is-enabled wex-gpupdate.timer 2>/dev/null)" = enabled ] && [ "$(systemctl is-active wex-gpupdate.timer)" = active ]; then ok timer_enabled
else notok timer_enabled "$(systemctl is-enabled wex-gpupdate.timer 2>&1) $(systemctl is-active wex-gpupdate.timer 2>&1)"; fi

# 5b. The GPO lab's fixtures (gpo-lab.sh fixtures) and an image with Group Policy steps 5 to 7 (it has
# wex-gpupdate.path): the order of the two objects, the security template, gpresult and a user's gpupdate.
if [ -n "$OU_ARG" ] && [ -f /lib/systemd/system/wex-gpupdate.path ]; then
    systemctl start wex-gpupdate.service 2>/dev/null
    l=$(value "$HKLM_POLICY" Limit); d=$(value "$HKLM_POLICY" DomainOnly); o=$(value "$HKLM_POLICY" OuOnly)
    if [ "$l" = 0x9 ] && [ "$d" = 0x1 ] && [ "$o" = 0x1 ]; then ok gpo_order_ou_after_domain
    else notok gpo_order_ou_after_domain "Limit [$l] DomainOnly [$d] OuOnly [$o]"; fi
    lb=$(value 'HKLM\Software\WineExplorer\Elevation' LockoutBadCount)
    ld=$(value 'HKLM\Software\WineExplorer\Elevation' LockoutDuration)
    so=$(value 'HKLM\Software\Microsoft\Windows\CurrentVersion\Policies\System' WexLabOption)
    if [ "$lb" = 0x7 ] && [ "$ld" = 0x1e ] && [ "$so" = 0x3 ]; then ok security_template_applied
    else notok security_template_applied "LockoutBadCount [$lb] LockoutDuration [$ld] WexLabOption [$so]"; fi
    gr=$(wine_cmd "$INSTALLED_USER" "wine gpresult /scope computer" | tr -d '\r')
    # the domain's objects (the Default Domain Policy among them) come before the OU's
    at_dom=$(grep -n '^        WexLab Domain$' <<<"$gr" | head -1 | cut -d: -f1)
    at_ou=$(grep -n '^        WexLab OU$' <<<"$gr" | head -1 | cut -d: -f1)
    if [ -n "$at_dom" ] && [ -n "$at_ou" ] && [ "$at_dom" -lt "$at_ou" ]; then
        ok gpresult_lists_objects_in_order
    else notok gpresult_lists_objects_in_order "$(grep -A4 'Applied Group Policy Objects' <<<"$gr")"; fi
    runs=$(journalctl --no-pager -b 0 -u wex-gpupdate.service 2>/dev/null | grep -c 'Finished\|Deactivated' || true)
    gu=$(wine_cmd "$INSTALLED_USER" "wine gpupdate /target:computer /wait:80" | tr -d '\r')
    runs2=$(journalctl --no-pager -b 0 -u wex-gpupdate.service 2>/dev/null | grep -c 'Finished\|Deactivated' || true)
    if grep -qx 'Computer Policy Refresh has completed.' <<<"$gu" && [ "$runs2" -gt "$runs" ]; then ok user_gpupdate_runs_a_refresh
    else notok user_gpupdate_runs_a_refresh "output [$gu] service runs $runs -> $runs2; path unit $(systemctl is-active wex-gpupdate.path 2>&1)"; fi
else
    echo "note gpo_lab_checks: skipped (fixtures [${OU_ARG:-none}], wex-gpupdate.path $( [ -f /lib/systemd/system/wex-gpupdate.path ] && echo present || echo absent))"
fi

# 6. the policy service runs once in session 0, and nothing failed
n=$(pgrep -u wexsys -f 'wexpolicy' 2>/dev/null | wc -l)
if [ "$n" -eq 1 ]; then ok policy_service_runs; else notok policy_service_runs "wexpolicy processes of wexsys: $n"; fi
failed=$(systemctl --failed --no-legend 2>/dev/null | grep -i wex || true)
if [ -z "$failed" ]; then ok no_failed_units; else notok no_failed_units "$failed"; fi

# 7. A domain user logs on through the greeter (docs/DOMAIN_LOGON.md, WO 4): when the host put wexuser1's
# password here (gpo-lab.sh fixtures) and the image has the admission step (WO 3). The guest prepares the host side
# (Winbind in NSS and PAM, the Welcome screen without automatic logon) and asks the host for the steps that need the
# screen and keyboard (machine.sh, "host steps"): the logon, the tile after it, a wrong password, the offline logon.
DUSER='WEXLAB\wexuser1'
LIBEXEC=/usr/local/libexec/wineexplorer
host_step()   # NAME [ARGS]: asks the host and waits for its answer (HOST_ANSWER); true when it is "ok"
{
    local name=$1 i
    rm -f "/root/host-done-$name"
    echo "AD-GUEST-HOST-STEP $*"
    for i in $(seq 1 300); do [ -e "/root/host-done-$name" ] && break; sleep 5; done
    HOST_ANSWER=$(cat "/root/host-done-$name" 2>/dev/null)
    [ "$HOST_ANSWER" = ok ]
}
user_session() { loginctl list-sessions --no-legend | awk '{ print $3 }' | grep -qixF "$DUSER"; }
user_reg() { wine_cmd "$DUSER" "wine reg query \"HKCU\\Software\\Policies\\WexLab\" /v UserLimit" | tr -d '\r' | awk '$1 == "UserLimit" { print $NF }'; }
user_record() { awk -v n="$DUSER" 'NR > 1 && tolower($1) == tolower(n)' /etc/wineexplorer/accounts.map; }
cached_users() { busctl --system call org.freedesktop.Accounts /org/freedesktop/Accounts org.freedesktop.Accounts ListCachedUsers 2>/dev/null; }

if [ -s /root/wexuser1pw ] && [ -x /usr/local/sbin/wex-domain-admit-pam.sh ]; then
    apt-get install -y libpam-winbind libnss-winbind >> /root/ad-apt.log 2>&1
    sed -i -E '/^(passwd|group):/ { /winbind/! s/$/ winbind/ }' /etc/nsswitch.conf
    pam-auth-update --enable winbind mkhomedir >> /root/ad-apt.log 2>&1 || pam-auth-update --enable winbind >> /root/ad-apt.log 2>&1
    systemctl restart winbind 2>/dev/null
    # template homedir = /home/%D/%U: the domain's folder (pam_mkhomedir makes the user's own)
    install -d -m 0755 /home/WEXLAB
    sleep 3
    pw=$(getent passwd "$DUSER")
    duid=$(cut -d: -f3 <<<"$pw")
    if [ -n "$duid" ] && [ "$duid" -ge 20000 ] && [ "$(cut -d: -f7 <<<"$pw")" = /bin/bash ]; then ok domain_user_resolves
    else notok domain_user_resolves "getent [$pw] wbinfo [$(wbinfo -i "$DUSER" 2>&1)]"; finish; fi
    if grep -q 'wex-domain-admit-pam.sh' /etc/pam.d/lightdm; then ok admission_step_installed
    else notok admission_step_installed "$(grep pam_exec /etc/pam.d/lightdm)"; fi
    # AccountsService caches a user when the user first logs on: before that, no tile
    if cached_users | grep -q "/User$duid\b"; then echo "note tile_before_first_logon: cached already"
    else echo "note tile_before_first_logon: not cached (the greeter has no tile; the first logon is typed)"; fi
    # the Welcome screen: no automatic logon of the installed user, LightDM restarted (that user's session ends)
    # (the setting is in /etc/lightdm/lightdm.conf and in a conf.d file of the image)
    mv /etc/lightdm/lightdm.conf.d/60-wineexplorer-autologin.conf /root/60-wineexplorer-autologin.conf.off 2>/dev/null
    sed -i 's/^autologin-user=/#autologin-user=/' /etc/lightdm/lightdm.conf
    systemctl restart lightdm

    # 7a. the first logon, online
    if ! host_step logon; then
        notok domain_logon "$HOST_ANSWER"
        # what the session said, and what session 0 and the policy refresh did meanwhile
        echo "note diag wine-session.log: $(tail -6 "/home/WEXLAB/wexuser1/.wine-session.log" 2>&1 | tr '\n' '|' | cut -c1-600)"
        echo "note diag journal: $(journalctl --no-pager -b 0 2>/dev/null | grep -i 'session0\|wex-gpupdate\|lightdm\|accounts-daemon' | tail -14 | cut -c1-170 | tr '\n' '|')"
        finish
    fi
    if user_session; then ok domain_logon
    else notok domain_logon "sessions: $(loginctl list-sessions --no-legend | awk '{ print $3 }' | tr '\n' ' ')"; finish; fi
    # admission: an active domain record and the recorded groups (Domain Users at least)
    rec=$(user_record)
    dsid=$(wbinfo -n "$DUSER" 2>/dev/null | awk '{ print $1 }')
    rid=${dsid##*-}
    if grep -q " active domain 21 " <<<"$rec" && [ "$(awk '{ print $NF }' <<<"$rec")" = "$rid" ]; then ok domain_record_admitted
    else notok domain_record_admitted "record [$rec] sid [$dsid]"; fi
    key=$(awk '{ print $3 }' <<<"$rec")
    if [ -n "$key" ] && awk -v k="$key" '$1 == k { for (i = 2; i <= NF; i++) if ($i == 513) f = 1 } END { exit !f }' /etc/wineexplorer/domain-groups 2>/dev/null
    then ok domain_groups_recorded; else notok domain_groups_recorded "key [$key] $(head -3 /etc/wineexplorer/domain-groups 2>&1)"; fi
    # the token's user and DOMAIN\user (whoami: GetUserNameExW and the token), the environment and the profile
    who=$(wine_cmd "$DUSER" "wine whoami /user /fo csv /nh" | tr -d '\r' | tail -1)
    if [ "$(tr 'A-Z' 'a-z' <<<"$who")" = "$(tr 'A-Z' 'a-z' <<<"\"$DUSER\",\"$dsid\"")" ]; then ok domain_token_and_name
    else notok domain_token_and_name "whoami [$who] expected [$DUSER $dsid]"; fi
    env_dom=$(wine_cmd "$DUSER" "wine cmd /c echo %USERDOMAIN%" | tr -d '\r' | tail -1)
    env_prof=$(wine_cmd "$DUSER" "wine cmd /c echo %USERPROFILE%" | tr -d '\r' | tail -1)
    if [ "$env_dom" = WEXLAB ] && [ "$(tr 'A-Z' 'a-z' <<<"$env_prof")" = 'c:\users\wexuser1' ]; then ok domain_environment
    else notok domain_environment "USERDOMAIN [$env_dom] USERPROFILE [$env_prof]"; fi
    home=$(getent passwd "$DUSER" | cut -d: -f6)
    if [ -d "$home/.wine/drive_c/users/wexuser1" ]; then ok domain_profile_folder
    else notok domain_profile_folder "home [$home]: $(ls "$home/.wine/drive_c/users" 2>&1 | tr '\n' ' ')"; fi
    # the domain's user policy (WexLab Domain, User/Registry.pol): the logon itself starts a refresh (the admission
    # step), so it arrives without anyone starting wex-gpupdate.service
    ul=
    for _ in $(seq 1 24); do ul=$(user_reg); [ "$ul" = 0x3 ] && break; sleep 5; done
    if [ "$ul" = 0x3 ]; then ok domain_user_policy_at_logon
    else notok domain_user_policy_at_logon "UserLimit [$ul] two minutes after the logon; $(journalctl --no-pager -b 0 -u wex-gpupdate.service 2>/dev/null | tail -3)"; fi
    systemctl start wex-gpupdate.service 2>/dev/null
    ul=$(user_reg)
    if [ "$ul" = 0x3 ]; then ok domain_user_policy_applied
    else notok domain_user_policy_applied "UserLimit [$ul]; $(journalctl --no-pager -b 0 -u wex-gpupdate.service 2>/dev/null | tail -4)"; fi
    gr=$(wine_cmd "$DUSER" "wine gpresult /scope user" | tr -d '\r')
    if grep -q '^        WexLab Domain$' <<<"$gr"; then ok gpresult_user_lists_domain_object
    else notok gpresult_user_lists_domain_object "$(grep -A4 'Applied Group Policy Objects' <<<"$gr" | head -6)"; fi

    # 7b. the tile after the first logon: the user logs off (the session ends, LightDM shows the Welcome screen);
    # AccountsService has the user now and the greeter lists them (the tile shows the real name)
    loginctl terminate-user "$duid"
    sleep 5
    if cached_users | grep -q "/User$duid\b"; then ok domain_user_cached_by_accountsservice
    else notok domain_user_cached_by_accountsservice "$(cached_users | cut -c1-300)"; fi
    if host_step tile 'Wex User'; then ok domain_user_tile_after_first_logon
    else notok domain_user_tile_after_first_logon "$HOST_ANSWER"; fi

    # 7c. offline: the controller becomes unreachable (a route that drops the packets); the first logon cached the
    # credentials (pam_winbind cached_login, winbind offline logon = yes). Winbind has to notice by itself. A wrong
    # password is refused, the right one logs on with the record and groups as they were, and nothing tombstones.
    rec_before=$(user_record); groups_before=$(cat /etc/wineexplorer/domain-groups)
    ip route add blackhole "$DC/32"
    off=
    for _ in $(seq 1 36); do
        wbinfo --ping-dc >/dev/null 2>&1 || { off=1; break; }
        sleep 5
    done
    echo "note winbind_online_status_offline: $(wbinfo --online-status 2>&1 | tr '\n' ' ' | cut -c1-200)"
    if [ -n "$off" ]; then ok controller_unreachable
    else notok controller_unreachable "wbinfo --ping-dc still answers with the route dropped"; fi
    if host_step logon-bad; then ok offline_wrong_password_refused
    else notok offline_wrong_password_refused "$HOST_ANSWER"; fi
    if user_session; then notok offline_wrong_password_refused "a session exists after a wrong password"; fi
    if host_step logon; then
        if user_session; then ok offline_logon; else notok offline_logon "no session after the offline logon"; fi
        if [ "$(user_record)" = "$rec_before" ] && [ "$(cat /etc/wineexplorer/domain-groups)" = "$groups_before" ]; then ok offline_record_and_groups_kept
        else notok offline_record_and_groups_kept "record [$(user_record)] was [$rec_before]; groups [$(cat /etc/wineexplorer/domain-groups | tr '\n' ' ')]"; fi
        who=$(wine_cmd "$DUSER" "wine whoami /user /fo csv /nh" | tr -d '\r' | tail -1)
        if [ "$(tr 'A-Z' 'a-z' <<<"$who")" = "$(tr 'A-Z' 'a-z' <<<"\"$DUSER\",\"$dsid\"")" ]; then ok offline_token_and_name
        else notok offline_token_and_name "whoami [$who] expected [$DUSER $dsid]"; fi
        # a sync while the controller cannot be asked leaves the domain record alone (no tombstone offline)
        "$LIBEXEC/wex-accounts" sync >/dev/null 2>&1
        if [ "$(user_record)" = "$rec_before" ]; then ok offline_sync_does_not_tombstone
        else notok offline_sync_does_not_tombstone "record [$(user_record)] was [$rec_before]"; fi
    else notok offline_logon "$HOST_ANSWER"; fi
    # the controller comes back: Winbind goes online again by itself (else it is told, and that is noted)
    ip route del blackhole "$DC/32"
    on=
    for _ in $(seq 1 40); do wbinfo --ping-dc >/dev/null 2>&1 && { on=1; break; }; sleep 5; done
    if [ -z "$on" ]; then smbcontrol winbindd online >/dev/null 2>&1; sleep 5; echo "note winbind_back_online: told to (it did not by itself within 200 s)"
        wbinfo --ping-dc >/dev/null 2>&1 && on=1; fi
    if [ -n "$on" ] && net ads testjoin >/dev/null 2>&1; then ok back_online
    else notok back_online "ping-dc / testjoin after the route is gone"; fi

    # 7d. a member of Domain Admins logs on (the fixtures' wexadmin1): at a real logon the token is the limited one,
    # Administrators and Domain Admins only for deny, Domain Users enabled; the full token (elevation) has them
    # enabled. Wine's whoami does not print the groups: the project's token probe does (machine.sh puts it here).
    PROBE=/usr/local/share/wex-lab-probe/tierprobe64.exe
    if [ -s /root/wexadmin1pw ] && [ -f "$PROBE" ]; then
        AUSER='WEXLAB\wexadmin1'
        # deny_only HEX: SE_GROUP_USE_FOR_DENY_ONLY (0x10) set and SE_GROUP_ENABLED (0x4) clear; enabled HEX: the reverse
        deny_only() { [ -n "$1" ] && [ $(( 0x$1 & 0x10 )) != 0 ] && [ $(( 0x$1 & 0x4 )) = 0 ]; }
        enabled() { [ -n "$1" ] && [ $(( 0x$1 & 0x4 )) != 0 ] && [ $(( 0x$1 & 0x10 )) = 0 ]; }
        grp() { sed -n "s/^$1=$2 attrs=//p" <<<"$TOK"; }
        # A default install does not enforce elevation (no elevation helper is configured: HKLM\Software\Wine\Elevation,
        # Helper), so no token is filtered; the check needs it on, set through session 0 (the machine's registry is
        # its) for this part, and taken away after it.
        s0_reg() { (cd / && runuser -u wexsys -- env -i WINEPREFIX=/var/lib/wineexplorer/machine HOME=/var/lib/wineexplorer/machine \
            PATH=/usr/bin:/bin WINEDEBUG=-all /usr/local/bin/wine "$@" </dev/null 2>/dev/null); }
        s0_reg reg add 'HKLM\Software\Wine\Elevation' /v Helper /t REG_SZ /d 'C:\windows\system32\consent.exe' /f >/dev/null
        loginctl terminate-user "$duid"
        sleep 5
        if host_step logon "$AUSER"; then
            if loginctl list-sessions --no-legend | awk '{ print $3 }' | grep -qixF "$AUSER"; then ok domain_admin_logon
            else notok domain_admin_logon "no session after the logon"; fi
            arec=$(awk -v n="$AUSER" 'NR > 1 && tolower($1) == tolower(n)' /etc/wineexplorer/accounts.map)
            akey=$(awk '{ print $3 }' <<<"$arec")
            if [ -n "$akey" ] && awk -v k="$akey" '$1 == k { for (i = 2; i <= NF; i++) if ($i == 512) f = 1 } END { exit !f }' /etc/wineexplorer/domain-groups 2>/dev/null
            then ok domain_admin_in_domain_admins; else notok domain_admin_in_domain_admins "key [$akey] $(cat /etc/wineexplorer/domain-groups 2>&1 | tr '\n' ' ')"; fi
            TOK=$(wine_cmd "$AUSER" "wine $PROBE token" | tr -d '\r')
            DOM3=$(sed -n 's/^user=\(S-1-5-21-[0-9]*-[0-9]*-[0-9]*\)-[0-9]*$/\1/p' <<<"$TOK")
            if [ -n "$DOM3" ] && grep -qx 'elevation=3' <<<"$TOK" && grep -qx 'linkedelevation=2' <<<"$TOK"; then ok domain_admin_token_is_split
            else notok domain_admin_token_is_split "$(grep -E '^(user|elevation|linkedelevation)=' <<<"$TOK" | tr '\n' ' ')"; fi
            if deny_only "$(grp group S-1-5-32-544)" && deny_only "$(grp group "$DOM3-512")" && enabled "$(grp group "$DOM3-513")"; then
                ok domain_admin_limited_token_filters_admin_groups
            else notok domain_admin_limited_token_filters_admin_groups "$(grep -E '^group=(S-1-5-32-544|.*-51[23]) ' <<<"$TOK" | tr '\n' ' ')"; fi
            if enabled "$(grp linkedgroup S-1-5-32-544)" && enabled "$(grp linkedgroup "$DOM3-512")" && enabled "$(grp linkedgroup "$DOM3-513")"; then
                ok domain_admin_full_token_has_them_enabled
            else notok domain_admin_full_token_has_them_enabled "$(grep -E '^linkedgroup=(S-1-5-32-544|.*-51[23]) ' <<<"$TOK" | tr '\n' ' ')"; fi
            # The policy editor's write (docs/GROUP_POLICY.md, "Local policy"): the limited token is refused; after the
            # consent dialog (the host answers Continue) the same call from the elevated process puts the setting in
            # the machine's registry through the spool, gpsvc and the refresh
            GPV='HKLM\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer'
            LIM=$(wine_cmd "$AUSER" "wine $PROBE gpset computer:NoRecentDocsHistory 1 0" | tr -d '\r')
            if grep -qx 'gpset=5' <<<"$LIM" && [ -z "$(value "$GPV" NoRecentDocsHistory)" ]; then ok domain_admin_limited_editor_write_refused
            else notok domain_admin_limited_editor_write_refused "$(grep -E '^gpset=' <<<"$LIM" | tr '\n' ' ') value [$(value "$GPV" NoRecentDocsHistory)]"; fi
            GPE=/tmp/gpset-elevated.out
            rm -f "$GPE" /tmp/spoolsteps.out; : > "$GPE"; : > /tmp/spoolsteps.out; chmod 666 "$GPE" /tmp/spoolsteps.out
            su - "$AUSER" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 280 wine $PROBE runas cmd.exe '/c Z:\\usr\\local\\share\\wex-lab-probe\\tierprobe64.exe spoolsteps > Z:\\tmp\\spoolsteps.out & Z:\\usr\\local\\share\\wex-lab-probe\\tierprobe64.exe spoolsteps Z:\\tmp >> Z:\\tmp\\spoolsteps.out & Z:\\usr\\local\\share\\wex-lab-probe\\tierprobe64.exe spoolsteps C:\\windows >> Z:\\tmp\\spoolsteps.out &Z:\\usr\\local\\share\\wex-lab-probe\\tierprobe64.exe gpset computer:NoRecentDocsHistory 1 0 > Z:\\tmp\\gpset-elevated.out'" \
                > /tmp/gpset-runas.out 2>&1 &
            if host_step consent; then
                for _ in $(seq 1 150); do grep -q '^gpset=' "$GPE" && break; sleep 2; done
                GOT=$(tr -d '\r' < "$GPE")
                if grep -qx 'gpset=0' <<<"$GOT" && [ "$(value "$GPV" NoRecentDocsHistory)" = 0x1 ]; then ok domain_admin_elevated_editor_write_applies
                else
                    notok domain_admin_elevated_editor_write_applies "result [$GOT] runas [$(tr -d '\r' < /tmp/gpset-runas.out | tail -2 | tr '\n' ' ')] value [$(value "$GPV" NoRecentDocsHistory)]"
                    tr -d '\r' < /tmp/spoolsteps.out | sed 's/^/note spoolsteps: /'
                    wc -l /tmp/gpset-trace.out | sed 's/^/note spooltrace lines: /'
                    grep -a -n -E 'diag|set_fd_name_info|open_file_object' /tmp/gpset-trace.out | cut -c1-230 | head -60 | sed 's/^/note spooltrace: /'
                fi
            else notok domain_admin_elevated_editor_write_applies "consent step: $HOST_ANSWER"; fi
        else notok domain_admin_logon "$HOST_ANSWER"; fi
        s0_reg reg delete 'HKLM\Software\Wine\Elevation' /v Helper /f >/dev/null
    else
        echo "note domain_admin_logon: skipped (wexadmin1 password $( [ -s /root/wexadmin1pw ] && echo present || echo absent), probe $( [ -f "$PROBE" ] && echo present || echo absent))"
    fi
else
    echo "note domain_logon: skipped (wexuser1 password $( [ -s /root/wexuser1pw ] && echo present || echo absent), admission step $( [ -x /usr/local/sbin/wex-domain-admit-pam.sh ] && echo present || echo absent))"
fi

finish
