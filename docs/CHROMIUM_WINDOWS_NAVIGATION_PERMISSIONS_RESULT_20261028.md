<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native Windows Chromium: sandbox permissions and navigation comparison

The native baseline's launch denial was resolved without elevation or disabling
Chromium's sandbox. The same cached Chromium executable and original HTTP fixture
now fetch and commit successfully. This is a successful **navigation** baseline,
but the fixture's stricter final result remains `passed=false` due to an rAF
timeout; it is not a complete visual/presentation pass.

## Change and scope

The original cached executable's DACL granted ordinary Users and Authenticated
Users, but lacked application-package entries. Chromium's pinned sandbox policy
checks its AppContainer's GENERIC_READ|GENERIC_EXECUTE access to the executable.
The previous native attempt logged access denied and network sandbox launch
failure. The exact inaccessible policy/ACE combination was not independently
resolved, but the following scoped change removed that observed denial.

Copied the same cached `chrome-win` tree to the NEW private D: runtime at
`D:/DeepThought/ChromiumLab/windows-navigation-baseline-20261028-r2/runtime/chrome-win`.
Applied inheritable read/execute only to ALL APPLICATION PACKAGES (S-1-15-2-1)
and ALL RESTRICTED APPLICATION PACKAGES (S-1-15-2-2) on that copy and descendants.
`icacls` reported262 successful paths,0 failures. Original cache and its DACL
were not modified. The copied and source `chrome.exe` SHA-256 both equal
`c84cd1e7ce7865ccc13bf00f33a3cb934d293c38a1a106585de3d3077c888c99`.
Windows process token was medium integrity; no elevation was used. No sandbox,
GPU, network or feature-disabling switches were added.

## Actual results

All attempts have distinct private profiles and outputs. The original failed run
and every subsequent run remain on D: under
`D:/DeepThought/ChromiumLab/windows-navigation-baseline-20261028*`.

| Run | Window startup | HTTP / page result | Owned cleanup |
| --- | --- | --- | --- |
| Original | normal | no HTTP, sandbox access denied, net_error=-3 |16 observed processes exited |
| r2 | hidden startup | HTML200, scene.js200; nonce readiness `passed=false`, rAF timeout |11 observed exited,0 forced |
| r3 | show without activation | HTML200, scene.js200; nonce readiness `passed=false`, rAF timeout |11 observed exited,0 forced |
| r4 | normal startup | HTML200, scene.js200; nonce readiness `passed=false`, rAF timeout |11 observed exited,0 forced |
| r5 | normal startup; attempted foreground | HTML200, scene.js200; nonce readiness `passed=false`, rAF timeout |11 observed exited,0 forced |

For r5 the app window was reported visible, but `SetForegroundWindow` returned
false. This does not establish why the rAF callbacks timed out. Both WebGL and
WebGL2 produced the expected default red, framebuffer green, final magenta/cyan
readbacks, complete FBO, error0, context not lost, using ANGLE Intel UHD750 D3D11.
Those are canvas readbacks; front presentation and the overall readiness pass are
not established. No further retries or feature switches were attempted.

The r5 HTTP fixture served original index637 bytes and scene.js3750 bytes with
status200. A browser title reported the same nonce and `kind=ready`; its own
`passed=false` and `Error: rAF timeout` must not be ignored. The favicon404 is
unrelated to the fixture's required files. No `Sandbox cannot access executable`
line or network-service crash/restart line occurred in the successful navigation
runs. Process scanning can miss short-lived descendants; the cleanup result is
limited to actually observed held identities. The fixture listener stopped.

## Concrete native trace sequence

`r5/trace.json` parsed as4811 events and `r5/netlog.json` as1860 events. Browser
Windows PID31324/UI TID46880; timestamps are raw trace monotonic microseconds.
These phase timestamps refer to the actual target navigation, not cross-process
clock comparison. The target nonce is present in private evidence only.

| Event | Trace timestamp |
| --- | ---: |
| NavigationRequest::Create |409297998652|
| NavigationRequest::BeginNavigation |409298010573|
| NavigationRequest::WillStartRequest |409298012532|
| NavigationRequest::OnStartChecksComplete |409298013358|
| NavigationURLLoaderImpl::Start |409298013563|
| ThrottlingURLLoader::StartNow |409298014330|
| ThrottlingURLLoader::OnReceiveResponse (target URL) |409298113369|
| NavigationRequest::OnResponseStarted (HTTP200) |409298113547|
| NavigationRequest::WillProcessResponse |409298114676|
| NavigationRequest::CommitNavigation |409298115191|
| RenderFrameHostImpl::CommitNavigation |409298116259|
| WebContentsImpl::DidFinishNavigation |409298139079|

The nonce-bearing DidFinishNavigation event records `has_committed=true`,
`is_error_page=false`, `net_error=0`; target-frame metadata identifies renderer
Windows PID37824. NetLog contains target URL_REQUEST_START_JOB for the document
and scene.js. The browser/network service can perform the full local navigation
on the host once the private runtime sandbox ACL is corrected.

## Limits and comparison

This supplies the ordered Windows expected navigation sequence, independent of
whether the final two rAF callbacks fire. It does not prove the Wine path has the
same sandbox/ACL failure or the same renderer/window behavior. Native host uses
ordinary Windows networking and Intel D3D11; Wine uses a private loopback-only
network namespace and VirGL. `r5/trace.json`, `netlog.json`, `fixture.json`,
`observation.json`, `foreground.json`, `chromium.log`, `launch.json`, and
`run.py` are private; no binaries, profiles or trace content are committed.
Further work should compare actual target-correlated Wine trace tracks against the
native sequence before selecting a Wine fix, and investigate rAF/presentation
only if a complete native graphics fixture pass is needed.
