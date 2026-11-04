# DirectComposition surface semantics (notes for the dcomp port)

Paraphrased from the public Win32 reference for `IDCompositionSurfaceFactory`, `IDCompositionSurface::BeginDraw`
and `::EndDraw`, to guide `wine/dlls/dcomp/device.c`. The Wine-Staging series (LGPL-2.1+) in
`build/review/staging-dcomp/` (patches 0047-0054) is the working prior art.

## CreateSurface (factory, device2/device3/desktop device)

- `CreateSurface(width, height, pixelFormat, alphaMode, &surface)`; `surface` must not be NULL (`E_INVALIDARG`).
- Formats: `DXGI_FORMAT_B8G8R8A8_UNORM`, `DXGI_FORMAT_R8G8B8A8_UNORM`, `DXGI_FORMAT_R16G16B16A16_FLOAT`; others fail.
- Alpha: `DXGI_ALPHA_MODE_PREMULTIPLIED` or `IGNORE`; `UNSPECIFIED` means `IGNORE`.
- Width or height above the maximum texture size fails (virtual surfaces exist for larger ones).
- A new surface is uninitialised and composes as 100% transparent until the first draw. The first `BeginDraw` of a
  non-virtual surface must cover the whole surface (NULL rect or the full rect).

## BeginDraw / EndDraw

- `BeginDraw(const RECT *updateRect /* optional */, REFIID iid, void **updateObject, POINT *updateOffset)`.
  `updateObject` must not be NULL. The rect must lie inside the surface or the call fails. NULL = whole surface.
- Returns either an `ID2D1DeviceContext` (only if the factory/device was created with a Direct2D device; otherwise
  `E_INVALIDARG`) or a DXGI surface (`IDXGISurface`-style) belonging to the application's rendering device.
- `updateOffset` is where the caller draws the update rect's top-left, not necessarily the rect origin; the caller
  must draw inside a rect of the same size at that offset and cover every pixel (initial contents are undefined).
- Only one surface of a device can be in an update at a time: a second `BeginDraw` (same or another surface of the
  same device) before `EndDraw` fails. `SuspendDraw`/`ResumeDraw` allow switching.
- `EndDraw()` completes the update; it fails if no update is pending. The update becomes visible only after
  `EndDraw` followed by `Device::Commit`. A `Commit` before `EndDraw` leaves the update pending.
- Callers must not cache the returned object and must not call `ID2D1DeviceContext::BeginDraw/EndDraw` on it.
