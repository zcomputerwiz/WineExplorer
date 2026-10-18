<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# OOBE continuation — 2026-10-18

Active branch: `codex/oobe-components-repair`, continuing `3d70da25`.
Not merge-ready yet. The original work order and independent repair review remain
the acceptance gates. This is a progress record, not an installed-image pass.

## Terms/source audit and implementation

- The actual Debian `winetricks_20250102-1_all.deb` contains a script with SHA256
  `9df4af038f04c753f3b0b02817cf60a51d8eac32357d070c45dfd139b93bd3f5`,
  matching the existing pin. The upstream tag's raw script is different:
  `53194dead910f8a5eb1deacaa4773d4e48f5873633d18ab1ecd6fdb0cb92243b`.
  Do not substitute one for the other or turn off integrity checking.
- Microsoft still publishes the full VC++ 2015-2022 DOCX at the official URL in
  the component metadata. Raw SHA256:
  `f1e3d56ceb2ad68aae0711b910375009e651ac5530fa0760f0dea6e81e54fae1`.
  The old extractor omitted inherited numbered headings, list markers and hyperlink
  targets. The new fail-closed renderer preserves these; its full rendered UTF-8
  snapshot is pinned independently. No vendor text/document is committed.
- Terms caches are now atomic trusted JSON snapshots bound to the raw document,
  reviewed extraction and full component-policy fingerprint. Arbitrary long text,
  changed packages/recipes and tampered caches cannot authorize installation.
  Consent fingerprints incorporate the package policy and exact displayed text;
  they are not secrets and do not by themselves prove a human interaction.
- The production bridge now requires a valid 64-digit digest, exact body length,
  no embedded NUL, complete response and bounded size. The PE worker rejects a
  changed fingerprint between its size query and data read.
- Terms bundling was suggested by the user. It would improve offline review, but
  public availability is not a redistribution grant. Current policy still excludes
  proprietary vendor documents from Git/ISO; do not change this without establishing
  applicable permission. Microsoft's general document-use terms do not supply an
  obvious blanket shipping grant:
  https://www.microsoft.com/en-us/legal/terms-of-use . Retrieval/cache remains the
  supported route while that question is unresolved.

## Human consent and private runtime research

The user explicitly confirmed acceptance of the linked full VC++ Runtime 2015-2022
terms for x86/x64 installation in the new disposable VM in this chat on 2026-10-18.
This authorizes that test, not a production auto-accept switch or bundling payloads.

Following that confirmation, the official VS2022 release redirects were resolved
and the two payloads downloaded **outside Git**, under the root-private WSL lab:

| Package | File/Product version | Bytes | SHA256 |
|---|---|---:|---|
| VC_redist.x86.exe | 14.44.35211.0 | 13,953,392 | `0c09f2611660441084ce0df425c51c11e147e6447963c3690f97e0b25c55ed64` |
| VC_redist.x64.exe | 14.44.35211.0 | 25,635,768 | `cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b` |

The immutable vendor URLs use the prefix
`https://download.visualstudio.microsoft.com/download/pr/990c720a-46ce-4dc7-87b9-c2eb615d34b9/`,
then the uppercase SHA256 and package filename. Observed payload hashes matched.
This does not yet prove installer compatibility, signed-publisher trust or shared
runtime loading. All six production entries remain disabled at this checkpoint.

## Infrastructure and current state

The user authorized cloning/creating a new WSL or VirtualBox VM for acceptance.
An independent full qcow2 clone of the stopped known polish X11 test installation
was created at `/var/tmp/wex-oobe-acceptance-20261018/vm/disk.qcow2` in Debian WSL.
It is running under its own QMP/serial sockets using vmtest.py, 2 CPUs/3 GiB RAM.
Original disks/VMs were not modified or started. This clone is an integration lab,
NOT a fresh install of the new OOBE image; fresh-ISO acceptance is still required.

The owned clone has the existing custom Wine 11.19 runtime. Test administrator
Alice authenticated sudo on its private serial console using the pre-existing
private fixture password file; the password was not printed/logged/committed.
cabextract, winetricks, Xvfb, curl, gcc, make and openssh-server were installed only
inside that clone. Debian winetricks pulled Debian Wine 10 as a dependency;
all product tests must explicitly use `/usr/local/bin/wine` and wineserver, not
accidentally test the Debian runtime. No real proprietary installer has yet run.

The ordinary build-VM SSH endpoint 2223 refuses connections; no existing VM was
started just to recover it. Debian WSL has KVM/QEMU and MinGW, but lacks several
native Wine build dependencies. A combined WoW64 build remains pending.

## Verification and retained failures

- Existing runtime fixtures: 57 discovered, 56 passed, one explicit Wine-execution
  skip in Debian WSL. The new document/cache fixtures and real bridge framing case
  pass; full final totals must be recorded after the continuation is complete.
- Actual OOBE source compiles in both MinGW architectures with the previously
  documented unused-parameter suppression. No full link/build claim yet.
- The first DOCX renderer test failed `ValueError: Unsupported legal list format`.
  Inspection showed alphabetic numbering; subsequent inspection also exposed
  inherited heading numbering. The renderer was extended instead of stripping it.
- Fetching the official VS2022 channel's catalog failed SHA256 verification:
  declared `a871a5ff41acebbe5037455fd6f51fcd4907b47d8f3c9860871ea0b222a2fd45`,
  30,443,686 bytes; retrieved
  `99dda859c4ae3c446eb25620d1a6237bb135d95d14d55cd1d30a15340bb39d80`,
  17,954,770 bytes. The code did not trust that catalog or silently update its pin.
  Runtime research instead resolved direct official download URLs and verified
  actual binaries only after human consent. Investigate the catalog mismatch
  separately before using it as an approval authority.
- Initial VM inspection used `/root/wineexplorer-vmtest`, which did not exist;
  the actual prior test disks are under `/var/tmp/wineexplorer-vmtest`.
- vmtest.py's serial helper reuses an already logged-in shell: passing `root` did
  not change Alice's identity. Explicit `id` exposed this; subsequent guest changes
  use authenticated sudo, verified with `uid=0`, rather than trusting the parameter.
- The clone emits `sudo: unable to resolve host WEX-PC: Name or service not known`;
  sudo authentication/commands still worked. Hostname configuration needs checking
  in fresh-installed acceptance rather than suppressing this diagnostic.
- A WSL shell loop passed through Windows quoting lost its `$p` filename and printed
  `strings: '': No such file`. A Python stdin script invoking `strings -el` with an
  explicit argument vector successfully inspected both versions; no payload changed.
- `src/dlls/wexhost/main.c` inspection failed because that path does not exist;
  the wrapper implementation is `src/dlls/wexhost/wexhost.c`. Git inspection without
  escalation again reported `fatal: this operation must be run in a work tree`;
  scoped escalated Git commands work. No other checkout was modified.
