# dcomp portals: snapshot design

**Signed off 2026-11-24 by the Session 0 reviewer and the orchestrator, for Integration, which was not running. To be shown
to Integration when it runs again.** The sign-off covers this note as amended with D1-D3 and A1-A4. The reviewer reviews
the code before it is batched.

The design replaces the cross-device reads of Wine 7f2fe720 (branch `claude/notepad-commit`), which the review held back:
- **P1:** a use after free;
- **P2:** a lock cycle;
- **P3:** NtCompareObjects at every commit;
- **P4:** no redraw when a portal is removed.

The goal is unchanged. A target opened from a shared visual handle is a portal: what its device commits shows inside every
visual opened from the same handle. This is how the Windows App SDK compositor shows Notepad's title, tabs and status bar
in the UI thread's window.

## Objects

- **Registry and share (D3).** DCompositionCreateSharedVisualHandle makes an unnamed event and a share
  (`struct dcomp_share`) for it, refcounted.
  - The share keeps a private duplicate of the event (its identity) and sits in the process-local registry of shared
    handles. While it is listed, the registry holds a reference to it.
  - Each share holds:
    - its shared visuals (one per device);
    - its portal target, or NULL;
    - its current snapshot, or NULL.
  - Its visuals and its target hold references to it.
- **Snapshot** (`struct dcomp_portal_snapshot`): immutable and refcounted. It holds:
  - the layer list that collection produces today (`wine_dcomp_layer_desc`: matrix relative to the portal root, clip and
    corner radii, opacity and its mode, interpolation, border, size, `subtree_end` relative to the snapshot);
  - one reference per layer content.
  - Contents are IDCompositionSurface only (D2). A snapshot refers to nothing else: no visuals, targets, devices,
    shares or other snapshots (D1).

## 1. Ownership

- **What it holds:** layers with their transforms, clip and radii, opacity and modes, and a reference to every surface.
- **Who creates it (A1):** the portal target's device, in its own Commit, under its own critical section, after the tree
  switch has succeeded. It is built from the now-active tree, with animations sampled at the commit time.
  - A failed Commit builds nothing, and the previous snapshot stays.
  - The layer and content limits were already checked by the validation that precedes the switch.
  - If building fails after the switch (out of memory), the commit is not undone: the previous snapshot stays published,
    the failure is logged with ERR, and the next Commit tries again.
- **How it is swapped in:** under the global shared lock (`share->snapshot = new`). The old snapshot is released after both
  the shared lock and the device's critical section are left.
- **A root of NULL** publishes NULL (nothing to show).
- **A Commit that changed nothing under a portal root** publishes nothing. A dirty flag is set by the setters, as pending
  state is today.
- **Nested portals (D1).** A shared visual inside a portal tree whose share has a snapshot is flattened into the new
  snapshot at build time: its layers are copied in, with a reference to each surface, composed under the nested visual's
  matrix and kept inside its subtree, until the layer limit (`WINE_DCOMP_MAX_LAYERS`) is reached.
  - Layers past the limit are dropped, with one WARN.
  - Because a snapshot never refers to another snapshot, a cycle (A shows B, B shows A) copies at most one limit's worth
    of layers per build, and memory stays flat however often both commit.
  - The nested content is as new as the outer portal's last Commit; a nested change alone does not rebuild the outer
    snapshot. That is enough for Notepad, which does not nest.

## 2. Readers

- **At its Commit or redraw, a viewer** reaches each shared visual's share through a pointer. Under the shared lock it
  takes a reference to the share's snapshot and leaves the lock. It then appends the snapshot's layers after the shared
  visual's content layer and before its children, so they lie inside the visual's subtree.
- **A viewer never enters another device's critical section.** Validation counts the snapshot's layers and surfaces
  instead of walking the portal's tree.
- **Lock order:** a device's critical section, then the shared lock. The shared lock is a leaf. Under it, the code only
  follows pointers, changes reference counts (never a final Release), and links or unlinks lists. It never allocates
  and never calls dxgi, d3d or another device.
- **Where a reader can still block:**
  - on its own device's critical section, held by another thread of the same device;
  - for a moment on the shared lock;
  - inside dxgi's `prepare_target_scene` and `set_target_scene` (present and GPU waits), as for its own content today.
  It never waits for a lock of another device, so two devices that show each other cannot deadlock.

## 3. Lifetime

- **Contents stay referenced while a scene that uses them may be presented.** Each window target of a viewer keeps the
  snapshot references of its active scene (`target->shown[]`).
  - After a successful Commit or redraw has handed the new scene to dxgi (`set_target_scene`), the previous references
    are released. On failure the new ones are released, and the previous scene keeps its own.
  - Final Releases run after every lock is left, because a surface's last Release enters its own device's critical
    section.
- **A released portal target** publishes NULL under the shared lock, unlinks itself from its share, and redraws its
  viewers (P4).
- **When the shared visual goes away first (A2):** it leaves the viewer's committed tree first, because the tree holds a
  reference. On its final Release it unlinks from its share, which clears the link; the portal's snapshot then has no
  viewer. The portal target keeps publishing. A visual opened later from a handle of the same object links to the share
  and shows the current snapshot at once.
- **When the portal device goes away while a viewer holds its snapshot (A2):** the snapshot holds only surfaces. A surface
  holds its dcomp device and its rendering device, so both live until the viewer's next scene releases the snapshot.

## 4. Redraw

- **Events that redraw viewers:**
  - a published snapshot, whether new or NULL;
  - a released portal target that had a snapshot.
- **Who is redrawn:** the devices of the share's visuals, each taken with a reference under the shared lock and redrawn
  once. A share's snapshot is flattened into an outer snapshot only when the outer device commits (D1), so there is
  nothing transitive to redraw.
- **On which thread:** the thread that committed (or released the target), after it has left every lock. It runs each
  viewer's `internal_commit(redraw)`, which takes only that viewer's own critical section.
- **A redraw never changes a tree:**
  - it uses the active trees only, and pending state is not read or touched;
  - it counts no batch and publishes no snapshot, so it cannot cascade.

## 5. Pairing (P3) and the registry (D3)

- **Handles.** DCompositionCreateSharedVisualHandle returns an unnamed event. Nothing is put into `\BaseNamedObjects`;
  this follows the cross-server rule, under which no new Wine-internal names go there.
- **Opening a handle.** OpenSharedObject accepts a handle only if NtCompareObjects matches it with a registry entry's
  duplicate, so it opens the same object as a handle that DCompositionCreateSharedVisualHandle made in this process.
  - Anything else is E_HANDLE, as measured on Windows 11: an event of the caller's own, a named event of another
    process, a closed handle, the process handle.
  - This lookup is the only place NtCompareObjects runs: once per listed share, under the shared lock.
  - Wine has no cross-process composition, so a handle passed to another process opens nothing there (E_HANDLE).
- **Linking.** The visual and the target store the share pointer and unlink on their final Release. Commit, collection
  and redraw only follow pointers.
- **A second target opened from the same object** replaces the first as the portal, with a FIXME. Windows' behaviour is to
  be measured with the probe before the code lands; if Windows refuses it, so will Wine.
- **Dropping registry entries.** An entry is dropped when ObjectBasicInformation shows that the share's duplicate is the
  only handle left to the event, which means every caller handle is closed. The check runs on each create and each open
  (a sweep of the registry).
  - Dropping releases the registry's reference. A share that is still linked to visuals or a target lives on, but cannot
    be opened again; no handle to it exists any more.
  - The duplicate is closed when the share is freed.

## 6. Limits and what a portal can show

- **Build time:** the layer and content limits are checked by the portal device's validation before the switch. A
  failure fails that Commit with E_NOTIMPL, as for a window target, and the previous snapshot stays. Flattened nested
  layers stop at the layer limit (D1).
- **Surfaces only (D2).** A snapshot carries only IDCompositionSurface content. A swap chain in a portal tree is refused
  when the snapshot is built: the layer is kept without content, so its children still show, with one FIXME.
  - The reason: a viewer would bind the portal device's swap-chain producer to its own window, the portal device could
    revoke it, and two viewers would fight over it.
- **Same D3D device.** A viewer presents portal surfaces through its window target's auxiliary producer, and that
  producer composes only surfaces of its own D3D device: it is made from the scene's first surface and refuses other
  devices' surfaces.
  - **Notepad:** the window's scene holds only the compositor's surfaces, all on the compositor's device. This was
    measured in the lab run of 7f2fe720, where the surfaces reached dxgi with no refusal.
  - **Otherwise:** when a portal's surfaces are on a device other than the producer's, or the viewer's scene already
    has a swap chain (no auxiliary producer), the viewer leaves that portal out of the scene with one WARN. The
    viewer's own Commit is not failed by another device's tree.
- **Viewer limits:** if appending a portal would pass the viewer's layer or content limit, the viewer leaves that portal
  out of the scene, with one WARN.

## 7. Composition and semantics (A3)

- **Composition.** Snapshot layers are stored relative to the portal root. The viewer multiplies the shared visual's
  world matrix into each layer's matrix, and puts the layers inside the shared visual's subtree (`subtree_end` rebased).
  - The renderer tests each layer's clip per pixel, in that layer's own space, and a layer's clip also cuts its whole
    subtree.
  - So the portal layers' own clips and the shared visual's clip are exact under any transform, rotated and skewed
    included. This is today's rule for nested visuals; nothing is refused.
  - Opacity and the opacity modes are inherited through the subtree, also as today.
- **Semantics.** A viewer shows only committed state: the newest snapshot published before it took the reference, at its
  own Commit or at a redraw. A snapshot is complete when published and never changes, so a viewer never sees part of a
  portal Commit. Between a portal Commit and the viewers' redraw, the window keeps the previous complete scene.
  Animations inside a portal tree are sampled at the portal's commit time.

## 8. Tests (dcomp.c, `test_shared_visual_portal` and companions)

1. A duplicate handle opens the same visual on the same device, also after the original handle is closed.
2. A portal Commit shows in the viewer's window (readback).
3. Removal: root NULL plus a portal Commit clears the area. A portal target released with a root clears it too (P4).
   Both are checked by readback.
4. Concurrency (P1): two threads, one committing the viewer, one switching the portal root between two surfaces and
   SetContent(NULL). About 500 iterations; refcounts are checked at the end.
5. A cycle (P2): devices A and B each show a portal of the other in a window target of their own, and both commit in a
   loop on two threads. Both return.
6. Nesting (D1): a portal tree contains a shared visual whose portal belongs to a third device, and the innermost content
   shows. With both devices of a cycle committing in a loop, the process's private bytes stay flat (Wine; checked with
   GetProcessMemoryInfo after a warm-up).
7. A swap chain in a portal tree (D2): refused (a FIXME), the viewer's Commit succeeds, and the swap chain's sibling
   surface still shows.
8. Registry (D3):
   - a named event of the test's own is refused (E_HANDLE);
   - a closed shared handle is refused;
   - after the last close, the entry is dropped: repeated create and close cycles leave the process's handle count flat
     (Wine).
9. Both architectures in the lab, and the same PE test on Windows 11, where cases 4-6 may only check that nothing hangs.
   The dcomp and dwmapi suites both run. Notepad with the lab bypass: the compositor's surfaces still reach the window.

## What does not change

- Commit stays synchronous; a redraw is one extra present of a viewer's last committed tree.
- Window targets and their scenes work as today; only the portal path changes.
