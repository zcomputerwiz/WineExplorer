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

Not done: the consent window for packages (a per-machine install would reuse the msiexec path of `consent.c`); a
Store-signed package (Microsoft's chain, its root and the Store EKU) has not been through it; the per-file hashes of the
block map are not compared (the digests cover the whole archive); revocation is not checked; countersignature time stamps
are ignored.
