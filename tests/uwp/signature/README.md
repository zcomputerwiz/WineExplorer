<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Package signature fixtures and the Windows oracle

Design: `docs/uwp/SIGNATURE_CHECK.md`. Measured 2026-11-21 on Windows 11 (10.0.26100).

## How Windows does it

One module: **`AppxSip.dll`**, "Appx Subject Interface Package", a signature provider (SIP) registered with wintrust. It is
registered for four subjects (`HKLM\SOFTWARE\Microsoft\Cryptography\OID\EncodingType 0\CryptSIPDllVerifyIndirectData\{GUID}`):

| GUID | Function prefix | Subject |
|---|---|---|
| `{0AC5DF4B-CE07-4DE2-B76E-23C839A09FD1}` | `AppxSip` | an `.appx` or `.msix` package |
| `{0F5F58B3-AADE-4B9A-A434-95742D92ECEB}` | `AppxBundleSip` | a bundle |
| `{5598CFF1-68DB-4340-B57F-1CACF88C9A51}` | `P7xSip` | the `AppxSignature.p7x` itself |
| `{1AD2DCB4-1FC8-42EF-8D9B-1EDFB2F7C75D}` | `ExtensionsSip` | extension packages |

Callers (`signtool verify`, `AppxPackaging`, the deployment service) all go through `WinVerifyTrust`; none checks a package
signature by itself. Wine's `msisip` (Windows Installer packages) is the same kind of module, and `CryptSIPRetrieveSubjectGuid`
already asks registered providers (`IsMyFileType2`), so an Appx SIP plugs in without a change to wintrust.

## What Windows says (`expected-windows.txt`, `sigtest.exe`; `signtool verify /pa` agrees)

| Package | WinVerifyTrust | Meaning |
|---|---|---|
| `signed.appx` (self-signed, in no trust store) | `800b0109` | chain ends in an untrusted root; the signer is known |
| `unsigned.appx` | `800b0100` | no signature |
| `bad-block.appx` (a stored byte of `logo.png` changed) | `80096010` | bad digest |
| `bad-blockmap.appx` (`AppxBlockMap.xml` changed) | `80096010` | bad digest |
| `bad-p7x.appx` (one byte of the PKCS #7 changed) | `80096010` | bad digest |
| `extra-member.appx` (a member added after the signature) | `80096010` | bad digest |
| `no-signature.appx` (the p7x member removed from a signed package) | `800b0003` | not recognised |
| `other-publisher.appx` (manifest changed, block map no longer fits) | `800b0003` | not recognised |
| `short-p7x.appx`, `wrong-magic.appx` (p7x of 3 bytes, `PKCY` for `PKCX`) | `800b0003` | not recognised |
| `rewritten.appx` (`signed.appx` through `tamper.py` unchanged) | `800b0109` | writer check: same as the original |
| `foreign-p7x.appx` (the PKCS #7 of a program signed by the same certificate as the signature file) | `80096010` | bad digest: a signature of another kind of subject is not this package's |
| `pe-prefix.appx` (a signed program, then the package; offsets shifted) | `800b0100` | no signature (read as a program, its table is damaged by the data behind it) |
| `pe-hidden-zip.appx` (the same, with the certificate table enlarged to hold the package) | `80096011` | rejected as a program too |

`makeappx` writes zip64 records even for a small package: the plain end record holds `0xffff` and `0xffffffff` (the disk
numbers too), and every central directory entry has `0xffffffff` for its sizes and offset with the values in a zip64 extra
field. `signtool` rewrites the package with plain entries but keeps the zip64 end records. A reader must understand both.

## Files

* `make-fixtures.ps1`: builds the signed package with a new ten-year self-signed test certificate (the key is deleted again,
  nothing is added to a trust store) and the unsigned copy. `fixtures/` holds the result of the 2026-11-21 run.
* `tamper.py`: the variants above, made from `signed.appx` byte for byte (a general zip writer would reorder the members and
  the package would no longer be recognised).
* `verify-fixtures.ps1`: signtool's answers for a directory of packages.
* `sigtest.c`: the same `WinVerifyTrust` call on every file given, with the signer name; built for x64 and x86 with mingw.
* `tiny.c`: the do-nothing executable inside the package.

## Wine (`appxsip.dll`, `src/dlls/appxsip`; `expected-wine.txt`, `lab-helpers/sig-run.sh`)

The same file list through `sigtest.exe` on Wine, x64 and x86, gives the lines above except two differences, both of
them the way Wine's wintrust is built, not the provider:

* A package whose digest does not fit (`bad-block`, `bad-blockmap`, `bad-p7x`, `extra-member`) gives no signer name: Windows builds the
  signer information before it compares the digest, Wine's `SoftpubLoadMessage` stops first.
* A damaged signature value (`bad-p7x`) is `80096004` (`TRUST_E_CERT_SIGNATURE`, the message's own verification) where
  Windows says `80096010`.

Three more differences, in the files that have a program in front: `foreign-p7x` is `800b0100` on Wine (the signed data is of
another type than Wine decodes, so it sees no signature) where Windows says bad digest; `pe-hidden-zip` is read as the signed
program (`800b0109`, untrusted signer) because wintrust decides the kind of file by its first bytes. Neither can be taken for a
package: `wexappx` hands wintrust the subject (a package) instead of letting it guess, the provider wants the first member at
byte 0, and the signed data must be the package information (see `wexappx-sig.sh`, which refuses all three even with the
signer's certificate trusted). Session 0's security review found that `WinVerifyTrust` accepted any digest type it could not
verify, which is what the first fixture needs a fix for (KNOWN_BUGS B67).

Found on the way and fixed on the Wine branch `claude/wintrust-sha256-policy`: `WinVerifyTrust` returned success for a
self-signed certificate whose signature is SHA-256 (see `docs/STOCK_CHANGES.md`); `signed.appx` is `800b0109` only with
that fix.
