<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Reusing the application test harness

The Chromium graphics tests contain reusable isolation, ownership, readiness,
shutdown and evidence helpers. They can support testing other Windows apps under
Wine. They are currently a collection of libraries and app-specific runners,
**not a portable, supported test-harness command or plugin API**.

Keep them in this repository for now: the runners depend on our Wine build,
compositor protocol and private lab configuration. A separate repository would
currently export those assumptions along with the useful parts.

## Lifecycle and responsibilities

An app test should follow this sequence:

1. Prepare a fresh run directory and immutable inventory of the executable,
   fixture, helpers, build versions, configuration and exact commands. Record
   required disk space and time reserves. Refuse existing output paths.
2. Start only an owned disposable environment. Preserve original disk layers;
   use a new writable overlay when retaining the prior guest state matters.
   Record process identity and the actual drive/socket arguments.
3. Establish authenticated guest readiness before deployment or app probes.
   A listening SSH port alone does not establish readiness.
4. Run one explicit app adapter with its own prefix/profile, fixture nonce,
   deadlines, unique display sockets and restricted network environment.
5. Collect the app's API results and actual front-window pixels at the relevant
   events. Record acceleration and security evidence separately.
6. Stop the owned app, compositor, VM and host controller. Prove their exit and
   preserve failures, logs and partial output before reporting the result.

The outer controller owns the total deadline and shutdown reserve. Inner
timeouts must fit inside it; a test timeout must leave time to stop the VM.
No helper alone performs or proves this complete lifecycle.

## Component map

Paths below are relative to the repository root. Read each helper's source and
tests before reuse; signatures and current lab restrictions are not stable APIs.

| Component | Useful responsibility | Limits the caller must handle |
| --- | --- | --- |
| `scripts/run-isolated-graphics.py` | Runs a nested command in private mount/network namespaces with a unique filesystem X socket directory; optional private loopback | Requires Linux namespace privileges and native Linux socket paths. The nested runner owns its children and cleanup. This is test isolation, not a malware sandbox. |
| `scripts/graphics_wexcomp.py` | Owns a headless compositor process and private Wayland endpoint; selects pixman or GLES2 | Depends on our `wex_window_manager_v1` protocol. Renderer selection is distinct from verified acceleration. |
| `scripts/graphics_resources.py` | Samples matching Wine-prefix processes, identity, CPU, RSS and threads | Best-effort sampling can miss processes/errors. Empty output is not proof of idle, absence, GPU use or low power. |
| `scripts/g4_guest_readiness.py` | Bounded authenticated SSH probes; requires two consecutive valid replies from the same boot with cloud-init done and systemd running | Currently fixes Debian UID/name, SSH option shape and port. Does not launch a VM or prove graphics readiness. |
| `scripts/g4_qmp_shutdown.py` | Bounded QMP framing, request correlation and retained asynchronous guest-shutdown events | Protocol evidence only. The caller must verify endpoint ownership, QEMU exit, fallback use and child cleanup. An ACK alone is insufficient. |
| `scripts/g4_virgl_budget.py` | Checks remaining time against raw UTC controller metadata | Pure calculation; it explicitly does not prove the recorded process is still owned/alive. |
| `scripts/stop_g4_virgl_launcher.py` | Stops the exact recorded launcher and checks its child identities/result | Requires the matching session records. Host Windows controller exit is a separate check. |
| `scripts/g4-virgl-launcher-session.ps1` | Keeps the owned WSL launcher session and records host-side session metadata | Currently tied to the private VirGL launcher path and Windows/WSL lab. |
| `scripts/g4_edge_owned_threads.py` | Keeps native HWND/Windows PID/image/creation identity separate from Linux process identity | Windows and Linux PID values must never be treated as interchangeable. This is a diagnostic helper, not app readiness. |
| `scripts/g4_edge_http_fixture.py` | Serves a tiny local fixture through exact nonce routes | Not a general file server. HTTP delivery alone does not prove script execution or rendering. |
| `scripts/check-g4-webview2.py`, `scripts/check-g4-edge-headful.py`, `scripts/check-g4-chromium-headful.py` | Examples of app-specific launch, fixture and pixel checks | Require separately acquired private runtimes. Each covers a scoped consumer contract, not full browser compatibility. |

The original `g4_native_exit*` native helpers distinguish a Windows child's full
32-bit exit status from the Linux launcher status. They still contain
Chromium/calibration-specific paths. Their live calibration is pending; do not
advertise them as a validated general-purpose Windows process launcher.

## Adding another application

Start with a small app-specific runner alongside the current runners. Reuse the
libraries rather than copying a private launcher wholesale. There is no existing
generic adapter interface to implement.

Define the executable and actual version, architecture, full argv, environment,
working directory, runtime/prefix/profile paths and a deterministic fixture.
Use a unique run ID and nonce. Record whether initialization may modify the
prefix, require a reboot or contact a network service. Do not silently add
dependencies or retry a failed launch with different flags.

Specify success before running: for example, a particular owned window appears,
the fixture acknowledges its nonce, expected pixels appear in its client area,
and a frozen scene survives an expose or resize. Capture at the event being
tested, with sufficient window/process identity to exclude a stale or unrelated
window. Animation, a successful launch API, a downloaded fixture or an exit code
alone cannot substitute for the intended visible result.

Keep separate identity records for the Windows HWND/PID/image/creation time and
the Linux PID/start time/argv/prefix/profile. Capture only the owned test display.
Refuse ambiguous ownership rather than selecting the first browser process.

For graphics, record the actual inner and outer renderer/backend from evidence.
Requested flags, a virtual GPU device name or synthetic capabilities do not prove
hardware acceleration. A software control remains useful, but label it as such.
Keep normal app security behavior; a sandbox request flag does not establish an
effective sandbox. Do not turn a failed test into a pass by disabling security,
GPU features or assertions.

Use fresh filesystem socket directories for every run, including parallel runs.
Network namespaces alone did not prevent collisions when tests shared an X socket
bind directory. Use native Linux paths for Unix sockets, not Windows/DrvFS paths.
For offline fixtures, private loopback may be enabled while external routes stay
absent; record the actual namespace/interfaces/routes before and after the test.

Preserve the original failed arm, raw stdout/stderr (including failed subprocess
output), actual captures and termination history. Report fallback termination as
fallback termination even if QEMU ultimately exits zero. Never signal a PID
without checking its recorded start identity and expected command.

## Local verification and evidence levels

One focused readiness/cleanup regression suite, from `scripts/` under Linux:

```sh
python3 -B -m unittest test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic -q
```

These tests include real local subprocess and socket exercises alongside fakes.
The integration tests inspect the private lab procedures without launching their
module bodies. They may require private lab files; inspect skip conditions on
another machine. They do not boot QEMU, connect to a guest or execute a browser.
Prefer a targeted suite over indiscriminate discovery of lab execution scripts.

Keep these claims separate in a report:

| Evidence | What it can establish |
| --- | --- |
| Unit/local protocol tests | Parser, timeout, identity and failure behavior for the exercised inputs |
| Actual guest readiness | Authenticated state of the observed boot at the sampled times |
| App fixture/API evidence | The particular app operations and replies actually observed |
| Actual front-window pixels | Visible content at the captured event, not every future frame |
| Actual backend evidence | The renderer/acceleration route observed in that run |
| Effective security evidence | Only the independently checked restrictions, not requested flags |
| Shutdown plus owned-process checks | Whether that run stopped gracefully or needed a fallback |

Neither a mock pass nor a skipped test establishes a live result. At the
`48183f33` preparation checkpoint, readiness/QMP integration has passed local
tests but the new protected guest boot has not run. Chromium startup and full
Edge compatibility remain open. See the dated
[readiness preparation report](CHROMIUM_GRAPHICS_G4_READY_BOOT_PREP_20261024.md)
and [continuation policy](CHROMIUM_GRAPHICS_CONTINUATION_POLICY_20261022.md)
for the scoped current handoff; later reports can supersede that checkpoint.

## Private lab configuration and possible extraction

Credentials are operator-supplied private prerequisites, never repository
payloads. Prefer an owned durable native Linux directory (0700) and key (0600),
for example the reviewed lab's `/var/tmp/wex-g4-lab-credentials/vm_key`, rather
than relying on a transient `/tmp` copy across separate WSL sessions. Verify
canonical/non-symlink paths, ownership, permissions, readability and derived
public-key match to the accepted seed before each execution. Never generate or
rotate a key, edit guest authorization or relax host-key verification to repair
a failed observation. Do not print private bytes; record public fingerprint and
match only. Two successful host observations do not guarantee persistence across
all future boots/namespaces. The observed `/tmp` disappearance cause is unknown.

Readiness explicitly labels OpenSSH missing/unavailable identity and unsafe
private-key permissions as fatal **host credential** errors, preserving stderr;
these must not be confused with guest auth rejection or retried as ordinary
network startup delays. Host-key/auth failures still fail closed. Network-only
banner/refused-connection failures may retry within the original gate budget.
Never overwrite a failed manifest to change its credential path/helper hash:
new paths or helper changes require fresh reviewed preparation and authority.

Current orchestration assumes Windows + WSL Debian + QEMU/KVM, a particular SSH
identity/port, cloud-init/systemd, our pinned guest binaries and a D:-backed lab.
Some reviewed orchestration files live in ignored `build/` paths; tracked reports
retain hashes and changes for review. Those files are not a public bootstrap
package. Do not copy credentials, disks, runtime caches or private captures into
Git to make a run reproducible. Inventory their identities instead, and require
the operator to supply lawful local inputs.

Keep vendor runtime license/consent handling separate from our original harness
code. A source file's SPDX header does not grant redistribution rights to its
test payloads. Public evidence should omit private binaries, credentials and
captured user data. This project's test VM, disk, runtime, profile and evidence
storage belongs on host D:.

A dedicated repository becomes useful after a second distinct app adapter
demonstrates the shared contract and we extract:

- Explicit configuration for paths, guest identity, SSH, backend and storage,
  replacing private hard-coded entrypoints.
- Portable tests and sample fixtures that need no private disk/runtime or D:
  installation; clear separation of optional live tests and local tests.
- A documented preparation/result/ownership contract with bounded lifecycle and
  failure behavior, plus a runnable example using redistributable inputs.
- License notices and documentation independent of vendor payloads and the
  WineExplorer compositor protocol.

Until then, keep shared fixes and their callers together here. Treat the helpers
as reusable building blocks, and retain app-specific acceptance tests beside
the app adapters.
