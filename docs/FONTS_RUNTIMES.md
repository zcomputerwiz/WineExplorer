<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fonts and runtimes repair branch — 2026-10-17

Status: implemented and independently smoke-tested, **not yet merge-ready**.
This is `codex/fonts-runtimes`, starting at master `1fa63233`, not a merge of
`gemini/fonts-runtimes`. The original review is
[fonts-runtimes-20261017.md](reviews/fonts-runtimes-20261017.md).

## Reused and repaired

Kept Gemini's verified release versions/checksums, Liberation file-path fixes,
Selawik filename mappings, and the exact `Noto Sans Symbols2` spelling. Kept
master's Trebuchet substitution and Wine's own Tahoma. No Wine source change.

The package now stages Selawik 1.01, Mono 11.3.0, Gecko 2.47.4 for both
architectures, DXVK 2.4.1 and vkd3d-proton 2.13. All downloads, including the
full upstream license documents, have SHA256 pins in
`installer/runtime-assets.json`. Gecko's full embedded license page and
OpenH264 notice are extracted from its verified MSI and independently hashed;
they are not replaced with a short license summary. Mono's notices include
the pinned FNA, Windows Forms, WPF and MonoDX dependency notices. Source and
notice completeness must still be audited at distribution time.

`installer/stage-runtime-assets.py --offline` accepts only a complete,
hash-matching local cache. It runs no Windows installers. Packaging no longer
installs build dependencies through sudo; provision Python 3.13 (Debian 13),
tar, zstd, unzip and msitools on the build host first.

The runtime package declares the actual open-font dependencies. Its postinst
refreshes fontconfig; every machine-root update imports our font substitutions
and creates the open-font links, reporting missing files rather than silently
claiming they were linked. Setup, OOBE and both desktop sessions no longer
disable Mono and MSHTML. Private greeter/lab scripts are not changed.

## Graphics operation and isolation

Use `wex-graphics vulkan on`, `off` or `status` **as the current user**. Changes
apply at the next login. They do not hot-replace DLLs beneath running programs.
No `wine reg`, registry deletion or `wineserver -w` is performed by the switch.

The supported layout is the normal split prefix. At login its shared Windows
symlink is restored for maintenance, then a private shallow Windows view is
built under `$WINEPREFIX/.wex-graphics/windows`. All ordinary files/folders
remain linked to the machine root; only graphics DLLs in the two private
system directories point to the bundled libraries. The view is refreshed at
each login, including newly installed shared files. User registry overrides
are not overwritten or discarded: the desktop's native/builtin load-order
settings are carried in its environment.

Both PE architectures and all fourteen DLLs are checked before activation.
Vulkan 1.3 enumeration is checked both when enabling and at login. Device loss
or an invalid bundle leaves the shared Wine view in place. Vulkan enumeration
is a minimum check, not a guarantee of every D3D12 driver feature or physical
GPU acceleration. vkd3d-proton's requirements are documented
[upstream](https://github.com/HansKristian-Work/vkd3d-proton/blob/v2.13/README.md#drivers).

## Windows Components safety and current limitation

`wex-components` opens the chooser; `wex-components list` lists the supported
verbs, and `wex-components install vcrun2022` uses the CLI. All optional choices
start unchecked. The allowlist is exactly `vcrun2022`, `corefonts`, `tahoma`,
`d3dcompiler_47`, `xact`, and `physx`. Other winetricks verbs cannot be passed
through. Limited users cannot launch installation.

The UI is unprivileged. After acceptance it calls a fixed `pkexec` backend;
there is no passwordless policy. The backend independently checks the verb,
root-owned vendor terms and their accepted digest, uses the root-owned machine
prefix and cache, locks out simultaneous maintenance, and rejects an already
active machine-prefix server rather than waiting for somebody's desktop.
Caller-supplied Wine settings, prefix, display sockets, executable paths and
cache paths are not inherited. An isolated Xvfb runs the installer.

**Vendor terms must currently be provisioned by an administrator.** Put the
complete, authentic terms for the exact selected package release in
`/etc/wineexplorer/component-licenses/<verb>.txt`, UTF-8, root-owned and not
group/world-writable. The directory and its ancestors must also be secure.
For example, Microsoft's official
[VC++ 2015–2022 terms page](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/)
links its actual document; a generic notice or that page's navigation text is
not a substitute. Match the terms to the package downloaded by the installed
winetricks version. The repository/ISO does not contain those proprietary
documents or redistributables. An absent notice disables the component and
downloads nothing. Automatic retrieval/version pairing is still follow-up work;
the default image is therefore not yet a turnkey Components installation.

The selected actual text is displayed and explicitly accepted; its digest is
rechecked after elevation. Changes to core Wine DLLs (including `msxml*`,
`mf*` and `ucrtbase`) and root-prefix DLL-override keys are restored even after
an installer failure. Root-only recovery snapshots are retained under
`/var/lib/wineexplorer/components-protected-*`; an administrator should remove
them only after verification. Installer errors are propagated, not converted
to success by a progress dialog. An unsuccessful installation can leave other
partial component files; recovery is not advertised as a full MSI rollback.
Only successful installs receive a digest receipt.

The launcher is packaged as a system application and copied to shared
Administrative Tools by machine setup. Its real menu placement and the
non-root desktop authorization flow still need installed-image verification.

## Evidence actually obtained

Local Debian 13:

- All **22** isolated regression tests pass: two-user DLL isolation, no live
  server wait, refresh/restore, missing/old/lost Vulkan device, invalid DLL
  bundles, verb restrictions, privilege boundary, declined/missing/changed
  terms, real installer exit propagation, protected DLL recovery, sanitized
  backend environment and offline checksum rejection.
- Changed shell scripts pass `bash -n` and ShellCheck's error-level checks.
- Both MinGW architectures compile the native probe; Python files compile.
- All target families exist by exact `fc-list : family` matching after
  installing the declared Debian font packages and verified Selawik release.
  Every `font-links.txt` target file exists. This is not a visual line-wrap test.
- The complete open payload stages from the verified cache with `--offline`.

ProtonExplorer build VM, pinned combined WoW64 build at
`/srv/protonexplorer/wex-wayland-l1/wow64-source/build/wine64`:

- Fresh prefixes in network namespaces with no external network. Private Xvfb
  for X11, then a separate fresh prefix and private headless wexcomp/D-Bus
  session for Wayland. Both runs report:

```text
Native 32-bit process: PASS
MSHTML 32-bit: PASS (HRESULT=00000000)
Native 64-bit process: PASS
MSHTML 64-bit: PASS (HRESULT=00000000)
Managed 32-bit process: PASS (4.0.30319.42000)
Managed 64-bit process: PASS (4.0.30319.42000)
```

The source-build addon cache path was used to test the exact staged MSIs;
this does not establish packaged-install datadir lookup or fresh-ISO acceptance.

- Real split-prefix graphics test in both architectures loads native
  `d3d11.dll`, reports `DXVK: v2.4.1` and creates a D3D11 device (feature b000).
  Device is **llvmpipe CPU Vulkan**, not hardware acceleration. After disabling,
  both architectures load builtin `d3d11.dll` and create their D3D11 devices.
  Shared DLL hashes and Bob's DLL targets are unchanged. The on/off commands
  return while a real Notepad client remains active. An invalid Vulkan ICD
  causes refusal with no enabled marker.

Reproduction scripts are `scripts/test_runtime_repairs.py`,
`scripts/check-font-targets.py`, `scripts/tests/runtime-smoke.sh`, and
`scripts/tests/graphics-smoke.sh`. Unlike Gemini's runner, they never use
global killall, real user prefixes or fixed X displays. Native/managed test
programs are project-authored. No Gemini screenshots or their prohibited art
were copied into this branch; no proprietary components were downloaded.

## Acceptance still required before merge

1. Fresh installed ISO, offline X11 and Wayland, including package installation,
   machine-root updates, menu launch as an ordinary administrator and a limited
   account, shared VC++ use after genuine user license acceptance, and recovery.
2. Actual WordPad 32/64 line-wrap and CJK glyph/visual checks, with open-only
   screenshots. Font-family enumeration is not equivalent to rendering parity.
3. D3D12/vkd3d-proton and physical Vulkan hardware. D3D11 CPU tests do not prove
   those. Include an installed-session lost-device fallback test.
4. ISO before/after size measurement and distribution/source/license audit.
   The initial staged payload was 225,514,301 uncompressed bytes before the
   added dependency notices: it is **not** an ISO size measurement. The three
   required offline MSIs alone are roughly 185 MiB, so the 150 MB growth target
   cannot be assumed. A concrete trimming option is an explicitly selected
   separate offline-runtime/graphics image or supplementary package, keeping
   a smaller base image; that changes the single-image offline requirement and
   needs approval. Do not silently drop an architecture or prune a pinned MSI.

## Failures, workarounds and retained state

- `rg src/wexhost` failed because that directory does not exist; the actual
  code is under the assembled Wine/project source layout. Debian has no `rg`;
  use grep there. Git commands omitting `--ignore-submodules=all` encountered
  `cannot chdir ... .git/modules/wexcomp: Permission denied`; scoped commands
  with that flag work. No submodule was reset or changed.
  Final `git rev-parse --short HEAD master origin/master` failed with
  `fatal: Needed a single revision` because --short accepts one revision;
  `git rev-parse HEAD master origin/master` successfully verified all three.
- The first license metadata enumeration used unresolved relative submodule
  URLs and received HTTP 404. Resolving the repository from the returned
  `html_url` fixed the lookup. No placeholder notices were substituted.
- Python ZipFile on Gecko's optimized `omni.ja` raises
  `BadZipFile: Bad magic number for central directory`; unzip reports the
  adjusted central-directory warning. Its extracted full notice bytes are
  separately hash-verified, with only expected unzip status codes accepted.
- The first exact-family check reported only the four uninstalled Selawik
  families; installing the staged verified OFL fonts fixed the test. No
  fallback-family success was accepted.
- WSL staging/diagnostic calls were interrupted at the user's request during
  the disk move (exit 1), then resumed after permission. Initial VM SSH refused
  connection because the VM was stopped/saved. It was started headlessly on
  explicit request. Tar emitted future-timestamp warnings after saved-state
  restore; extraction and test results were unaffected.
- First native test link failed with `undefined reference to CLSID_HTMLDocument`
  in MinGW's uuid archive. The test now defines the public COM class identifier
  locally; both architecture probes link and pass. The first disposable prefix
  from that run is retained, not used as passing evidence.
- ProtonExplorer-VM remains running. Tests stopped only their own prefix
  servers, Xvfb instances and compositor; no installed desktop was restarted.
  Open payload/test files remain in `/tmp/wex-fonts-repair.v6LIFsg2`; passing
  runtime evidence/prefixes in `/tmp/wex-runtime-smoke.PE7AcHUN` (X11) and
  `/tmp/wex-runtime-smoke.wa0xLamf` (Wayland); final graphics evidence in
  `/tmp/wex-graphics-smoke.iBE9JvKh`. Earlier probe directories remain too.
  The build VM gained the declared `vulkan-tools` utility; local Debian gained
  build-inspection tools and the declared open font packages/Selawik. No
  proprietary installation was run. The user's `.gitignore` edit is preserved.
