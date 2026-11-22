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
| `extra-member.appx` (a member added after the signature) | `800b0003` | not a package of this subject |
| `no-signature.appx` (the p7x member removed from a signed package) | `800b0003` | not recognised |
| `other-publisher.appx` (manifest changed, block map no longer fits) | `800b0003` | not recognised |
| `short-p7x.appx`, `wrong-magic.appx` (p7x of 3 bytes, `PKCY` for `PKCX`) | `800b0003` | not recognised |
| `rewritten.appx` (`signed.appx` through `tamper.py` unchanged) | `800b0109` | writer check: same as the original |

`makeappx` writes the zip64 end records even for a small package (a package of a few members has `0xffff` and `0xffffffff`
in the plain end record), so a reader must understand zip64.

## Files

* `make-fixtures.ps1`: builds the signed package with a new ten-year self-signed test certificate (the key is deleted again,
  nothing is added to a trust store) and the unsigned copy. `fixtures/` holds the result of the 2026-11-21 run.
* `tamper.py`: the variants above, made from `signed.appx` byte for byte (a general zip writer would reorder the members and
  the package would no longer be recognised).
* `verify-fixtures.ps1`: signtool's answers for a directory of packages.
* `sigtest.c`: the same `WinVerifyTrust` call on every file given, with the signer name; built for x64 and x86 with mingw.
* `tiny.c`: the do-nothing executable inside the package.
