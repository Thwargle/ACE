# Remote movement and floating VR panels — September 27, 2026

Scope: shared client movement presentation, tracked-avatar timing, and small-head-movement jitter reported with World-anchored panels on standalone Quest. This is a source/build validation pass, not a published release.

## Retail comparison

Reviewed the local 2013 retail decompilation under `ThirdParty/acclient-AI-RE/2013-09 11.4186/acclient-src/src`:

- `PORTAL/cpmanager/CInterpolationManager.c`, `adjust_offset` and `UseTime`: grounded position correction has a 0.05 AC-unit completion tolerance, a speed bound derived from twice adjusted movement speed, and a separate correction queue. Normal locomotion and network correction are separate responsibilities.
- `PORTAL/cphysobj/CPhysicsObj.c`, `MoveOrTeleport`: teleport epochs are distinct from ordinary position updates. Retail also has different airborne correction handling; this pass preserves our existing tested ballistic handling rather than replacing it wholesale.

Our visible actor previously exponentially filtered every simulated movement step. Even a perfect constant-speed stream accumulated a trailing offset, then accelerated/decelerated again at changes in movement. Every small grounded packet difference also reset the predictor and introduced lateral correction movement.

The client now transports simulated movement directly and smooths the remaining network error. Grounded corrections smaller than 5 cm no longer reset the predictor. The existing correction-speed bound, teleport handling, collision-constrained prediction, and terrain seating remain in effect. Transport is limited to available progress toward the predictor so a correction just behind the visible actor cannot carry it past a wall or cliff. Grounded vertical contact correction is not applied twice to the already seated visible feet.

These client changes apply to both desktop and VR observers, including on stock ACE/GDLE servers. They do not change the wire format or require those servers to send more frequent ordinary movement packets.

## Tracked-avatar timing

- Each outgoing tracked pose now follows a current AutonomousPosition report. The independent timed position sender yields to an active pose stream and resumes when tracking pauses, avoiding duplicate timed reports.
- On our VR server, a same-cell pending movement report is consumed through `UpdatePlayerPosition` before the pose's authoritative root is published. Teleport, alive, cell and epoch guards remain; cell crossings keep normal world-update ordering. The incoming cosmetic pose never supplies an unchecked collider position.
- Equipping a weapon or ammunition no longer discards the entire pose interpolation history. Root, head and hands continue on their timeline; only the changed item's transform starts from its new pose.

The server relay improvement requires the corresponding server update. The client movement and buffer improvements operate independently of that update.

## Floating panels

World anchors already detach from the tracked head/controller hierarchy. Added repeated tiny position/rotation-noise checks confirming that compass and vitals transforms stay fixed, including between tracking and panel ticks. World mode does not acquire a head-follow filter.

There was a separate rendering defect: the panel render targets had a single mip, and the engine's default widget material uses a shared world sampler whose default mip filter is point. Fine details can shimmer as their projected size changes even while the panel transform is fixed.

Vitals, compass and fellowship now use `UACEVRWidgetComponent`:

- Full mip chains for their render targets, regenerated after successful Slate content draws and after resizing.
- A dedicated pass-through HUD material honoring the texture's trilinear sampler, with view mip sharpening disabled.
- Existing manual redraw/cadence limits remain. Head motion alone does not regenerate mipmaps. Other large retail/menu surfaces retain their existing rendering path.
- The material is included in `ACEPrepareRuntimeMaterials` and shared to Quest by the normal source/material synchronization script. Releases must use the normal cook pipeline to include it; an incremental native-code APK alone is not a complete updated release package.

Body-follow panels additionally ignore a 0.75 cm resting-head translation band while transporting locomotion directly. This is separate from the reported World-mode case.

## Validation

Logs and automation reports: `Unreal/Saved/ReleaseValidation/sep27-network-panels/`.

- 83 server VR combat/physics tests passed, including queued-root validation, epoch rejection, and preserving cell-transition ordering.
- Windows editor, Windows game, and Android/Quest native builds passed.
- Runtime material preparation succeeded with 36 parents, including the new filtered HUD material.
- All eight client automation suites passed: `InteractionRecovery`, `MovementReview`, `NetworkTransport`, `DesktopObserverNegotiation`, `PoseBuffer`, `RenderingAndReplication`, `RigAndMenus`, and `WidgetFiltering`. Results are recorded in `Report/index.json`.
- The straight-running fixture measured 0.0000 cm/s speed error and 0.0000 cm lateral noise at 30/90/144 FPS. The cliff fixture measured 0.0000 cm overshoot at all three rates. These are deterministic fixture results, not measurements of a live Internet connection.
- Shared Unreal/Quest source and material verification found zero differences; `git diff --check` passed.

The movement fixtures exercise 30/90/144 FPS, sparse position updates, small lateral noise, stops, real DAT actors, slopes, cliffs, jumps, bounded late corrections, and teleports. GPU readback checks actual lower-mip contents after the initial draw, a redraw at the same clock time, and resizing; material checks ensure the trilinear sampler is used. VR fixtures cover anchor transforms, control interaction, and remote rendering/replication.

No Quest was connected during validation. The changes address identified code/rendering causes, but perceived World-panel stability, GPU cost, and live side-by-side network movement still need an in-headset comparison. No version bump, publication, installation, or Git commit was made.
