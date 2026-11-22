<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Package signature check for `wexappx` (design, 2026-11-21)

`wexappx install` takes only `--development` today and says "The signature of the package was not verified."
What the consent and elevation work already gives this, and what is left.

## Reusable from the consent and elevation work

* **Policy** (`E1_DECISIONS.md`): unsigned and unknown publishers get a click consent with an accurate label; no automatic
  approval from the signer; an invalid signature is labelled as invalid; failing to read or bind the file denies. The same
  rules fit a package: a package signed by an unknown publisher installs after consent (with the publisher shown), an
  invalid signature is shown as such, an unreadable one is refused.
* **Presentation** (`src/programs/consent/consent.c`): `WinVerifyTrust` with the state kept, `signer_name()` (the first
  signer's certificate subject through `WTHelperGetProvSignerFromChain` and `WTHelperGetProvCertFromChain`), the
  verified-publisher banner and the "Unknown publisher" label. A package mode exists for `msiexec /i`: from the broker only
  the file it froze is described, by the absolute path it froze (B55). An appx or msix mode would use the same path with
  the package's display name from the manifest (the manifest reader exists in `wexappx`).
* **Request integrity** (`E1_DECISIONS.md`): hold the file open with write and delete denied and check hash and identity
  right before the install. `wexappx` should verify and install from one handle.
* **Wine**: crypt32 has `CryptMsgOpenToDecode` and `CryptMsgControl`, and its chain engine carries Microsoft's root
  certificates. Wine's digest check for installer packages (`SOFTPUB_VerifyImageHash`, `STOCK_CHANGES.md`) shows the
  pattern for a format that WinVerifyTrust does not know.

## Not covered by any of it (the new part)

WinVerifyTrust has no provider for `.appx` and `.msix`. The signature is `AppxSignature.p7x` in the package: the four
bytes `PKCX` and then a PKCS #7 SignedData whose content is an indirect data block with the SHA-256 digests of the
package parts (central directory, zip entries, `[Content_Types].xml`, `AppxBlockMap.xml`, optionally the code integrity
catalog). The check is therefore ours:

1. Read `AppxSignature.p7x` from the package and require the `PKCX` prefix.
2. `CryptMsgOpenToDecode`, `CryptMsgUpdate`; verify the signature against the signer certificate in the message.
3. Build the certificate chain (`CertGetCertificateChain`) and classify it: trusted root, untrusted root, invalid.
4. Take the digests from the content and compare them with SHA-256 of the actual central directory, `[Content_Types].xml`
   and `AppxBlockMap.xml`. The block map already checks every file (done in `wexappx`).
5. Require the manifest's `Publisher` to equal the signer's subject (the package identity depends on it).
6. Report the signer name and the trust class; an untrusted or invalid package is refused or goes to consent, per the
   policy above.

## Status (2026-11-21)

Windows does it in one module, `AppxSip.dll`, a signature provider that wintrust asks (`tests/uwp/signature/README.md` has
the measurements), so that is what was built, not a check inside `wexappx`:

1. **`src/dlls/appxsip`**: the provider for `.appx` and `.msix` (subject GUID `{0AC5DF4B-...}`). It recognises a package,
   hands out the PKCS #7 of `AppxSignature.p7x`, and verifies the SHA-256 digests it holds (`AXPC`, `AXCD`, `AXCT`, `AXBM`,
   `AXCI`). `wine.inf` registers it (one line, Wine branch `claude/appxsip-registration`). `WinVerifyTrust` on a package
   gives on Wine, x64 and x86, what it gives on Windows, except two differences that are in wintrust (see the README).
2. **`wexappx install`** asks `WinVerifyTrust` on the file handle it holds and compares the signer's subject with the
   `Publisher` of the manifest: a trusted and matching package installs; one that is unsigned, untrusted, invalid or
   inconsistent is refused, and `--development` installs it anyway and says which it was. It also reads genuine
   `makeappx` packages now (zip64). Tested with the fixtures in both architectures, with the test certificate in the
   root store of the prefix (installs without the flag) and without; both older installer suites pass (59 and 63 checks).
3. **Found on the way**: stock Wine's `WinVerifyTrust` ended its policy silently for a certificate with a SHA-256
   signature and reported success for an untrusted root (Wine branch `claude/wintrust-sha256-policy`). The consent window
   used the same call.

### Security review (session 0, 2026-11-21) and what was done

Found and fixed: `WinVerifyTrust` accepted a digest type it could not verify (B67: the signature of a signed cabinet in a
package's signature file verified; Wine branch `claude/wintrust-sha256-policy`), the subject was guessed from the file
(a signed program with an archive behind it passed for a package; now `wexappx` gives the subject, `appxsip` wants the first
member at byte 0 and nothing but SIP information as signed data), an offset check wrapped (zip64 end record), and the signer
was compared with the publisher as text (now as names, and a signer that cannot be read is refused). Fixtures for the
attacks: `foreign-p7x`, `pe-prefix`, `pe-hidden-zip` (`tests/uwp/signature`).

### The installer window and revocation

What Windows shows when a package is opened (App Installer, measured 2026-11-21 with the untrusted fixture): the name,
publisher ("Unknown" when not verified), version and source, and Install and Cancel; for a package whose publisher is not
trusted the button is greyed out and the window says why. Windows has no way around it in that window (a developer's way is
Developer Mode or trusting the certificate). The screenshot is kept local (`WineExplorer-local/uwp-oracle`).

`src/programs/wexappinstall` is that window: it shows what `wexappx query` finds and runs `wexappx install` (never with
`--development`) on Install; `.appx` and `.msix` open in it (Wine branch `claude/appxsip-registration`, `wine.inf`). `wexappx`
checks the whole chain for revocation: a revoked certificate refuses; no revocation information or nobody to ask is said and
passed (as far as measured: Windows asks for revocation online for a Microsoft-signed file and stops at the untrusted root
for the test package; a chain with a revoked certificate was not measured, it needs a trusted test certificate on the host).

### Cabinets (2026-11-22, B70)

Windows computes the Authenticode digest of a cabinet; Wine did not (its SIP was a stub and `softpub` let cabinets pass). It is now
computed in `CryptSIPVerifyIndirectData`: SHA-256 (the algorithm of the signature) of the file from its start to the signature,
which must end the file, leaving out the reserved bytes at 4, `cbCFHeader` at 36 and the header reserve area at 40 (it holds the
offset and size of the signature). Found by trying that on signed cabinets against their signatures. A signed cabinet made with
`makecab` and `signtool` and seven copies that break it (a data byte, a file name, flags, version, set id, a byte behind the
signature, and the signature taken off) give the same `WinVerifyTrust` results on Windows and on Wine x64 and x86 (`tests/uwp/signature`).

### The block map at extraction (2026-11-22, B68)

The bytes behind the mapping of a package can change between the signature check and the extraction (a program of the host
system ignores share modes). The block map is covered by the signed digest, so `wexappx` now compares each file it extracts
with it, as Windows does while it stages a package: the size and the SHA-256 of every block of 64 KiB (the `Size` attribute
of a block is the compressed size and is not used). A file the map does not list is refused, except the members that no map
lists (`[Content_Types].xml`, `AppxBlockMap.xml`, `AppxSignature.p7x`, `AppxMetadata\CodeIntegrity.cat`). A package that has
no block map and no signature (the unsigned packages of the installer tests) is not compared; one that has a signature
member and no block map is refused with every flag (`no-blockmap` in `tamper.py`: the signature covers the map, so stripping it
would leave nothing to compare the files with; without `--development` the signature provider already rejects it). Because the check is on the extraction,
it also holds for `--development`: `bad-block`, `other-publisher` (the manifest no longer fits the map) and `extra-member`
are refused with it where they installed before (`wexappx-sig.sh`); the signed fixture, whose `data.bin` has four blocks,
installs. The installer suites (59 and 64) pass as before.

### Installing for every user (2026-11-22)

What Windows does (measured on Windows 11, not elevated): provisioning a package for all users
(`Add-AppxProvisionedPackage -Online`) answers "The requested operation requires elevation" for a good package, an unsigned
one and a file that does not exist alike, so the elevation check comes first and the file is not read before it; the
elevated process then judges the signature as for any package. `Add-AppxPackage` has no all-users switch there. The user's
own install (App Installer, `Add-AppxPackage`) needs no elevation and no consent beyond its window. So the consent for an
all-users install is the elevation prompt, and it has to describe the package.

`wexappx install [--development] --all-users PACKAGE` (also `uninstall --all-users NAME`, `list --all-users`) does that:

* The package goes to `C:\Program Files\WindowsApps\FullName`, its record to the machine catalog
  (`%ProgramData%\WineExplorer\appx-catalog`), where every user's server finds it. A package for all users can depend only on
  packages that are in the machine catalog too (the frameworks), else it is refused.
* With elevation enforced (`HKLM\Software\Wine\Elevation\Helper` set) and a limited token, `wexappx` runs its own command
  line again through the elevation broker (`WexElevateRun`, as `msiexec` does for a per-machine install). Nothing is asked of
  the broker without a Helper (the command goes on as before); with one and no broker, or a standard user, nothing is
  installed and the message says why. Cancelling the consent gives "not allowed".
* The broker freezes the package like the file of an `msiexec` request (`doc_of` in `wexelevate/main.c`: held open, lease,
  SHA-256) and hands the elevated copy `WEXELEVATE_DOC_PATH` and `WEXELEVATE_DOC_SHA256`; the elevated `wexappx` installs
  only that file, hashing the very bytes it checks and extracts. A `wexappx install` request whose package the broker cannot
  name is refused (`ERROR_INVALID_PARAMETER`), not run unchecked.
* The consent window (`consent.c`) describes the frozen file by what `wexappx query` reads from it (the same reading as the
  installer window): the display or identity name, the signer when the publisher is trusted, "Unknown publisher" for an
  untrusted or unsigned one, "Invalid signature" for a package or signature that does not hold. A package that cannot be
  read gets the plain description of `wexappx.exe`.
* Wine (`claude/uwp-machine-apps`, `STOCK_CHANGES.md`): the server and `kernelbase` accept an application from the machine
  catalog (they accepted only frameworks), after the user's own catalog.

Tested: `test-wexappx.sh` with `WEX_MACHINE=1` (install, list, extract, launch the nine packaged checks, uninstall, per
architecture, plus the frozen-file checks: a wrong hash, another file, a path without a hash are refused; the frozen
file installs: 64 checks), and in the user mode as before (59). `consent.c` built with the test hook (`consent-test.sh` in the
lab helpers) and run standalone on `wexappx install --all-users` of the fixtures shows the name "Signature test" and, as the
publisher, the signer for a package from a trusted publisher, "Unknown publisher" for the unsigned one and "Invalid signature"
for the one with a changed byte (the untrusted-publisher label is the same branch as the unsigned one; the prefix used had the
test certificate trusted already). The broker side (`appxdoc`, `appxnone` in `wexelevate_probe.c`, `test_sysrun_server.sh`) is
written and compiles with the suite's own compile lines; it runs in session 0's suite, as does the whole path through the
broker (limited token, consent on the secure desktop, the elevated copy).

Not done: a Store-signed package (Microsoft's chain, its root and the Store EKU) has not been through it; countersignature time stamps are ignored; no logo or capabilities in
the window yet; the installer window installs for the user only (App Installer has no all-users choice either); the consent
window for an all-users install was not looked at on a screen (the text it shows was read from the test hook's file).
