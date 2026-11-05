#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Personalize a copied Wine prefix: replace the template profile name (wexsetup) in the registry hives that the prefix
# owns. Shared by the session first-logon code (wine-explorer-session-common.sh), the installer acceptance test and the
# lab scripts. The registry text has two backslashes per separator (users\\wexsetup\\), so the grep pattern below has
# four backslashes after shell quoting and the sed patterns two; do not "simplify" the escapes.
# usage: personalize-prefix.sh PREFIX USER [TEMPLATE_USER]
set -Eeuo pipefail

usage() { echo "usage: $0 PREFIX USER [TEMPLATE_USER]" >&2; exit 2; }
(( $# == 2 || $# == 3 )) || usage
prefix=$1
user=$2
template=${3:-wexsetup}
[[ $user =~ ^[A-Za-z_][A-Za-z0-9_.-]*$ ]] || { echo "invalid user: $user" >&2; exit 1; }
[[ $template =~ ^[A-Za-z_][A-Za-z0-9_.-]*$ ]] || { echo "invalid template user: $template" >&2; exit 1; }
reg=$prefix/user.reg
[[ -f $reg && ! -L $reg ]] || exit 0

if grep -qs "users\\\\\\\\$template\\\\\\\\" "$reg"; then
    for hive in user.reg userdef.reg system.reg; do
        path=$prefix/$hive
        [[ -f $path && ! -L $path ]] || continue
        sed -i -e 's/\\\\users\\\\'"$template"'\\\\/\\\\users\\\\'"$user"'\\\\/g' \
               -e 's/\\\\users\\\\'"$template"'"/\\\\users\\\\'"$user"'"/g' \
            "$path"
    done
fi
