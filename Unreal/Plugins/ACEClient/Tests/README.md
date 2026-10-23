# Retail parity regression tests

## Toolbar icon depth (2026-10-23)

Run `ACE.RetailParity.SelectionToolbar`, `ACE.RetailParity.UIScreens`,
`ACE.RetailParity.UIInteractions`, and `ACE.RetailParity.Housing` with rendering
and retail DATs. SelectionToolbar checks all six panel buttons above the end
trim, compares opaque rim pixels with/without trim in normal and selected
states at 100%, 150%, and 200% UI scale, and checks clicks on those rim pixels.
It also verifies the unsigned JSON representation of retail tooltip depth -1.
The other suites exercise window stacking, controls, clipping, and input after
the common DAT-to-canvas depth conversion.

## Permanent augmentation presentation (2026-10-23)

Run `ACE.RetailParity.CharacterStats`, `ACE.RetailParity.AugmentationStats`,
`ACE.RetailParity.Augmentations`, and `ACE.RetailParity.UIScreens` with the retail
DATs and rendering enabled. AugmentationStats covers permanent gains across
untrained, trained, and specialized skills, live property changes/removal,
attribute-derived buffs, direct buffs/debuffs, multiplicative penalties, vitae,
expiry, and repeated refreshes. UIScreens checks the actual neutral row colors
and absent bonus suffix for Jack of All Trades and specialized luminance, then
positive/negative spell and vitae indicators. Raw equipment requirement values
and the retail post-enchantment augmentation order must remain unchanged.
This presentation deliberately treats all permanent gains as the unbuffed
baseline rather than reproducing retail's raw-versus-current UI comparison.

## Character-selection button masks (2026-10-21)

Run `ACE.RetailParity.CharacterButtonMasks`, `ACE.RetailParity.CharacterManagement`,
`ACE.RetailParity.CharacterScreenRendering`, `ACE.RetailParity.UIScreens` and
`ACE.RetailParity.UIInteractions` with rendering enabled. The mask test compares
every pixel of the real DAT Enter/Create/Delete/Restore/Credits/Exit interaction
masks against retail's `UIRegion::PointIsOverRegion` / `SurfaceWindow::PointTestOverlay`
rule. It exercises Canvas pointer routing at multiple window sizes and DPI scales,
disabled buttons, image-only states, masked children, and cancelling a click by
releasing outside the mask. Masks are decoded once on the CPU, without GPU readback.

## Low-end video settings (2026-10-21)

Run `ACE.Rendering.LowEndVideoSettings`, `ACE.Rendering.MobileShadowReceiver`,
`ACE.Rendering.CrowdEffectLighting` and `ACE.Rendering.DisplaySettings` with
rendering enabled. Coverage includes saved Off/Low/Full dynamic lighting,
shadow quality and distance, actual effect-light allocation, loaded and newly
streamed doorway lights, and restoring lighting without reloading. The real
DAT character shadow fixture renders Shadows Off and restored shadows into
`Saved/PerformanceShadowTests/Shadow-Settings-*.png`. This verifies reduced
rendering work, not an FPS guarantee for a specific laptop.

## Plugin button layout (2026-10-21)

Run `ACE.Plugins.LiveUI` and `ACE.Plugins.HostAndPanel` with rendering enabled.
They check content centering and border clearance across native plugin panels,
retained Lua controls, compact buttons, multiline and fallback-font captions,
and normal/hover/pressed/disabled states. Single-symbol actions reserve a usable
interior beyond the retail bevel even when zero padding is requested. Run
`ACE.Plugins.Ledgers` for the actual Supply Ledger add control at minimum window
size, including clicking it while pinned. Captures are written to
`Saved/PluginButtons.png`, `Saved/PluginButtons-ucm.png` and
`Saved/PluginButtons-itemtools.png`. `ACE.Plugins.AutoCraft.Production` also
checks that the shared dropdown retains its open searchable menu on refresh.

## Plugin hover text (2026-10-21)

Plugin controls, icons, window chrome and API-provided hover text use the same
DAT object tooltip as inventory slots. `ACE.Plugins.Tooltips` compares rendered
short and multiline plugin tooltips pixel-for-pixel with inventory tooltips,
including font, frame, background, padding and wrapping. It also checks empty
and changing bound text; `ACE.Plugins.LiveUI` covers live API snapshot updates.
Run alongside `ACE.RetailParity.TooltipWidget`. Comparison captures are saved as
`Saved/PluginTooltip.png` and `Saved/InventoryTooltip-reference.png`.

## Action Deck display options and target timers (2026-10-21)

Run `ACE.Plugins.ActionDeck` with rendering enabled. It covers pinned toolbar
collapse without moving slots, hidden-control hit testing, Quick-show, right-click
unpinning and settings toggling, and a macro's footer appearing only while running
and collapsing without reserved space after completion or Stop. It also
checks settings persistence and character isolation, independent native
spell/combat bar suppression and restoration, and real DAT debuff/DoT durations
through session cast and chat confirmation events with UCM disabled. It checks
resists, fizzles, refreshes, target changes, death, expiry, object deletion and
character changes. Render captures include `Saved/ActionDeck-Pinned.png`,
`Saved/ActionDeck-PinnedMacro.png`, `Saved/ActionDeck-RightClickSettings.png` and
`Saved/ActionDeck-TargetTimers.png`. Existing tests still cover keyboard priority,
inventory availability, native icon composition and foreground dragging.
The optional fourth-row checks cover legacy three-row profiles, unique Alt
defaults, saved hidden assignments, released hotkeys, square cells, pinned drops,
and live custom-shortcut tooltips without moving the cursor. Captures include
`Saved/ActionDeck-FourRowsSettings.png` and `Saved/ActionDeck-FourRowsPinned.png`.

## Apostate resonator hand-in (2026-10-20)

Run `ACE.Input.DesktopHighlights` and `ACE.RetailParity.CustomWorldPresentation`
with the editor renderer enabled. The former checks eight elevated/circling
screen-space drops in the real deposit room and writes the GiveObjectRequest
action to `Unreal/Saved/Automation/ApostateGiveObject.bin`. The latter covers
the gateway's reported ledge viewpoint, deferred appearance and cell visibility.

Replay the generated action through ACE's real hand-in and NPC reward paths:

```powershell
$env:ACE_TEST_CONFIG = 'C:/path/to/local/test/Config.js'
$env:ACE_TEST_DAT = "C:/Turbine/Asheron's Call"
$env:ACE_TEST_APOSTATE_PACKET = "$PWD/Unreal/Saved/Automation/ApostateGiveObject.bin"
dotnet test Source/ACE.Server.Tests/ACE.Server.Tests.csproj -p:Platform=x64 --filter 'FullyQualifiedName~ApostateQuestIntegrationTests'
```

Requires standard world templates/quests in the configured database. Reads
templates; captures all inventory persistence in an inert queue without running
a shard or writing character data. Tests begin with the quest accepted and a
resonator in inventory. They verify reach at 0/2/12 metres of elevation, waiting
for approach, item consumption, quest stamping, Ned's XP/luminance/title reward
and duplicate-reward prevention. Simulated approach arrival does not test route
finding or a full live dungeon traversal. Missing inputs report inconclusive.

## Carried weapon illumination (2026-10-19)

`ACE.Rendering.DualWieldLighting` uses actual DAT Flaming, Frost and Lightning
Nekodes plus the Quintessence Sickle, attached to both human holding locations.
It checks moving/turning, swapping hands and a camera nearest the Sickle's black
overlay. Before the fix that overlay won the light slot: the Sickle illuminated
zero of 240 measured frames in either hand. Black effects now remain visible
particles without claiming scene lights. Carried-light color uses the texture's
hue independently of its dark padding; authored luminosity and live particle
opacity still control intensity.

Rendered captures isolate neutral receivers from particle sprites and verify
simultaneous fire and purple Sickle contributions. `CrowdEffectLighting.LocalHeldWeapons`
checks both local held actors among 32 nearer effects, desktop/mobile-sized
budgets, camera changes, dropping, black overlays and disabling lighting.
The existing global light-count and overlapping-intensity caps remain enforced.

Retail `CPartArray::InitLights` and `CObjCell::add_dynamic_to_global_lights`
process each object's lights without a main-hand exception. Our inferred
particle illumination remains a bounded scene-light approximation; these tests
do not claim exact retail photometric matching.

Validation: Windows editor build, all nine `ACE.Rendering.DualWieldLighting`,
`CrowdWeaponLighting` and `CrowdEffectLighting` tests, and
`ACE.RetailParity.SceneryParticleLighting` pass. The 64-weapon fixture retained
eight lights and measured 0.0406 ms/frame for lighting work (CPU-only fixture,
not a live gameplay FPS measurement). Quest shared sources pass the sync check.
Reports are in `Saved/OffhandProbe`; no release was published.

## Lua chat filters and window controls (2026-10-17)

`ACE.Plugins.PublicAPI` checks permission-gated chat filters, per-window masks,
sender/type/literal matching, expiry, atomic rejection, and cleanup on Stop and
character change. A filtered response produces no native chat row while still
reaching the Lua reader. The documented Chat Tools example executes in the real
sandbox with polling off by default and an on-demand `/myquests` request.

Rendered checks exercise separate, toggle and hidden lifecycle controls, custom
labels, optional settings, and unchanged widget identities. The `item_icon`
control is compared pixel-for-pixel with Supply Ledger artwork including its
background, underlay, overlay and effects. `LootIconProperties` verifies the
corresponding snapshot fields.

Validation: Windows editor build passed. PublicAPI, Companions, HostAndPanel,
LootIconProperties, LiveUI and ChatInputAndCommands all pass in
`Saved/Automation/PluginAPIUpdates` with the corrected PublicAPI rerun in
`Saved/Automation/PluginAPIUpdates-Final`. No live server commands are sent by
these tests. Quest shared sources pass the sync check; no release was published.

## Post-kill auto-target (2026-10-17)

`ACE.RetailParity.UIScreens` exercises the selection-loss callback and deferred
combat tick in melee and missile modes, outdoors (75 m radar) and indoors
(25 m). After a kill, it requires the nearest eligible creature even when a
farther creature attacked recently, includes creatures behind the player, and
leaves selection empty when only out-of-range or ineligible creatures remain.
Combat entry retains recent-attacker preference only within radar range.
`ACE.RetailParity.CameraAndEdges` covers the shared nearest-target filtering,
distance ordering, and radar boundaries.

## Container and inventory follow-up (2026-10-17)

`ACE.RetailParity.InventoryOrder` covers fourteen transfers across two packs,
including PublicUpdateInstanceId(Container=0) before InventoryRemoveObject,
with and without a cached ViewContents list, followed by another drag reorder.
`WorldLootPlacement` and `UIScreens` cover F pickup and occupied/empty pack-slot
drops of portable backpacks. Fixed chests remain nonportable.

`ACE.Plugins.Summoning` covers repeated summoning with Combat disabled and
priority appraisal of selected essences. `ResourceMaintenance` covers junk
mana donors with an unrelated consumables selection; `LootIconProperties`
checks explicit zero mana, dead/positionless pet records, and completed-corpse
history outside the Lua VM. The history follows VT's `fo` lifetime: retained
across Stop and visibility releases, invalidated on GUID reuse at another
position, and expired after an hour absent. `LootRules` covers bounded waits
for missing corpse descriptions, excluding player-opened packs from looting,
and a confirmed final-item pickup that closes the corpse automatically.

Item Tools has an explicit server-bank purchase setting. Its transaction tests
require supply receipts without physical currency changes; a note sale without
inventory proceeds stops with an explanation instead of selling further notes.
Actual custom-server bank balances are not available through the standard
protocol, so live Conquest/Dreamweave acceptance is still needed.

`GroundItemPlacement` checks every placement frame in the human DAT setup stays
unchanged over ticks when the server supplies no motion table. Retail
`CPhysicsObj::set_description` calls `SetMotionTableID` even for zero, replacing
the setup default, before selecting a placement frame. Explicit rest poses
also bypass chest On/Off handling. Unnamed fixed scenery has no selection UI,
while creatures, portals, doors, vendors and lifestones retain interaction.

`ChatInputAndCommands` tests the reading anchor when a full history evicts its
oldest row in main and extra chat. `UIScreens` checks scrollbar dragging and
the individual chat windows' jump-to-bottom indicators.

The supplied ContainerHang logs contain no fatal crash record; Hang2 completes
normal D3D12/engine shutdown. The separate leftover-process report remains
unreproduced and is not claimed fixed by these tests.

Validation: Windows editor build succeeded. All 61 targeted suites pass across
`Saved/Automation/ContainerFollowupFinal` and the five affected suites rerun in
`Saved/Automation/ContainerFollowupUI`. The rerun corrects an overbroad unnamed
scenery filter caught by the NPC/portal/door interaction tests. Final chat checks
pass in `Saved/Automation/ContainerFollowupChat`, including retail state 1
artwork and gutter bounds; the extra-window yellow arrow was visually checked.
Quest shared
sources pass `Sync-ClientSource.ps1 -CheckOnly`; no release was published.

## Cliff and ledge support

`ACE.RetailParity.LedgeSafety` covers flat and sloping ledges, desktop and VR,
walking/running, low frame rates, diagonal edge slides, room-scale movement,
Use-approach, wall slides, deliberate jumps, unsupported spawns and an open gap
whose endpoints are both supported. It checks the half-radius support boundary
on both box and cooked triangle collision, terrain's vertical low-point rule,
and agreement between visible feet and the position/contact state sent to the
server. The footprint assertions reproduced failures with the old sphere-only
support check. No server-side fall damage is suppressed or changed.

Retail references: `SPHEREPATH::check_walkables` (half-radius polygon test),
`CPolygon::check_walkable`/`check_small_walkable`,
`OBJECTINFO::validate_walkable` (terrain low point), and
`CTransition::calc_num_steps`, `step_down`, `check_walkable`, `edge_slide`.
The cooked collision BVH supplies polygon geometry; normal full-sphere collision
and verified lower-tread clearance remain separate from precipice support.
Desktop movement now samples support between endpoints using retail's
lower-sphere-radius insertion distance.

Validation on 2026-10-17: Windows editor build succeeded. `LedgeSafety`,
`LedgeStairs`, `FortTethStairs`, `RoofEdges`, `RunSpeed`, `EntranceTransitions`,
`TerrainHeight` and `TerrainSeams` passed. The broader `ACE.VR.WallContact` suite
passed its wall/ramp-seam cases but retained the same 14 assertions in two
144-Hz creature-top cases (setup `02000A95`, scale 1.2) as the unchanged baseline.
Reports: `Saved/Automation/LedgeParityFinal`, `LedgeTerrainRegression`, and
`LedgeOriginalComparison`. Shared Quest sources were synchronized; no release
or Quest binary was built. These offline checks do not establish the cause of
an unspecified live cliff death or prove complete client/server parity.

### Mansion roof at A9A10016

`ACE.RetailParity.MansionEdges` loads the real A9A1 mansion shell and connected
EnvCells, including collision residency changes. It starts at the reported
`[68.678311 127.423019 147.944502]` pose and on the outer front-door roof rim.
The 96 cases cover desktop/VR, walking/running, three headings (including the
reported quaternion), and 10/30/90/144 Hz. They check support, backing away,
visible/reported position agreement and contact flags. Four additional cases
verify deliberate jumps still cross the edge. Diagonal edge slides stop before
entering the adjoining sloped roof, where the flat-roof assertions do not apply.

The review found an outward-creep fallback in `ACELedgeSlide::Resolve`: local
movement restored the last supported boundary point instead of the original
position when no tangent was possible. Both fallback exits now honor the
original position. Flat polygon support also retains the authored plane rather
than treating the rounded sphere/rim contact as a progressively lower floor.
Walkability retries and center-ray fallbacks use retail's intermediate
step-down probe, so a rejected roof rim is not validated against a much lower
balcony across the entire descent budget. The full physical collision sweep
remains separate for ramp/flat seams.

`CPartArray::GetStepDownHeight` returns the scaled Setup value without the
client's former one-meter clamp: the human Setup specifies 1.5 meters.
`CTransition::transitional_insert`/`check_walkable` split a long two-sphere
descent, while single-sphere bodies use half their radius. The synthetic ledge
test checks a rejected rim above a balcony, a valid clear 1.2-meter descent,
and the single-sphere limit. `CameraAndEdges` checks repeated rejected input
does not creep outward.

An offline comparison with ACE's port of those retail collision transitions
using the same DAT confirmed that some diagonal approaches from the outer rim
can legitimately step down to this balcony; the fixture preserves those cases.
This was a source/physics comparison, not a live retail executable comparison.
The Windows editor build and all ten targeted suites passed in
`Saved/Automation/MansionParityFinal`: `MansionEdges`, `LedgeSafety`,
`LedgeStairs`, `FortTethStairs`, `RoofEdges`, `CameraAndEdges`, `RunSpeed`,
`EntranceTransitions`, `TerrainHeight`, and `TerrainSeams`. The broader
`WallContact` suite retains only the same 14 baseline creature-top assertions
described above; its ramp/flat seam cases pass. Quest shared-source verification
passes. Temporary exploratory reports, logs and the standalone comparison
project were removed. Live multiplayer acceptance at this mansion is still
required before claiming complete retail parity.

## Summoning and salvage transfers

`ACE.Plugins.Summoning` exercises the reported level-100 essence with 35 charges
and a 615 Summoning requirement, selected alone in the equipment pool. Essences
do not restrict automatic weapon selection; explicit weapon selections still do,
including unavailable selections. Summoning and refilling share the same essence
selection, while a combat retry delay cannot pause the separate pet activity.
Coverage includes pending and rejected summons, cooldown expiry, an active pet,
level/skill/training/mastery/charge requirements, and continued void casting.
`ACE.Plugins.VirindiRuntime` also checks meta wand selection with an essence-only
pool and with an unavailable selected weapon.

The Item Tools give-all report duplicates the v121 bonded/attuned fix.
`ACE.Plugins.ItemTools.Transactions` now repeats the 14-bag scenario for both
Steel and Green Garnet Salvage (100), with eight ordinary bags and six bonded
bags in another pack, an unlimited transfer count, and a 250 ms confirmation
delay. Each inventory-removal packet is processed through the session; every
bag must transfer and the job must finish cleanly.

Validation on 2026-10-17: the new summoning suite reproduced 20 failed assertions
before the fix. Afterward, the editor build and all 10 targeted suites passed:
`Summoning`, `ItemTools.Transactions`, `ActivityFailureIsolation`,
`ResourceMaintenance`, `CharacterSettings`, `VoidCombat`, `Decisions`,
`RouteAndTellRequests`, `EquipmentBuffCycle`, and `VirindiRuntime`.
These are offline policy/session tests, including ordinary Use packets in all
combat stances; a live-server summon/transfer session was not performed.
Quest shared sources were synchronized; no release packages were built.

## Tailored clothing layers

`ACE.RetailParity.TailoringLayers` decodes the actual Hoary Mattekar Over-robe
and Pathwarden Scale Hauberk clothing tables, sends overlapping appearances
through F745/F625, reverses the server's layer order, and removes the garments.
It checks every resulting mesh part, uncovered setup defaults, cache fingerprints,
texture replacement scope, and full/partial dye precedence using actual DAT colors.
`CharacterCreationData`, `CustomObjectReplication`, `AppearancePlacement`,
`StreamingCost`, and `ResolvedAppearanceCache` cover the adjacent paths.

Retail references: `ObjDesc::UnPack`, `AddSubpalette` (005AB970),
`RemoveDuplicateTextureMapChange` (005AB6B0), `RemoveDuplicateAnimPartChange`
(005AB720), `Subpalette::replaces` (005AD830), `Subpalette::supercedes` (005AD870),
`CPartArray::DoObjDescChanges` (00519210), and `SmartBox::UpdateVisualDesc`
(00451F40). Full 2048-color replacements suppress partial overrides; another
full replacement can replace them. Different overlapping partial ranges remain
ordered. The same insertion rules now apply to wire descriptions and character
creation's clothing composition. Preview merging also preserves the creature's
base palette, matching `ObjDesc::operator+=` (005AC0A0).

The server supplies the worn character's composed appearance. Top/Bottom tool
priority is resolved there; the client must preserve that result rather than
sort by equipment slots, item names, or inventory order. These fixtures verify
that contract, but cannot establish the cause of an unspecified live outfit's
overlap or verify every server's tailoring rules.

Validation on 2026-10-16: editor build and all six suites above passed using
Windows D3D12 with `-RenderOffscreen`; the front/side armor captures were reviewed.
Report: `Saved/Automation/TailoringReview`. Shared Quest sources were synchronized
and checked for differences; no Quest binary was built for this review.

## Dungeon and inventory regressions

- `ACE.RetailParity.RemoteMovementReplay` checks server corrections obstructed by walls and floors, for both players and monsters at 30–240 FPS. It also retains jump, stair, jitter and VR pose coverage. `ACE.RetailParity.RemoteSupport` checks ordinary collision and ground support. Retail reference: `InterpolationManager::adjust_offset`, `NodeCompleted`, and `UseTime`.
- `ACE.RetailParity.DungeonInterfaces` checks keyboard targeting in culled dungeon rooms and at the radar boundary, plus the 18 category names and masks from `VendorItemsUI::OpenVendor`.
- `ACE.RetailParity.InventoryOrder` checks external-container backpack discovery with either ordering of `ViewContents` and object creation. `ACE.RetailParity.UIScreens` renders installed and empty chest pack slots.
- `ACE.Plugins.ItemTools.Transactions` gives all 14 salvage bags across packs, distinguishes bonded from attuned items, and confirms note proceeds through real inventory updates before purchasing supplies.
- `ACE.Performance.WaspVisibility` sweeps resident portal visibility through the reported wasp rooms and all Glenden Wood Dungeon cells. This measures portal traversal, not end-to-end FPS or visual correctness.
- `ace.PerfScene glenden` is an offline rendered benchmark at the authored Glenden Wood drop with a moving, rotating camera. It does not connect to a server or reproduce a full dungeon traversal.

Validation on 2026-10-16: all seven suites above passed, including rendered
`UIScreens`. The visibility sweep covered 572,436 camera samples (maximum
0.692 ms). The Windows D3D12 Glenden entrance run, with eight synthetic NPCs at
1920×1080, averaged 250.90 FPS over 7,527 sampled frames; p99 was 4.945 ms and
the slowest frame was 28.430 ms. These checks did not reproduce the reported
1 FPS room or establish the cause of the intermittent white polygons. Raw
benchmark output was removed after analysis.

## Wasp dungeon under network lag

`ACE.Network.WaspDungeonLag` runs deterministic virtual-time cases at 0, 200,
2,000 and 23,000 ms RTT over loopback UDP with encrypted checksums. Each case
sends 7,200 cast messages from 120 wasps and 15 authoritative player corrections,
including jitter, reordered arrivals, delayed recovery and duplicates. Assertions
cover bounded dispatch, complete ordered delivery, visible position correction,
and preservation of local facing. It can run with `-NullRHI`; frame-rate validation
uses the rendered check below.

From the repository root, after building `ACUnrealEditor`:

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/dev/ACE/Unreal/ACUnreal.uproject' -unattended -NullRHI -nosound '-ExecCmds=Automation RunTests ACE.Network.WaspDungeonLag' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=C:/dev/ACE/Unreal/Saved/Automation/WaspNetworkLag'
& Unreal/Build/Tests/Test-WaspNetworkLag.ps1 -OutputRoot C:/dev/ACE/Unreal/Saved/WaspLagRun
```

Use a fresh output directory for each run. This Windows D3D12 check renders the
actual Phyntos dungeon near cell `0143015F`, with 120 wasps, keyboard movement,
jumps, frost volleys and combat chat. It compares matching no-lag and 23-second
RTT runs (11.5 seconds of incoming delay), with 80 ms jitter and 800 ms recovery
delay every 97th packet. The synthetic server sends actual position and motion
messages through the normal receive/dispatch path. Frost projectile effects
maintain the same visual workload in both runs.

Default acceptance limits are 60 average FPS, p99 at most 33.34 ms, and no frame
above 150 ms. The lag run must also remain within 25% + 1 ms of baseline mean
frame time and 50% + 2 ms of baseline p99, while demonstrating position correction,
packet recovery, movement and jumping. Thresholds, sample duration, resolution and
lag settings are script parameters. A failed assertion exits with an error.
Reports contain frame times, network workload and movement counters; heavyweight
traces and screenshots are disabled. These tests exercise a synthetic server
workload and do not establish performance for every live-server event.

### Moving crowds in the Glenden Wood hallway

`-ACEPerfCrowdMotion` makes synthetic creatures repeatedly approach the player
through the real remote movement and collision path. With `ace.PerfScene glenden`,
`-ACEPerfGlendenHall` selects the corridor near the reported video's 79.40N,
100.73W position (DAT cell `01E3029F`), rather than the dungeon entrance. The
wasp count, volley, player-input and loopback-lag parameters also apply here.
For example, after building the editor, run a ten-minute sample in a fresh
temporary user directory:

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/dev/ACE/Unreal/ACUnreal.uproject' -game -d3d12 -RenderOffscreen -unattended -nosound -nohmd -ACEPerfQuit -ACEPerfNoCapture -ACEPerfNoTrace -ACEPerfCrowdMotion -ACEPerfGlendenHall -ACEPerfWaspCount=120 -ACEPerfVolleyWasps=8 -ACEPerfVolleyInterval=0.5 -ACEPerfCastingSeconds=600 -ACEPerfPlayerMotion -ACEPerfNetworkLag -ACEPerfLagRttMs=16000 -ACEPerfLagJitterMs=80 -ACEPerfLagRecoveryMs=800 -ACEPerfResolution=1920x1080 '-UserDir=C:/dev/ACE/Unreal/Saved/DungeonCrowdRun' '-ExecCmds=t.MaxFPS 0,r.VSync 0,ace.PerfScene glenden'
```

The sample begins after 45 seconds of warmup. Inspect frame times by minute,
network recovery/correction counters, and `movement-summary.json`; dense crowds
can block the requested walking/jumps, so input alone does not prove traversal.
This is a synthetic crowded corridor, not a replay of the live server's AI or
the user's entire route. Remove its temporary user directory after analysis.

`ACE.Collision.CrowdSupport` compares batched and reference floor queries with
0–120 overlapping bodies at large world coordinates, including valid creature
top support. `ACE.RetailParity.RemoteSupport` also checks wall/headroom blocking
at 2 and 5 FPS. The batching optimization must not change collision results.
Informational query timings have no machine-specific pass/fail threshold.

`/acueperf` and automatic slow-frame reports now include inclusive CPU work from
the previous frame: player, remote creatures, network, world, terrain, portal
visibility, animation, appearance, effects, HUD and sky. Nested costs must not
be added together. Only two fixed-size frame records are retained; this does
not enable a trace or accumulate a session history.

Validation on 2026-10-17 (Windows D3D12, 1920x1080): the same 60-second,
120-creature hallway workload improved from 33.074 to 24.822 ms mean frame time
(30.24 to 40.29 FPS); p99 improved from 49.094 to 41.828 ms. The blocked-crowd
input did not produce meaningful traversal in those short comparison runs.
The separate ten-minute run with 16-second simulated RTT, jitter and recovery
averaged 33.22 FPS (32.24–33.85 FPS by minute), p99 43.877 ms, maximum 96.541 ms.
It received 9,600 casts and 155 player corrections with zero correction errors;
movement included 203 jumps and server correction displacement. No 1–2 FPS
collapse was reproduced. These measurements do not prove the reported live
server hang is resolved. Seven focused collision, remote movement, ledge,
network and frame-diagnostic suites passed; the final crowd/diagnostic rerun
had no warnings. Raw performance captures were removed after analysis.

## Retail parity suite

Run from `C:/dev/ACE` in PowerShell after building `ACUnrealEditor`:

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/dev/ACE/Unreal/ACUnreal.uproject' -unattended -NullRHI -nosound -NoSplash '-ExecCmds=Automation RunTests ACE.RetailParity' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=C:/dev/ACE/Unreal/Saved/Automation/RetailParity' '-abslog=C:/dev/ACE/Unreal/Saved/Logs/RetailParityTests.log'
```

`-RetailDatDir="path"` overrides the test default `C:/Turbine/Asheron's Call`.
The fixture comparisons require the same DAT version used to generate the fixtures.

For GPU coverage, replace `-NullRHI` with `-RenderOffscreen`. The material, font,
weather, UI, Yaraq scene, and runtime actor checks
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

### VR character creation

Run `ACE.RetailParity.CharacterCreation`, `ACE.VR.InputLifecycle`, and
`ACE.VR.RigAndMenus` with `-RenderOffscreen` (not `-NullRHI`). The VR fixture
opens the actual creator and routes both controller rays through Slate to all
six pages. It covers canceled captures, name-field detection, PC keyboard
typing/reopening, non-overlapping keyboard placement, navigation during editing,
and Enter validation. The creation screen test covers native keyboard updates,
cancel/stale callbacks, corrected names after server rejection, and a single
creation request over a loopback socket. No live server characters are created.

`Quest/Tests/Test-NativeKeyboardSubmit.py` additionally checks the actual Android
UPL Java Enter/Done callbacks with stubbed Android/JNI boundaries. Headset
acceptance still needs PC VR and Quest: create a character with each hand,
accept the name with Enter/Done, and test Finish and any unspent-points dialog.

`ACE.RetailParity.CharacterCreationScreens.Preview` runs independently under
`-game -FeatureLevelES31 -ini:Engine:[/Script/Engine.RendererSettings]:r.MobileHDR=False
-RenderOffscreen`. It compares the composed UI with/without the character mesh,
catching the mobile FinalColorLDR transparency failure rather than merely checking
mesh transforms. `CharacterCreationScreens.Controls` retains the desktop Slate
keyboard/window checks. Both preview pages also use the scene-color/inverse-alpha
compositor used by the inventory model. Reports for this change are
`Saved/Automation/CreatorPreviewMobileFixed` and `CreatorPreviewDesktopFixed`.

### Desktop movement and chat recovery

`ACE.RetailParity.AcademyCorners` loads the Shoushi academy's actual stair and
arched-door cells (7F030133–140 and their connected cells), presses the player
into obstacles, and verifies retreat with desktop and tracked VR inputs at 60
and 15 FPS, Run 600, and 15-degree approach increments. It reproduced traps
between overlapping room meshes and beveled stair corners before the shared
recovery fix. The expanded fixture passed 2,417 blocked approaches in v54.
`ACE.VR.StairCeiling` covers desktop and VR wading, including
DC56001F [95.269531 147.908203 6.0]: the retail bed is 90 cm below the water.
`MovementReview` checks Academy Cestus attachment frames and local death-pose
release on respawn; `RuntimeActors` also covers remote revival and corpse holds.

`ACE.RetailParity.ChatInputAndCommands` checks native-font chat copying and
input copy/paste against the OS clipboard, restoring its previous contents.
Its network traffic stays on loopback. `UIScreens` covers repeated Enter
send/reopen cycles without intervening camera ticks or key-up events, slash
opening from gameplay and UI, and existing chat wrapping/resizing.
Chat output supports drag selection, Ctrl+A/C within a message, and a right-click
menu for copying a message or the whole chat window; the input supports Ctrl+V.

### Shoushi memory and Steam Link controls

`ACE.RetailParity.ShoushiTerrainBudget` counts unique terrain blends from the
actual DA55 destination at each loading radius and checks the native radius cap.
`ACE.RetailParity.TerrainArrival` loads and renders the desktop 121-block area
and Shoushi's 49-block native budget through the normal async worker and actor
paths, checking that land textures keep no duplicate CPU bulk mip payloads.
`ACE.Packaging.LandscapeTextureFidelity` compares the shared source and every
generated upload mip byte-for-byte, then recreates the texture resource.
`ACE.VR.RigAndMenus` checks hold-X options, tap inventory, and cancellation when
tracking is lost. The packaged Windows executable passes the latter two suites,
`TextureBudget`, and `AcademyCorners` in `Saved/Automation/v54Packaged`.
## Armor distance rendering

`ACE.RetailParity.ArmorDistance` audits every clothing table in the installed
portal DAT and checks each distinct part's authored detail levels, draw modes,
thresholds, close-model surface mapping, and independent collision geometry.
The standard DAT covers 1,917 tables, 15,803 table/setup combinations, 3,261 part
roots, and 5,046 detail levels. It also exercises male/female Celdon combinations,
over-robe layering (including intentionally hidden body parts), local-player and
paperdoll detail, and scale-aware distance selection.

Offscreen Celdon captures at 3, 9, 40 and 90 meters are compared pixel-for-pixel
with single-level reference meshes, then the camera returns to 3 meters to check
restored detail and color without a geometry/animation rebuild. Captures are
written to `Saved/Automation/ArmorDistance-*.png`.

Retail references: `CPhysicsPart.cpp` `LoadGfxObjArray` (0050DCF0),
`UpdateViewerDistance` (0050E030), `Draw` (shared customized surface array), and
`GfxObjDegradeInfo.cpp` `get_degrade` (0051E4B0). The local player's part stays at
level zero; other characters select their own part levels using authored ideal
distances. Tests also cover the reference bias/offset formula and exact boundary
semantics. No armor-name, clothing-family, or item-ID rendering exceptions are
used. Existing placement, tailoring, appearance cache, and dual-wield lighting
regressions should run alongside this test when changing the shared renderer.

## Weapon appraisal properties and colors

Weapon appraisal checks: `ACE.RetailParity.AppraisalPresentation` verifies that
gear ratings keep their server value, end with retail punctuation, and start a
separate paragraph after workmanship. It checks neutral, increased and decreased
caster damage colors across the heading, PvM and PvP rows. `ACE.RetailParity.UIScreens`
exercises every DAT creature enum as a slayer, including casters without weapon
profiles, partial appraisal, unknown types and the retail human-slayer title.
It also checks the rendered text widget's colors and captures
`Saved/Automation/RetailParity/GameplaySlayerSceptreInspection.png`.
References: `ItemExamineUI::Appraisal_ShowSpecialProperties`,
`Appraisal_ShowRatings`, `Appraisal_ShowCasterData`,
`AppraisalSystem::InqCreatureDisplayName`, and `AppraisalProfile::InqFloatEnchantmentMod`.

## Idle UCM appraisal workload and self selection

`ACE.Performance.UCMIdleWorkload` runs the stock Lua policy with combat disabled,
512 carried items, 2,048 retail spells and eight nearby creatures. It reports
snapshot/Lua averages and total p95/peak decision time over 64 samples, with and
without background appraisal replies. Timing is diagnostic rather than a flaky
hardware-dependent assertion. Assertions check immediate appraisal updates,
unchanged row reuse, immutable previous snapshots, equipment and inventory
changes, wield requirements, resource consumption and disconnect invalidation.

Run it with `ACE.Plugins`, `ACE.Performance.UIAndLuaWorkload`,
`ACE.Input.DesktopHighlights` and `ACE.RetailParity.SelectionToolbar` after
changing snapshot caches. DesktopHighlights checks that self remains selected
for actions without a world frame or direction arrow. Selecting self applies the
normal object glow to the possessed pawn in desktop and VR; changing selection,
disabling glow, portal/session transitions and unpossessing clear it. Other
creatures retain their selection frame/highlight. `/acueperf` now includes plugin tick,
snapshot and Lua CPU scopes (inclusive, so do not add them together).

## Gameplay interface in portal space

`ACE.Input.DesktopHighlights` also checks usable attackable creatures using the
stock cow model (WCID 14). A single click selects; a double click emits one Use
request with the correct GUID and preserves the server use radius. Busy clicks
do not duplicate requests. Interact shares the same dispatch; changing the name
and template ID does not change behavior. Non-usable and target-only monsters
remain select-only, and NeverWalk creatures send Use without a local approach.
The stock TippedRight server emote is replayed through the world actor/DAT
animator and checked for visible mesh motion without death state.

`ACE.RetailParity.PortalInterface` uses the real DAT gameplay layout and a local
UDP peer to check that a portal retains the HUD, inventory window, chat history
and draft. It checks incoming/outgoing chat, panel shortcuts through the portal
controller tick, the retail busy cursor, manual interface hiding, rejection of
combat requests without optimistic stance changes, cancellation of held attacks,
and resumption after the transition flags clear. Server stance updates remain
authoritative. It does not connect to a live server.

Run with `ACE.RetailParity.LoadingTransition` (initial login HUD prefetch),
`ACE.RetailParity.PortalDrops` (destination placement and transition completion),
`ACE.RetailParity.ChatInputAndCommands`, `ACE.RetailParity.KeyboardBindings`,
and `ACE.RetailParity.UIInteractions`. The retail references are
`gmSmartBoxUI::UseTime`, `CPlayerSystem::SetTeleportInProgress`,
`ClientUISystem::UpdateCursorState`, and `ClientCombatSystem::SetCombatMode`.

### Windowed pointer release and Windows shutdown

`ACE.Input.DesktopPointerConfinement` exercises login, camera turning and return
from camera capture with the actual Unreal input modes. Windowed and borderless
release the visible pointer; exclusive fullscreen retains confinement. Temporary
mouse-down capture remains available for item dragging. The Linux SDL companion
fixture verifies mode transitions and native grab release as well.

`Build/VR/Tests/Test-WindowsExit.ps1` builds an isolated native fixture using the
same Windows guard as the packaged game. It checks normal/active execution,
blocked teardown, repeated exit signals, and another healthy client surviving a
neighbor's shutdown timeout. The packaged Windows guard starts its 15-second
grace period only on an explicit exit/error notification; it never uses frame
rate, window visibility or focus as a reason to terminate. It uses the current
process handle and avoids Unreal logging/graphics locks in the timeout path.
This bounds leftover-process shutdown but does not identify the underlying
intermittent driver/engine stall.

For a normal Quit smoke test, launch an isolated development client with
`-game -unattended -nohmd -ACEQuitProcessTest -ExecCmds="ace.TestQuitButton"`.
The test broadcasts the live login Quit button after five seconds. Require the
`ACE quit process test: pressing the lobby Quit button` log entry, normal engine
shutdown and process exit, not just a closed window. Do not run this command in
an interactive session; it intentionally closes its test process.

### Inventory icon cutouts and scaled edges

`ACE.RetailParity.IconRendering` checks the stock torch (0x06001072) against
retail `IconData::RenderIcons` and `SurfaceWindow::ReplaceColor`, including every
authored effect-map entry. Native alpha coverage and visible colors must match;
only invisible RGB is extended at final texture upload to prevent white DAT
texels bleeding into scaled edges. All 256 alpha levels, including visible white
art, are preserved. A rendered cutout regression at 32, 40, 48 and 64 pixels
detects the original white halo and requires it to be absent after preparation.
`Saved/Automation/ItemIcons/TorchScaling.png` shows the old torch above the fixed
torch at those sizes. Run with `ACE.RetailParity.ItemPresentation` (structure
meters and inventory invalidation) and `ACE.Plugins.ActionDeck` (shared item and
spell composition). These checks use local retail DAT artwork, not a live server.

### Area loading and retained world textures

`ACE.Rendering.WorldTextureRetention` collects real human, creature and prop
surfaces from the DATs. It compares runtime mip retention against the former
LZ4HC encoder and reports CPU time and retained bytes, without asserting a
machine-dependent timing threshold. Full mip uploads, reduced-detail tails and
restored full-quality uploads must reproduce every RGBA byte. Tiny and
incompressible inputs exercise raw retention. Run with `LowEndVideoSettings`,
`ResolvedAppearanceCache` and `ACE.Packaging.TextureBudget` to check actual GPU
detail changes, cache lifetime and UI isolation.

For loading profiles, start tracing with `-trace=cpu,frame,bookmark` and an
absolute `-tracefile` when launching `ace.PerfScene outdoor` or `facility`.
The fixture's normal 45-second warmup intentionally excludes loading from its
FPS summary, so use the early trace frames to measure area entry. Compare
separate fresh `-UserDir` directories, identical scene/settings and no concurrent
benchmarks. This fixture is offline and does not measure server response time.
Scopes `ACE_CreateWorldTexture`, `ACE_RetainWorldTextureMip`,
`ACE_BuildSetupAppearance` and `ACE_ApplySetupParts` separate texture retention
from geometry and component/material installation.

To isolate steady-state regressions, use `-ACEPerfResolution=2560x1440` (or the
reported viewport size), `-ACEPerfNoCapture` and fresh per-run `-UserDir` paths.
The development-only `-ACEPerfLegacyMipCompression` flag restores the previous
LZ4HC encoder for a same-binary comparison; it does not exist in Shipping and
does not persist. Resource snapshots at sample start/end include retained CPU
payload/allocation sizes, process/system memory, RHI texture allocations and
effective quality levels. Compare the same quality settings, not only the same
resolution. An offline fixture cannot establish the cause of a live-session
regression. `/acueperf` also reports effective render settings and whether the
client is running in editor play, so logs expose overrides of saved preferences.

`ACE.RetailParity.RemoteTurnTargets` replays TurnToObject packets against the
Pumpkin Buffer's retail motion table. It distinguishes current local prediction,
current remote physics, stale position echoes, pending teleports/forced positions,
and targets whose physics body has not spawned. A slow orbit crosses heading
wrap at 30/60/144/240 FPS and checks each frame against the DAT turn rate. The
target is sampled once per command, as in retail TargetManager/MoveToManager.
Run with `RemoteMovementReplay` for position corrections, patrols and stop/death
transitions.

`ACE.RetailParity.StreamingRetryBudget` injects a 550 ms frame into the real
terrain retry timer, checks a single drain on recovery and normal subsequent
progress, then restores subscriptions and checks that position callbacks are not
duplicated. It isolates residency from asset/IO timing; it is not an FPS benchmark.
Use `ColdTerrainLoad`, `StreamingCost`, `SpawnSupport`, `WorldEntry`,
`LoadingTransition`, `StreamingRetirement`, `ArmorDistance`,
`ACE.Rendering.DeferredWorldAppearance`, `ACE.Rendering.ResolvedAppearanceCache`
and `ACE.Packaging.LandscapeTextureFidelity` for the related DAT/residency checks.

`ACE.Plugins.ActionDeckMacroSyntax` checks command/wait parsing, CRLF, whitespace,
argument preservation and validation before any execution. `ACE.Plugins.ActionDeck`
also exercises macro persistence, hotkeys, minimum delays, portal waits,
cancellation and single-send recovery after long frames. A private loopback socket
verifies `/hom` emits mansion recall and `/f` emits fellowship chat; no real
server is contacted. Rendered modal checks cover Save/Cancel/Escape, validation,
blocked underlying drops/input, incremental catalog button visibility/identity,
and recovery at 480×360. Screenshots are saved as
`Saved/ActionDeck-MacroEditor.png` and `Saved/ActionDeck-MacroEditor-Small.png`.

Packaged Linux DAT fixtures for deferred appearance, selection toolbar, UI
interactions, world entry and landscape fidelity use `-RetailDatDir=` through
`ACETestDatDirectory.h`; the Windows install directory is only a default.
The world-entry fixture uses RGBA32f depth readback supported by Vulkan and
D3D. Windows scan-code normalization is tested on Windows; other platforms
feed Unreal keypad keys instead of interpreting Windows VK values as native codes.
