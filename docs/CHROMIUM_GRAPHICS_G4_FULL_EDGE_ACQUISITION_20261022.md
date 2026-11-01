<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Full Edge acquisition — verified, NOT executed

Acquisition/provenance only, following explicit human acceptance/private-testing
authority recorded in plan 09013186. Official [terms/download page](https://www.microsoft.com/en-us/edge/server/download)
and authorization are recorded privately. No host msiexec/setup, VM boot/install,
browser execution, fixture/diagnostic code, default/profile change or vendor
payload/art in Git. Parent review required before any later step.

Resolved [Microsoft's current enterprise metadata](https://edgeupdates.microsoft.com/api/products?view=enterprise)
at `2026-10-22T20:08:26Z`: Stable / Windows / x64 / MSI, version **156.0.4314.42**,
ReleaseId **132982** (highest numeric Stable x64 version in that response).
Raw metadata SHA-256:
`5f939fbfba7510f887ede700a965a178affdbfd8631a6d8276ca4767518e6d09`.
Exact product/release/artifact JSON preserved, including original date strings;
no current-version claim beyond this acquisition snapshot.

Private root:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/private-edge-acquisition-20261022-r1/`.
Payload `MicrosoftEdgeEnterpriseX64-156.0.4314.42.msi`, **215,257,088 bytes**.
Official artifact URL and final URL (same, HTTPS/HTTP 200):
[Microsoft MSI](https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/62209208-6eb6-4f5e-bff5-feb45cfe41d3/MicrosoftEdgeEnterpriseX64.msi).
Final host `msedge.sf.dl.delivery.mp.microsoft.com`. Download UTC
`20:08:26.669–20:08:52.866Z`; response headers and URLs retained privately.

Metadata algorithm **SHA256**, expected AND actual raw MSI hash:
`324d226c5ade08c2776645cfc2a7b12af9c08214abf18c6499410cd8ed92d1b3`.
Length/hash both match. Host read-only `Get-AuthenticodeSignature`: **Valid**,
Microsoft Corporation, Microsoft Code Signing PCA 2024 → Microsoft Root CA 2011.
Signer SHA-1 thumbprint `4028CAD637509D4744B17EC5B42AED8D7A31E6AF`;
SHA-256 `cb97e8e85e8e9321fb2646e9574efd17669b3b0581d24262ac7c8a227433a244`.
Signer and Microsoft timestamp-service chains built valid with online revocation
checks, no chain status errors. Public certificates/subjects/issuers/validity and
full chain records retained in `acquisition.json` and `.cer` files.

Additional SDK `signtool verify /pa /all /v /tw` exit **0**, one signature verified,
zero warnings/errors. Full log retains timestamp `Thu Oct 22 02:33:56 2026`
in host Central time (not relabeled UTC), Microsoft Time-Stamp Service/PCA 2010/
Root CA 2010 chain. Its printed Authenticode verification digest is separate
from the raw file hash above; both retained, not confused or substituted.

`acquisition.json`, `final-verification.json`, exact original acquisition script,
HTTP records, raw/selected metadata, signature logs and consent provenance all
remain private on D:. Verification completed `20:08:54.247Z`; no failed download,
hash or signature gate, no retry/bypass. Only this original summary tracked.
Return for parent provenance verification. Shader/texture draw and full tabbed UI
remain separate future gaps in the smoke plan, not claims from this acquisition.
