# Physical output testing

## First pass: 2026-10-09

The direct X11 backend was tested through Display Properties in WineExplorer-VM.
The read-only `scripts/display-output-probe.c` checks XRandR's active output and
the kernel DRM connector/CRTC independently. It opens the DRM device read-only;
it never acquires DRM master, changes a mode, or competes with Xorg for ownership.
It supports the current single-output, unrotated test configuration.

| GUI action | XRandR | DRM scanout | Result |
|---|---|---|---|
| Initial state | 800x600 | 800x600 at 60 Hz | Pass |
| Apply 1024x768, before answering | 1024x768 | 1024x768 at approximately 60 Hz | Pass |
| No | 800x600 | 800x600 | Pass |
| Apply 1024x768, Yes | 1024x768 | 1024x768 | Pass |
| Apply 800x600, Yes | 800x600 | 800x600 | Pass |
| Apply 1024x768, let countdown expire | 800x600 after timeout | 800x600 after timeout | Pass |

The VM was restored to accepted 800x600. DRM driver is `vmwgfx`; the active
connector is `/dev/dri/card0` connector 41 (`Virtual-1`). This establishes that the
guest scanout changes, rather than only Wine's desktop size. A VirtualBox guest
connector is virtual hardware: this does not test a physical monitor attached to
the Windows host, nor VMware or Hyper-V.

Build and run inside the graphical Linux session:

```sh
cc -Wall -Wextra scripts/display-output-probe.c -o /tmp/display-output-probe \
    $(pkg-config --cflags --libs x11 xrandr libdrm)
/tmp/display-output-probe 800 600
```

The process needs access to the session's X display and DRM device. An optional
third argument selects another DRM card. Exit 0 means agreement with the expected
dimensions, 1 means disagreement/unsupported topology, and 2 means the check
could not access its required interfaces. An unavailable probe is not a pass.

## Gamescope prerequisites and remaining tests

Gamescope is not installed, and the VM's configured Debian 13 package repositories
have no candidate. Enumerating physical devices with `libvulkan.so.1` reports only
`llvmpipe (LLVM 19.1.7, 256 bits)`, Vulkan device type CPU. The accelerated embedded
DRM path therefore remains untested; installing a compositor alone would not
establish GPU acceleration. See the [upstream requirements](https://github.com/ValveSoftware/gamescope).

The session launcher now starts Explorer directly on Xorg so its X11 window manager can host independent Linux and Proton clients. Gamescope may be launched per application; there is no implemented session-wide compositor/service bridge. In a nested Gamescope test it runs inside the host display;
resizing its Xwayland output must not be described as changing physical scanout.
Embedded testing needs a separate DRM session with a suitable Vulkan device and
session/seat ownership, rather than taking DRM ownership away from the running
Xorg session.

The reference Gamescope sources provide a compositor-specific path:

- `src/steamcompmgr.cpp`, `update_mode_atoms`: physical external connector modes
  are published in `GAMESCOPE_DISPLAY_MODE_LIST_EXTERNAL` as `WxH@Hz` entries.
- `src/Backends/DRMBackend.cpp`, `get_saved_mode` and `setup_best_connector`:
  external mode selection uses `GAMESCOPE_MODE_SAVE_FILE`, keyed by the display's
  description, and validates the chosen mode against the connector.
- `src/steamcompmgr.cpp`, the `GAMESCOPE_DISPLAY_MODE_NUDGE` handler: asks the
  backend to reconsider output state. This is distinct from Xwayland RandR.
- Explicit preferred output dimensions/rate take priority over saved modes.
  The launcher's current `-W`, `-H`, and `-r` arguments must be accounted for in
  any future runtime mode-selection implementation.

These observations are implementation research, not a tested Gamescope backend.
Next runtime tests must compare the compositor's mode list, selected mode, and
actual DRM CRTC; then exercise Keep, No, timeout, restart persistence, removed
modes, and disconnected outputs. Test nested Gamescope separately and establish
which host compositor owns physical output configuration. Preserve the current
X11 fallback while adding compositor-specific control.
