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

## Order

1. A test fixture. The Calculator's signature is Microsoft's (lab only, never in the repository). For the repository: a
   self-signed package signed with a test certificate, plus tampered copies (changed block map, changed entry, wrong
   publisher), each measured on Windows 11 first (`Add-AppxPackage`, `signtool verify`).
2. The check in `wexappx`, with `--development` kept as the explicit way to skip it.
3. The consent mode for packages (reuse of the msiexec path in `consent.c`), only for per-machine installs; per-user
   installs show the result on the console.
