#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Qwen: root in an exclusively owned disposable Linux guest, never the host.
set +x
set -euo pipefail
finish() { status=$?; if [ "$status" -eq 0 ]; then echo 'E1S8_EVIDENCE OK'; else echo 'E1S8_EVIDENCE FAIL'; fi; }
trap finish EXIT
[ "$(id -u)" -eq 0 ] || { echo 'FAIL: root required' >&2; exit 1; }
[ "$#" -eq 1 ] && [ "$1" = --disposable-guest ] || {
    echo 'usage: bash scripts/test_evidence.sh --disposable-guest' >&2; exit 1;
}
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname -- "$SCRIPT_DIR")"
command -v python3 >/dev/null
command -v flock >/dev/null
exec 9</run
flock -n 9 || { echo 'FAIL: /run fixture lock busy' >&2; exit 1; }
FIXTURE="$(mktemp -d /run/wex-evidence-test-XXXXXXXX)"
# No fixed users, sockets, services, PAM configuration or persistent authority.
# No preexisting fixture can be adopted; mktemp creates only our unique tree.
FIXTURE_ID="$(stat -c '%d:%i:%u:%a' "$FIXTURE")"
cleanup()
{
    status=$?
    trap - EXIT
    case "$FIXTURE" in
        /run/wex-evidence-test-????????)
            if [ ! -L "$FIXTURE" ] && [ "$(stat -c '%d:%i:%u:%a' "$FIXTURE")" = "$FIXTURE_ID" ]; then
                rm -rf -- "$FIXTURE" || status=1
            else
                status=1
            fi ;;
        *) status=1 ;;
    esac
    if [ "$status" -eq 0 ]; then echo 'E1S8_EVIDENCE OK'; else echo 'E1S8_EVIDENCE FAIL'; fi
    exit "$status"
}
trap cleanup EXIT
PYTHONDONTWRITEBYTECODE=1 python3 "$REPO/tools/e1s6/test-evidence.py" "$FIXTURE"
