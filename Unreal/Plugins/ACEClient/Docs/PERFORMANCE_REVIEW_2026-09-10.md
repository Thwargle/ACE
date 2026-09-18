# Client performance review — 2026-09-10

This review covers the Unreal client runtime: network dispatch, world and terrain
presentation, cell visibility and collision, DAT/cache loading, character
appearance and animation, particles/lights, UI refresh and scene captures. Server
code and retail disassembly were used as protocol/behavior references. This is a
source review plus deterministic fixtures, not a live-server frame-time benchmark.

## Evidence and measurement limits

The reported marketplace screenshots show about 12.2–13.6 ms of game-thread time,
roughly 2,500–3,300 draws, and 76,000–109,000 primitives. Low polygon count does not
make thousands of component updates, material submissions, collision queries and
draw calls inexpensive. The hardware query confirms an RTX 5090 with 32 GB VRAM
and an Intel Core Ultra 9 285K (24 cores). More GPU capacity cannot remove serial
game-thread work.

The screenshot's approximately 3 ms GPU figure is insufficient to establish the
main-view GPU cost: it also reports a 128×128 render resolution, consistent with
a scene capture affecting the displayed counters. Measure the main view in
Unreal Insights before assigning an overall CPU/GPU bottleneck or promising FPS.
The configured renderer already disables Lumen GI/reflections and uses classic
cascaded shadows; this change does not lower visual quality or drawing distance.

## Corrected unnecessary work

1. **Settled remote characters still ran movement ticks.**
   `ACEWorldEntityActor::RefreshActorTickEnabled` treated every creature as needing
   a tick, even after prediction, ground placement and rotation had settled.
   Idle actors now sleep; position/motion packets, strafe, pending placement,
   attachments and active prediction still wake or retain them. Appearance
   animation remains independently active. The 64-idle-player fixture changed
   from 64 ticking actors / 0.049 ms actor work to zero ticking actors / below
   displayed timing precision. Animation remained about 0.60–0.63 ms. This fixture
   has no room collision geometry and is not a marketplace FPS measurement.

2. **Static inventory portraits recaptured eight times per second.**
   `RefreshPaperDollPreview` holds an authored pose. It now captures when the
   appearance changes or selection flashing needs a new image. Hidden creature
   inspection also disables its otherwise invisible animation tick. Visible
   inspection retains its animated capture schedule.

3. **Short-lived particles recreated render proxies.**
   A glow could expire and be replaced with the same pooled component in one
   simulation update, yet hide/show operations invalidated its rendering state.
   Components left unused are now hidden after all emitters update. A replacement
   glow retains its component, material and render proxy. `ParticleTiming`
   verifies this after flushing prior render work, along with continuous pillar
   visibility/scale/opacity at 24, 30, 60, 120, 144 and 240 Hz.

4. **Effect lights repeated work between simulation updates.**
   Their updates now follow the particle simulation cadence. The portal-roof
   occlusion query is restricted to portal effects instead of running for wand
   lights as well. Held-item ticks wait for their wielder's animation pose, and
   scripts wait for their own appearance pose. DAT birth intervals, lifetimes,
   sizes and trajectories remain authored values.

5. **Stationary terrain rewrote unchanged material parameters.**
   `SetLandLookOutClip` now propagates material parameters only when the camera,
   enable state or aperture records change. New material creation still receives
   the current values. Portal masks and visibility behavior are preserved.

6. **Cache cleanup could run with an incomplete DAT identity.**
   With deferred cell loading, the portal-only fingerprint could trigger removal
   of valid full-DAT cache shards. Maintenance now waits for both portal and cell
   DATs and permitted world streaming. MeshApplication checks the deferred state
   and stable restored fingerprint. This prevents avoidable cache regeneration
   at login; it is separate from steady-state rendering cost.

7. **Bitmap chat measurement and paint disagreed about line wrapping.**
   Measurement used an effectively unlimited width while paint used the actual
   narrow chat window, causing clipped multi-line messages and alternating layout
   cache widths. Measurement now tracks the allocated native width. Main and
   auxiliary chat fixtures verify the resulting height against multiple lines.

## Remaining costs to profile before redesigning

- `ACECellTransit::RestrictRoomCollision` iterates resident environment actors to
  build exclusion lists for queries. Active movement and ground resolution can
  repeat this work in crowded interiors. A room/collision index may help, but its
  invalidation must follow residency and cross-cell furniture membership.
- `ACETerrainPresenterComponent::UpdateCameraVisibility` performs aperture/frustum
  traversal and applies room/land visibility repeatedly. A cache would need camera,
  cell, portal and residency revisions; a camera-only shortcut risks missing
  geometry after streaming or teleporting.
- Multipart characters, procedural particle components and numerous material
  sections create many submissions relative to the small triangle count. Shared
  geometry and compatible-material batching are candidates. Existing particle
  instancing must not be enabled indiscriminately: alpha/depth behavior was a
  source of earlier visual defects.
- Appearance rebuilds allocate/build parts and materials on object-description
  changes. Captured logs contain individual rebuilds taking approximately 15–18 ms.
  Profile their frequency and separate decoding, component creation and render
  updates before adding broader caches.
- Selection marker lookup scans world entities while a selection is active.
  The HUD fixture is around 0.75 ms in the binder and 0.85 ms overall; this is
  smaller than the reported total game-thread time but merits measurement in a
  populated scene. A GUID lookup with correct spawn/destruction invalidation
  would avoid the actor scan.

`stat ACE` now exposes Network/dispatch, World presentation, Terrain presentation,
Portal visibility, Remote movement/ground, Character animation, Appearance
construction, Scripts/particles/lights and HUD refresh. Corresponding `ACE_*`
Unreal Insights CPU scopes are compiled into Development builds. Some scopes are
nested, so do not sum all inclusive durations. Compare a stationary marketplace,
walking through it, a portal transition and an outdoor run at the same resolution,
frame cap and UI state. Measure the main view's GPU pass timings separately.

## Marketplace freeze investigation

The preserved user log continued receiving object creation, appearance and sound
updates during the reported interval. It does not prove a wholly stalled
connection. Two concrete failure paths were corrected:

- Ordered UDP recovery now requests a single missing packet after a reorder grace
  period and retries on the session tick, even without additional arrivals. It
  retains queued packets and the acknowledgement cursor; it does not skip gaps.
- Manual movement clears stale client approach/use-wait state instead of allowing
  that state to suppress movement transmission. Server use completion remains
  authoritative; there is no speculative timeout that completes a use operation.

NetworkTransport verifies the single-gap, retry/rate-limit and subsequent ordered
drain behavior. The exact marketplace immobilization has not been reproduced, so
live confirmation remains necessary.

## Verification

The editor build passed. `ACE.RetailParity` passed all 36 tests (32 clean, four
with warnings, zero failed or skipped), in
`Unreal/Saved/Automation/2026.09.10.1-full/index.json`. Warnings are the deliberate
world-entry failure fixtures, unresolved non-applicable effects in particle
fixtures, and console-setting priority in scalability tests. BuildCookRun
completed with exit code 0. The packaged executable passed Materials,
MeshApplication and Weather (three tests, zero failures), recorded in
`Unreal/Saved/Automation/2026.09.10.1-packaged/index.json`. Two tests carry
warnings: the existing uncooked engine debug vertex-color material fallback and
the screen-percentage console-setting priority. Neither warning is hidden by the
test run; the debug-material fallback remains a packaging limitation to address.

Inspection tests now switch through actual DAT setups, including a creature
without a human head part followed by a server-style head replacement. They
verify geometry, capture visibility and animated framing; the resulting image
shows the restored head. Wand tests cover real DAT emitters, gravity direction,
parent space, size and render-proxy continuity. These tests do not replace a
live visual comparison of every rending effect or establish a measured FPS gain.
