# dcomp portals: snapshot design (for sign-off)

The design replaces the cross-device reads of Wine 7f2fe720 (branch `claude/notepad-commit`), which the review held back:
- **P1:** a use after free;
- **P2:** a lock cycle;
- **P3:** NtCompareObjects at every commit;
- **P4:** no redraw when a portal is removed.

The goal is unchanged. A target opened from a shared visual handle is a portal: what its device commits shows inside every
visual opened from the same handle. This is how the Windows App SDK compositor shows Notepad's title, tabs and status bar
in the UI thread's window.

## Objects

- **Share** (`struct dcomp_share`): one per kernel object of a shared visual handle, refcounted. It holds:
  - the list of its shared visuals (one per device, as today);
  - its portal target, or NULL;
  - its current snapshot, or NULL.
  It is found only when a handle is opened (see Pairing). Its visuals, its target and the snapshots that refer to it hold
  references to it.
- **Snapshot** (`struct dcomp_portal_snapshot`): immutable and refcounted. It holds:
  - the layer list that collection produces today (`wine_dcomp_layer_desc`: matrix relative to the portal root, clip and
    corner radii, opacity and its mode, interpolation, border, size, `subtree_end`);
  - a reference to every layer's content (surface or swap chain);
  - **nested markers**: a layer of a visual that is itself shared records a reference to that visual's share at its place
    in the list, not the share's layers. The markers are resolved when a viewer reads the snapshot (see Limits).
  A snapshot never holds visuals, targets or devices.

## 1. Ownership

- **What it holds:** layers, transforms, clip, opacity and their modes, a reference to every content, and references to
  the shares of nested shared visuals.
- **Who creates it:** the portal target's device, during its own Commit and under its own critical section. It is built
  from the pending tree after validation and before anything is activated, with animations sampled at the commit time,
  exactly as the tree will be after activation. Allocation and limit failures fail that Commit before any change is made.
- **How it is swapped in:** only after the Commit succeeded, under the global shared lock (`share->snapshot = new`). The
  old snapshot is released after both the shared lock and the device's critical section are left.
- **A failed Commit** releases the snapshot it built and leaves the previous one in place, together with the previous
  tree, as today.
- **A root of NULL** publishes NULL (nothing to show).

## 2. Readers

- **At its Commit or redraw, a viewer** reaches the share of each shared visual in its tree through a pointer. Under the
  shared lock it takes references to the share's snapshot and to the snapshots of its nested markers, and leaves the lock.
  It then appends their layers outside the lock. The layers are composed with the shared visual's matrix and placed in
  front of its content and behind its children.
- **A viewer never enters another device's critical section.** Validation counts the snapshot's layers instead of walking
  the portal tree.
- **Lock order:** a device's critical section, then the shared lock. The shared lock is a leaf. Under it, the code only
  follows pointers, changes reference counts, and links or unlinks lists. It never allocates, never makes a final
  Release, and never calls into dxgi, d3d or another device.
- **Where a reader can still block:**
  - on its own device's critical section, held by another thread of the same device;
  - for a moment on the shared lock;
  - inside dxgi's `prepare_target_scene` and `set_target_scene` (present and GPU waits), as for its own content today.
  It never waits for a lock of another device, so two devices that show each other cannot deadlock.

## 3. Lifetime

- **Contents stay referenced while a scene that uses them may be presented.** Each window target of the viewer keeps the
  snapshot references of its active scene (`target->shown[]`).
  - After a successful Commit or redraw has handed the new scene to dxgi (`set_target_scene`), the target's previous
    references are released. On failure the new ones are released, and the previous scene keeps its own.
  - This covers dxgi's later presents of the same scene, not only the hand-off.
  - Final Releases run after the viewer's critical section is left.
- **A released portal target** publishes NULL under the shared lock, unlinks itself from its share, and marks its viewers
  for redraw (P4). Its previous snapshot lives on while viewers' `shown[]` still hold it.
- **When the shared visual goes away first:**
  - It leaves the viewer's committed tree first, because the tree holds a reference. The viewer's next Commit stops
    showing the share, and its `shown[]` references go when that scene replaces the old one.
  - When its last reference is released, the visual unlinks itself from its share.
  - The portal target stays and keeps publishing snapshots that no one reads. A visual opened from the handle later links
    to the share and shows the current snapshot at once.
- **A swap chain removed from a portal tree** is unbound by the portal device at its Commit, as today. Its viewers redraw
  right after that Commit, and the swap chain object stays alive as long as `shown[]` holds it.

## 4. Redraw

- **Events that redraw viewers:**
  - a published snapshot, whether changed or NULL (root set to NULL, or a Commit that changed the portal tree);
  - a released portal target that had a snapshot.
  A Commit that left a portal's tree unchanged publishes nothing. The snapshot is replaced only when a commit applied
  changes under that root (a dirty flag set by the setters, as today's pending state is).
- **Who is redrawn:** the devices of the share's visuals, plus, transitively, the viewers of every share whose snapshot
  holds a marker of that share. They are gathered under the shared lock with a visited set, so cycles end. Each device
  is taken with a reference and appears once.
- **On which thread:** the thread that committed (or released the target), after it has left every lock. It runs each
  viewer's `internal_commit(redraw)`, which takes only that viewer's own critical section.
- **A redraw never changes a tree:**
  - it uses the active trees only, and pending state is not read or touched;
  - it changes no batch count and publishes no snapshot of the viewer's own portal targets, so it cannot cascade.

## 5. Pairing (P3)

- A share is found or created when a handle is opened as a visual or a target. That is the only place NtCompareObjects
  runs: once per existing share, under the shared lock.
- The visual and the target store the share pointer and unlink on their final Release.
- Commit, collection and redraw only follow pointers.
- A second target opened from the same object replaces the first as the portal (logged with a FIXME). Windows' behaviour
  there is not measured yet; the probe will measure it before the code lands.

## 6. Limits

- **When a snapshot is built:** the layer limit (`WINE_DCOMP_MAX_LAYERS`) and content limit (`WINE_DCOMP_MAX_CONTENTS`)
  are checked in the portal device's Commit, which fails with E_NOTIMPL as for a window target, and the previous
  snapshot stays.
- **When a viewer composes:**
  - If appending a portal (with its nested shares) would pass a limit, the viewer skips that portal for this scene and
    logs it once (WARN). The viewer's own Commit is not failed by another device's tree.
  - The same applies to a portal whose surfaces are on a D3D device the viewer's producer cannot compose
    (E_NOTIMPL today).
- **Nesting:** markers are resolved at read time with a depth bound of 4 and a visited set along the path.
  - A share already on the path (a cycle: A shows B, B shows A) is skipped, with one FIXME.
  - A deeper one is skipped too.
  - So resolution always ends, and since no reader takes another device's lock, nothing hangs.

## 7. Semantics

- A viewer shows only committed state: the newest snapshot published before it took the reference, at its own Commit or
  at a redraw. A snapshot is complete when published and never changes, so a viewer never sees part of a portal Commit.
- Between a portal Commit and the viewers' redraw, the window keeps the previous complete scene.
- Animations inside a portal tree are sampled at the portal's commit time (today's behaviour for any committed tree).

## 8. Tests (dcomp.c `test_shared_visual_portal`, extended)

1. A duplicate handle opens the same visual on the same device (also after the original handle is closed).
2. A portal Commit shows in the viewer's window (readback, as now), and the viewer's layer count follows. The count comes
   from a Wine-only query of the scene, or is checked by readback only.
3. Removal: root NULL plus a portal Commit clears the area in the viewer's window. A portal target released with a root
   clears it too (P4). Both are checked by readback.
4. Concurrency (P1): two threads, one committing the viewer and one switching the portal root between two surfaces (and
   SetContent(NULL)), each running about 500 iterations. No crash and no leak; refcounts are checked at the end.
5. A cycle (P2): devices A and B, each showing a portal of the other in a window target of its own. Both commit in a loop
   on two threads; they return, and the cycle is skipped. A nested chain three deep shows the innermost content.
6. Both architectures in the lab and the same PE test on Windows 11, where cases 4 and 5 may only check that nothing
   hangs. The dcomp and dwmapi suites both run. Notepad with the lab bypass: the compositor's surfaces still reach the
   window.

## What does not change

- Commit stays synchronous; a redraw is one extra present of a viewer's last committed tree.
- Handles of other objects are refused (E_HANDLE), as measured.
- Window targets and their scenes work as today; only the portal path changes.
