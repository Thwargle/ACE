# Collision, remote movement, camera and VR interface review

## Findings and changes

The movement fixes are shared by Windows desktop, PC VR and Quest. Reported
locations are regression fixtures; there are no location-specific runtime rules.

### Creature bodies and passage width

Compared the actual DAT setups with retail `CPhysicsObj` / `SPHEREPATH` use of
authored spheres. Creature components already used the authored shapes rather
than visible wings or arms. Remote movement prediction, however, still swept the
larger `Setup.Radius` bound. It now derives its sweep width, height and vertical
offset from the collision shapes, keeping the bounding radius for interaction
range calculations.

- White Phyntos Wasp (weenie 7105, setup `02001121`, scale 1.2): authored radius
  0.50 AC becomes 0.60 in the world; the old bounding radius became about 0.849.
- Tusker Guard/Slave (`02000964`): one 0.996 AC sphere, versus a 1.409 bound.
- Virindi (`02000041`): two 0.534 AC spheres, versus a 0.755 bound.

Shared body sweeps now query the lower and upper spheres rather than filling
their connecting waist with a capsule cylinder. The actual creature meshes and
analytic sphere-pair passage widths are exercised on both sides of the blocking
threshold. This does not remove creature collision or make limbs into obstacles.

### Walls, stair lips and existing embedded positions

Upper-body contacts now remain obstructions even when their separating normal
points upward. Floor-clearance, step and landing logic may only treat lower-body
contact as support. Otherwise an upper sphere touching a ledge could be treated
as a floor and ignored during a stair step.

The two reported Mosswart positions (`E454001E`, approximately
79.809/133.745/20.761 and 79.648/134.836/20.869) are already inside the tower's
parapet according to the retail PhysicsBSP too. In that state, per-triangle
separation directions can contradict each other. A bounded placement recovery
now searches contact directions for the nearest clear body position, validates
the reverse path, and rejects newly crossed solids. Opposite directions are
considered only for deep overlap of both spheres; ordinary shallow contacts
cannot invoke that escape. Creature contacts cannot invoke this fallback.

The tests distinguish moving into a wall from recovering a position already
inside one. Recovery from the latter may legitimately fall to the lower walkway;
it must land and restore movement rather than remain suspended. Existing valid
stair approaches must remain grounded.

Retail references: `PORTAL/cphysics/CTransition.c`,
`PORTAL/cphysics/CSpherePath.c`, `PORTAL/cgeometry/CBSPTree.c` (placement insertion),
and `PORTAL/cphysobj/CBuildingObj.c` under the local 2013-09 11.4186 client source.

### Remote movement and network presentation

Reviewed F748 position/contact/velocity parsing, F74C vector updates, sequence
and teleport handling, locomotion integration and VR root presentation. Ordinary
updates continue correcting the predicted anchor; real tracked teleports retain
their explicit snap path.

- Grounded extrapolation follows supported steps and slides/stops at unsupported
  edges, using the setup's step-up/down values. It cannot simply run horizontally
  into the void until the next sparse position packet arrives.
- The support check tolerates the authored step range for server feet offsets on
  slopes; a small height discrepancy no longer drops the support constraint.
- Prediction and display sample support at their own XY positions, retaining
  smooth terrain contact while the displayed position converges.
- Large corrections have a speed bound, following retail
  `CInterpolationManager::adjust_offset` (twice adjusted locomotion speed;
  7.5 AC/s fallback). Exponential smoothing alone allowed very large individual
  frame displacements. Normal and tracked-root presentation use the same bound.

This retains the current predictor; it is not a wholesale replacement with
retail's entire physics engine/interpolation queue. Server corrections remain
authoritative. Live side-by-side multiplayer testing is still needed to assess
real packet jitter, latency and contact behaviour with a full creature crowd.

### Mosswart courtyard scenery

Retail `CLandBlock::init_buildings` associates a building with the outdoor sort
cell containing its origin; `get_land_scenes` rejects generated scenery in that
cell. The prior triangle-footprint suppression left courtyard gaps. Scenery
generation now uses the sort-cell rule. The reported tree is in building-occupied
cell `E4540015`. Scenery disk-cache schema was advanced so old placements rebuild.
Authored building/scenery objects are retained.

### Camera

Retail `CameraSet::Raise/Lower` rotates the camera offset while preserving its
length. The pitch-dependent zoom limit now applies when zooming farther, rather
than reducing the user's saved distance while changing angle. A raised, fully
zoomed camera can lower its angle without zooming in, and zooming closer still
works from that position.

### VR interface

The fellowship HUD shows only other members' names and vital bars. It omits the
local player, fellowship title/status and full grey background, sizes to the
visible roster, and retains selection and unlocked placement controls.

VR options are grouped into Movement, Panels, Combat, Controls and Graphics.
Each section scrolls independently, with shared button, slider and dropdown
styles. Category captions and dropdown arrows were checked in rendered images.

## Validation

Results and screenshots are stored in
`Unreal/Saved/ReleaseValidation/sep26-collision-network`.

- Final automated run: **11 suites passed, zero failures** (`Report-final/index.json`,
  `tests-final.log`). This includes both the latest upper-body ledge regression
  and sparse cliff updates with a server position 20 cm above the support plane.
- Windows editor and game Development builds succeeded (`build-final.log`,
  `build-win64.log`). Quest Android Development compile and APK generation
  succeeded (`build-android.log`).
- Quest source/runtime material synchronization check: zero differences.
- Visually inspected the fellowship HUD and each VR settings category from
  rendered automation screenshots.

Coverage:

- DAT-based creature passage tests, with White Phyntos Wasp, Tusker and Virindi.
- Fort Teth/Mosswart wall zigzags, embedded-position recovery, stair ascent,
  descent, close-edge traversal and falling/jumping at stair entrances in both
  desktop and tracked modes.
- Existing academy corners, ledges, stair ceilings and wall-contact suites.
- Remote cliff stopping, incline contact and correction convergence at 30, 90
  and 144 FPS; explicit tracked teleport retained.
- Camera angle/distance regression, scenery suppression and VR panel rendering.
- Existing desktop observer negotiation and tracked rendering/replication tests.

No release has been published or installed on a headset. The Android build
generated a local development APK; release version numbers were not changed.
Local build/test results are separate from live gameplay verification.
