<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1 R2/R3: package context, activation and the package resolver

Design of the code on Wine branch `codex/uwp-runtime` (2026-11-12). It implements work packages R2 and R3 of
`IMPLEMENTATION_CONTRACT.md` for the U1.0 fixture. Status and evidence are at the end; what is listed under
"Not done" is not claimed.

## Model

**Package identity is a property of a token.** The server keeps an immutable package context with the token
(`struct token`: `package`, `package_pending`). A process is packaged when its primary token has a context, so the
process, the token of that process and a duplicate of it always agree: there is one copy to keep consistent, not two.

* `token_duplicate` copies the context, so a child process started by a packaged process is packaged too (the Windows
  behaviour for full-trust packaged programs), and `DuplicateTokenEx` keeps it.
* Tokens the server builds from scratch do not have one: `token_create_admin`, the linked (elevated or filtered)
  token, service tokens and the description a user's server sends to session 0 (`token_describe`). Package identity
  therefore never follows an elevation or crosses the session 0 boundary.
* Identity gives no right: no group, privilege, integrity or ACL decision looks at it. It is not AppContainer.

**The context comes from the catalog of the prefix, never from the client.** A client names an application
(`FamilyName!ApplicationId`); the server finds the package, reads its record and builds the context itself
(`server/uwp_context.c`). Nothing from the environment, the PEB, the image path or a caller's strings is used.

**The context is settled by the image the process maps.** A context starts *pending*. The first executable image of a
process whose token has a pending context must be the executable of the catalog record, compared by device and inode
when the process maps it (`map_image_view`, the same pattern as the elevation broker check in `sysrun_main_image`).
If it is, the context becomes final; if not, mapping fails with `STATUS_ACCESS_DENIED` and the process does not start.
`init_process_done` refuses a process whose context is still pending (an image that never went through the check), and
`set_process_info` refuses a pending token as a new primary token.

## Catalog

The directory `appx-catalog` of the prefix, read by the prefix's own server through `config_dir_fd`. A package is
published by the record `appx-catalog/PackageFullName.wexappx`, written last (and removed first). The record is
UTF-16LE text, printable ASCII only, at most 64 KiB:

```
WEXAPPX 1
root <tab> package directory: a path on drive C: whose last component is PackageFullName
app <tab> ApplicationId <tab> relative path of the executable
class <tab> ActivatableClassId <tab> relative path of the in-process server
```

Per-user package files live in the user's profile (`%LOCALAPPDATA%\WineExplorer\WindowsApps\PackageFullName`):
`Program Files` belongs to the machine root in the system tier and a standard user cannot write it
(`COORDINATION_SYSTEM_TIER.md`, answer 3). The record carries the directory because the server cannot know a profile
path (the profile name comes from each client's `USER`). Machine-wide packages in `C:\Program Files\WindowsApps`,
written by a session 0 service, are a later step.

The full name is the record's file name and the family name is derived from it. Names and paths are validated in the server (character sets, no drive,
stream, `.`/`..` or absolute path, no symbolic link as the record or the executable). A record that is malformed is an
error (`STATUS_FILE_CORRUPT_ERROR`), not "no package"; two packages of one family are refused. Non-ASCII file names are
not supported yet.

The catalog is a file store rather than registry keys because a user's hive may be held by session 0 (system tier
T4a), so a user's server cannot rely on reading `HKCU` itself.

## Interfaces

| Layer | Interface |
|---|---|
| server | `set_token_package` (token handle with `TOKEN_ADJUST_DEFAULT`, the application name): only for a primary token that no process runs with and that has no context. `get_token_package` (`TOKEN_QUERY`): the strings and the class table. |
| ntdll | Wine-private token information class `TokenWinePackage` (1000) for `NtQueryInformationToken` / `NtSetInformationToken`, `WINE_TOKEN_PACKAGE_INFORMATION` (pointer-free, passed through by wow64). |
| kernelbase | `GetCurrentPackageFullName`, `GetCurrentPackageFamilyName`, `GetCurrentPackagePath`, `GetCurrentPackageId`, `GetCurrentApplicationUserModelId`, `GetPackageFullName`, `GetPackageFamilyName`, `GetPackageFullNameFromToken`, `GetPackageFamilyNameFromToken` read the token's context. Unpackaged results are unchanged. |
| combase | `RoGetActivationFactory`: a class declared by the package of the process is loaded from that package's module only, and a load failure is the result (no other registration is tried). A class the package does not declare, and every process without a context, takes the unchanged lookup. The class name is compared by its counted length. |
| twinapi.appcore | `CLSID_ApplicationActivationManager`, `IApplicationActivationManager::ActivateApplication`. |

## Activation

`ActivateApplication` runs **in the calling process**: it duplicates the caller's own primary token, asks the server
to attach the application's context, and starts the catalog executable with `CreateProcessAsUserW` (current directory:
the package directory). No server process's token is ever used, so there is no caller to impersonate and nothing to
confuse across an elevation. A client that asks for a local server (the documented way to create the class) reaches a
`dllhost.exe` surrogate; the object marshals itself by handing the client an in-process manager (`IMarshal`), so the
call still runs in the caller.

The compatibility gate `HKCU\Software\WineExplorer\Shell\UwpCompatibility` (exactly `REG_DWORD` 1) is checked by
`ActivateApplication`. Without it the call returns `HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)` and nothing is started.
An application that is not in the catalog gives `E_APPLICATION_NOT_REGISTERED`.

## Not done

* The gate is not enforced by the server: a program that calls `NtSetInformationToken(TokenWinePackage)` directly gets
  a pending context for a catalogued application without the gate. It still cannot run anything but that
  application's executable with it, and identity grants nothing.
* The installer is a development installer. `src/programs/wexappx` (`wexappx install --development PACKAGE`,
  `uninstall`, `list`) reads an `.appx`/`.msix` archive with bounds (member count, sizes, expansion ratio, names,
  duplicates, checksum), stages beside the destination, publishes the directory and then the record, and removes
  the record first on uninstall. It verifies **no signature** (hence the required `--development`), refuses a
  package that declares a package dependency, and handles no bundle, 64-bit archive or non-ASCII name. It is behind
  the gate. `tests/uwp/u1_0/test-wexappx.sh` covers it; `register-wine.py` remains as the loose-file variant.
* `RtlQueryPackageIdentity`, `GetCurrentPackageInfo`, `GetPackagePathByFullName`, package dependencies, `TokenSecurityAttributes`
  (`WIN://SYSAPPID`) are not implemented. `GetPackageFullName` on a handle with only the limited query right fails
  while elevation is enforced, because the token cannot be opened.
* The catalog and the package files are writable by the user's own programs (as on Windows a user can register a
  development package). A binding, administrator-set gate would be an `HKLM` policy read by the client through
  session 0 (`COORDINATION_SYSTEM_TIER.md`, answer 4); nothing of that exists.
* Windows behaviour for a class the package does not declare, the working directory and the options of
  `ActivateApplication`, and x86 packaged activation on Windows are not measured.
