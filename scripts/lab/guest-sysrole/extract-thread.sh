# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
K=/var/tmp/wex-g4-lab-credentials/vm_key
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
# Thread 03dc after the RoGetActivationFactory failure, until its next CreateProcessInternalW, without ntdll noise
$SSH "L=/tmp/gsr-out/sdhold.log; N=9523647; tail -n +\$N \$L | grep -a '^03dc:' | grep -a -v 'ntdll\.\(wcslen\|_wcsnicmp\|RtlInitUnicodeString\|RtlEnterCriticalSection\|RtlLeaveCriticalSection\|RtlAllocateHeap\|RtlFreeHeap\|memcpy\|memset\|RtlSizeHeap\|wcsncmp\|RtlGetLastWin32Error\|RtlSetLastWin32Error\|NtCurrentTeb\)' | grep -a '^03dc:Call' | cut -c1-200 > /tmp/gsr-out/thread-calls.txt; wc -l /tmp/gsr-out/thread-calls.txt; grep -a -n 'CreateProcessInternalW\|ShellExecute' /tmp/gsr-out/thread-calls.txt | head -5; awk 'NR<=120' /tmp/gsr-out/thread-calls.txt | cut -c6-180" 2>&1 | grep -v Warning
