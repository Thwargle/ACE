# Retail parity regression tests

Run from `C:/dev/ACE` in PowerShell after building `ACUnrealEditor`:

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/dev/ACE/Unreal/ACUnreal.uproject' -unattended -NullRHI -nosound -NoSplash '-ExecCmds=Automation RunTests ACE.RetailParity' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=C:/dev/ACE/Unreal/Saved/Automation/RetailParity' '-abslog=C:/dev/ACE/Unreal/Saved/Logs/RetailParityTests.log'
```

`-RetailDatDir="path"` overrides the test default `C:/Turbine/Asheron's Call`.
The fixture comparisons require the same DAT version used to generate the fixtures.

For GPU coverage, replace `-NullRHI` with `-RenderOffscreen`. The full suite contains
25 tests. The material, font, weather, UI, Yaraq scene, and runtime actor checks
perform GPU checks with that option and write PNGs under `Saved/Automation/RetailParity`. Inspect the report
test states: Unreal can return process exit code zero even when an assertion fails.

The suite checks portal plane convention and clipping through successive doors;
terrain continuity across cell boundaries; layout control types, fonts and state
inheritance; authored collision triangles and portal planes against the independent
server DAT reader; drawing-side counts, distinct front/back surfaces and UVs; and
real Chaos collision cooking with downward pawn sweeps over 32 authored indoor floors.
Runtime application checks cover procedural meshes, entity parts, and shared static
collision bodies, including preservation of stair faces and empty physics meshes.
The terrain mask test draws three disjoint triangular apertures, rejects their
corners and gaps, verifies that discarded terrain does not occlude a surface behind
it, and checks a live update to the plane texture.

Font checks compare all 49 fonts, every glyph record, and 392 label widths against
the server reader. The widget test compares GPU glyph coverage with source DAT A8
pixels, verifies native desired width and atlas reuse, and checks the spellbook's
32px item-slot template. Shared gameplay labels now use DAT bitmap fonts; the
spellbook uses the authored icon, text position, font, and row background.

The weather test renders the actual `01004C44` and `01004C42` rain curtains, checks
their winding and fixed world height, verifies UV animation, and tests outdoor
enable/disable, animation after indoor/outdoor restart, cached/new material fog
updates, and authored rainy/clear intervals. The clock uses retail's positive
epoch offset and PY 10 in day selection. Persistent daylight affects lighting without
freezing the weather clock.

RuntimeActors tests real building shadow casting across all 232 DAT lighting keys
at Yaraq's original world coordinates, isolated building receivers, and instanced
DAT foliage. Identical geometry and signed net darkening reject the old test's
noise/depth false positives. `-RetailShadowAtOrigin` is an optional diagnostic
control, not the normal acceptance position. StreamingCost records cold/warm DAT
human appearance build time and prevents per-polygon texture decoding; it does
not establish live traversal FPS. ParticleTiming verifies retail's one-birth
update ordering and newborn age after a hitch using an actual wand particle.

Regenerate the independent fixtures with the server's `ACE.DatLoader`:

```powershell
dotnet run --project Unreal/diag/UIInventory/UIInventory.csproj -c Release -- --world-fixtures "C:/Turbine/Asheron's Call" C:/dev/ACE/Unreal/Plugins/ACEClient/Tests/Fixtures/RetailWorld.json
dotnet run --project Unreal/diag/UIInventory/UIInventory.csproj -c Release -- --font-fixtures "C:/Turbine/Asheron's Call" C:/dev/ACE/Unreal/Plugins/ACEClient/Tests/Fixtures/RetailFonts.json
```

The checked-in sample covers 3,759 cells and 159 GfxObj models, including towns and
samples distributed across the cell DAT. There are 286 drawing-portal/physics ID
overlaps and 36 empty physics trees in the cell sample. Drawing and physics polygon
IDs belong to separate dictionaries; drawing opacity cannot remove collision faces.
`CPolygon::UnPack` and `D3DPolyRender::ConstructMesh` define the side rules: NoPos and
NoNeg omit UV index arrays; they do not omit geometric faces. Cull None shares the
front surface on both windings; Clockwise has distinct positive and negative surfaces.

Regenerate the retail UI layouts:

```powershell
dotnet run --project Unreal/diag/UIInventory/UIInventory.csproj -c Release -- "C:/Turbine/Asheron's Call" C:/dev/ACE/Unreal/Plugins/ACEClient/Docs/UI
```

The resolver follows `LayoutDesc::GetElementDesc`, `ElementDesc::Incorporate`, and
`StateDesc::UpdateSizeAndPosition` in the retail decompile. It preserves inherited
types, properties, media, states, and template child reflow. The gameplay layout has
1,870 fully typed elements. One unused root in layout `21000040` references missing
base element `10000480`; export reports and omits that root. Missing gameplay bases
fail the export.

Passing this suite is not a claim of complete one-to-one retail parity. It includes
focused offscreen renders, not a side-by-side gameplay comparison with retail.
Remaining work includes per-cell fragment clipping, remaining material opacity approximations, Setup sphere/cylinder collision, and
complete retail movement/transit and dynamic attachment behavior. Some manually
positioned UI lists and editable controls still use Slate fallback text or custom
geometry; international font fallback, rich text tags, and truncation also need
dedicated parity coverage. The shared labels and spellbook template are covered here.


Build 2026.09.05.2 adds the Yaraq scene regression (7D630100–119, five occupied
camera cells and 20 views, including the exterior building shells). Comparisons
exclude a one-pixel raster edge around the shell. Pedestal 01002A1B is checked
through the runtime static-building path for all three material slots.

The landscape transition tests cover full resolution and the 2/4 rings, including
the X/Y direction correction and preservation of source heights. The GPU portal
material test covers basement entry masking and restoration beyond courtyard exits.
Authored transparent portal polygons define building openings; guessed rectangular
cuts and wholesale suppression of translucent building sections were removed.

Font storage is byte-exact, but drawing bearings are signed and GetCharWidthA
returns an 8-bit advance. The previous fixture summed unsigned offsets without
retail's byte truncation; it incorrectly validated huge spacing in the fancy fonts.
The revised fixture includes ENTER, CREDITS and EXIT. UIScreens renders character
select at 800x600 and 1600x900 and checks button/text bounds, item-type background
mapping and mouse activation of casting tabs. It uses an offline client without a
network session, so the click check asserts the activation event rather than a
server-connected spell-set change.

Build 2026.09.06.1 fixes a runtime terrain material ownership regression: switching
the lookout state cloned the land material, leaving the rendered component outside
the cache that receives portal mask updates. RuntimeActors now exercises the actual
landblock actor, repeated entry/exit updates, the material attached to its mesh, and
doorway captures. YaraqDoorways covers the main room and vestibule in 7D64, including
admission of connected interior rooms from both sides of exterior portals.

Indoor vertex lighting now reads Setup light records and follows retail's point
light attenuation, visible-cell light gathering, and world ambient term. EnvCell
materials use a separate lighting channel so entering a room does not change the
sun or skylight for the entire outdoor scene. The EnvCell mesh cache schema is 11.
Particle textures retain straight RGB; Unreal's additive blend applies alpha once.

RuntimeActors also spawns all eight authored building shells through the normal
scenery path, checks immediate collision queries against authored indoor floors,
and compares captures with building shadows enabled and disabled. The final run
measured 3,953 darkened pixels. Skeleton and Banderling corpses are compared against
every part of the DAT's final death pose after spawning, receiving a late Ready
motion, and refreshing appearance. Corpses must not enter the chest/door On/Off
motion path.

Combat controls now retain slider power when selecting attack height, use authored
boolean-button states, and update the player options for repeat, targeting, and
combat view. Attack requests follow server CommenceAttack/AttackDone events;
CombatProtocol tests decoding, cancellation, and truncated messages. UIScreens
checks checkbox clicks, slider dragging, height selection, and preference
preservation across combat-mode changes. Chat window buttons have their labels;
the loot window retains the authored width and nested frame layout. Melee attacks
also apply the target facing and server turn requests to the local pawn.

Validation: build 2026.09.06.1 compiled successfully, and all 17 tests passed with
D3D12 offscreen rendering. The report is
`Saved/Automation/RetailParity20260906/index.json`; the log is
`Saved/Logs/RetailParity20260906.log`.

Live summoning remains unverified. The saved client log contains the Use request
for Acid Grievver Essence (50), but no subsequent pet ObjectCreate. Idle MoveToState
messages are now suppressed while an item use is busy, but this does not establish
the cause of that failed summon: the server's inventory PetDevice activation is
synchronous, and pet creation can fail before any object reaches the client. A
live server replay/log is required to resolve that remaining failure. The combat
protocol and control checks also do not replace a live melee gameplay test.


Build 2026.09.06.2 addresses the September 6 doorway, animation, and UI reports.
Visibility is refreshed after the final player camera update (TG_PostUpdateWork),
so the terrain portal mask and visible room set follow camera motion every frame.
The timer-driven collision/streaming path no longer supplies a stale camera mask.
Building openings now retain DrawingBSP PORT's CBldPortal index and polygon, and
use the building frame and CBldPortal side as PView::DrawPortal/ConstructView do.
They are not inferred from the reverse interior aperture or a building AABB.
Visible rooms are included in streaming requests; interior spawning can consume
its configured time budget instead of being limited to one cell per refresh.

Detail-level changes retain building/scenery resources within the keep ring.
Returning to full detail reuses components and restores their exact collision
mode, preserving separate visual and PhysicsBSP meshes. Destroying scenery also
releases its separate building collision components. Doorway suppression no longer
tears down the exterior shell. This removes known churn; it is not a measured
claim that live traversal now matches retail frame times.

Indoor ambient RGB values are used as lighting coefficients (byte / 255), not
sRGB texture colors. Burned vertex lighting is combined with ambient without
multiplying it by the outdoor diffuse term a second time, following retail's
D3DPolyRender lighting setup. This does not increase the outdoor sun globally.

Live Dead motion now plays the death action before holding its last frame; newly
created corpses still start at the final DAT death pose. Movement cancels idle
fidgets, and distance-throttled pose evaluation accumulates elapsed time instead
of slowing animation to a quarter of its speed. Standing mouse-turn prediction
is retained when the corresponding character option is enabled.

UI corrections include Say-popup placement and click priority, locked floaty-chat
frames, DAT title fonts, inspection layout 2100006B without the obsolete 2100001C
frame graft, a clipped/scrollable appraisal body, and its anchored inscription strip.
Inventory now displays the authored shortcut numerals, uses the container slot's
empty art, and clears double-click state after a cancelled drag. The equipment
checkbox uses its DAT BooleanButton state. These focused corrections do not cover
all inventory interactions or fix the remaining global stacking of custom overlay
text when floating panels overlap; editable controls and some popup styling also
still need parity work.

Validation: ACUnrealEditor Win64 Development compiled successfully. All 17 test
groups passed with D3D12 offscreen rendering on the final .2 binary (11 clean,
6 with warnings, 0 failed). Warnings include transient static-mesh bounds during
build/serialization and diagnostic material/cache messages; runtime visual and
collision assertions pass. Report:
`Saved/Automation/RetailParity20260906b/index.json`; log:
`Saved/Logs/RetailParity20260906b.log`. The expanded Yaraq scene checks 32 views in
eight occupied cells, including 7D64012C, 7D640131 and 7D640135 from the user's log.
The complete scene includes the heightfield: a reference without terrain falsely
exposed the pedestal's buried 7D640150/151 rooms through the ground. With terrain
present, the reported room views have zero lost opaque pixels. RuntimeActors also
checks final-camera portal updates, stable room residency, building reuse and
collision-mode preservation, plus live death progression. UIScreens checks a
clickable General choice, all four chat frames, appraisal scroll range, the third
shortcut numeral texture and the live mouse-turning preference.

These automated checks do not establish complete retail parity or a live server
replay. Live summoning remains unresolved as described above. The build is ready
for manual traversal and interaction testing through `Unreal/Launch.bat`.


Build 2026.09.06.3 continues the complete retail-client replacement work. Generated
UI content now follows the paint order of its owning floaty window, including
cached inventory contents. Foreground window bodies consume hits so hidden items,
sliders, and drop targets cannot be operated through another panel. Popup and drag
layers remain above ordinary windows, with Z values representable exactly in
Slate's float canvas ordering. Tests raise inventory and appraisal in both orders,
check draw order and hit occlusion, and exercise cancelled/same-cell drags.

The Say menu uses classic_chat 21000006's 191x17 two-column rows, font 40000001,
state art, text margins, and retail insertion order. Selected-player tell and
squelch actions and Olthoi chat are wired. Membership/listening-dependent enabled
states and localization still need review. Inventory activation now follows the
owned-object branch of ItemHolder::DetermineUseResult instead of guessed wearable
and charged-device exclusions. Tests inspect real packet construction for Use,
Wield, targeted activation, non-usable/wield-only items and local pack navigation.
This is not full ItemHolder parity: target compatibility, confirmation dialogs,
force-use, component packs and the complete conflict sequence remain outstanding.

NetworkTransport is a new eighteenth group. Damaged or out-of-order fragments do
not mutate reassembly before checksum validation and contiguous delivery. A
message completes at its ordered final fragment; duplicated packets cannot
recreate partial state. TimeSync is also validated and ordered. Outbound long
messages respect the 464-byte packet payload budget with separately sequenced
448-byte fragments; an actual loopback socket verifies metadata, payload and CRC.
World entry now waits for F7DF before sending F657, rejects unknown/deleted
characters and duplicate clicks/readiness, and permits retry after rejection.
The first full .3 run caught the missing rejection rollback; that was corrected
and verified before the final run below.

Final validation: ACUnrealEditor Win64 Development compiled successfully. All
18 groups passed on the final .3 DLL with D3D12 offscreen rendering: 12 clean,
6 with warnings, 0 failed, 0 skipped. Report:
`Saved/Automation/RetailParity20260906d/index.json`; log:
`Saved/Logs/RetailParity20260906d.log`. Warning groups include transient static-mesh
bounds during engine build/serialization and diagnostic material/cache messages.
New assertions confirm finite bounds on completed render and collision meshes;
the serialization warnings themselves have not been eliminated. Earlier Yaraq
scene, doorway, collision, shadow, weather, font and animation checks also pass.

The build identifier is 2026.09.06.3 in both ACEClientBuild.h and DefaultGame.ini;
`Unreal/Launch.bat` opens the rebuilt editor project for manual testing. This is
not a packaged standalone release or a feature-complete retail replacement.
`Docs/RETAIL_PARITY.md` records the evidence and remaining acceptance requirements,
including character lifecycle, live summoning and measured traversal performance.


Build 2026.09.06.4 fixes the reported building-entry crash. The preserved .3 dump
resolves GameThread to AACEEnvCellActor::TrySpawnOneStaticObject / ISM AddInstance.
Visual decorations without DAT PhysicsBSP had physics enabled despite lacking a
BodySetup, leaving an empty body array that later instance additions corrupted.
Both interior and outdoor ISMs now preserve the distinction between solid meshes
and non-colliding decorations. InteriorStreaming is a new group using the actual
7D63010D, 7D63010E and 7D630112 rooms: four collision transitions with continued
furniture spawning, five solid pools, two decoration pools, 32 instances.
Full evidence and symbol locations are in Docs/CRASH_20260906_BUILD3.md.

The redundant full-pool physics rebuild after each instance has been removed.
Room collision cooks the combined PhysicsBSP once, portal stencil state changes
only when necessary, and landscape loading no longer imposes a competing 192
entry setup cache limit over the DAT subsystem's 2,048 entry budget. These remove
known repeated work but do not establish measured retail-equivalent traversal.

The first full .4 run caught a separate setup cache lifetime fault in material
creation. Cache entries now have shared immutable ownership; render builders hold
their geometry across operations that pump completion tasks. Late duplicate jobs
preserve existing entries, and stale generations cannot clear current pending
requests. MeshApplication verifies growth, late completion and eviction while a
setup is held. The failing intermediate report is RetailParity20260906e; the
corrected full run is RetailParity20260906f below.

ParticleLighting is the twentieth group. All twelve directional blood PlayScript
IDs 0x5B..0x66 remain non-luminous for red, green and white blood on player pawns,
remote players and scenery/weapon owners. Neighboring non-blood and direct weapon
effects keep lights, including red spells. Blood SetLight hooks are also excluded.
Building shells use two-sided shadow casting, retaining authored main-pass sides.
RuntimeActors now compares actual sky lighting at all eleven DAT time keys, with
shell color excluded so differences measure shadows on surrounding receivers.

Inspection now constructs the retail basic-creature attribute rows, type and level
headings, bitmap fonts, alignment, health percent and appraisal colors. Value and
burden presence and creature highlight masks survive network decoding. Item text
keeps details following spell lists, preserves scroll position for the same target,
and remains visible when scrolled to the bottom. UIScreens checks those behaviors
and captures GameplayCreatureInspection.png and GameplayInspectionScrolled.png.
The independent DAT decoder can verify enum names and the two level headings with
`dotnet run --project Unreal/diag/UIInventory --no-restore -- --appraisal-resources
"C:/Turbine/Asheron's Call"` from the repository root.

Final validation: ACUnrealEditor Win64 Development built successfully. All 20
groups passed with D3D12 offscreen rendering on the final .4 binary: 13 clean,
seven with warnings, zero failed/skipped. Report:
`Saved/Automation/RetailParity20260906f/index.json`; log:
`Saved/Logs/RetailParity20260906f.log`. Engine transient mesh-bounds/serialization
warnings and material/cache diagnostics remain; final bounds and collision checks
pass. The build identifier is 2026.09.06.4 in ACEClientBuild.h and DefaultGame.ini.
Use Unreal/Launch.bat for manual testing. Full inspection content, live summoning,
long-walk performance and the broader retail replacement checklist remain open.

## Build 2026.09.06.5: world entry and portal space

Preserved reproduction log:
`Saved/Logs/ACUnreal-user-20260906-blocked-entry.log`. The destination EnvCell
7D630112 loaded successfully, but login waited on cell-collision for over 76
seconds. The presenter's portal-space visibility return skipped collision setup.
WorldEntry now loads that exact DAT room at local (36.71,89.43,9.20) and exercises
the actual hidden-room path without manually activating its physics. It verifies
a supported, unobstructed placement at the saved position and stable repeated
visibility updates. Additional physics fixtures cover clear ground, empty space,
a small obstruction with nearby placement, and a fully blocked destination.

The same group verifies the real encrypted loopback GameAction 0xF7B1 / action
0x63 on WeenieQueue. Unsafe entry requests one lifestone recall after the 45-second
grace period. A new destination invalidates the old LoginComplete while keeping
the one-attempt limit and withholding autonomous position reporting. Both an
unanswered recall and a second unusable destination disconnect with an explanation
instead of looping. The server owns Sanctuary, recall eligibility, normal mana
cost and animation delay; these tests do not connect to a live account.

PortalSpace checks the retail quantized easing samples, camera eye, roll
handedness, direct angle interpolation, DAT setup/animation IDs, interpolated
40-fps source animation and a single clock across repeated visits. Separating
translation/rotation tolerances fixes pose quantization: both visits produce 120
distinct poses across 120 simulated display frames. Three offscreen render
captures are saved as `Saved/Automation/RetailParity/PortalSpace0.png` through
`PortalSpace2.png`. Those checks establish camera/animation behavior, not complete
retail lighting equivalence or measured loading frame rate.

Final validation on build 2026.09.06.5: ACUnrealEditor Win64 Development compiles;
all 22 D3D12 offscreen groups pass (13 clean, nine with warnings, zero failed,
skipped or unfinished). Report: `Saved/Automation/RetailParity20260906j/index.json`.
Log: `Saved/Logs/RetailParity20260906j.log`. Engine mesh/material warnings remain;
recovery cases also intentionally emit warnings. Existing building/shadow,
inventory/inspection, weather and network regressions pass in this full run.

Manual acceptance still needed: relog at the reported indoor position, repeatedly
enter/leave buildings, recall to an attuned lifestone, and measure cold-cache
landscape/portal frame times. The editor background-throttle exemption is scoped
to an active client; global editor settings are unchanged. Source parsing/cooking
can still exceed a per-frame budget for an individual asset. Track those results
and remaining client features in `Docs/RETAIL_PARITY.md`.

## Build 2026.09.06.7: camera cells, lighting, input and inventory

Final ACUnrealEditor Win64 Development binary passed all 25 D3D12 offscreen
groups (14 clean, eleven with diagnostic warnings, zero failed/skipped/unfinished).
Report: `Saved/Automation/RetailParity20260906aa-final/index.json`.
Log: `Saved/Logs/RetailParity20260906aa-final.log`.

- CameraAndEdges checks stationary RMB orbit/body-follow separation, equivalent
  turning at 30/60 Hz, head-on ledge stability and repeated diagonal sliding.
- YaraqScene adds 7E650100..10A, adjacent feet/camera cells, and 76 rendered views.
  Eleven camera classifications failed before the endpoint correction. The blue
  tower PORT aperture is preserved without its solid blue draw face; the pedestal
  keeps both textured surfaces. Sealed/global ambient transitions are checked on
  actual room material instances.
- WorldDat compares 3,759 cells and 159 GfxObjs against the independent server DAT
  decoder, counting shell PORT apertures separately from ordinary draw triangles.
  Physics geometry is still compared in full. Regenerate with the documented
  `--world-fixtures` exporter after changing the fixture schema.
- UIScreens uses native canvas press/release/double-click events, checks Say and
  vendor hit targets, captures trade state/overflow, and examines loopback packets
  for use, trade acceptance, clear and close. Mana-stone tests require a target,
  visible destruction confirmation, explicit Yes/No and Retained rejection.
  Equipped innate spells and equipment/pack shortcut numerals are covered.
- StreamingCost alternates creature motion tables 200 times, checks table reuse
  and walk/run/charge decisions, and retains appearance geometry/surface checks.
- RuntimeActors retains GPU building/foliage caster and building receiver checks,
  and verifies that indoor creatures keep shadow casting enabled.

Reviewed captures include GameplayTrade.png, GameplayManaStoneConfirmation.png,
GameplayInnateSpell.png and GameplayPackShortcut.png under
`Saved/Automation/RetailParity`. Existing weather/particle/network/world-entry
groups passed on this binary. These are regression checks, not live account or
complete retail parity acceptance. Follow `Docs/RETAIL_PARITY.md` for the remaining
trade/vendor flows, equipment/appraisal presentation, movement prediction,
summoning and cold traversal performance work.


Build 2026.09.06.8 extends YaraqScene with cameras 2cm/32cm inside authored
entrances and stairwell portals, and checks that blue portal 080006FD is absent
from both the exterior tower and its interior draw sections. MeshApplication
rejects deferred editor recompilation after runtime mesh construction. UIScreens
uses 2560x1354 for backpack/main-pack/item shortcut drags with intervening layout
refreshes, checks displaced bindings and matching numeral art, draws the actual
paperdoll capture, validates target cursor states and hotspot geometry, and forces
GC with unparented innate-spell widgets. CameraAndEdges checks immediate relative
mouse deltas, zero-delta pauses and pitch clamping on a real controller/boom.

Final .8 verification: 25 D3D12 offscreen groups passed, 22 clean and three with
warnings (portal camera/sky diagnostics and intentional unsafe-spawn recovery).
Zero failed, skipped or unfinished. No NaN-bounds or tangent warnings appeared.
Report: `Saved/Automation/RetailParity20260907-final/index.json`.
Log: `Saved/Logs/RetailParity20260907-final.log` (confirms build 2026.09.06.8).

## Build 2026.09.07.1: dungeon residency, authored effects and pedestal visibility

Final ACUnrealEditor Win64 Development compilation succeeded. The D3D12 offscreen
suite passed all 25 groups: 24 clean, one with intentional unsafe-spawn recovery
warnings, zero failed/skipped/unfinished. No NaN-bounds or tangent warnings occurred.
Report: `Saved/Automation/RetailParity20260907-dungeon5-final/index.json`.
Log: `Saved/Logs/RetailParity20260907-dungeon5-final.log` (build 2026.09.07.1).

- RuntimeActors exercises the real 16919 Pedestal Weak Spot DAT On/Off transition,
  partial fade, repeated open/close, already-open creation, GC while faded out,
  and restoration of the original material. It checks identical remote walking
  distance at 10/60 Hz, held ATOYOT frame 19, server Hidden, and building doorway
  admission beyond 80 meters within the resident ring.
- InteriorStreaming checks dungeon player/item residency independently of the
  prior outdoor ring and destruction of separately spawned scenery children
  across repeated terrain/room unloads.
- WorldDat now compares 4,255 EnvCells and 159 GfxObjs; Town Network ceiling
  polygons are retained without reintroducing the Yaraq blue outside apertures.
  YaraqScene passes 130 rendered camera positions.
- StreamingCost validates actual indexed head palette changes. ParticleTiming
  preserves authored Weeping Wand/emitter sizes and intervals, and checks hidden
  body transparency does not suppress bubble particles. Existing blood-light
  exclusions continue to pass.
- WorldEntry checks full-radius floor placement on 15/30/45-degree slopes and
  scaled-pawn foot alignment. UIScreens checks jump width constraints and Height
  text separately from the fill. `GameplayJumpCharge.png` was visually reviewed.

The pedestal diagnostic uses the independent ACE.DatLoader implementation:
`dotnet run --project Unreal/diag/UIInventory -- --pedestal-probe
"C:/Turbine/Asheron's Call"`. `Fixtures/PedestalWeakSpot.sql` is upstream reference
data only and is never executed by the tests. See `Docs/RETAIL_PARITY.md` for its
source and the remaining live multiplayer, appearance and traversal acceptance.
