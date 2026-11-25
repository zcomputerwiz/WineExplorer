# dcomp portals: snapshot design

**Signed off 2026-11-24 by the Session 0 reviewer and the orchestrator, for Integration, which was not running. To be shown
to Integration when it runs again.** The sign-off covers this note as amended with D1-D3 and A1-A4. The seven departures
listed under "History" were accepted on 2026-11-25 as amendments by the reviewer (code review of Wine 82f9cc65) and the
orchestrator. The body below states the rules as implemented (Wine branch `claude/notepad-portal`).

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
    - its visual: one at a time, on the device that opened the object as a visual first;
    - whether a target was ever opened from it, and whether that target is alive;
    - its current snapshot, or NULL.
  - Its visual and its target hold references to it.
- **Snapshot** (`struct dcomp_portal_snapshot`): immutable and refcounted. It holds:
  - the layer list that collection produces (`wine_dcomp_layer_desc`: matrix relative to the portal root, clip and corner
    radii, opacity and its mode, interpolation, border, size, `subtree_end` relative to the snapshot);
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
- **Every successful Commit publishes** a new snapshot for each portal target with a root. A surface drawn since the
  last Commit changes no setter, and is shown only after a Commit of its device, so there is no dirty flag.
- **A redraw publishes too:** it builds the device's portal snapshots again from its committed trees (no tree change)
  before its own scenes, so a portal tree that holds another portal's visual shows that portal's newest snapshot.
- **How it is swapped in:** under the global shared lock (`share->snapshot = new`). The old snapshot is released after the
  shared lock is left, still under the device's critical section. A snapshot may be released under a device's lock, never
  under the shared lock: a surface's last Release takes no lock (it clears the device's drawing pointer with a
  compare-exchange), and a device's destruction takes none either.
- **A root of NULL** publishes NULL (nothing to show), if something was shown.
- **Nested portals (D1).** A shared visual inside a portal tree whose share has a snapshot is flattened into the new
  snapshot at build time: its layers are copied in, with a reference to each surface, composed under the nested visual's
  matrix and kept inside its subtree, until the layer limit (`WINE_DCOMP_MAX_LAYERS`) is reached.
  - Layers past the limit are dropped, with one WARN.
  - Because a snapshot never refers to another snapshot, a cycle (A shows B, B shows A) copies at most one limit's worth
    of layers per build, and memory stays flat however often both commit.
  - The nested content stays current through the redraws (section 4): a Commit of the inner portal's device redraws the
    device that holds its visual, which publishes its own portals again. Notepad nests this way: its window shows the
    UI thread's portal, whose tree holds the shared visual of the compositor's portal.

## 2. Readers

- **At its Commit or redraw, a viewer** reaches each shared visual's share through a pointer. Under the shared lock it
  takes a reference to the share's snapshot and leaves the lock. It then appends the snapshot's layers after the shared
  visual's content layer and before its children, so they lie inside the visual's subtree.
- **A viewer never enters another device's critical section.** Validation does not walk portal trees; a portal that does
  not fit the scene is left out when the scene is collected (section 6).
- **Lock order:** a device's critical section, then the shared lock. The shared lock is a leaf. On the commit, redraw and
  collection paths the code under it only follows pointers, changes reference counts (never a final Release), and links or
  unlinks lists; it never allocates and never calls dxgi, d3d or another device. Opening a handle (and making one) also
  queries the server under it (the registry's lookup and sweep), and opening a visual creates it there, so that of two
  opens at the same time one is refused.
- **Where a reader can still block:**
  - on its own device's critical section, held by another thread of the same device;
  - for a moment on the shared lock;
  - inside dxgi's `prepare_target_scene` and `set_target_scene` (present and GPU waits), as for its own content.
  It never waits for a lock of another device, so two devices that show each other cannot deadlock.

## 3. Lifetime

- **Contents stay referenced while a scene that uses them may be presented.** Each window target of a viewer keeps the
  snapshot references of its active scene (`target->shown[]`).
  - After a successful Commit or redraw has handed the new scene to dxgi (`set_target_scene`), the previous references
    are released. On failure the new ones are released, and the previous scene keeps its own.
  - These releases run under the viewer's critical section, which is allowed (section 1).
- **A released portal target keeps showing its last snapshot** (measured on Windows 11: what a released portal target
  committed last stays visible in the shared visual, also after the viewer commits, takes the visual out of its window and
  puts it back). Release publishes nothing and redraws nothing. Only a root of NULL committed before takes a portal away.
- **The snapshot goes once neither the target nor a visual is left.** When the visual's or the target's last reference
  goes and the other is gone too, the share drops its snapshot.
- **When the shared visual goes away first (A2):** it leaves the viewer's committed tree first, because the tree holds a
  reference. On its final Release it unlinks from its share; the portal's snapshot then has no viewer. The portal target
  keeps publishing. A visual opened later (by the same device) shows the current snapshot at once.
- **When the portal device goes away while a viewer holds its snapshot (A2):** the snapshot holds only surfaces. A surface
  holds its dcomp device and its rendering device, so both live until the viewer's next scene releases the snapshot.

## 4. Redraw

- **The event that redraws viewers:** a successful Commit (or a redraw) of a device that publishes a portal snapshot,
  whether new or NULL. A released target redraws nothing (section 3).
- **Who is redrawn:** the device of the share's visual, taken with a reference under the shared lock. A redraw republishes
  that device's own portals (section 1), and the devices that show those are redrawn in turn. All of this runs as one work
  list per Commit in which each device appears at most once (`DCOMP_MAX_VIEWERS`), so a cycle of portals ends.
- **On which thread:** the thread that committed, after it has left every lock. It runs each device's
  `internal_commit(redraw)`, which takes only that device's own critical section. The viewers are redrawn before the
  committing device signals its commit completion event.
- **A redraw never changes a tree:**
  - it uses the active trees only, and pending state is not read or touched;
  - it counts no batch (methods 45 and 46).
- **A redraw revokes** an auxiliary producer it no longer presents through, as a Commit does, so a portal taken away leaves
  the window.

## 5. Pairing (P3) and the registry (D3)

- **Handles.** DCompositionCreateSharedVisualHandle returns an unnamed event. Nothing is put into `\BaseNamedObjects`;
  this follows the cross-server rule, under which no new Wine-internal names go there.
- **Opening a handle.** OpenSharedObject accepts a handle only if NtCompareObjects matches it with a registry entry's
  duplicate, so it opens the same object as a handle that DCompositionCreateSharedVisualHandle made in this process.
  - Anything else is E_HANDLE, as measured on Windows 11: an event of the caller's own, a named event of another
    process, a closed handle, the process handle.
  - This lookup is the only place NtCompareObjects runs: once per listed share, under the shared lock.
  - Wine has no cross-process composition, so a handle passed to another process opens nothing there (E_HANDLE).
  - An unsupported interface is E_INVALIDARG (measured).
- **One visual at a time, on its first device.** While a visual opened from the object lives, another open is refused with
  E_ACCESSDENIED. Another device stays refused after it is released; the first device opens the object as a visual again.
  Measured on Windows 11 (`tests/uwp/alpc/dcomp-sharedopen-probe.c`). The share remembers the first device by a serial
  number, not a pointer.
- **An object opens as a target once.** A second target opened from the same object is refused with E_ACCESSDENIED, on
  the same device or another one, and still after the first target has been released (measured, same probe).
- **Linking.** The visual and the target store the share pointer and unlink on their final Release. Commit, collection
  and redraw only follow pointers.
- **Dropping registry entries.** An entry is dropped when ObjectBasicInformation shows that the share's duplicate is the
  only handle left to the event, which means every caller handle is closed. The check runs on each create and each open
  (a sweep of the registry).
  - Dropping releases the registry's reference. A share that is still linked to a visual or a target lives on, but cannot
    be opened again; no handle to it exists any more.
  - The duplicate is closed when the share is freed.

## 6. Limits and what a portal can show

- **Build time:** the layer and content limits are checked by the portal device's validation before the switch. A
  failure fails that Commit with E_NOTIMPL, as for a window target, and the previous snapshot stays. Flattened nested
  layers stop at the layer limit (D1).
- **Surfaces only (D2).** A snapshot carries only IDCompositionSurface content. A swap chain in a portal tree is left out
  when the snapshot is built: the layer is kept without content, so its children still show, the Commit succeeds, and a
  FIXME is logged once per device.
  - The reason: a viewer would bind the portal device's swap-chain producer to its own window, the portal device could
    revoke it, and two viewers would fight over it. Leaving out one layer is the smaller failure than stopping every
    update of the device's windows.
- **Same D3D device.** A viewer presents portal surfaces through its window target's auxiliary producer, and that
  producer composes only surfaces of its own D3D device. It is made from the viewer's own first surface, or from the
  first portal surface when the scene has none of its own.
  - **Notepad:** the window's scene holds only the compositor's surfaces, all on the compositor's device. Measured in
    the lab: the window's scene holds the shared visual, the UI thread's portal layer and the compositor's five layers.
  - **Otherwise:** a portal surface on a device other than the producer's is left out of the scene (its layer keeps no
    content), with one WARN. The viewer's own Commit is not failed by another device's tree.
- **Viewer limits:** if appending a portal would pass the viewer's layer limit, the viewer leaves that portal out of the
  scene, with one WARN.

## 7. Composition and semantics (A3)

- **Composition.** Snapshot layers are stored relative to the portal root. The viewer multiplies the shared visual's
  world matrix into each layer's matrix, and puts the layers inside the shared visual's subtree (`subtree_end` rebased).
  - The renderer tests each layer's clip per pixel, in that layer's own space, and a layer's clip also cuts its whole
    subtree.
  - So the portal layers' own clips and the shared visual's clip are exact under any transform, rotated and skewed
    included. This is the rule for nested visuals; nothing is refused.
  - Opacity and the opacity modes are inherited through the subtree, as for children.
- **Semantics.** A viewer shows only committed state: the newest snapshot published before it took the reference, at its
  own Commit or at a redraw. A snapshot is complete when published and never changes, so a viewer never sees part of a
  portal Commit. Between a portal Commit and the viewers' redraw, the window keeps the previous complete scene.
  Animations inside a portal tree are sampled at the portal's commit time.
- **Surface pixels** are copied when a scene is presented (dxgi's frame of a composition surface is a CPU copy of its
  texture at that moment). A surface whose draw is open (between BeginDraw and EndDraw) composes as transparent, so a
  redraw that meets a portal surface in the middle of the compositor's next draw shows that layer empty for one present,
  not half drawn. Keeping the last finished frame per surface is a possible follow-up.

## 8. Tests (dcomp.c, `test_shared_visual_portal`, `test_shared_visual_portal_cases`, `test_shared_visual`)

1. A duplicate handle is the same object, also after the original handle is closed. A second visual is E_ACCESSDENIED
   while the first lives; another device stays refused after it is released; the first device opens it again. A second
   target is E_ACCESSDENIED, while the first lives and after it is released.
2. A portal Commit shows in the viewer's window, and a Commit of the portal device alone with new surface pixels shows
   them (readback).
3. Removal: root NULL plus a portal Commit clears the area (readback). A released portal target keeps showing its last
   content, also after the viewer commits and puts its visual back.
4. Concurrency (P1): two threads, one committing the viewer, one switching the portal root between two surfaces and
   SetContent(NULL), 500 iterations each; failures and reference counts are checked at the end.
5. Nesting (D1): a third device's window shows A's portal, whose tree holds the visual of B's portal; B's content shows,
   and a Commit of B alone shows B's new content.
6. A cycle (P2): A's portal tree shows B's portal and B's shows A's, and both commit in a loop on two threads. Both
   return, and the process's private bytes stay flat (Wine).
7. A swap chain in a portal tree (D2): left out (a FIXME), the Commits succeed, and the sibling surface still shows.
8. Registry (D3): an unnamed and a named event of the test's own are refused (E_HANDLE); a closed shared handle is
   refused; repeated create and close cycles leave the process's handle count flat (Wine).
9. Both architectures in the lab, and the same PE test on Windows 11 (Windows releases some objects of the last frames
   later, so some reference counts are broken() there). The dcomp and dwmapi suites both run. Notepad with the lab bypass:
   the compositor's surfaces reach the window.

## What does not change

- Commit stays synchronous; a redraw is one extra present of a device's last committed tree.
- Window targets and their scenes work as before; only the portal path changes.
- Cost to watch (review Q2): every compositor Commit redraws the UI device, republishes its portals and redraws the
  window. Collapsing redraws (once per device per clock tick) is a possible later step.

## History: how the code came to differ from the first signed-off note

Measurements and Notepad made the code differ from the note as first signed off. The reviewer and the orchestrator
accepted all seven on 2026-11-25; the body above already states them.
1. **Visuals:** an object has one visual at a time, on the device that opened it first (measured), not one visual per
   device.
2. **Released portal target:** keeps showing its last snapshot (measured); release does not clear the portal, a root of
   NULL committed does. The snapshot is dropped once neither the target nor a visual is left.
3. **Nested portals stay current:** Notepad nests, so "the nested content is as new as the outer portal's last Commit"
   would freeze the compositor's content. A redraw republishes the redrawn device's portals, and redraws propagate as a
   work list with each device once per Commit.
4. **Every Commit publishes** (no dirty flag): a surface drawn since the last Commit changes no setter.
5. **Freeing:** a surface's last Release takes no lock, so a snapshot is freed where its last reference goes, never under
   the shared lock, instead of after every lock is left.
6. **Allocation at open:** opening a handle as a visual creates the visual under the shared lock.
7. **Redraws revoke** an auxiliary producer they no longer present through, as a Commit does.
