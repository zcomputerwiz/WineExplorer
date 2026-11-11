<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R2 preparation: native process/token identity observations

Planner/reviewer/setup: Codex. Implementer: Antigravity. Only new parent
`tests/uwp/context-observations/` files are allowed. No runtime edits, changes
to existing identity/u1_0/package-name/storage fixtures, staging or commits.
Read CLAUDE.md and docs/TOOLING.md. No additional delegation. Original GPL
source only, compiled against the installed public SDK; no SDK code import.

This is a measurement tool, not package implementation. Use SDK-declared
Win32 types/functions and typed interfaces. Never guess a GUID/vtable/ABI or
infer a broken host from a malformed probe. Do not call undocumented native
functions in this order. Rtl identity remains a separate verified-ABI task.

## Probe

Create a small self-owned native x64/x86 console executable. Default mode is
unpackaged observation, with an explicit packaged mode usable later via an
AUMID launcher. JSONL includes schema, architecture, context, API, handle kind,
call/length units, status domain/value, initialized input/output sizes and
checked-canary results. Expected-status assertions must be separate from
observed values. Use bounded buffers and correctly escaped strings. Printing
identity is permitted only for our synthetic WineExplorer fixture; refuse
unexpected non-fixture packaged context without logging its values.

Measure GetCurrentPackageFullName/FamilyName/Path and
GetCurrentApplicationUserModelId; GetPackageFullName/FamilyName on pseudo and
opened self-process handles; GetPackageFullNameFromToken and
GetPackageFamilyNameFromToken on the opened primary token and an ordinary
DuplicateTokenEx copy. Check 0/query/one-short/exact/one-extra lengths and
sentinels where meaningful. Record valid own handles with insufficient query
rights, an owned event handle of the wrong type, NULL and a known closed own
handle, with valid argument pointers. Never target unrelated processes/tokens
or use invalid/crash pointers. Record errors rather than guessing precedence.

In an explicit child-observation mode, launch this exact same executable by
ordinary CreateProcessW (no package API, custom environment or token change).
Record whether its identity was inherited when the parent was genuinely
packaged. Use private output in the driver's owned directory, bound child
startup/run/wait and kill only that exact owned child on failure. Observe the
child from the parent's limited-query handle while it waits on a uniquely
owned synchronization object; verify external-handle identity consistency.
No global object names, process kills or environment-based identity tricks.

## Driver and evidence

Preview-default PowerShell build/run driver for installed MSVC/SDK, static CRT,
both ABIs. Actual execution now is limited to unpackaged probes. A future
packaged run will be performed by Codex after safe fixture setup; expose a
documented command rather than installing/registering packages yourself.
No certificates/trust stores, registry/policy, software/tool installation,
WSL/VM/Wine or host Python. Do not claim packaged observations now.

Use the accepted identity driver's fresh direct ignored out-* output policy,
reparse-ancestor refusals and preservation behavior. Bound compilation, run,
asynchronous pipe drain, kill and final wait; every nonzero/timed-out child
fails the driver. Record full OS build/UBR, compiler/SDK, source/executable/
record hashes and exact commands. Retain old evidence, never recursively
delete arbitrary destinations. Provide a validator for record structure,
complete unique cases and internally consistent lengths/status domains;
negative copies must reject truncation/duplicates/altered fields. It must not
turn an unobserved error code into a guessed conformance result.

Deliver exact files, commands, native numeric observations and limitations.
Any ABI or setup uncertainty must be reported, not papered over by stubs.
