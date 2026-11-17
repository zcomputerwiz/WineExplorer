#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root-only, disposable fixture: installer/scripts/wex-gpupdate.sh's users of the accounts map (docs/DOMAIN_LOGON.md,
# WO 4) with a fake net, wbinfo, testparm and wine: which accounts are asked for policy, with which name Samba is asked,
# and the SID that policy is written under ("wexpolicy apply /user:SID"), for a version 1 and a version 2 map.
# Needs nothing from the host but runuser and bash; writes only under a temporary directory and /run/wineexplorer-policy.
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo 'FAIL: run as root' >&2; exit 1; }
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="$HERE/../installer/scripts/wex-gpupdate.sh"
FX="$(mktemp -d)"
chmod 755 "$FX"   # the fake loader runs as session 0's account (nobody) and has to reach its files
trap 'rm -rf -- "$FX"; rm -rf /run/wineexplorer-policy' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

GUID='{AAAAAAAA-0000-0000-0000-000000000001}'
POL="$FX/cache/gpo_cache/AD.WEXLAB.EXAMPLE/POLICIES/$GUID/USER"   # Samba's cache names the folder in capitals
mkdir -p "$POL" "$FX/bin"; touch "$FX/keytab"
printf 'PReg' > "$POL/Registry.pol"

cat > "$FX/bin/testparm" <<EOF
#!/bin/sh
# the file "unjoined" in the fixture: no realm, so no join
case "\$*" in
    *realm*) [ -e "$FX/unjoined" ] || echo ad.wexlab.example ;;
    *security*) echo ADS ;;
    *workgroup*) echo WEXLAB ;;
    *cache*) echo "$FX/cache" ;;
esac
EOF
cat > "$FX/bin/samba-gpupdate" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$FX/bin/net" <<EOF
#!/bin/sh
# net ads testjoin | net ads gpo list NAME -P | net ads search ...
case "\$2" in
    testjoin) [ -e "$FX/unjoined" ] && exit 1; [ -e "$FX/offline" ] && exit 1; exit 0 ;;
    gpo) [ -e "$FX/offline" ] && exit 1; echo "gpo list \$4" >> "$FX/net.log"
         case "\$4" in
             *\\$) printf 'name:\t%s\ndisplayname:\tWexLab Domain\nversion:\t1\nfilesyspath:\t/x\nlink_type:\t0\nmachine_extensions:\t[{35378EAC-683F-11D2-A89A-00C04FBBCFA2}]\n' '$GUID' >&2 ;;
             wexuser1|bob) printf 'name:\t%s\ndisplayname:\tWexLab Domain\nversion:\t1\nfilesyspath:\t/x\nlink_type:\t0\nuser_extensions:\t[{35378EAC-683F-11D2-A89A-00C04FBBCFA2}]\n' '$GUID' >&2 ;;
         esac ;;
    search) exit 0 ;;
esac
exit 0
EOF
cat > "$FX/bin/wbinfo" <<'EOF'
#!/bin/sh
# only these are users of the domain
case "$2" in
    'WEXLAB\wexuser1'|bob) echo "S-1-5-21-111-222-333-1104 SID_USER (1)"; exit 0 ;;
esac
exit 1
EOF
cat > "$FX/wine" <<'EOF'
#!/bin/sh
# the fake loader: what wexpolicy was asked. wexpolicy.log: "COMMAND TARGET"; detail.log: the contents of the staged
# files in the order given ("apply TARGET [first] [second]") or the names of a recorded list ("record TARGET <id,name;...>")
D=$(dirname "$0")
[ "$1" = wexpolicy ] || exit 0
shift
echo "$1 $2" >> "$D/wexpolicy.log"
cmd=$1; target=$2; shift 2
out="$cmd $target"
case $cmd in
    apply|security) for a in "$@"; do f=$(printf '%s' "${a#Z:}" | tr '\134' '/'); out="$out [$(cat "$f" | tr '\n' ' ')]"; done ;;
    record) f=$(printf '%s' "${1#Z:}" | tr '\134' '/'); out="$out <$(cut -f1,5 "$f" | tr '\t\n' ',;')>"
            echo "$target $(cut -f9 "$f" | tr '\n' ';')" >> "$D/ext.log" ;;
esac
printf '%s\n' "$out" >> "$D/detail.log"
exit 0
EOF
chmod 0755 "$FX"/bin/* "$FX/wine"
# runuser and the script's own tools come from the fakes first
run_gpupdate()
{
    : > "$FX/wexpolicy.log"; : > "$FX/net.log"; : > "$FX/detail.log"; : > "$FX/ext.log"
    # session 0's account is unprivileged, as in use: the fake loader runs as it, so its logs are open to it
    chmod 666 "$FX/wexpolicy.log" "$FX/detail.log" "$FX/ext.log"
    PATH="$FX/bin:$PATH" WEX_KEYTAB="$FX/keytab" WEX_GPO_STATE="$FX/state" WEX_GPO_LOCK="$FX/lock" WEX_ACCOUNTS_MAP="$1" WINE="$FX/wine" WEX_SESSION0_USER=nobody WEX_MACHINE_ROOT="$FX/mroot" \
        bash "$SCRIPT" > "$FX/out.log" 2>&1 ||
        { cat "$FX/out.log"; fail "wex-gpupdate.sh failed on $1"; }
}

# version 1: "21" is column 5 and the SID parts are columns 6 to 9; a local account is told apart by Winbind
cat > "$FX/map1" <<'EOF'
WEXACCOUNTS 1 3 1002 11 22 33
alice 1001 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa active 21 11 22 33 1001
bob 1002 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb active 21 11 22 33 1002
gone 1003 cccccccccccccccccccccccccccccccc tombstone 21 11 22 33 1003
EOF
run_gpupdate "$FX/map1"
grep -qx 'apply /user:S-1-5-21-11-22-33-1002' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'v1: the user was not applied under the map SID'; }
! grep -q 'alice\|1001\|1003' "$FX/wexpolicy.log" || fail 'v1: a local or removed account got policy'
grep -qx 'gpo list bob' "$FX/net.log" || fail 'v1: Samba was not asked for bob'

# version 2: local|domain in column 5; the domain account's SID is the domain's, asked for by its plain name
cat > "$FX/map2" <<'EOF'
WEXACCOUNTS 2 4 1030 11 22 33 111 222 333 WEXLAB
alice 1001 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa active local 21 11 22 33 1001
bob 1002 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb active local 21 11 22 33 1002
WEXLAB\wexuser1 21104 dddddddddddddddddddddddddddddddd active domain 21 111 222 333 1104
WEXLAB\old 21105 eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee tombstone domain 21 111 222 333 1105
EOF
run_gpupdate "$FX/map2"
grep -qx 'apply /user:S-1-5-21-111-222-333-1104' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'v2: the domain user was not applied under the domain SID'; }
grep -qx 'record /user:S-1-5-21-111-222-333-1104' "$FX/wexpolicy.log" || fail 'v2: the applied list was not recorded under the domain SID'
! grep -q '1002\|1001\|1105\|-11-22-33' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'v2: a local or removed account got policy'; }
grep -qx 'gpo list wexuser1' "$FX/net.log" || { cat "$FX/net.log"; fail 'v2: Samba was not asked for the plain name'; }
! grep -q 'WEXLAB' "$FX/net.log" || fail 'v2: Samba was asked for a name with the domain'
echo 'OK: wex-gpupdate.sh users of a version 1 and a version 2 accounts map'

# ---- local policy (docs/GROUP_POLICY.md, "Local policy"): the local object's Registry.pol files in session 0's root
LG="$FX/mroot/drive_c/windows/system32/GroupPolicy"
mkdir -p "$LG/Machine" "$LG/User" "$LG/Archive" "$FX/cache/gpo_cache/AD.WEXLAB.EXAMPLE/POLICIES/$GUID/MACHINE"
printf 'DOMAINU' > "$POL/Registry.pol"
printf 'DOMAINM' > "$FX/cache/gpo_cache/AD.WEXLAB.EXAMPLE/POLICIES/$GUID/MACHINE/Registry.pol"
detail() { grep -qxF "$1" "$FX/detail.log" || { echo "--- detail.log:"; cat "$FX/detail.log"; echo "--- net.log:"; cat "$FX/net.log"; fail "$2 (wanted: $1)"; }; }
cat > "$FX/map3" <<'EOF'
WEXACCOUNTS 2 4 1030 11 22 33 111 222 333 WEXLAB
daemon 1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1 active local 21 11 22 33 1000
alice 1001 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa active local 21 11 22 33 1001
bob 1002 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb active local 21 11 22 33 1002
gone 1003 cccccccccccccccccccccccccccccccc tombstone local 21 11 22 33 1003
WEXLAB\wexuser1 21104 dddddddddddddddddddddddddddddddd active domain 21 111 222 333 1104
EOF
# a machine that is not joined, with local policy files: they apply to the computer and to every person, Samba is not asked
touch "$FX/unjoined"
printf 'LOCALM' > "$LG/Machine/Registry.pol"
printf 'LOCALU' > "$LG/User/Registry.pol"
run_gpupdate "$FX/map3"
detail 'apply /machine [LOCALM]' 'local: the computer'
detail 'record /machine <LocalGPO,Local Group Policy;>' 'local: the computer'"'"'s applied list'
for sid in S-1-5-21-11-22-33-1001 S-1-5-21-11-22-33-1002 S-1-5-21-111-222-333-1104; do
    detail "apply /user:$sid [LOCALU]" "local: the user policy of $sid"
    detail "record /user:$sid <LocalGPO,Local Group Policy;>" "local: the applied list of $sid"
done
! grep -q 'daemon\|-1000\|-1003' "$FX/detail.log" || fail 'local: a system account or a removed account got policy'
[ ! -s "$FX/net.log" ] || fail 'local: Samba was asked on a machine that is not joined'
# joined: the local object first, the domain's after it (the domain's overrides it), for the computer and for a domain user
rm -f "$FX/unjoined"
run_gpupdate "$FX/map3"
detail 'apply /machine [LOCALM] [DOMAINM]' 'joined: the local object first, then the domain'"'"'s, for the computer'
detail 'record /machine <LocalGPO,Local Group Policy;'"$GUID"',WexLab Domain;>' 'joined: the computer'"'"'s applied list'
detail 'apply /user:S-1-5-21-111-222-333-1104 [LOCALU] [DOMAINU]' 'joined: a domain user: local, then domain'
detail 'apply /user:S-1-5-21-11-22-33-1002 [LOCALU]' 'joined: a local user keeps the local policy only'
# the local security template (the lockout and the security options): where Windows' local Group Policy keeps it; the
# local object's first, the domain's after it, and the object's list of applied objects names the security extension
SECDIR="$LG/Machine/Microsoft/Windows NT/SecEdit"
mkdir -p "$SECDIR" "$FX/cache/gpo_cache/AD.WEXLAB.EXAMPLE/POLICIES/$GUID/MACHINE/microsoft/windows nt/secedit"
printf 'LOCALT' > "$SECDIR/GptTmpl.inf"
printf 'DOMAINT' > "$FX/cache/gpo_cache/AD.WEXLAB.EXAMPLE/POLICIES/$GUID/MACHINE/microsoft/windows nt/secedit/gpttmpl.inf"
SECX='{827D319E-6EAC-11D2-A4EA-00C04F79F83A}'
# not joined, a template and no registry file: the template applies and the object is listed for the security extension only
touch "$FX/unjoined"; rm -f "$LG/Machine/Registry.pol" "$LG/User/Registry.pol"
run_gpupdate "$FX/map3"
detail 'security /machine [LOCALT]' 'template: the local template, not joined'
grep -q "^/machine \[$SECX{803E14A0-B4FB-11D0-A0D0-00A0C90F574B}\];\$" "$FX/ext.log" || { cat "$FX/ext.log"; fail 'template: the local object was not listed for the security extension only'; }
# joined: the local template first, the domain's after it
rm -f "$FX/unjoined"
run_gpupdate "$FX/map3"
detail 'security /machine [LOCALT] [DOMAINT]' 'template: the local template first, then the domain'"'"'s'
rm -f "$SECDIR/GptTmpl.inf"
# a link in place of the template is not followed
ln -s /etc/hostname "$SECDIR/GptTmpl.inf"
touch "$FX/unjoined"
run_gpupdate "$FX/map3"
[ -z "$(grep -v '^refreshed' "$FX/wexpolicy.log")" ] || { cat "$FX/wexpolicy.log"; fail 'template: a link was taken for the template'; }
rm -f "$SECDIR/GptTmpl.inf"
# nothing to do: not joined, no local files, nothing written before: no call at all
touch "$FX/unjoined"; rm -f "$LG/Machine/Registry.pol" "$LG/User/Registry.pol"
run_gpupdate "$FX/map3"
[ -z "$(grep -v "^refreshed" "$FX/wexpolicy.log")" ] || { cat "$FX/wexpolicy.log"; fail 'no policy: the writer was called'; }
# a policy that was removed: the target had an archive, so it is cleared (the registry policy goes away)
: > "$LG/Archive/Machine.pol"; : > "$LG/Archive/User-S-1-5-21-11-22-33-1001.pol"
run_gpupdate "$FX/map3"
grep -qx 'clear /machine' "$FX/wexpolicy.log" && grep -qx 'clear /user:S-1-5-21-11-22-33-1001' "$FX/wexpolicy.log" ||
    { cat "$FX/wexpolicy.log"; fail 'removed policy: the targets with an archive were not cleared'; }
[ "$(grep -vc "^refreshed" "$FX/wexpolicy.log")" = 4 ] || { cat "$FX/wexpolicy.log"; fail 'removed policy: more than the two targets and their lists were touched'; }
# a link in place of the file is not followed
rm -f "$LG/Archive/"*.pol; ln -s /etc/hostname "$LG/Machine/Registry.pol"
run_gpupdate "$FX/map3"
[ -z "$(grep -v "^refreshed" "$FX/wexpolicy.log")" ] || { cat "$FX/wexpolicy.log"; fail 'a link was taken for the local file'; }
# Local files are read as session 0's account: a file that only root can read (what a link swapped in after the check
# would reach) is not published to it, and not used; the run goes on without it
rm -f "$LG/Machine/Registry.pol" "$SECDIR/GptTmpl.inf"
printf 'SECRET' > "$LG/Machine/Registry.pol"; chmod 0600 "$LG/Machine/Registry.pol"
printf 'SECRET' > "$SECDIR/GptTmpl.inf"; chmod 0600 "$SECDIR/GptTmpl.inf"
touch "$FX/unjoined"
run_gpupdate "$FX/map3"
! grep -rq SECRET /run/wineexplorer-policy "$FX/detail.log" || fail 'a file only root can read was published to session 0'"'"'s account'
grep -q 'cannot be read: left out' "$FX/out.log" || { cat "$FX/out.log"; fail 'an unreadable local file was not reported'; }
grep -qx 'clear /machine' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'an unreadable local file was not left out (no clear without it)'; }
! grep -q '^security' "$FX/wexpolicy.log" || fail 'an unreadable template was applied'
rm -f "$LG/Machine/Registry.pol" "$SECDIR/GptTmpl.inf"
echo 'OK: wex-gpupdate.sh local policy (not joined, joined and in order, a template, nothing to do, removed, a link, an unreadable file)'
# ---- the local objects of the users: GroupPolicyUsers/<SID>/User/Registry.pol for the Administrators (S-1-5-32-544), the
# Non-Administrators (S-1-5-32-545) and single people. Order for a person: the local User policy, the Administrators' or the
# Non-Administrators' (wex-is-admin says which), the person's own, then the domain's. Here alice is the only administrator.
cat > "$FX/bin/wex-is-admin" <<'EOF2'
#!/bin/sh
[ "$1" = alice ]
EOF2
chmod 0755 "$FX/bin/wex-is-admin"
GU="$FX/mroot/drive_c/windows/system32/GroupPolicyUsers"
mkdir -p "$GU/S-1-5-32-544/User" "$GU/S-1-5-32-545/User" "$GU/S-1-5-21-11-22-33-1001/User" "$GU/S-1-5-21-11-22-33-1002/User"
printf 'ADMINS' > "$GU/S-1-5-32-544/User/Registry.pol"
printf 'NONADMINS' > "$GU/S-1-5-32-545/User/Registry.pol"
printf 'ALICE' > "$GU/S-1-5-21-11-22-33-1001/User/Registry.pol"
printf 'LOCALU' > "$LG/User/Registry.pol"
touch "$FX/unjoined"
run_gpupdate "$FX/map3"
detail 'apply /user:S-1-5-21-11-22-33-1001 [LOCALU] [ADMINS] [ALICE]' 'users: an administrator gets the local User policy, the Administrators'"'"' and her own'
detail 'apply /user:S-1-5-21-11-22-33-1002 [LOCALU] [NONADMINS]' 'users: a standard user gets the Non-Administrators'"'"''
detail 'apply /user:S-1-5-21-111-222-333-1104 [LOCALU] [NONADMINS]' 'users: a domain user on an unjoined machine gets the local ones too'
detail 'record /user:S-1-5-21-11-22-33-1001 <LocalGPO,Local Group Policy;LocalGPO-S-1-5-32-544,Local Group Policy\Administrators;LocalGPO-S-1-5-21-11-22-33-1001,Local Group Policy\alice;>' 'users: the applied list names each local object, in the order applied'
detail 'record /user:S-1-5-21-11-22-33-1002 <LocalGPO,Local Group Policy;LocalGPO-S-1-5-32-545,Local Group Policy\Non-Administrators;>' 'users: a standard user lists the Non-Administrators object'
# joined: the domain's policy comes after all of them
rm -f "$FX/unjoined"
run_gpupdate "$FX/map3"
detail 'apply /user:S-1-5-21-111-222-333-1104 [LOCALU] [NONADMINS] [DOMAINU]' 'users: the domain'"'"'s user policy after the local objects'
# only a person's own file, no local User policy: it applies, and the object is listed
touch "$FX/unjoined"; rm -f "$LG/User/Registry.pol" "$GU/S-1-5-32-544/User/Registry.pol" "$GU/S-1-5-32-545/User/Registry.pol"
printf 'BOB' > "$GU/S-1-5-21-11-22-33-1002/User/Registry.pol"
run_gpupdate "$FX/map3"
detail 'apply /user:S-1-5-21-11-22-33-1002 [BOB]' 'users: a person'"'"'s own file alone'
detail 'record /user:S-1-5-21-11-22-33-1002 <LocalGPO-S-1-5-21-11-22-33-1002,Local Group Policy\bob;>' 'users: the person'"'"'s own file lists its object'
# a link in place of a file is not followed, and a name that is not a SID is not looked at
rm -f "$GU/S-1-5-21-11-22-33-1002/User/Registry.pol"; ln -s /etc/hostname "$GU/S-1-5-21-11-22-33-1002/User/Registry.pol"
mkdir -p "$GU/bob/User"; printf 'BYNAME' > "$GU/bob/User/Registry.pol"
run_gpupdate "$FX/map3"
! grep -q '^apply /user:S-1-5-21-11-22-33-1002' "$FX/wexpolicy.log" || { cat "$FX/detail.log"; fail 'users: a link, or a folder named for the account, was taken for the person'"'"'s policy'; }
# a SID that is not in the grammar of the store (a subauthority over 4294967295) names no object, whatever the folder holds
mkdir -p "$GU/S-1-5-21-11-22-33-99999999999/User"; printf 'CAROL' > "$GU/S-1-5-21-11-22-33-99999999999/User/Registry.pol"
cat > "$FX/map4" <<'EOF2'
WEXACCOUNTS 2 4 1030 11 22 33 111 222 333 WEXLAB
carol 1003 cccccccccccccccccccccccccccccccc active local 21 11 22 33 99999999999
EOF2
run_gpupdate "$FX/map4"
! grep -q CAROL "$FX/detail.log" || { cat "$FX/detail.log"; fail 'users: a folder named for an invalid SID was taken for a person'"'"'s policy'; }
rm -rf "$GU" "$FX/bin/wex-is-admin" "$LG/User/Registry.pol" "$LG/Machine/Registry.pol"
echo 'OK: wex-gpupdate.sh local objects of the users (Administrators, Non-Administrators, one person; the order; a link)'
# ---- the domain out of reach (a joined machine whose controller does not answer): what was applied is not cleared
rm -f "$FX/unjoined" "$LG/Archive/"*.pol "$LG/Machine/Registry.pol" "$LG/User/Registry.pol" "$SECDIR/GptTmpl.inf"
rm -rf "$FX/state"
run_gpupdate "$FX/map3"                       # the domain answers: its lists are kept
[ -f "$FX/state/machine.list" ] && [ -f "$FX/state/user-S-1-5-21-111-222-333-1104.list" ] || { ls "$FX/state"; fail 'offline: the lists were not kept'; }
touch "$FX/offline"
printf 'LOCALM' > "$LG/Machine/Registry.pol"; : > "$LG/Archive/Machine.pol"; : > "$LG/Archive/User-S-1-5-21-111-222-333-1104.pol"
run_gpupdate "$FX/map3"
detail 'apply /machine [LOCALM] [DOMAINM]' 'offline: the last known list is used for the computer'
detail 'apply /user:S-1-5-21-111-222-333-1104 [DOMAINU]' 'offline: the last known list is used for the domain user'
! grep -q '^clear' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'offline: policy was cleared because the domain did not answer'; }
[ ! -s "$FX/net.log" ] || fail 'offline: Samba was asked for lists although the join test failed'
grep -q 'last time it answered' "$FX/out.log" || { cat "$FX/out.log"; fail 'offline: the use of the old list was not said'; }
# no list known (the controller was never reached) and something applied before: nothing is touched
rm -rf "$FX/state"
run_gpupdate "$FX/map3"
[ -z "$(grep -v '^refreshed' "$FX/wexpolicy.log" | grep -v '^security\|^apply /user:S-1-5-21-11-22-33\|^record /user:S-1-5-21-11-22-33')" ] || { cat "$FX/wexpolicy.log"; fail 'offline: with no known list the applied policy was touched'; }
grep -q 'what was applied stays' "$FX/out.log" || { cat "$FX/out.log"; fail 'offline: the kept policy was not said'; }
rm -f "$FX/offline" "$LG/Machine/Registry.pol" "$LG/Archive/"*.pol
# a failed writer is not lost: the run says so and ends with 1
cp "$FX/wine" "$FX/wine.ok"
printf '#!/bin/sh\nexit 3\n' > "$FX/wine"; chmod 0755 "$FX/wine"
printf 'LOCALM' > "$LG/Machine/Registry.pol"
if PATH="$FX/bin:$PATH" WEX_KEYTAB="$FX/keytab" WEX_GPO_STATE="$FX/state" WEX_GPO_LOCK="$FX/lock" WEX_ACCOUNTS_MAP="$FX/map3" WINE="$FX/wine" WEX_SESSION0_USER=nobody WEX_MACHINE_ROOT="$FX/mroot" bash "$SCRIPT" > "$FX/out.log" 2>&1; then
    cat "$FX/out.log"; fail 'a failed wexpolicy did not make the run fail'
fi
grep -q 'wexpolicy apply /machine failed' "$FX/out.log" || { cat "$FX/out.log"; fail 'a failed wexpolicy was not said'; }
mv "$FX/wine.ok" "$FX/wine"; rm -f "$LG/Machine/Registry.pol"
# a refresh asked for during a run is not lost: the run tells which request it served (the number it read when it began)
# and, finding a newer one, runs again; a run with no request file says no number
rm -f "$FX/mroot/drive_c/windows/system32/GroupPolicy/Refresh"
run_gpupdate "$FX/map3"
grep -qx 'refreshed 0' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'a run with no request file did not serve 0'; }
cp "$FX/wine" "$FX/wine.ok"
cat > "$FX/wine" <<'EOF2'
#!/bin/sh
# the first "refreshed 5" is the moment a second request (6) comes in
D=$(dirname "$0")
if [ "$1" = wexpolicy ] && [ "$2" = refreshed ]; then
    echo "refreshed $3" >> "$D/refreshed.log"
    [ "$3" = 5 ] && printf '6\n' > "$D/mroot/drive_c/windows/system32/GroupPolicy/Refresh"
    exit 0
fi
exec "$D/wine.ok" "$@"
EOF2
chmod 0755 "$FX/wine"; : > "$FX/refreshed.log"; chmod 666 "$FX/refreshed.log"
printf '5\n' > "$LG/Refresh"; chmod 666 "$LG/Refresh"
run_gpupdate "$FX/map3"
[ "$(tr '\n' ' ' < "$FX/refreshed.log")" = 'refreshed 5 refreshed 6 ' ] || { cat "$FX/refreshed.log"; fail 'a request made during the run did not make a second run that served it'; }
mv "$FX/wine.ok" "$FX/wine"; rm -f "$LG/Refresh" "$FX/refreshed.log"
# the same with no request file when the run begins (the first request is 1), and with the number wrapping
for case in "none:0:1" "4294967295:4294967295:0"; do
    start=${case%%:*}; rest=${case#*:}; first=${rest%%:*}; next=${rest#*:}
    cp "$FX/wine" "$FX/wine.ok"
    cat > "$FX/wine" <<'EOF2'
#!/bin/sh
D=$(dirname "$0")
if [ "$1" = wexpolicy ] && [ "$2" = refreshed ]; then
    echo "refreshed $3" >> "$D/refreshed.log"
    [ "$3" = "$(cat "$D/first")" ] && echo "$(cat "$D/next")" > "$D/mroot/drive_c/windows/system32/GroupPolicy/Refresh"
    exit 0
fi
exec "$D/wine.ok" "$@"
EOF2
    chmod 0755 "$FX/wine"; echo "$first" > "$FX/first"; echo "$next" > "$FX/next"
    : > "$FX/refreshed.log"; chmod 666 "$FX/refreshed.log"
    rm -f "$LG/Refresh"
    [ "$start" = none ] || { echo "$start" > "$LG/Refresh"; chmod 666 "$LG/Refresh"; }
    [ "$start" != none ] || { : > "$LG/Refresh"; chmod 666 "$LG/Refresh"; }   # the file is there, empty (read while written)
    run_gpupdate "$FX/map3"
    [ "$(tr '\n' ' ' < "$FX/refreshed.log")" = "refreshed $first refreshed $next " ] || { cat "$FX/refreshed.log"; fail "request $next during a run that began at '$start' was lost"; }
    mv "$FX/wine.ok" "$FX/wine"; rm -f "$LG/Refresh" "$FX/refreshed.log" "$FX/first" "$FX/next"
done
echo 'OK: wex-gpupdate.sh serves the request it began with and runs again for one that came in meanwhile'
echo 'OK: wex-gpupdate.sh with the domain out of reach (the last list, nothing cleared, nothing touched without one) and a failing writer'
