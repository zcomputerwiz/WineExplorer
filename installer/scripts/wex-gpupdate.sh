#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Group Policy for Windows programs: the domain's (docs/GROUP_POLICY.md, step 4) and the local object's ("Local
# policy" there).
#
# Local policy needs no domain: the Registry.pol files of session 0's root (drive_c/windows/system32/GroupPolicy/
# Machine and User) apply to the computer and to every person of the accounts map, as the first object; on a joined
# machine the domain's objects follow and override it (Windows applies Local, Site, Domain, OU). A machine that is
# not joined, or has no Samba, does only that. Everything below is for the domain's part.
#
# Samba fetches the policy; session 0's writer applies it. Run as root by wex-gpupdate.service (at start, every
# 90 minutes, when a user's gpupdate asks, and at a domain user's logon), on a machine joined with Samba (net ads
# join, Winbind):
#
#   1. samba-gpupdate --force brings Samba's cache of the domain's policy objects up to date
#      (/var/cache/samba/gpo_cache/<REALM>/POLICIES/{GUID}/...).
#   2. "net ads gpo list" names the objects that apply to this computer, and to each user of the accounts map
#      who is a domain user, in the order Samba applies them.
#   3. Their Registry.pol files are copied to a directory that session 0's account can read, and
#      "wexpolicy apply" writes them: /machine for the computer, /user:SID for each user (the SID is the one
#      of the accounts map; a user who is not logged on gets the policy at the next logon).
#   An account with no Registry.pol at all gets "wexpolicy clear" (a domain user, or a target that had policy).
#
#   Also: the security templates (wexpolicy security), the list of applied objects (wexpolicy record), and at the
#   end of every run "wexpolicy refreshed" for gpupdate. Started by wex-gpupdate.timer, and by wex-gpupdate.path
#   when a user's gpupdate asked session 0's gpsvc for a refresh.
#
#   wex-gpupdate.sh           do it
#   wex-gpupdate.sh --print   steps 1 and 2 only: print the objects ("object ...") and the files ("machine FILE",
#                             "security FILE", "user NAME FILE"), write nothing
#
# This is a shell script on Samba's command line tools on purpose: an extension inside samba-gpupdate would be
# a Python file under Samba's licence (GPL-3.0-or-later).
set -u

# the shared library (scripts/lib/wex-common.sh): installed, else next to this script in the repository
for wex_lib in "${WEX_LIB_DIR:-/usr/local/lib/wineexplorer}/wex-common.sh" "$(dirname -- "$(readlink -f -- "$0")")/lib/wex-common.sh" "$(dirname -- "$(readlink -f -- "$0")")/../../scripts/lib/wex-common.sh"; do [ -r "$wex_lib" ] && break; done
. "$wex_lib"

PRINT=0
[ "${1:-}" = --print ] && PRINT=1
ARGS=("$@")

MACHINE_ROOT=${WEX_MACHINE_ROOT:-$(wex_machine_root)}
S0_USER=${WEX_SESSION0_USER:-wexsys}
WINE=${WINE:-/usr/local/bin/wine}
MAP=${WEX_ACCOUNTS_MAP:-/etc/wineexplorer/accounts.map}
STAGE=/run/wineexplorer-policy
LISTS=

# --logon NAME: root's step of a logon (the PAM line of the greeter, wex-domain-admit-pam.sh), docs/GROUP_POLICY.md,
# "Policy before the desktop". Windows applies a user's policy before the desktop on the user's first sign-in on a
# computer (fast logon optimization is off then), and at every sign-in when "Always wait for the network at computer
# startup and logon" is enabled (SyncForegroundPolicy); at other sign-ins the policy is applied while the user already
# works. This is the first kind: it runs only the user's part of this script, and only when there is policy to apply
# and the user is new here (no archive of the user's policy) or the computer asks to wait. Anything else ends at
# once, status 0, and the session's own refresh after the shell is up does the work. NAME is looked up in the accounts
# map (active records only); a name that is not there ends the run too.
ONLY_SID=
LOCK_WAIT=270
# A DWORD of session 0's registry (the policies of Windows that steer this script: the file system.reg, as root reads
# it): KEY below HKLM with single backslashes, VALUE; the number on stdout, nothing when it is not there
hive_dword()
{
    awk -v key="$1" -v name="$2" '
        BEGIN {
            n = split(tolower(key), parts, "\\"); want = parts[1]
            for (i = 2; i <= n; i++) want = want "\\\\" parts[i]
            want = "[" want "]"; nm = "\"" tolower(name) "\"=dword:"
        }
        { l = tolower($0) }
        index(l, want) == 1 { inside = 1; next }
        substr(l, 1, 1) == "[" { inside = 0 }
        inside && index(l, nm) == 1 { v = substr(l, length(nm) + 1); sub(/[^0-9a-f].*/, "", v); print v; exit }' \
        "$MACHINE_ROOT/system.reg" 2>/dev/null | { read -r hex && [ -n "$hex" ] && echo $((16#$hex)); }
}
sync_foreground_policy() { [ "$(hive_dword 'Software\Policies\Microsoft\Windows NT\CurrentVersion\Winlogon' SyncForegroundPolicy)" = 1 ]; }
if [ "${1:-}" = --logon ]; then
    LOGON_NAME=${2:-}
    LOGON_MAP=${WEX_ACCOUNTS_MAP:-/etc/wineexplorer/accounts.map}
    case $LOGON_NAME in ""|-*|*[!A-Za-z0-9._\\\ \$@-]*) echo "wex-gpupdate: --logon: not a user name"; exit 0 ;; esac
    [ -r "$LOGON_MAP" ] || exit 0
    # name, kind (local or domain: a version 2 map says it), SID, as the loop over the map further down reads them
    LOGON_REC=$(awk -v want="$LOGON_NAME" '
        function plain(s) { sub(/^.*\\/, "", s); return tolower(s) }
        NR == 1 { v = $2; next }
        $4 == "active" && $2 >= 1000 && $2 != 65534 && (tolower($1) == tolower(want) || plain($1) == plain(want)) {
            o = (v == 2) ? 1 : 0
            print $1, (v == 2 ? $5 : "unknown"), "S-1-5-21-" $(6 + o) "-" $(7 + o) "-" $(8 + o) "-" $(9 + o); exit }' "$LOGON_MAP")
    [ -n "$LOGON_REC" ] || { echo "wex-gpupdate: --logon $LOGON_NAME: no active record in the accounts map"; exit 0; }
    read -r _ logon_kind ONLY_SID <<<"$LOGON_REC"
    LOGON_GPO=$MACHINE_ROOT/drive_c/windows/system32
    logon_has=0
    for f in "$LOGON_GPO/GroupPolicy/User/Registry.pol" "$LOGON_GPO/GroupPolicyUsers/S-1-5-32-544/User/Registry.pol" \
             "$LOGON_GPO/GroupPolicyUsers/S-1-5-32-545/User/Registry.pol" "$LOGON_GPO/GroupPolicyUsers/$ONLY_SID/User/Registry.pol"; do
        [ -f "$f" ] && [ ! -L "$f" ] && [ -s "$f" ] && logon_has=1
    done
    # a domain account (or one whose kind is not known) may have objects of the domain: the run finds out
    [ "$logon_kind" = local ] || logon_has=1
    if [ "$logon_has" = 0 ]; then echo "wex-gpupdate: --logon $LOGON_NAME: no policy to apply"; exit 0; fi
    if [ -e "$MACHINE_ROOT/drive_c/windows/system32/GroupPolicy/Archive/User-$ONLY_SID.pol" ] && ! sync_foreground_policy; then
        echo "wex-gpupdate: --logon $LOGON_NAME: not a first logon; the session refreshes after the shell is up"
        exit 0
    fi
    LOCK_WAIT=40
fi

# --background: the timer's run (wex-gpupdate-background.service, every 5 minutes), docs/GROUP_POLICY.md, "The refresh
# interval". Windows refreshes in the background every 90 minutes plus a random 0 to 30 (GroupPolicyRefreshTime and
# GroupPolicyRefreshTimeOffset, in minutes: 0 to 44640 and 0 to 1440), unless "Turn off background refresh of Group Policy"
# (DisableBkGndGroupPolicy = 1) is enabled. A run does nothing until the time that the last run set (/run/wex-gpupdate.due, gone after a boot);
# every run sets the next one. The requests of gpupdate, the logons and the first run after boot are not limited by it.
DUE_FILE=${WEX_GPO_DUE:-/run/wex-gpupdate.due}   # in /run: a boot starts again, as Windows refreshes at startup
if [ "${1:-}" = --background ]; then
    due=$(tr -dc 0-9 < "$DUE_FILE" 2>/dev/null)
    # (the first run after a boot, with no due time yet, is the refresh at startup, which the policy does not turn off)
    if [ -n "$due" ] && [ "$(hive_dword 'Software\Microsoft\Windows\CurrentVersion\Policies\System' DisableBkGndGroupPolicy)" = 1 ]; then
        echo "wex-gpupdate: --background: the background refresh is turned off by policy"
        exit 0
    fi
    if [ -n "$due" ] && [ "$(date +%s)" -lt "$due" ]; then exit 0; fi
fi
# when the next background run is due: the interval and a random part of the offset after now
schedule_background()
{
    local interval offset add
    interval=$(hive_dword 'Software\Policies\Microsoft\Windows\System' GroupPolicyRefreshTime); interval=${interval:-90}
    offset=$(hive_dword 'Software\Policies\Microsoft\Windows\System' GroupPolicyRefreshTimeOffset); offset=${offset:-30}
    [ "$interval" -le 44640 ] || interval=44640
    [ "$offset" -le 1440 ] || offset=1440
    # (a time of 0 asks for a refresh about every 7 seconds on Windows, for tests: here every tick of the timer)
    [ "$interval" -ge 5 ] || interval=5
    add=$(( (RANDOM * 32768 + RANDOM) % (offset * 60 + 1) ))
    echo $(( $(date +%s) + interval * 60 + add )) > "$DUE_FILE.new" && mv -f "$DUE_FILE.new" "$DUE_FILE"
}

# wexpolicy's own exit status is the function's (not tr's): a failed apply or record is not lost
s0_wexpolicy()
{
    (cd / && runuser -u "$S0_USER" -- env -i WINEPREFIX="$MACHINE_ROOT" HOME="$MACHINE_ROOT" PATH=/usr/bin:/bin \
        WINEDEBUG=-all "$WINE" wexpolicy "$@" </dev/null 2>/dev/null) | tr -d '\r'
    return "${PIPESTATUS[0]}"
}

# a failure of the writer is said in the journal and makes the run end with 1; the run goes on with what is left
wexpolicy_failed() { echo "wex-gpupdate: wexpolicy $* failed" >&2; [ -z "$LISTS" ] || : > "$LISTS/failed"; }

# Only one run at a time: a manual run and the service's would share the staging folder and the writer. (--print
# only reads.) A run that finds another one waits for it, so that a request made meanwhile is served by a run that
# started after it.
if [ "$PRINT" != 1 ]; then
    exec 9> "${WEX_GPO_LOCK:-/run/wex-gpupdate.lock}"
    flock -w "$LOCK_WAIT" 9 || { echo "wex-gpupdate: another run did not end in time" >&2; exit 1; }
fi

# gpsvc numbers the refresh requests and leaves the last number in the request file (Refresh). The run reads it now,
# when it has the lock: it serves every request up to that one, and says so at its end, so that a program that asked
# later does not take this run for the answer.
REQUEST_FILE=$MACHINE_ROOT/drive_c/windows/system32/GroupPolicy/Refresh
request_number() { head -c 20 "$REQUEST_FILE" 2>/dev/null | tr -dc '0-9'; }
REQ0=
[ "$PRINT" = 1 ] || REQ0=$(request_number)
REQ0=${REQ0:-0}   # no file yet (or read while it is written): nothing is served beyond 0, so a request that comes in reruns

# Every run ends by telling session 0 it is done, also on a machine that is not joined: a gpupdate that asked for
# it waits for that (gpupdate.exe, wexpolicy refreshed). A request that came in during the run is not served by it
# (the path unit does not start a service that is running again): the run begins again.
finish()
{
    local now
    [ -z "$LISTS" ] || rm -rf "$LISTS"
    [ "$PRINT" = 1 ] || [ -n "$ONLY_SID" ] || schedule_background || true
    [ "$PRINT" = 1 ] || [ -n "$ONLY_SID" ] || s0_wexpolicy refreshed $REQ0 >/dev/null || true
    now=$(request_number)
    # the number only moves forward (and may wrap): a different one is a newer request (a logon run, which serves
    # no request, leaves it to the run that does)
    if [ "$PRINT" != 1 ] && [ -z "$ONLY_SID" ] && [ -n "$now" ] && [ "$now" != "$REQ0" ]; then
        exec "$0"
    fi
}
trap finish EXIT

# The local Group Policy object (docs/GROUP_POLICY.md, "Local policy") works without a domain: its Registry.pol
# files are in session 0's root, where Windows keeps them. The domain's objects need Samba and a join.
LOCAL_GPO=$MACHINE_ROOT/drive_c/windows/system32/GroupPolicy
REG_EXT='[{35378EAC-683F-11D2-A89A-00C04FBBCFA2}{D02B1F72-3407-48AE-BA88-E8213C6761F1}]'
SEC_EXT='[{827D319E-6EAC-11D2-A4EA-00C04F79F83A}{803E14A0-B4FB-11D0-A0D0-00A0C90F574B}]'
JOINED=1
ONLINE=0
STATE=${WEX_GPO_STATE:-/var/lib/wineexplorer/gpo-lists}
CACHE=
if ! { command -v net >/dev/null 2>&1 && command -v samba-gpupdate >/dev/null 2>&1; }; then
    echo "wex-gpupdate: Samba's tools are not installed; local policy only"; JOINED=0
else
    # Joined is what the machine is set up as (a realm and a keytab: the detection of wex-domain-state.sh, no
    # network); whether the domain answers now is another matter. A joined machine whose controller is out of reach
    # (a laptop on a train) keeps the policy it has applied, as Windows does: it must not be cleared because the
    # question could not be asked.
    wex_domain_state="${WEX_DOMAIN_STATE_HELPER:-/usr/local/bin/wex-domain-state.sh}"
    [ -e "$wex_domain_state" ] || wex_domain_state="$(dirname -- "$(readlink -f -- "$0")")/wex-domain-state.sh"
    if [ ! -e "$wex_domain_state" ] && command -v wex-domain-state.sh >/dev/null 2>&1; then
        wex_domain_state="$(command -v wex-domain-state.sh)"
    fi
    kind=$({ [ -x "$wex_domain_state" ] && "$wex_domain_state" --kind 2>/dev/null; } || { [ -f "$wex_domain_state" ] && sh "$wex_domain_state" --kind 2>/dev/null; })
    if [ -n "$kind" ] && [ "$kind" != winbind ]; then
        # Winbind is the supported join (docs/DOMAIN_LOGON.md, "The supported join"): Group Policy is built on Samba's tools
        echo "wex-gpupdate: this machine is joined with $kind; the domain's policy needs a Winbind join (wex-domain-join.sh): local policy only"; JOINED=0
    elif { [ -x "$wex_domain_state" ] && "$wex_domain_state" --joined 2>/dev/null; } || \
       { [ -f "$wex_domain_state" ] && sh "$wex_domain_state" --joined 2>/dev/null; }; then
        REALM=$(testparm -s --parameter-name=realm 2>/dev/null | tr 'a-z' 'A-Z')
        CACHE=$(testparm -s --parameter-name='cache directory' 2>/dev/null)/gpo_cache/$REALM/POLICIES
        if net ads testjoin >/dev/null 2>&1; then
            ONLINE=1
            # the fetch is the costly part; runs that follow each other closely (a user's gpupdate, a logon) share it
            if [ ! -e "$STATE/fetched" ] || [ -n "$(find "$STATE/fetched" -mmin +1 2>/dev/null)" ]; then
                samba-gpupdate --force >/dev/null 2>&1 || echo "wex-gpupdate: samba-gpupdate failed; the cache may be old" >&2
                install -d -m 0700 "$STATE" && : > "$STATE/fetched"
            fi
        else
            echo "wex-gpupdate: the domain does not answer; the policy applied before stays"
        fi
    else
        echo "wex-gpupdate: not joined to a domain; local policy only"; JOINED=0
    fi
fi

# the local object's file for MACHINE or USER policy: a regular file, not a link, not empty
local_file()
{
    local f="$LOCAL_GPO/$1/Registry.pol"
    [ -f "$f" ] && [ ! -L "$f" ] && [ -s "$f" ] && echo "$f"
}

# The local objects of the users (Windows Vista and later, kept here as there): GroupPolicyUsers/<SID>/User/Registry.pol
# for the Administrators (S-1-5-32-544), the Non-Administrators (S-1-5-32-545) and single users, beside GroupPolicy.
# A person gets, in this order, the local object's User policy, the policy of Administrators or of Non-Administrators
# (whichever the person is), the person's own, and then the domain's; the last wins. Same rules for the file as above.
LOCAL_USERS=$MACHINE_ROOT/drive_c/windows/system32/GroupPolicyUsers
# a folder name that is a SID of these forms and nothing else (the grammar of the store's C code, gps_target_valid: digits
# and dashes, four subauthorities after S-1-5-21, no leading zeros, none over 4294967295)
target_sid_valid()
{
    local part rest
    case $1 in S-1-5-32-544|S-1-5-32-545) return 0 ;; S-1-5-21-*) ;; *) return 1 ;; esac
    rest=${1#S-1-5-21-}
    [[ $rest =~ ^(0|[1-9][0-9]{0,9})(-(0|[1-9][0-9]{0,9})){3}$ ]] || return 1
    for part in ${rest//-/ }; do [ "$part" -le 4294967295 ] || return 1; done
}
local_users_file()
{
    local f="$LOCAL_USERS/$1/User/Registry.pol"
    target_sid_valid "$1" || return 1
    [ -f "$f" ] && [ ! -L "$f" ] && [ -s "$f" ] && echo "$f"
}

# is the account an administrator, by the rule of the system tier (wex-is-admin)? Not known counts as no: a restriction
# meant for the Non-Administrators then holds
user_is_admin()
{
    local helper=${WEX_IS_ADMIN:-}
    [ -n "$helper" ] || helper=$(command -v wex-is-admin 2>/dev/null) || helper=/usr/local/bin/wex-is-admin
    [ -x "$helper" ] && "$helper" "$1" 2>/dev/null
}

# the local object's security template (the account lockout policy and the security options: where Windows' local
# Group Policy keeps them, GroupPolicy\Machine\Microsoft\Windows NT\SecEdit\GptTmpl.inf), under the same rules
local_template()
{
    local f="$LOCAL_GPO/Machine/Microsoft/Windows NT/SecEdit/GptTmpl.inf"
    [ -f "$f" ] && [ ! -L "$f" ] && [ -s "$f" ] && echo "$f"
}

# the line of the local object for the list of applied objects (wexpolicy record): the name LocalGPO, display name
# "Local Group Policy", link "Local", no directory path; $2 is its extensions (what it has: registry policy, a
# security template)
local_entry()
{
    printf 'LocalGPO\t0\t0\t0\tLocal Group Policy\tC:\\windows\\system32\\GroupPolicy\\%s\t\tLocal\t%s\n' "$1" "$2"
}

# the same for one of the users' local objects ($1 the SID of its folder, $2 the name shown after "Local Group
# Policy\", $3 its extensions): the name LocalGPO-SID keeps it apart from the others in the list
local_user_entry()
{
    printf 'LocalGPO-%s\t0\t0\t0\tLocal Group Policy\\%s\tC:\\windows\\system32\\GroupPolicyUsers\\%s\\User\t\tLocal\t%s\n' "$1" "$2" "$1" "$3"
}

# does session 0 hold an archive of the last policy it wrote for a target ("Machine" or "User-SID")? Without one,
# a target with no files needs no "clear" (a machine that has no policy at all makes no work of a refresh)
has_archive() { [ -e "$LOCAL_GPO/Archive/$1.pol" ]; }

# The objects that apply to an account ($1), in the order Samba applies them, as the lines that
# "wexpolicy record" takes: GUID, version, link type, options, display name, file system path, DS path, link and
# the extensions of $2 (MACHINE or USER), separated by tabs. Samba's dump_gpo prints them on the error stream,
# one block per object starting with "name:"; the extension details below it are indented and skipped.
gpo_parse()
{
    awk -v kind="$1" '
        function val(s) { sub(/^[a-z_]+:[ \t]*/, "", s); gsub(/\t/, " ", s); return s }
        function first(s) { s = val(s); sub(/[ \t].*/, "", s); return s + 0 }
        function flush() {
            # Windows gives the two directory paths as LDAP URLs
            if (ds != "" && ds !~ /^LDAP:/) ds = "LDAP://" ds
            if (ln != "" && ln !~ /^LDAP:/) ln = "LDAP://" ln
            if (guid ~ /^\{[0-9A-F-]+\}$/)
                printf "%s\t%.0f\t%d\t%d\t%s\t%s\t%s\t%s\t%s\n", guid, ver, lt, opt, dn, fs, ds, ln, ext
            guid = ""
        }
        /^name:/ { flush(); guid = toupper(val($0)); ver = lt = opt = 0; dn = fs = ds = ln = ext = ""; next }
        guid == "" { next }
        /^displayname:/ { dn = val($0) }
        /^version:/ { ver = first($0); if (ver < 0) ver += 4294967296 }
        /^filesyspath:/ { fs = val($0) }
        /^dspath:/ { ds = val($0) }
        /^options:/ { opt = first($0) }
        /^link:/ { ln = val($0) }
        /^link_type:/ { lt = first($0); if (lt < 0 || lt > 4) lt = 0 }
        /^machine_extensions:/ { if (kind == "MACHINE") { ext = val($0); if (ext !~ /^\[/) ext = "" } }
        /^user_extensions:/ { if (kind == "USER") { ext = val($0); if (ext !~ /^\[/) ext = "" } }
        END { flush() }'
}

# The domain's objects for the account $2 (kind $3, MACHINE or USER) into the file $4, in Windows' order; $1 names
# the list in the state folder. When the domain answers, the list is kept there. When it does not (the controller is
# out of reach, or the question failed), the list of the last time it did is used, with the files of Samba's cache:
# an unreachable controller is not the same as "no policy applies". 1 when there is neither: the caller then leaves
# what is applied alone.
domain_list()
{
    local key=$1 name=$2 kind=$3 out=$4 raw
    if [ "$ONLINE" = 1 ] && raw=$(net ads gpo list "$name" -P 2>&1); then
        printf '%s\n' "$raw" | gpo_parse "$kind" > "$out"
        win_order "$out" "$name"
        if install -d -m 0700 "$STATE" && cp "$out" "$STATE/$key.list.new"; then mv -f "$STATE/$key.list.new" "$STATE/$key.list"; fi
        return 0
    fi
    if [ -f "$STATE/$key.list" ]; then
        cp "$STATE/$key.list" "$out"
        echo "wex-gpupdate: $name: the domain's objects are those of the last time it answered"
        return 0
    fi
    : > "$out"
    echo "wex-gpupdate: $name: the domain's objects are not known" >&2
    return 1
}

# Put a list of gpo_parse ($1, rewritten in place) in the order Windows applies it, for the account $2 (MS-GPOL
# 3.2.5.1, "GPO Search", and 3.2.5.2). Samba decides which objects apply (security filtering, blocked
# inheritance, disabled links) but keeps each container's links in gPLink order, so the last link wins; on
# Windows the first link (link order 1) wins: each container's non-enforced links are put in front of the list in
# gPLink order, containers from the account's own up to the domain and the site, and enforced links follow at
# the end. Samba's order stays when the directory cannot be read.
win_order()
{
    local dn som soms=() t
    dn=$(net ads search -P "(sAMAccountName=$2)" distinguishedName 2>/dev/null | sed -n 's/^distinguishedName: //p' | head -n 1)
    [ -n "$dn" ] || return 0
    # the containers above the account that can have links: its OUs, nearest first (other containers, such as
    # CN=Computers, are passed over), then the domain; then any site that Samba listed
    som=${dn#*,}
    while [ -n "$som" ]; do
        case ${som:0:3} in
            OU=|ou=|Ou=|oU=) soms+=("$som") ;;
            DC=|dc=|Dc=|dC=) soms+=("$som"); break ;;
        esac
        [ "$som" = "${som#*,}" ] && break
        som=${som#*,}
    done
    while read -r som; do soms+=("$som"); done < <(awk -F '\t' '$3 == 2 { sub(/^LDAP:\/\//, "", $8); print $8 }' "$1" | sort -u)
    t=$(mktemp)
    for som in "${soms[@]}"; do
        printf '%s\t%s\n' "$som" "$(net ads search -P "(distinguishedName=$som)" gPLink 2>/dev/null | sed -n 's/^gPLink: //p' | head -n 1)"
    done > "$t"
    awk -F '\t' -v links="$t" '
        BEGIN {
            # the order of the GUIDs as Windows applies them
            while ((getline rec < links) > 0) {
                split(rec, f, "\t"); s = f[2]
                while (match(s, /\[[^]]*\]/)) {
                    e = substr(s, RSTART + 1, RLENGTH - 2); s = substr(s, RSTART + RLENGTH)
                    o = e; sub(/.*;/, "", o); o = o + 0
                    if (o % 2 == 1 || !match(e, /\{[0-9A-Fa-f-]+\}/)) continue    # disabled link, or no GUID
                    g = toupper(substr(e, RSTART, RLENGTH))
                    if (int(o / 2) % 2 == 1) enf = enf " " g                     # enforced: appended
                    else nonenf = g " " nonenf                                   # not enforced: in front
                }
            }
            n = split(nonenf " " enf, order, " ")
        }
        { line[$1] = $0; seen[NR] = $1 }
        END {
            for (i = 1; i <= n; i++) if (order[i] in line && !(order[i] in done)) { print line[order[i]]; done[order[i]] = 1 }
            # what the directory did not explain stays where Samba put it, at the end
            for (i = 1; i <= NR; i++) if (!(seen[i] in done)) { print line[seen[i]]; done[seen[i]] = 1 }
        }' "$1" > "$t.out" && mv "$t.out" "$1"
    rm -f "$t" "$t.out"
}

# the files of the objects in a list of gpo_list ($1), in order; $2 is MACHINE or USER, $3 the file's path below
# it (Registry.pol, or the security template)
pol_files()
{
    local guid file
    cut -f1 "$1" | while read -r guid; do
        file=$(find "$CACHE/$guid/$2" -ipath "$CACHE/$guid/$2/${3:-registry.pol}" -type f 2>/dev/null | head -n 1)
        [ -n "$file" ] && [ -s "$file" ] && echo "$file"
    done
}

# the list of applied objects for GetAppliedGPOList: "/machine" or "/user:SID", the gpo_list file
record()
{
    local copy="$STAGE/$(basename "$2")"
    install -m 0644 "$2" "$copy"
    s0_wexpolicy record "$1" "Z:${copy//\//\\}" || wexpolicy_failed record "$1"
}

# apply the files of one target: "/machine" or "/user:SID"
# Stage a policy file ($1) as $2 for session 0's account to read. The domain's files (Samba's cache, root's) are
# copied by root. The local object's files are in session 0's root, where session 0's account, and whoever it lets
# edit local policy, can replace them: they are read as session 0's account, so that a link put in after the check
# reaches only what that account could read anyway, never a file that only root can.
stage_file()
{
    case $1 in
        "$LOCAL_GPO"/*|"$LOCAL_USERS"/*)
            runuser -u "$S0_USER" -- cat -- "$1" > "$2" 2>/dev/null || { rm -f "$2"; return 1; }
            chmod 0644 "$2" ;;
        *) install -m 0644 "$1" "$2" ;;
    esac
}

apply()
{
    local target=$1 name=$2 n=0 args=() file copy
    shift 2
    for file in "$@"; do
        copy="$STAGE/$name-$((n + 1)).pol"
        if ! stage_file "$file" "$copy"; then echo "wex-gpupdate: $file cannot be read: left out" >&2; continue; fi
        n=$((n + 1))
        args+=("Z:${copy//\//\\}")
    done
    if [ "$n" -gt 0 ]; then s0_wexpolicy apply "$target" "${args[@]}" || wexpolicy_failed apply "$target"
    else s0_wexpolicy clear "$target" >/dev/null || wexpolicy_failed clear "$target"; echo "wexpolicy: $target has no domain policy"; fi
}

HOST=$(hostname -s | tr 'a-z' 'A-Z')
LISTS=$(mktemp -d)
# the domain's objects (only when joined), then the local object in front of them: Windows applies Local, Site,
# Domain, OU in that order and the last wins, so the domain's policy overrides the local one
: > "$LISTS/machine.domain.list"
MACHINE_UNKNOWN=0
if [ "$JOINED" = 1 ] && [ -z "$ONLY_SID" ]; then
    domain_list machine "$HOST\$" MACHINE "$LISTS/machine.domain.list" || MACHINE_UNKNOWN=1
fi
MACHINE_FILES=()
LOCAL_MACHINE=$(local_file Machine)
[ -z "$LOCAL_MACHINE" ] || MACHINE_FILES+=("$LOCAL_MACHINE")
mapfile -t DOMAIN_FILES < <(pol_files "$LISTS/machine.domain.list" MACHINE)
MACHINE_FILES+=("${DOMAIN_FILES[@]}")
LOCAL_TEMPLATE=$(local_template)
LOCAL_EXTS=; [ -z "$LOCAL_MACHINE" ] || LOCAL_EXTS=$REG_EXT; [ -z "$LOCAL_TEMPLATE" ] || LOCAL_EXTS=$LOCAL_EXTS$SEC_EXT
{ [ -z "$LOCAL_EXTS" ] || local_entry Machine "$LOCAL_EXTS"; cat "$LISTS/machine.domain.list"; } > "$LISTS/machine.list"
# the security templates: the account lockout policy and the security options (wexpolicy's sectmpl.c); the local
# object's first, the domain's after it (the last one applied wins where they set the same value)
TEMPLATES=()
[ -z "$LOCAL_TEMPLATE" ] || TEMPLATES+=("$LOCAL_TEMPLATE")
mapfile -t DOMAIN_TEMPLATES < <(pol_files "$LISTS/machine.domain.list" MACHINE 'microsoft/windows nt/secedit/gpttmpl.inf')
TEMPLATES+=("${DOMAIN_TEMPLATES[@]}")

if [ "$PRINT" = 1 ]; then
    sed 's/^/object machine /' "$LISTS/machine.list"
    for f in "${MACHINE_FILES[@]}"; do echo "machine $f"; done
    for f in "${TEMPLATES[@]}"; do echo "security $f"; done
else
    rm -rf "$STAGE"
    install -d -m 0755 "$STAGE"
    if [ -z "$ONLY_SID" ]; then   # (a logon run is the user's alone)
    # a machine with no policy at all (not joined, no local file or template, nothing written before) has nothing to do
    if [ "$MACHINE_UNKNOWN" = 1 ] && has_archive Machine; then
        echo "wex-gpupdate: the domain's computer policy is not known: what was applied stays"
    elif [ "${#MACHINE_FILES[@]}" -gt 0 ] || [ -n "$LOCAL_TEMPLATE" ] || [ "$JOINED" = 1 ] || has_archive Machine; then
        apply /machine machine "${MACHINE_FILES[@]}"
        record /machine "$LISTS/machine.list"
    fi
    # security settings stay when an object no longer sets them, as on Windows: no template, nothing to do
    args=(); n=0
    for f in "${TEMPLATES[@]}"; do
        if stage_file "$f" "$STAGE/security-$((n + 1)).inf"; then
            n=$((n + 1)); args+=("Z:${STAGE//\//\\}\\security-$n.inf")
        else
            echo "wex-gpupdate: $f cannot be read: left out" >&2
        fi
    done
    [ "${#args[@]}" -eq 0 ] || s0_wexpolicy security /machine "${args[@]}" || wexpolicy_failed security
    fi
fi

# The people of the accounts map (UID 1000 or more): name in column 1, UID in column 2, "active" in column 4, the
# SID's last four parts after the "21" (columns 6 to 9 of a version 1 map, where "21" is column 5; a version 2 map has
# local|domain in column 5, so they are one further on, and the name of a domain record is the Winbind name
# DOMAIN\user, docs/DOMAIN_LOGON.md). Every one of them gets the local object's user policy; a domain user also gets
# the domain's, after it.
[ -r "$MAP" ] || { [ ! -e "$LISTS/failed" ]; exit; }
LOCAL_USER=$(local_file User)
awk 'NR == 1 { v = $2; next }
     $4 == "active" && $2 >= 1000 && $2 != 65534 {
         o = (v == 2) ? 1 : 0
         print $1, (v == 2 ? $5 : "unknown"), "S-1-5-21-" $(6 + o) "-" $(7 + o) "-" $(8 + o) "-" $(9 + o) }' \
    "$MAP" | while read -r name kind sid; do
    # a logon run is for one user (ONLY_SID)
    [ -z "$ONLY_SID" ] || [ "$sid" = "$ONLY_SID" ] || continue
    # only domain users have policy objects of the domain; a local account is not known to the domain ("net ads
    # gpo list" does not fail for an unknown name, so Winbind is asked)
    # (a version 2 map says it: "domain" is the answer of the join and of the logon that recorded the account, which
    # Winbind may not be able to repeat with the controller out of reach)
    domain_user=0
    if [ "$JOINED" = 1 ] && [ "$kind" = domain ]; then
        domain_user=1
    elif [ "$JOINED" = 1 ] && [ "$kind" != local ] && wbinfo --name-to-sid "$name" 2>/dev/null | grep -q 'SID_USER'; then
        domain_user=1
    fi
    # the directory knows the account by its plain name (sAMAccountName); a name that could be taken for an option or
    # a filter is not asked for
    acct=${name##*\\}
    case $acct in -*|*[!A-Za-z0-9._\ \$-]*) echo "wex-gpupdate: $acct: not a name the directory is asked for" >&2; domain_user=0 ;; esac
    : > "$LISTS/user-$sid.domain.list"
    USER_UNKNOWN=0
    if [ "$domain_user" = 1 ]; then
        domain_list "user-$sid" "$acct" USER "$LISTS/user-$sid.domain.list" || USER_UNKNOWN=1
    fi
    USER_FILES=()
    [ -z "$LOCAL_USER" ] || USER_FILES+=("$LOCAL_USER")
    if user_is_admin "$name"; then layer=S-1-5-32-544; else layer=S-1-5-32-545; fi
    LAYER_FILE=$(local_users_file "$layer"); PERSON_FILE=$(local_users_file "$sid")
    [ -z "$LAYER_FILE" ] || USER_FILES+=("$LAYER_FILE")
    [ -z "$PERSON_FILE" ] || USER_FILES+=("$PERSON_FILE")
    mapfile -t DOMAIN_USER_FILES < <(pol_files "$LISTS/user-$sid.domain.list" USER)
    USER_FILES+=("${DOMAIN_USER_FILES[@]}")
    # the applied list names each local object that has policy for the person, in the order applied
    {
        [ -z "$LOCAL_USER" ] || local_entry User "$REG_EXT"
        if [ -n "$LAYER_FILE" ]; then
            if [ "$layer" = S-1-5-32-544 ]; then lname=Administrators; else lname=Non-Administrators; fi
            local_user_entry "$layer" "$lname" "$REG_EXT"
        fi
        [ -z "$PERSON_FILE" ] || local_user_entry "$sid" "$acct" "$REG_EXT"
        cat "$LISTS/user-$sid.domain.list"
    } > "$LISTS/user-$sid.list"
    if [ "$PRINT" = 1 ]; then
        sed "s/^/object user $acct /" "$LISTS/user-$sid.list"
        for f in "${USER_FILES[@]}"; do echo "user $acct $f"; done
    elif [ "$USER_UNKNOWN" = 1 ] && has_archive "User-$sid"; then
        echo "wex-gpupdate: $acct: the domain's user policy is not known: what was applied stays"
    elif [ "${#USER_FILES[@]}" -gt 0 ] || [ "$domain_user" = 1 ] || has_archive "User-$sid"; then
        apply "/user:$sid" "user-$sid" "${USER_FILES[@]}"
        record "/user:$sid" "$LISTS/user-$sid.list"
    fi
done
[ ! -e "$LISTS/failed" ]; exit
