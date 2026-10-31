# Edge startup surface analysis — 2026-10-31

## Result

Parent completed independent binary inspection and one successful native Windows Edge startup capture, without using the shared guest or changing Wine. This establishes a usable Edge reference and a bounded compatibility queue. It does not establish that Edge works on the current reconciled Wine build, nor provide a complete executed Windows-API trace.

The strongest findings are:

1. Local Edge and WebView2 at **156.0.4314.50** have byte-identical msedge.dll and msedge_elf.dll. Their different launchers import identical sets of APIs. The three-image import union is identical:1613 distinct (normal/delay,DLL,symbol) bindings. Different runtime modes and initialization paths remain material; successful WebView2 does not prove full-browser startup.
2. Native Edge loaded both original fixture resources, committed the nonce target with net_error0, and returned nonce-matched passed=true for the existing WebGL1/2 clear/FBO/readback fixture. No feature/security bypass was supplied. Browser root exited0 after WM_CLOSE; all26 held observed process identities reached signaled state without forced termination. One identity_helper.exe exited0xc000022d while the target page still succeeded. That helper status alone is therefore not evidence of a page-startup blocker.
3. The exact Wine-tested Edge **156.0.4314.42** was extracted as data from the preserved MSI. Its msedge.exe SHA256 matches the previously accepted installed executable. Its three core images have identical import sets to .50, although their implementation bytes differ. Actual .42 call sites are available for focused diagnosis.
4. Additional imports relative to Chrome are an inventory, not an instruction to implement every API. A verified package-query caller requests **Microsoft.GetHelp_8wekyb3d8bbwe**; that is not evidence that GetHelp or a complete AppX stack must work for the target page. Import presence, a static caller and an executed failing caller are separate evidence levels.

Microsoft documents the shared [WebView2/Edge process model](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/process-model). The binary identity above is independently measured for these particular installations, not inferred from that documentation.

## Inputs and provenance

| Input | Version | What was done |
| --- | --- | --- |
| Installed Microsoft Edge |156.0.4314.50|Read-only PE analysis; one native private-profile capture|
| Installed Microsoft Edge WebView2 |156.0.4314.50|Read-only PE analysis; engine/ELF hashes compared|
| Installed Google Chrome |156.0.8078.79|Read-only launcher/engine/ELF import analysis|
| Preserved unbranded Chromium snapshot |156.0.8078.0 /1706509|Read-only import comparison; prior native reference retained|
| Preserved Edge MSI used in Wine |156.0.4314.42|Read-only extraction, PE analysis and static call witnesses|
| Project Wine source |e1a90b9430bc664a9ba21549cff6b13c1510d2ad|Read-only export/source audit|

These are the same Chromium milestone, not an identical Chromium source/build configuration. Differences cannot automatically be attributed to Microsoft modifications.

The .42 MSI remains the original215257088-byte artifact with SHA256324d226c5ade08c2776645cfc2a7b12af9c08214abf18c6499410cd8ed92d1b3. Read its Binary.MicrosoftEdgeInstaller stream through Windows MSI database APIs in read-only mode; no msiexec installation or custom action executed. The compressed resource uses LZMA followed by BCJ2 and a tar archive, then a nested browser installer/7z payload. The public-domain LZMA SDK BCJ2 decoder was used as a private extraction tool; no Microsoft implementation was transcribed. A first partial extraction and failed archive interpretation remain labeled in the private evidence rather than treated as valid inputs.

Exact extracted .42 core hashes:

- msedge.exe:bb74db4c753133706f4ab0a483a87aa28a90ddd6eeb305fe001376ba73a40fc3
- msedge.dll:e30fdd5b7778300b363d9f4fa994a7970a5929a1eadf40c576f0cf3861b1a06b
- msedge_elf.dll:5db8ccdc59df37c8b5687fb64df18e1374696b2f4d5c36b71d25c3f8f6a542a8

The executable matches retained installation evidence. This is not an attestation that an arbitrary later guest process loaded the same DLL bytes.

The .50 engine SHA256 is e51e4c1e9b1dff0d7a9016160848c752fc1d687107e4e6d0f98c3042f385b7d3 and its ELF is dd9e21d26f67757f896ed95f0532fb088d242145641173abf1aeb7fdcb1aa714, identical in the Edge and WebView2 version directories. Their launchers differ in bytes and size; identical imports do not mean identical startup behavior.

## Native startup reference

Used the existing original g4-edge-probe/index.html and scene.js (637/3750 bytes) and the established held-process/owned-window runner. Launched the installed Edge as verified medium-integrity user with a fresh private profile, app URL and ordinary first-run/default-browser-check suppression, plus logging/startup-trace/Default-NetLog diagnostics. No normal user profile, browser setting, sandbox feature, WPR session or VM changed. The new private artifact directory received read/execute ACLs for application-package groups, as in the successful native Chromium reference.

The trace is JSON startup tracing, not kernel ETW. NetLog used numeric duration40, not the previously invalid40s suffix. Trace ran35s; observation45s. All vendor binaries, profiles, raw logs and disassembly remain private on D:.

Target-correlated phase graph, relative to the first target NavigationRequest in browser PID32704:

| Phase | Observed role/PID | Relative milliseconds |
| --- | --- | ---: |
| NavigationRequest::NavigationRequest |Browser32704|0|
| CorsURLLoader construction, index |NetworkService29220|109.839|
| URLLoader response to client, index |NetworkService29220|223.738|
| CorsURLLoader construction, scene.js |NetworkService29220|236.327|
| URLLoader response to client, scene.js |NetworkService29220|243.732|
| MojoURLLoaderClient::OnReceiveResponse, scene.js |Renderer17556|244.140|
| WebContentsImpl::DidFinishNavigation |Browser32704|271.145|

DidFinishNavigation records has_committed=true, net_error=0. Both resource responses were200; favicon404 was separate from the two target resources. The owned root-window title carried the exact nonce, kind=ready and passed=true with WebGL1/2 error0, complete FBOs and final magenta/cyan readbacks. This is the legacy fixture's actual scope, not all shader/presentation/full browser functionality. No screenshot or kernel API-call coverage is claimed from this capture.

Earlier Wine Edge captures on the older4315be stack served index.html but not scene.js and showed a blank client. They predate the current reconciled candidate and cannot establish its present behavior or exact blocked API. Sampling no renderer was not proof of no renderer creation. The current work should compare these phases on the accepted working Chromium stack rather than repeating old generic thread snapshots.

## Static API inventory and source triage

Original PE parsing separated normal and delayed import descriptors according to the [PE specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format). Direct x64 IAT call witnesses were validated at instruction boundaries inside PE unwind-function ranges. Raw assembly was kept private; the tracked summary contains only API names, RVAs and original observations.

Against the three installed Google Chrome images, Edge has302 additional binding entries before exclusions.80 are imports of supplied Edge-side libraries, not extra Wine system APIs. Of222 remaining system import entries,3 names already occur in Chrome under a different binding category;219 names remain in this bounded static comparison. GetProcAddress and transitive/runtime-loaded libraries are not exhaustively covered. None of these counts measures the minimum API set for a successful page.

Wine export-declaration audit of those219 names:

| Classification | Count |
| --- | ---: |
| Named/ordinal export declaration exists; semantics unverified |184|
| Explicit export stub |17|
| Direct named export not found |6|
| API-set forwarding needs resolution |11|
| Binary ordinal binding unresolved |1|

An exported function may itself be a partial C implementation or failure stub, so184 is not a sufficiency count. Ordinals were checked by numeric declaration rather than falsely searched as names. Direct2D ordinal1 and API-set bindings remain explicitly unresolved. Wine loader.c:1207-1250 can allocate unresolved-function stubs during import fixup; missing a named import is not alone proof the module never loaded. Executing such a stub is a different observation from a valid native negative result.

Selected .42 witnesses and useful contracts:

| Surface | Exact call RVA / evidence | Practical implication |
| --- | --- | --- |
| CreateAppContainerProfile |msedge.dll0x4448392; following branch recognizes0x800700b7 and tests HRESULT success|Reuse reviewed per-user registration/lookup/profile behavior. Existing-profile HRESULT is explicitly handled; do not force every call to S_OK. This does not prove token isolation.|
| FindPackagesByPackageFamily |0x983c31f; argument Microsoft.GetHelp_8wekyb3d8bbwe, flags0x20010; tests success and count1|Missing direct Wine export is a candidate if this caller executes. The observed argument is GetHelp integration, not a demonstrated page prerequisite.|
| OpenPackageInfoByFullName / GetPackageApplicationIds |0xb43af66 /0xb43afa8,0xb43afdd|First call is status-checked; buffer sizing recognizes122. Probe exact path/status if reached; no broad fake package database.|
| GetCurrentPackageFullName |0xcc140b3,0xcc1411a|Caller checks122 and returned size. Wine returns APPMODEL_ERROR_NO_PACKAGE. Negative package identity can be legitimate for an unpackaged process.|
| Create/QueryMemoryResourceNotification |0x1495ccb /0x2bc31f6|Already exported in Wine; if implicated, test event/query semantics rather than treating export presence as sufficient.|
| NtReadFile |0xe044021|Existing transport/read implementation still needs caller-specific completion/status evidence if this path stalls.|

A separate read-only original native helper measured GetCurrentPackageFullName=15700/length0 for its unpackaged Python process, and a fresh nonexistent synthetic family query returned0/count0/chars0 with flags0x20010. Those are helper observations, not intercepted Edge call results. They align with Microsoft's [package-identity contract](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-getcurrentpackagefullname) and [package-family query](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-findpackagesbypackagefamily). The [AppContainer profile contract](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-createappcontainerprofile) documents the existing-profile HRESULT recognized by the observed code.

Other concrete source gaps include exported event-log/ESE stubs, missing direct exports for MFCreateSensorGroup and two WinINet cookie APIs, a partial AppState notification implementation and RoOriginateLanguageException returning FALSE. They are mostly delayed or feature-dependent surfaces. No trace in this task proves any of them caused the older blank page. Implementing ESENT, account/WAM integration or every event-log function is not a prerequisite established by this report.

## Recommended next step

Keep the active Chromium/token work as the sole guest owner. Once normal Chromium validates on the reconciled, resource-complete build, perform one ordinary Edge comparison using the retained .42 installation and the same actual working stack, fresh profile/output and current ownership/cleanup safeguards. Capture this target phase graph and NetLog. The expected renderer/JS/commit sequence is now concrete.

If Edge diverges, select the first missing phase and correlate the actual caller, requested access/flags, return status and completion notification. Use the exact .42 RVAs above where relevant; version .50 RVAs differ. Prioritize shared sandbox/Mojo/process contracts before unmeasured optional account/package features. One identity_helper nonzero exit or a prelaunch network-service log is not sufficient causality. A supported small fix plus focused native/Wine semantic test and bounded normal verification is preferable to a broad API-completeness or harness project.

No vendor implementation, payload, raw disassembly or user profile is committed. Only original analysis and derived API/trace facts are tracked. Raw evidence and original utility sources remain at D:/DeepThought/ChromiumLab/edge-startup-analysis-20261031/. The native run is in native-edge-control/; exact extraction provenance and complete inventories are adjacent.
