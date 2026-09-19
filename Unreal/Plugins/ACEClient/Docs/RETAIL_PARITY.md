# Retail client replacement: acceptance and evidence

The target is the complete retail 1802 client reproduced in Unreal: gameplay,
network behavior, controls, DAT content, and interface design. Graphical fidelity
work follows feature completion. A feature is not complete merely because its
button exists, its opcode has a constant, or a focused regression test passes.

This is the working acceptance record, updated for build 2026.09.12.2. The client
is **not yet a feature-complete retail replacement**.

## Sources and comparison method

- Retail decompile: `C:/dev/retail_client_source`. Cite the actual class/function
  when changing behavior; decompiler variable names and inferred types are not
  specifications by themselves.
- Runtime assets: `C:/Turbine/Asheron's Call/client_*.dat`.
- Independent data decoder: `Source/ACE.DatLoader`, with fixture exporters in
  `Unreal/diag/UIInventory`. Resolved interface trees are in `Docs/UI/Resolved`.
- Protocol counterpart: `Source/ACE.Server/Network` and server world objects.
  Do not alter server rules to compensate for a client discrepancy.
- User reproduction: `Unreal/Saved/Logs/ACUnreal-user-20260906-afternoon.log` and
  the supplied screenshots. Reported Yaraq rooms include 7D64012C, 7D640131,
  and 7D640135. Preserve these fixtures when changing culling or streaming.

For each feature, compare retail data and control flow, implement the behavior,
verify runtime outcomes with independent fixtures, then exercise the live server
flow. Record any part that has not been exercised. Automated coverage and live
acceptance are distinct evidence.

## Changes in 2026.09.12.2

- The local walking radius now comes from the lower authored Setup sphere, as
  used by retail `CPhysicsObj` when initializing `CTransition::init_sphere`.
  Human setup 02000001 has two 0.48 AC spheres; the enclosing Setup radius was
  previously clamped to 0.55 AC and used for walking. This oversized body
  snagged the final Arcanum stair riser. Use-radius calculations still use the
  enclosing Setup radius, matching `CPartArray::GetRadius`.
- A validated stair descent retains the actual sphere contact above a tread's
  center-ray height. A radius bound and the step-up limit prevent substituting
  the top of a tall wall. Ground support checks use the walkable normal threshold
  (FloorZ), while airborne landing keeps LandingZ. Cell transit now starts at
  the actual capsule center, without adding its half-height a second time.
- Rising ramp support samples the lower sphere's clearance rather than placing
  its bottom on the center ray. A walkable collision re-sweeps the remaining
  motion along that plane so the first ramp cannot hide a later wall. Final
  downward ground adjustment also sweeps the full capsule, preventing the next
  tread's ray height from pulling the body through the landing lip. The contact
  normal, including its Z denominator, is used consistently at rounded edges.
- `/pkl` already sent EnterPkLite (028F). The missing path was public quality
  134 (02CE), used by `Player::HandleActionEnterPkLite` on completion. Both public
  and private updates now update self stats and the PK/PKL/Free descriptor flags
  exactly as `ACCWeenieObject::OnStatUpdated` and
  `PublicWeenieDesc::SetPlayerKillerStatus`. Existing actor collision updates
  through a status-only event, without restarting motion or appearance.
  Open inspection, Attributes, and selection/radar colors read the new state.
  The server still decides entry eligibility and damage; no predicted PKL toggle
  or server-rule changes were introduced.

Regression coverage uses the actual Arcanum rooms 7D640149-14F and staircase
02000A4A, with the player capsule derived from its Setup. It traverses both
directions at center and near both walls with 16, 33 and 50 ms frame intervals,
checks airborne state and body overlap,
holds forward against a roof edge, and reverses away without jumping. The previous
stair fixture used an unrealistically narrow 22 cm radius; its earlier passing
result did not cover the reported player. Protocol tests inject public/private
PK updates and transitions back to NPK/Free; UI tests keep an old NPK appraisal
open while a PKL broadcast changes its heading and selection/radar color.

Validation: editor LedgeStairs, RoofEdges and RuntimeActors pass in
`Saved/Logs/Test-2026.09.12.2-final.log`; InteriorStreaming, PkStatus and UIScreens
pass in `Saved/Logs/Test-2026.09.12.2-ramp.log`. BuildCookRun completed successfully
in `Saved/Logs/Package-2026.09.12.2.log`. The packaged executable passes
LedgeStairs, PkStatus, UIScreens and Packaging.Materials in
`Saved/Logs/Test-2026.09.12.2-packaged.log`. The package lacks Unreal's HTML report
template; this does not prevent running the tests or recording their results.

Live multiplayer damage and the user's exact movement path still require a
server playtest; automated fixtures are not evidence of a completed live session.

## Changes in 2026.09.12.1

- Loaded indoor cells no longer manufacture a floor when support is absent.
  Airborne collision recovery handles existing wall/soffit penetration and
  re-sweeps before landing; trying to jump while falling cannot charge a jump.
- Missile aim/reload and eating/drinking use their timed Ready-to-action links,
  not their stationary endpoint cycles. Unchanged object-descriptor updates
  cannot replay a completed action or duplicate its followup queue. Actual
  appearance rebuilds still restore the latest motion.
- Stable hand attachments no longer reset/recreate picking collision every
  tick. Part collision no longer unconditionally recreates its physics state;
  redundant appearance physics application is removed. Radar reads the session
  object view without copying every object's properties/clothing. Appearance
  queue sorting copies each candidate once, and ordinary pose updates avoid
  copying the complete transform array unless a transition needs blending.
- Console/debug key bindings are cleared, including inherited saved bindings.
  GameplayDebugger activation is disabled. Backtick remains the game's combat
  toggle; semicolon/apostrophe remain enemy-target controls.
- Escape dismisses popups, keyboard mapping, inspection, vendor/loot/trade/
  salvage, then the main panel. Once clear, it opens Options on Game Play,
  regardless of the previously selected settings tab; another press closes it.
- Dropping a pack on a vendor stages its full, sellable content stacks on Sell.
  The pack itself, retained/attuned items, and equipped contents are omitted.
  Repeated drops update quantities without duplicate entries or auto-selling.
  The main backpack is also a bulk drop source. Retail `gmVendorUI::AddItem`
  expands contained items and applies `VendorProfile::InqAcceptability`:
  preserve vendor type/min/max fields from ApproachVendor and filter per-unit
  values (including the promissory-note maximum-value exception).
- Pack slots paint retail's empty image (06000F6E), at the authored 32x32 inset
  in each 36x36 slot, up to the player's pack capacity. Occupied pack icons
  retain their item-type background and custom underlay; the main pack uses
  Container type as in `IconData::RenderIcons`. This also restores backgrounds
  for packs inside external containers. Empty-state evidence is DAT layout
  21000037, ItemSlot_Backpack/ItemSlot_Icon, state 1000001C, together with
  `UIElement_ItemList::UpdateEmptySlots` and `UIElement_UIItem::UIItem_SetState`.
  Removing, scrolling, or hiding a pack clears its former artwork; cached pack
  overlays now also react to type, underlay, effect, and player capacity changes.

Evidence: seven suites passed in `Saved/Automation/2026.09.12.1-review`:
InteriorStreaming, LedgeStairs, MovementReview, RoofEdges, RuntimeActors,
UIScreens, and WorldEntry, with actual D3D12 rendering enabled. LedgeStairs
loads Arcanum cells 7D640149-14F, including staircase setup 02000A4A, and walks
the controller from the basement onto the main floor. Missile actions cover
the human bow, crossbow, and thrown stances; food/drink must finish while idle.
Object echo tests verify no mesh rebuild or completed-action restart.
The final UIScreens run in `Saved/Automation/2026.09.12.1-vendor` verifies
actual side-pack and main-backpack drops, repeat drops, no automatic sale, and
vendor type/value filtering. An additional LedgeStairs run in
`Saved/Automation/2026.09.12.1-input`
verifies console/debug key removal even after injecting old saved bindings.
The final UI run in `Saved/Automation/2026.09.12.1-packs-final` also verifies
three occupied packs plus four default empty slots, main-pack type override,
underlay/type refresh, removal, augmented-slot scrolling, inventory reopen,
and nested-container background updates. Screenshots are in
`Saved/Automation/RetailParity/GameplayPackBackgrounds.png`,
`GameplayEmptyPackScroll.png`, and `GameplayLootPackBackground.png`.
The final editor run (`Saved/Automation/2026.09.12.1-final`) passes LedgeStairs,
MovementReview, and UIScreens. The built Windows executable also passes those
three suites plus Packaging.Materials, with zero warnings/errors, in
`Saved/Automation/2026.09.12.1-packaged-verified`. Test worlds explicitly bind
their owning game instance so runtime collision cooking receives a game world;
UI fixtures use explicit resize dimensions and absolute command-file paths,
independent of saved editor preferences and packaged relative directories.
BuildCookRun completes successfully (`Saved/Logs/Package-2026.09.12.1.log`).

Performance limits: the user screenshots show ~26-27 ms Draw versus ~5 ms GPU,
with 10,350-11,641 draws. This points to render-thread submission cost outdoors,
not lack of GPU capacity. The engine's procedural mesh proxy submits dynamic
sections separately, including shadow views. No terrain quality, visibility
distance, or shadow quality reduction is included. The focused 1,000 held-item
tick benchmark measured 0.118 ms versus 52.826 ms for the prior repeated
collision path; this is not a live crowded-scene FPS measurement. The roof,
stairs, crowded multiplayer scene, and full live consumable flow still require
user acceptance in the new executable. The captured user log was build
2026.09.11.5, predating the preceding roof recovery changes in .6.

## Changes in 2026.09.11.6

- Cold terrain workers share immutable TexMerge blends by PCode and DAT
  fingerprint. They consult the existing tile cache before baking and compress
  each missing blend once. Visible landblocks share pixel storage; unused recent
  blends have a 128 MiB retention limit. Worker count, geometry, texture quality,
  reveal gates, and the rollback of the precompiled world bundle are preserved.
- Roof overlap recovery uses the same LandingZ threshold as clean airborne
  contacts. It re-sweeps after recovery before accepting touchdown. Forced server
  corrections also release standing-jump locks and stale charge/launch state.
- Login remains visible until character selection is ready. Missing DAT and
  packaged UI assets produce an actionable error, instead of a black screen and
  per-frame retries. Login explicitly retries a failed index. DatDirectory now
  honors the documented Engine.ini setting.

Evidence: the preserved user logs show 65.35s and 80.4s world transitions,
mostly waiting for visible terrain. The two-worker cold CPU bake benchmark
improved from 10.301s to 2.909s at 2B120000 and 12.111s to 6.383s at 7E650000.
Raw-reference RGBA comparisons passed. A separate cold runtime test at 7E650000
completed all 121 terrain meshes in 21.490s, including DAT indexing, disk writes,
mesh application, and render flush. Its 1,209 sections shared 179 blends
(716 MiB, versus 4,836 MiB if each section owns its pixels). This test does not
measure live login, building/scenery completion, or steady-state FPS.

RoofEdges reproduced three stuck overlaps on the old code (50/60/75 degree
roofs) and passed after correction, along with the existing thin roof, corner,
ceiling, and wall cases. WorldEntry, MeshApplication, YaraqScene (130 rendered
views), RuntimeActors, LocalLoginSettings, and MissingDatLogin passed. The
missing-DAT test verifies visible errors, suppressed retries, explicit retry,
and recovery to valid character-selection resources. Initial missing-file test
output required declaring both expected loader error messages; the final run is
in `Saved/Automation/2026.09.11.6-login`.

Editor build and Windows BuildCookRun succeeded. Packaged Materials,
MeshApplication, MissingDatLogin, and Weather passed in
`Saved/Automation/2026.09.11.6-packaged`. The packaged automation runner lacks
its optional HTML report template; JSON and logs were written successfully.
Weather retains the existing ScreenPercentage priority warning. Live roof-jump,
full login duration, and the friend's installation still need user verification.
Distributable: `Saved/Packages/2026.09.11.6/Windows/ACUnreal.exe`.

## Changes in 2026.09.11.5

- Reverts the 2026.09.11.4 world-cache update after a reported performance
  regression. Restores prior DAT reads, cache identity, terrain baking and
  static model loading. The precompiled bundle is no longer staged.
- All fixes through 2026.09.11.3 are retained.

Validation: editor build and Windows BuildCookRun succeeded. WorldEntry,
MeshApplication and YaraqScene passed in `Saved/Automation/2026.09.11.5-check`;
WorldEntry emitted its three expected blocked-entry/recall warnings. Packaged
Materials, MeshApplication and Weather passed in `2026.09.11.5-packaged`, with
the existing ScreenPercentage priority warning in Weather. The package listing
contains no `ACE/WorldCache` entries. Live performance has not been remeasured.
Distributable: `Saved/Packages/2026.09.11.5/Windows/ACUnreal.exe`.

## Changes in 2026.09.11.3

- Empty spell hotbar slots 1–9 use the blue numbered DAT artwork
  (`0x060010FA`–`0x06001102`) shown in the user's retail comparison. Later empty
  slots remain brown (`0x06001A97`). Occupied spells retain their small key
  badges, and scrolling keeps bindings and artwork tied to absolute slots.

Validation: both rendered UI groups passed without warnings in
`Saved/Automation/2026.09.11.3-ui`. Captures `GameplayEmptyNumberedSpellBar.png`
and `GameplayShortSpellBar.png` in that folder confirm the blue 1–9 artwork,
brown later cells, and occupied-spell badges. Existing scrolling/key checks also
passed. Editor build and BuildCookRun succeeded. All three packaged smoke checks
(Materials, MeshApplication, Weather) passed in `2026.09.11.3-packaged`, one with
the existing scalability warning. Generated runtime settings/logs were moved to
`2026.09.11.3-packaged-runtime` under Automation.
Distributable: `Saved/Packages/2026.09.11.3/Windows/ACUnreal.exe`.

## Changes in 2026.09.11.2

- Empty item shortcuts 1–9 again use their blue numbered DAT artwork. This
  initially missed the spell bar's empty slots, corrected in 2026.09.11.3.
  Scrolling still leaves spell keys pinned to absolute entries 1–10.
- Chess draws all 32 pieces using DAT icons instead of unsupported Unicode
  glyphs. Its panel is titled Game Center, with the retail Resign and Stalemate
  controls and selection art.
- The small pack/burden button opens Character Information. Its scrollable
  content includes birth, time played, deaths, resistance/regeneration categories,
  innate attributes, chess/fishing ranks, weapon and summoning masteries,
  augmentations, and carrying burden.
- Outdoor world entry waits for terrain coverage across the configured load
  radius before revealing the world. Outer scenery can continue streaming;
  the indoor cell/neighborhood readiness path is unchanged.
- Bitmap text ignores nonprinting CR/BOM/zero-width characters instead of
  painting fallback question marks. Inscription editing uses the same DAT glyph
  atlas as the display, including multiline editing, selection, and clipboard.
- Spellbook selection draws the template's `ItemSlot_Icon_Selected` image over
  the selected row. Components use their authored header, row, count, and numeric
  entry templates. Desired counts are typed directly, clamped to 0–5000, and
  persisted through SetDesiredComponentLevel. The default list shows carried or
  requested components.
- `/fillcomps` fills the vendor buy list with deficits after counting all carried
  stacks and existing cart quantities. Category filters, maximum cost, finite
  stock and `clear` are supported. Purchase remains on Buy All. Vendor supply
  quantities are retained from the ApproachVendor packet.
- Miscellaneous command support includes `/age`, `/birth`, `/pklite` (`/pkl`),
  `/day`, `/endurance`, `/framerate`, `/loc`, `/version`, `/loadfile`, `/log`, and
  auxiliary-window `/title`, plus `/help <command>` and `@` equivalents. Age and
  birth use the dedicated string-payload actions; the age reply is displayed.
  Command files expand `%DATE%`, and log output appends until `/log` stops it.

Reference: retail `gmSpellComponentUI`, `gmVendorUI`, `gmCharacterInfoUI`,
`ClientCommunicationSystem`, DAT templates, and ACE server handlers. Validation
uses isolated loopback protocol fixtures and rendered UI; a live vendor/chess
session and real login/portal timing still need user acceptance.

Validation: all 36 `ACE.RetailParity` tests passed (32 clean, four with known
fixture/scalability warnings) in `Saved/Automation/2026.09.11.2-check`. After the
final title, sizing, input routing and command-help changes, both UI groups passed
in `2026.09.11.2-ui-focus`. Captures verify all 32 chess icons, component entry
editing, spellbook selection, blue shortcut slots and the Character Information
scroll area. Typed characters, Enter, and Escape were routed through a registered
Slate window; refill accounting and miscellaneous command packets used loopback.
The final BuildCookRun succeeded without header-order diagnostics after moving
each affected source's own header to its first include. Packaged Materials,
MeshApplication and Weather all passed in `2026.09.11.2-packaged`. The packaged
runner lacks Unreal's optional HTML report template; its JSON results are intact.
Distributable: `Saved/Packages/2026.09.11.2/Windows/ACUnreal.exe`. Generated
smoke-test settings/logs were moved to `Saved/Automation/2026.09.11.2-packaged-runtime`.

## Changes in 2026.09.11.1

- Spell keys 1-0 and their badges refer to absolute entries 1-10 even while the
  spell list scrolls. Item shortcuts select on a single click and use/equip on a
  double click; numeric activation stays immediate. Blank item shortcuts use the
  inventory slot background with a separate numeric overlay.
- The vendor uses its DAT dropdown arrow and a complete row of default empty
  slots. Inventory foreground composition adds the retail for-sale overlay to
  items queued in the vendor sell list.
- Effect, harmful-effect and vitae captions occupy their authored title bars.
  Runtime labels, menus, tooltips and legacy widget fallbacks use the shared DAT
  bitmap font resolver. Maxed trained/specialized skills display `Infinite` in
  the XP-to-raise footer.
- Escape closes inspection, then the active equipment/panel window, before
  opening gameplay options. A late identify reply no longer reopens a dismissed
  examination. Right-clicking an equipped item in the 3D doll sends Identify.
- Game boards are recognized by their public Gameboard item type and are not
  discarded as stuck non-usable scenery. Their interaction approaches the board
  and sends Use, allowing the server's JoinGame response to start chess.
- A corpse's Dead animation no longer makes its container a dying creature.
  Selection, markers and approach/use remain available. Actual dying creatures
  retain the prior smite protection. Missile/particle objects are excluded from
  targeting, and cosmetic emitter meshes never intercept selection traces.
- Particle lights are hidden when the last emitter expires or stops. A server
  projectile impact (NoDraw/Cloaked) stops extrapolated flight and the launch
  effect before the Explode effect. The server decides terrain collision and
  damage; the client no longer displays a spent projectile continuing to fly.
- Optional mouse controls follow the documented WoW distinction: left-drag
  orbits freely, right-drag faces the character to the view, both buttons advance.
  Forward keyboard movement retains a free-orbit view. Left click selects.
  With mouse turning disabled, mouse orbit stays disabled and wheel zoom works.
  Brackets cycle nearby selectable world objects; semicolon/apostrophe cycle
  nearby attackable enemies.
- Map hover uses the retail 53 named location rectangles and the original
  257x267 aspect ratio. Housing uses readable DAT text, wrapped purchase/rent
  descriptions, Bought/maintenance/location labels, and the retail extra rent
  period when the current period is already paid.
- Components use the 32-pixel DAT item row, separate count/desired columns, and
  a draggable scrollbar. Inventory/list scrollbar drags preserve the point
  grabbed on the thumb and its continuous position during content refresh.
- Config now scrolls through functional sound, camera, graphics and individual
  rendering-quality controls, using retail arrows, slider art and round toggles.
  Chat opacity sliders remain visible above its window/filter options.
  Master/effect/ambient gain, inactive-window audio, camera stiffness/adjustment,
  field of view, brightness and anisotropy persist locally.
- Scripted sky/cloud meshes receive the same current time-of-day ambient/sun
  parameters as other sky layers. They retain authored luminosity rather than
  inheriting the ordinary particle visibility fallback.
- Luminosity no longer forces a clipped sign into additive blending. World
  materials preserve authored luminosity independently of ambient-light updates;
  this applies to the small streetlamps in Yaraq (the user's clarification),
  including the luminous 0x0800012D surface. Outdoor material parents advance to
  v3 and are regenerated for cooking.
- The vertex-color fallback has its own cooked shader instead of depending on
  an editor-only EngineDebugMaterials asset. The runtime parent set now has 32
  materials. The module build stamp matches the project version.

Validation: the final full D3D12 offscreen run passed 35 groups; its one outdated
camera-release assertion was corrected for left-drag orbit and the camera group
then passed separately. All 36 groups have passing results. Added checks cover absolute spell binding, single/double/numeric item
activation, corpse death-pose selection, chess/corpse use approach, components
thumb dragging, town hover, maxed skill text, config scrolling, chat sliders,
cloud lighting and lamp luminosity. Editor build and material preparation succeed.
BuildCookRun succeeds with exit code 0. The final packaged executable passes
`ACE.Packaging.Materials` (all 32 cooked shader parents),
`ACE.RetailParity.MeshApplication`, and `ACE.RetailParity.Weather` with zero
failures. The only packaged-test warning is the deliberate 100% screen-percentage
fixture override taking priority over scalability. The prior missing vertex-color
material warning is gone. Reports: `Saved/Automation/2026.09.11.1-final`,
`2026.09.11.1-camera`, and `2026.09.11.1-package-final`. The distributable is
`Saved/Packages/2026.09.11.1/Windows/ACUnreal.exe`; generated smoke-test Saved
files were moved outside that folder.
The optional mouse-control reference is Blizzard's [WoW guide](https://media.battle.net/documents/wow/WoW-BradyGAMES-enUS-Guide.pdf)
and [classic manual](https://us.media.blizzard.com/manuals/wow/wow-classic-manual-enUS.pdf).
The authored interface/data references are `gmMapUI::s_rgLocations`, `gmHouseUI`,
`HousePayment::ComposeText2`, `ItemHolder::DetermineUseResult`, and
`classic_spellcomponent` (0x21000033). Projectile termination follows the
server's `ProjectileImpact` state transition.

Live server chess joins, combat damage, the specific Arcanum sign and exact sky
appearance at the user's server time still require in-game comparison; automated
fixtures are not a claim of full live retail parity.

## Changes in 2026.09.10.5

- Moving particle sprites use a 1.8 linear output gain, while the stationary
  glow returns to unity. The accepted DAT emission, twinkle, birth frames and
  running trails are preserved. Particle material version 7 is regenerated for
  cooking. Weather particles retain unity gain. No particle-light intensity
  was increased.
- All five chat windows use the DAT edge resize flags and width/height limits.
  Saved widths are restored alongside heights. Text rows reflow when their
  allocated width changes, including offscreen rows. Scroll input, both arrow
  buttons, and the segmented blue thumb work in each window; incoming messages
  preserve a scrolled-back reading position. Opacity applies to the message
  backdrop without blending each scrollbar layer into the gold track.
- Escape toggles the options/game menu from gameplay or a focused panel control.
  Text editing and key-binding capture retain their existing Escape handling.
- Numbers-off vitals propagate the authored ShowDetail state to the textured
  fill/background. Numbers-on uses HideDetail. Both stacked and horizontal
  layouts are covered, for health, stamina and mana.
- Airborne capsule sweeps recover an initial upward support penetration on a
  walkable sloped roof. Previously that overlap could hold the airborne state
  indefinitely and prevent manual movement. Recovery still rejects walls and
  soffits. RoofEdges covers launch, landing, thin edges/corners and ceilings.
- UE mouse smoothing is disabled in DefaultInput.ini. The supplied log's
  `SampleCount > 0` ensure originates in that engine smoother; the client already
  integrates raw mouse deltas through its own camera controls. The log alone
  does not establish the cause of the reported roof freeze.
- World occupants and their attached weapons follow the terrain presenter's
  admitted EnvCell visibility. Hidden surface-level dungeon rooms therefore
  hide their occupants, suppress picking/collision and radar dots, and clear
  selection. Appearance and attachment refreshes cannot reveal a hidden actor.
  Admitted rooms and exterior portal views restore visibility normally.
- Server Dead motion is retained on the object record, including deferred
  appearances and repeated descriptions of the same incarnation. A late Ready
  update cannot cancel a slain NPC's death pose. Dying creatures stop being
  attack targets/radar dots and release selection. A new incarnation may become
  attackable again; player revival remains allowed. `WeenieError::TargetNotAcquired`
  (0x042C) now displays readable text. Basis: server `Creature::Die` / `Smite`,
  `GameMessageUpdateMotion` and `WeenieError`.

Editor build succeeded. All 36 RetailParity tests pass (32 clean, four with the
existing fixture warnings), including 32 smite-shaped motion/removal sequences,
real creature death animations, room/occupant/weapon visibility transitions,
roof support overlap and rendered chat/vital/particle checks. Evidence:
`Saved/Automation/2026.09.10.5-final/index.json` and
`Saved/Logs/Test-2026.09.10.5-final.log`. The roof and admin-smite reports have
automated regression coverage; the user's exact live incidents have not been
reproduced in this pass. Live acceptance remains distinct from those checks.

BuildCookRun succeeded, including cooking the version 7 additive parent and
archiving `Saved/Packages/2026.09.10.5/Windows/ACUnreal.exe`. Packaged
MeshApplication and Weather tests pass with their existing fixture warnings
(`Saved/Automation/2026.09.10.5-packaged/index.json`). The separate
`ACE.Packaging.Materials` check passes cleanly and verifies all 31 persistent,
compiled runtime parents (`Saved/Automation/2026.09.10.5-packaged-materials/index.json`).
Generated package test profiles/logs are moved out of the release archive.

## Changes in 2026.09.10.4

The user confirmed the particle emission and running trails now behave as
expected, but requested more brightness. Additive particles now apply a 1.35
linear output gain after the existing display-space source modulation. This is
a requested visibility adjustment, not a change to the DAT brightness rules.
It preserves sprite color ratios and fade endpoints without clipping before
the gain. Emission, random spread, lifetime, motion, and ordinary alpha-blended
particles are unchanged. The additive parent is versioned to v6 for cooking.

The rendered `ACE.RetailParity.ParticleTiming` test passed, including the moving
baton lifecycle regression and pixel checks of both the boosted source and
unity-gain retail modulation, texture alpha, fading, and front/back brightness.
Its two warnings are the existing deliberately unresolved effect fixtures.
Evidence: `Saved/Automation/2026.09.10.4-particles/index.json`. Editor build and
BuildCookRun succeeded; the archive is `Saved/Packages/2026.09.10.4/Windows`.
Packaged Materials, MeshApplication, and Weather checks also passed (one clean,
two with the existing debug-material/scalability warnings); evidence is
`Saved/Automation/2026.09.10.4-packaged/index.json`.
Live visual acceptance of the new brightness remains to be confirmed.

## Changes in 2026.09.10.3

The follow-up Slashing Baton report exposed a lifecycle fault that the previous
isolated shader test did not cover. `TryAttachToParent` calls
`NotifyAppearanceReady` when refreshing held equipment. That notification was
overwriting **every live particle's** random offset with the exact hook origin
and moving its birth frame to the current weapon. `InitializeFromObject` also
reseeded the random stream on every property refresh. Together these collapsed
the drip into one line and pulled surviving particles along with the runner.

- Appearance readiness now rebinds emitter parents without rewriting existing
  particle offsets, birth frames, ages, or emitter travel history. Future births
  use the current hand/weapon pose; non-parent-local particles keep the saved
  birth frame as in retail `ParticleEmitter::UpdateParticles` and
  `Particle::Init`/`Update`.
- An object's random stream is seeded once for its identity, not restarted by
  property updates. Existing material/fade fixes and DAT emission rates, sizes,
  motion, and lifetimes remain in use.

The regression uses the actual baton setup attached through the presenter to a
DAT character moving at 4 m/s while refreshing the weapon's state/attachment.
Before the fix it failed radial-offset, birth-frame, trail, and random-diversity
checks, producing one distinct offset over five seconds. After the fix it
produces 21 distinct offsets in the authored 10–20 cm annulus, retains the
world-space gravity trajectory and remaining lifetime, leaves stars behind the
moving tip, and drains them after emission stops. Evidence:
`Saved/Automation/2026.09.10-baton-motion-baseline/index.json` and
`Saved/Automation/2026.09.10-baton-motion/index.json`. This verifies the
simulation/lifecycle correction; final live visual comparison remains distinct.

Validation: all 36 `ACE.RetailParity` tests passed (32 clean, four with the
existing fixture/scalability warnings) in
`Saved/Automation/2026.09.10.3-full/index.json`. Editor build and BuildCookRun
succeeded. Packaged Materials, MeshApplication and Weather passed (one clean,
two with the existing debug-material/scalability warnings) in
`Saved/Automation/2026.09.10.3-packaged/index.json`. The archive is
`Saved/Packages/2026.09.10.3/Windows/ACUnreal.exe`.

## Changes in 2026.09.10.2

The reported Slashing Baton is setup `02001491`, script `33000F6B`. Its DAT
contains a one-particle glow (`32000803`, surface `080002EC`) and a five-particle
star drip (`32000802`, surface `08000309`): one star every 0.2 seconds, one-second
lifetime, 0.1–0.2 AC unit radial offsets, and downward world acceleration. Both
layers have 0.7 translucency. The independent decoder in `Unreal/diag/BatonFx`
confirms these parameters and exports the actual glow and eight-point star
textures. No substitute artwork or weapon-specific emission multiplier is used.

- Particle materials now cull back faces because `ACEPolygonMeshBuilder` already
  emits the authored reverse-facing polygons. Previously both copies rendered
  from either side. A rendered white-card test measured twice the intended
  additive brightness; the corrected result is one contribution from either view.
- Additive particles apply their source color/fade in display color space before
  conversion for Unreal's linear framebuffer. Retail `D3DPolyRender::SetSurface`
  and `RenderDeviceD3D` use fixed-function modulation/blending with sRGB sampling
  and writes disabled. Linear-space 0.3 opacity made the broad glow too bright.
- Sprite textures follow `ImgTex::InitHardwareTexture`'s four-mip limit and
  `RenderDeviceD3D`'s linear min/mag, point mip selection. The additional 2x2/1x1
  levels and trilinear mip blending previously softened small stars into blobs.
  Terrain and other texture callers retain their full mip chains.

`ACE.RetailParity.ParticleTiming` now includes the exact baton, eight rendered
frames, its persistent star population, and twelve brightness checks across
front/back views, emitter fade, and texture alpha. The shader contribution is
verified against an independent sRGB formula on a black background. Unreal
still composites into a linear scene; this is not a claim of identical retail
gamma-space blending over every background. Live visual acceptance of the wand
and other rending effects remains necessary.

Validation: editor build and BuildCookRun succeeded. All 36 retail-parity tests
passed (32 clean, four with existing fixture/scalability warnings), recorded in
`Saved/Automation/2026.09.10.2-full/index.json`. The packaged Materials,
MeshApplication and Weather tests passed (one clean, two with the existing
uncooked debug-material fallback/scalability warnings) in
`Saved/Automation/2026.09.10.2-packaged/index.json`. Rendered baton frames are in
`Saved/Automation/BatonFx`. Package:
`Saved/Packages/2026.09.10.2/Windows/ACUnreal.exe`.

## Changes in 2026.09.10.1

- Inspection resets hidden-part state when reusing its capture rig. A creature
  with fewer or null parts no longer leaves the next subject's head hidden.
  The model is posed before camera fitting, UI previews bypass world-distance
  animation culling, and closed inspection stops animating. The rendered test
  includes an Undead head replacement after a Shreth preview.
- Bitmap chat reserves its actual wrapped height in main and auxiliary windows.
- Ordered UDP recovery handles and retries single-packet gaps. Manual movement
  cancels stale client approach/use-wait state. The reported marketplace freeze
  has not been reproduced; these are verified recovery-path fixes.
- Particle replacement retains matching pooled render proxies, effect lights
  update with simulation, and held effects follow the current animation pose.
  The real DAT wand/pillar fixtures pass; live rending appearance remains to be
  compared.
- Idle movement ticks, unchanged portrait captures, repeated terrain material
  updates and premature DAT-cache cleanup were removed. `stat ACE` and Insights
  scopes expose the remaining work. See
  [the performance review](PERFORMANCE_REVIEW_2026-09-10.md) for evidence,
  remaining costs and measurement limits.

Validation: all 36 `ACE.RetailParity` tests passed (32 clean, four with warnings,
zero failed/skipped). The editor build and BuildCookRun succeeded. The packaged
Materials, MeshApplication and Weather tests also passed (two with warnings).
The archive is
`Saved/Packages/2026.09.10.1/Windows/ACUnreal.exe`. See the review for the complete
test-report paths and live acceptance limitations.

## Changes in 2026.09.09.13

- Creature inspection chooses the character or creature header using retail's
  profession/display-title presence test, including NPC character profiles.
  Gender and heritage use their numeric appraisal enums; display titles fall back
  to profession when unknown. PK/PK Lite use the actual object flags. Inspected
  character names include the allegiance rank title for their heritage/gender.
- The nine attribute/vital rows retain the authored font, spacing, highlight
  colors and partial-appraisal health percentage. Character details use the
  actual two-column ExtraInfo list and row template, including monarch/patron,
  society rank, three body-armor groups, ratings, fellowship, birth/age, chess,
  fishing, deaths, title count and armor legend. Creature profiles retain their
  rating rows without inheriting character-only fields. The list scrolls when
  needed, grows with the frame and preserves its scroll position on refresh.
- Inspection portraits fit the subject's visible DAT mesh bounds with retail's
  heading and vertical field of view. The camera stays independent of object
  scale, and the capture composites its transparent background correctly. Editor
  camera visualization geometry is excluded from both the portrait and bounds.
  A missing appearance is retried instead of caching an empty preview.

Sources: retail `gmExaminationUI::RecvNotice_AppraisalInfo`,
`BasicCreatureExamineUI::Init`/`AddLineToMiscInfo`,
`CharExamineUI::SetAppraiseInfo`, `CreatureExamineUI::SetAppraiseInfo`,
`AttributeInfoRegion`/`Attribute2ndInfoRegion`, `AppraisalSystem`,
`AllegianceData::GetFullName`, `AllegianceSystem::GetTitle`, and
`ClientUISystem::DeltaTimeToString`. Layout/font evidence: DAT 2100006B and its
2100001C/10000337 detail-row template.

UIScreens covers character/creature switching, headers, property mappings,
partial appraisal, scrolling and resizing, rank/title fallback, and actual
rendered model coverage/framing at different scales. These checks use fixture
appraisals and real DAT models. Live identification of players, NPCs and enemies
still requires gameplay acceptance; the fixtures do not prove full retail parity.
Society relationship colors and periodic combat appraisal polling remain outside
the current verified coverage.

Validation: editor build succeeded. The full `ACE.RetailParity+ACE.Packaging`
suite passed 36 tests (32 clean, four with warnings, zero failures or skipped),
recorded in `Saved/Automation/2026.09.09.13-final/index.json`. Warnings concern
deliberate world-entry failure fixtures, unresolved effects in the particle
fixture, and saved console settings taking precedence over test scalability.
The final rank-title and legend changes passed UIScreens again in
`Saved/Automation/2026.09.09.13-rank/index.json`. BuildCookRun completed with
exit code 0 and archived `Saved/Packages/2026.09.09.13/Windows/ACUnreal.exe`.
The packaged executable passed Materials, MeshApplication and Weather (three
tests, zero failures, two with warnings). MeshApplication could not load the
optional engine debug VertexColorMaterial and used WorldGridMaterial; Weather
reported console-setting precedence. Results:
`Saved/Automation/2026.09.09.13-packaged/index.json`. UIScreens requires the
editor context, so packaged smoke tests do not repeat its inspection assertions.
Test-generated package settings/logs were moved out of the distribution to
`Saved/Automation/PackagedRuntime-2026.09.09.13`.

## Changes in 2026.09.09.12

- Walking off an outdoor ledge enters the same swept ballistic movement used by
  jumps, without sending a Jump action. Prediction and position reporting continue
  with no keys pressed. Idle reconciliation includes altitude; ForcePositionSeq
  corrections apply the server's full position without invoking portal placement.
  Landing expires the pre-contact server anchor so idle reconciliation cannot
  pull the character back up. WorldEntry covers an idle 30 metre fall with that
  stale anchor present, landing, and vertical server corrections.
- Right-clicking a side pack identifies the pack. Both trade offer lists now allow
  selecting and identifying their objects, with the retail selected-slot frame.
  Trade errors 044C through 0450 have readable messages. Protocol 044C means ignored
  requests; 044E means the trade approach/distance check failed, not trade blocking.
- Item inscription paper accepts editing for owned inscribable items, including
  nested inventory and equipped objects, when the scribe permits it. Focus loss
  sends SetInscription (00BF), including an empty string to erase text; Escape
  cancels. The client reads the appraisal back because ACE has no writing ack.
  Existing inscriptions show their scribe. Source: retail ItemExamineUI focus and
  editable-state handlers, ACE GameActionSetInscription and Player_Inventory.
  UIScreens covers pack/trade clicks and multiline writing, clearing, cancellation,
  another scribe's read-only text, and the emitted packet fields.
- Portal resource prefetch preserves the existing gameplay DAT tree when reusing
  the binder. This keeps inventory chrome and its overlays on the same panel tree.
  UIScreens checks panel identity and visibility after prefetch.
- Held objects stream against their parent's network position even before the
  parent actor has spawned. Residency restoration includes attached objects.
  InteriorStreaming covers either arrival order, deferred attachment, actual
  Weeping Wand geometry, and restoring the weapon when its player returns.

Live cliff/portal/trade exchanges and the reported remote player loadout still
require gameplay acceptance. Automated checks do not establish full retail parity.

Validation: editor build succeeded. The full `ACE.RetailParity+ACE.Packaging`
suite passed 36 tests (32 clean, four with warnings, zero failures or skipped),
recorded in `Saved/Automation/2026.09.09.12-final/index.json`. The final landing
change also passed WorldEntry and CameraAndEdges in
`Saved/Automation/2026.09.09.12-landing/index.json`. Warnings concern deliberate
world-entry failure fixtures, unresolved effects in the particle fixture, and
saved console settings taking precedence over test scalability settings.
BuildCookRun completed with exit code 0 and archived
`Saved/Packages/2026.09.09.12/Windows/ACUnreal.exe`. The packaged executable
passed Materials, MeshApplication and Weather (three tests, zero failures):
`Saved/Automation/2026.09.09.12-packaged/index.json`. The packaged runner lacks
the optional HTML report template; JSON results were exported successfully.
Test-generated package settings/logs were moved out of the distribution to
`Saved/Automation/PackagedRuntime-2026.09.09.12`.

## Changes in 2026.09.09.11

- Remote players reject stale position/teleport stamps and motion from a different
  instance. Extrapolation now uses a 50 cm minimum drift allowance, replacing the
  accidental 50 AC-unit (50 m) floor. Runtime tests cover stopped remote actors,
  equal elapsed time at different frame rates, and a subsequent server position.
  This addresses the position discrepancy reported for Stormbringer II; the exact
  live two-client portal scene has not yet been repeated.
- Terrain keeps its 128 authored triangles at every distance. Coarsening a
  landblock's 8x8 grid changed the ground elevation beneath fixed buildings.
  Distance transitions no longer rebuild meshes or recook collision. All requested
  blocks are queued nearest first to the bounded workers, with a 4 ms application
  budget and faster expansion. Radius 5 still means an 11x11 region (121 blocks),
  needed for the selected viewing distance, not 121 simultaneous synchronous loads.
- Sky particle children inherit their celestial slot's rendering order instead
  of drawing over every star/cloud layer. Background sky materials only shade
  clear-depth pixels, matching retail's before-world pass even where a large cloud
  card intersects distant terrain. Long-lived cloud emitters retain their authored
  3300 second lifespan; disabling weather retains before-pass celestial particles.
  The rendering fixture now includes actual PES 330007DB, the camera used for
  billboarding, every day group, and foreground occlusion at 5 m, 1 km and 5 km.
- Trade renders the complete visible offer grids and labels its Trade/Clear All
  controls. Register/add/clear events reset offers and acceptance correctly;
  0x0451 displays "Trade closed." The spell hotbar retains all 13 visible tiles,
  including its default blank spaces, while insertion appends after the last spell.
- Enter receives preview-key handling before other UI controls consume it and
  focuses the chat keyboard entry. Portal arrivals reuse the existing HUD/binder,
  preserving open windows instead of reinitializing their state.
- Chat configuration includes independent routing for the main window and all
  four auxiliary windows, plus linked inactive/active background opacity sliders.
  These settings persist and participate in Apply/Reset/Defaults. Incoming public
  channels retain distinct message types for filtering and avoid duplicate labels.
- Particle pooling first reuses matching geometry/materials, avoiding repeated
  rebuilds when glow and drip emitters retire meshes in alternating order. Actual
  fire, frost, acid, blunt and piercing weapon setups now have fixture coverage.
  Their timing/parenting checks do not establish visual parity for every rending
  effect; that remains a live comparison requirement.
- Hardware identified: Core Ultra 9 285K (24 cores) and RTX 5090. CPU fixture
  measurements include about 0.87 ms for the HUD tick and about 2.8 ms for a warm
  appearance construction. These are isolated costs, not a measured game frame.
  A populated live-world CPU/GPU capture is still required to establish the FPS
  bottleneck and verify the end-to-end improvement.
- Validation: all 36 editor regression tests passed (32 clean, four with logged
  warnings), with zero failures or skipped tests in
  `Saved/Automation/2026.09.09.11-final/index.json`. Win64 Development
  build/cook/stage/archive completed with exit 0 in
  `Saved/Logs/Package-2026.09.09.11.log`. The packaged executable passed
  `ACE.Packaging.Materials`, the rendered `ACE.RetailParity.Weather` test, and
  `ACE.RetailParity.MeshApplication`; JSON results are in
  `Saved/Automation/Packaged-2026.09.09.11` and
  `Saved/Automation/Packaged-world-2026.09.09.11`. The packaged runner cannot
  generate its optional HTML template, but exported JSON and test results are
  available. The archive is `Saved/Packages/2026.09.09.11/Windows`.

## Changes in 2026.09.09.10

- Commandlets disable MCP auto-start on their process-local settings object
  before the UE 5.8 plugin's post-engine-init callback. Previously, cooking
  inherited the editor's saved auto-start preference and failed when its HTTP
  listener could not also bind port 8000. The setting is never saved by this
  override; interactive editor MCP startup remains enabled.
- Validation: Win64 Development build/cook/stage/archive completed with exit 0
  while an exclusive loopback listener held port 8000 throughout packaging
  (`Saved/Logs/Package-2026.09.09.10.log`). A subsequent non-commandlet editor
  launch successfully created the MCP listener on port 8000 and passed
  `ACE.Packaging.Materials` (`Saved/Automation/Editor-MCP-2026.09.09.10/index.json`).
  The saved `bAutoStartServer=True` preference was preserved.

## Changes in 2026.09.09.9

- Packaging no longer compiles the offline DAT exporter as a runtime module.
  World Partition login holds use DisableStreamingIn/EnableStreamingIn, preserving
  the baked partition configuration and only restoring a hold owned by this subsystem.
  World Composition changes its live tile info without editor asset-update calls.
- All 31 active material parents are generated before cook asset discovery and
  loaded from cooked assets in game builds. Graph defaults are persistent textures;
  material editor-data subobjects are saved without inherited transient flags.
  Forced asset-registry rescanning includes newly generated parents on the first
  cook. The invalid, unused CustomStencil-masked terrain graph was retired in favor
  of the active portal-plane material. Examination variants also key on sidedness.
- Runtime static material slots no longer use the editor-only constructor signature.
  Baked landscapes use dynamic material instances for live fog in game worlds;
  constant-instance editing and editor-only test fixtures stay in editor builds.
- Generated static meshes are owned by the DAT subsystem, giving Chaos a valid
  game world for runtime collision cooking. Transient-package ownership produced
  a mesh with triangles but no blocking body outside the editor. MeshApplication
  now provides an actual game-instance/world context and runs in packaged clients;
  its DAT geometry counts and downward pawn sweeps pass in both builds.
- The default package cooks the startup map and runtime shader assets, avoiding
  unrelated offline world exports. DAT files and saved account credentials are
  external to the package. Cooked output uses disk files: a repeat package exposed
  Zen exiting between cook and staging, leaving the staging operation without its
  input store. See the project README for custom map packaging.
- Validation: Win64 Development build/cook/stage/archive succeeded, including
  repeated material generation. Editor and packaged GPU tests pass for all active
  shader parents, portal clipping/depth/texture updates, and DAT static collision.
  UIScreens, WorldEntry,
  and CameraAndEdges passed in the editor (WorldEntry's three intentional recovery
  warnings remain). Packaged startup reaches the login UI. Live server gameplay
  in the packaged executable is not yet accepted.

## Changes in 2026.09.09.8

- Attributes and skills now both toggle a selected row off, following
  gmAttributeUI::SetSelection and gmSkillUI::SetSelection. Attribute refresh no
  longer coerces an empty selection to Strength. Skill clicks refresh the footer
  and rows immediately. Clearing highlights goes through SetIconDid's cache, so
  selecting a previously cleared row restores the retail selection texture.
  Selected highlights remain hit-test-invisible, like the unselected row overlays.
- Automatic item approach retains the target heading when Mouse Turning is on.
  The generated forward input no longer activates camera-follow yaw. Keyboard
  movement still cancels approach, and manual camera-follow controls retain their
  existing behavior.
- MoveTo failure checks use full 3D displacement from the original approach start
  and let arrival succeed first, matching MoveToManager::HandleMoveToPosition.
  Crossing a finite server limit stops movement and reports the retail 003D
  "You charged too far!" error. Receiving that server error also cancels local
  approach prediction. Repeated MoveTo packets retain the starting position,
  while an unlimited update clears an earlier finite limit. Finite limits above
  100 AC are no longer incorrectly treated as unlimited.
  Limits remain server-authored: Player_Move::GetChargeParameters supplies 15 AC
  for melee charges; ordinary use/pickup defaults to float.MaxValue in both
  retail MovementParameters and the server. This change does not impose a new
  charge-distance limit on interactions for which the server supplies none.
- Validation: editor build succeeded. UIScreens, WorldEntry and CameraAndEdges
  passed (3 suites, no failures). WorldEntry's three warnings are intentional
  unsafe-spawn/recall failure fixtures. Report:
  Unreal/Saved/Automation/2026.09.09.8/index.json.
  Native mouse tests click icon, text and value areas repeatedly, checking the
  selected ID, immediate footer, actual selection texture and empty selection
  after stat refresh. Movement tests exercise the controller and loopback wire
  packets at orbit offsets -90, +90 and 180 degrees with Mouse Turning off/on;
  all six reach the use cylinder and emit exactly one Use action. Tests also
  cover the 15 AC charge boundary, vertical displacement, start preservation,
  arrival precedence, finite/unlimited packet decoding, stop packets and server
  failure handling. Live server reproduction of these fixes remains to be checked.

## Changes in 2026.09.09.7

- The September 9 12:00 crash (UECC-Windows-432B88E94F0F11BC4E4B48A011F2855C)
  enters SACERetailTextBlock's Slate fallback during prepass. The Slate DLL
  instruction maps to FPlainTextLayoutMarshaller::SetText reading StrikeBrush's
  resource. UTextBlock installs a pointer to its own StrikeBrush in Slate; a
  retained Slate label must not use it after the owning UObject is released.
  Desired-size and paint now return without entering fallback in that case.
  Live labels still fall back normally while their DAT font is unavailable.
- The blue banner-pillar glow at 87.4S, 67.2W uses emitter 3200075A, scripts
  33000DBA/33000DBB and setups 02000F8B/02000F8C in landblock 2B12. Its lifespan
  is .03 seconds and its scale shrinks from 3.3 to 1 before rebirth. Retail
  CPhysicsObj::animate_static_object and update_position only update particles
  after MIN_QUANTUM_97 (1/30 second, initialized in global_funcs.cpp). Advance
  particle simulation on that cadence so high-refresh rendering cannot expose
  a shrink/rebirth strobe between physics updates. Keep the authored timing,
  normal expiration, and one birth per update after a hitch.
- Login entries save after a short typing pause, on field commit, on Connect,
  and when the login widget closes. A fresh launch restores the last address,
  port, account and password. First-install defaults remain empty except for
  port 9000; clearing a field replaces the saved value. The entire payload in
  Saved/Login/LastLogin.dat uses Windows DPAPI for the current user, and no
  credentials are written as plaintext or logged. Damaged files fail closed.

Validation: editor build succeeds. LocalLoginSettings covers encrypted Unicode
round trips, replacement with empty entries and corrupt-file rejection.
TextLifetime retains Slate across UObject collection, then performs prepass and
GPU paint; it measures zero size and draws nothing after release. UIScreens,
ParticleLighting and Weather pass. ParticleTiming checks the actual glow's
visibility, size and opacity on every frame at 24, 30, 60, 120, 144 and 240 Hz.
ParticleTiming also verifies normal expiry after stopping the assigned emitter
instance and passes in the corrected follow-up run. Its two unresolved-effect
fixture warnings predate these changes. Reports: Saved/Automation/2026.09.09.7 and 2026.09.09.7-Particle.
The original live crash sequence and live pillar appearance still need user
play verification; the automated lifetime and emitter fixtures are distinct
from a live-server reproduction.

## Changes in 2026.09.09.6

- Login server address, account name and password now default to empty. The
  editable fields remain available, and the existing default port is retained.

## Changes in 2026.09.09.5

- Character selection still uses the native 800x600 layout and the authored
  40000009 sans font. Fixed the CPU text compositor storing premultiplied RGB
  where Slate expects straight alpha, which applied glyph coverage twice and
  made character names thin and dark. Text lands on integer pixel positions.
  Character rows now use the server slot limit and the twenty-row density cap
  from gmCharacterManagementUI::RebuildCharacterList; text and hit areas agree.
- Camera translation uses CameraManager's default stiffness 0.45 and its
  stiffness * delta-time * 10 response. Running lets the view trail the player,
  then it settles back when the player stops. This preserves manual zoom,
  camera collision and the existing mouse/keyboard control preferences.
- Corrected background-sky material behavior against D3DPolyRender::SetSurface
  and RenderMeshSubset. The starfield's black background now alpha-composites
  over the day dome. The clipped/translucent cloud surface takes retail's
  alpha-blend override instead of additive blending, retaining its authored
  opacity. Removed duplicate opacity modulation from the sky's emissive path.
- Sky vertex lighting now combines authored emissive, ambient and diffuse
  sunlight, with texture modulation in display space as in the fixed-function
  renderer. This restores the rainy night's purple cloud/horizon tint instead
  of the bright stars and green/yellow day-dome colors. Layer order, authored
  geometry, scrolling and depth occlusion remain active.

Validation: editor target builds successfully. CameraAndEdges, NetworkWeather,
UIScreens and Weather pass. NetworkWeather exercises handshake time, clock
advance, portal/interior transitions, day changes and reconnects using packet
fixtures. UI captures verify native-size placement, antialiased text and full
roster selection. Weather renders all twenty DAT day groups at three phases and
four directions, checks opaque foreground occlusion, and captures unobstructed
rainy-night horizons with an explicit purple-tint regression check. The latter
were visually compared with the supplied retail screenshot; no green strip
appears in those four captures. Reports are under Saved/Automation/2026.09.09.5
with follow-up reports in 2026.09.09.5-Visual and 2026.09.09.5-Horizon; images
are in Saved/Automation/RetailParity. Live server
play and an exact pixel-for-pixel retail sky comparison remain unverified.

## Changes in 2026.09.09.4

- Roof-edge jumping now sweeps the full airborne capsule, including its lower
  sphere, against both roof and soffit faces. Each remaining movement segment is
  clipped in three dimensions and swept again. A falling contact uses retail
  LandingZ; an initial support can be cleared only when the jump is departing it.
  This replaces center-ray landing and the airborne 2D wall-slide path. Grounded
  bench/step support remains on the existing lower-sphere solver.
- UI uses physical pixels at the default application scale. Unreal's inherited
  DPI curve had enlarged the 300-pixel panels and their glyphs at higher
  resolutions. Gameplay retains DAT edge anchoring; character selection and
  creation remain centered at their authored 800x600 size with black surrounds.
- Corrected the 2100003F font catalog mapping, including distinct base/sans
  families, field-value aliases and the fancy-font IDs. Generated text labels
  now resolve DAT glyph atlases too. Glyph drawing modulates RGBA atlases by the
  requested text color, matching TextureBasedFont::RenderText diffuse colors.
  This does not replace Slate's editable-text implementation with a DAT editor.
- Skill and attribute rows retain base/current data and show the current value
  in retail green/red. Their selected footer now includes the signed modifier,
  for example Focus: 240 (+60), with only the modifier colored. Coordination and
  Quickness follow retail's row order; XP raise actions use the corresponding IDs.
- Toolbar buttons maintain their DAT highlighted state while their panel is
  open. Equipment preview selection lights the visible garment parts identified
  by gmPaperDollUI::GetSelectionMaskFromObject/ApplyPartSelectionLighting. The
  preview capture updates through the two 0.2-second lighting phases and restores
  its materials afterward; selecting self also retains the world-model flash.
- Contracts now exposes Contracts, Journal and Page List with selected tab art.
  Journal pages support editable title/label/notes, location, timers, navigation,
  search, sorting, deletion and a scrollable page list. Single click selects;
  double click opens the page. Notes are stored locally per server/character,
  separately from server contracts, following gmJournalUI's client-local role.
- Config's implemented video and mouse controls use DAT slider bar/thumb
  06001285/86, green dropdown display/arrow 060012B3/B1, round toggle states
  06004D15..18 and 060012C5 dividers. The existing Apply/Reset/Defaults footer
  handles these controls, avoiding a duplicate set of native buttons. The
  original retail Config's complete sound/camera/renderer option set is not
  claimed implemented by this styling change.

Validation: Unreal Editor Development build succeeds. CameraAndEdges, RoofEdges
and WorldEntry pass, covering thin roof edges/corners, vertical departure,
underside blocking, airborne wall sliding and the existing actual-DAT bench
movement cases. CharacterCreationScreens passes with the unscaled layout.
UIScreens passes, including journal persistence/search/scrolling, selected-toolbar
states, stat modifiers and opaque character-screen surrounds. Captures were
visually checked for the native-size character screen, Config controls, attribute
footer and Journal tabs. Reports and captured images are recorded under
`Saved/Automation/2026.09.09.4-UI` and `Saved/Automation/RetailParity`.
Live traversal of every building and complete visual/interaction parity across
all retail windows remain acceptance work, not conclusions of these fixtures.

## Changes in 2026.09.09.3

- A/D and Left/Right arrows turn the player in either Mouse Turning mode; Q/E
  remain sidestep controls. Explicit keyboard turning takes precedence over
  camera-follow steering, including W+A/W+D with an existing orbit offset.
  The existing turn axis, predicted AC rotation, and movement-message path carry
  the turn; no server protocol changes are needed.
- Options button captions reset visibility before each page refresh. Apply,
  Reset, and Default no longer remain on Game Play after visiting a list page.
  Footer captions use the DAT button's full rectangle, authored font, margins,
  and centering instead of clipping an 18-pixel font into an 11-pixel estimate.
  Game Play button lookups are scoped to their active page.

Validation: the editor build succeeded. CameraAndEdges, UIScreens, and WorldEntry
all passed in `Saved/Automation/2026.09.09.3/index.json`. The movement fixture uses
Unreal input events and full controller ticks to check A/D and arrows both at rest
and with forward movement, under both mouse modes; predicted facing matches the
requested keyboard turn and Q/E still sidestep. UI coverage visits every list page
and returns to Game Play, checks caption visibility and full font-height allocation,
and refreshes each page repeatedly. `GameplayOptionsReturn.png` and
`GameplayChatOptions.png` were visually inspected: captions are centered and
unclipped, with no footer labels on Game Play. WorldEntry's existing deliberate
unsafe-arrival/recall fixtures still emit their expected warnings. Live hardware
input was not exercised in this pass.

## Changes in 2026.09.09.2

- Per the user's follow-up, mouse-wheel zoom is independent of Mouse Turning.
  OFF still disables orbit/capture and two-button movement; both ON and OFF allow
  zoom in/out. UI scrolling and chat/key-binding focus retain their input guards.
- Config now exposes Mouse turn speed (10%-300%) and a Default speed button.
  The default is 0.75 degrees per mouse pixel, up from 0.22 (about 3.4 times faster).
  The preference scales yaw and pitch immediately and saves locally to
  `[ACE.Camera] MouseTurnSpeed` in GameUserSettings.ini, alongside the existing
  client settings. This is a user-requested control preference, not a retail
  protocol extension. It remains available across reconnects and client restarts.
- Existing RMB orbit, two-button forward movement, and left-click selection are
  preserved. The controller clamps invalid/out-of-range sensitivity values.

Validation: build 2026.09.09.2 succeeded. CameraAndEdges and UIScreens both passed
with zero warnings/errors in `Saved/Automation/2026.09.09.2/index.json`. Coverage
includes zoom in/out with either turning mode, focus blocking, immediate yaw/pitch
scaling, persisted setting reload, recreating Config with the saved speed, and
restoring defaults through its button. `GameplayVideoOptions.png` was visually
checked for the new controls and layout. Live hardware mouse input was not
exercised in this pass.

## Changes in 2026.09.09.1

- The character's Mouse Turning option (PlayerOption 0x31) now gates world-camera
  orbit and mouse-wheel zoom. OFF preserves left-click selection and right-click
  inspection. ON enables captured RMB orbit, and holding both mouse buttons moves
  forward after aligning the character with the camera's horizontal heading.
  Left alone still selects. Keyboard camera controls remain available.
- Mouse gestures must start outside the UI. Inventory, spell, and scrollbar drags
  cannot start camera capture. Releasing either button stops mouse-driven forward
  movement; releasing RMB restores the cursor. A gesture used for movement cannot
  become an inspect click. Disabling the option or focusing chat/keybind editing
  cancels capture and requires a new press before another orbit.
- The equipment portrait composites the captured model over the existing DAT
  panel background. Retail `CreatureMode::Render` draws the model into its viewport;
  the PaperDoll element in layout 0x21000005 does not supply an opaque background.
  `UACECaptureImage` uses Slate's inverse-alpha and premultiplied-alpha draw flags
  with SceneCapture HDR RGB/inverse opacity. This preserves the panel texture,
  partial coverage, and widget fading without a material or editor-generated asset.
- The optional two-button movement behavior is the user's requested control scheme,
  not a claim that the retail camera uses those controls. Option bitfield/network
  persistence continues through the existing SetSingleCharacterOption path.

Validation: the editor build succeeded. CameraAndEdges, UIScreens, and WorldEntry
all passed in `Saved/Automation/2026.09.09.1-Final/index.json`. The UI test checks
inverse capture alpha, visible foreground pixels, transparent background pixels,
and the final panel composition (467 visible model samples; 18 of 2,133 background
samples differed at filtered edges). The rendered equipment panel was also inspected.
WorldEntry retains its deliberate unsafe-arrival warning fixture. Build/runtime logs
are `Saved/Logs/Build-2026.09.09.1.log` and `Saved/Logs/2026.09.09.1-Final.log`.
Live server mouse/viewport input has not been exercised in this pass.

## Changes in 2026.09.08.6

- The spell hotbar displays one trailing empty insertion cell and adds another when
  it is filled. Both arrows are hidden while the favorites plus insertion cell fit
  in the thirteen-cell viewport. Scrolling changes only the viewport; selection is
  an absolute favorite index and casting still uses that favorite. Inserting or
  moving a favorite preserves the selected spell's identity. Favorites can grow
  beyond the previous arbitrary 56-entry limit. The innate slot displays the held
  caster and casts through `UseWithTarget`, with its item/spell name beneath it.
- The preserved user log `Saved/Logs/ACUnreal-user-20260908-InventoryInspect.log`
  shows PlayerDescription before PlayerCreate on both logins. The inventory footer
  was stored under player ID zero and the equipment footer was discarded. The
  pending profile now transfers to the actual player on PlayerCreate. Equipped
  membership survives ObjectCreate arriving before or after that transfer, and
  missing creates retain their saved positions in each container's item/pack lists.
  This fixes a concrete cause of differing inventory and bag order after relogin.
- Inventory reordering continues to send the server's `PutItemInContainer` action
  and use authoritative acknowledgements. Forward pack insertion accounts for
  source removal, as `UIElement_ItemList::AcceptDragObject` does. Bag order and pack
  occupancy now participate in the panel's refresh hash. Retail capacity meters
  use the authored 5x30 background/fill artwork on side packs and the main pack.
  Open-pack markers and selected-item frames are separate, bag icons use the
  authored inner 32x32 rectangle, and the burden meter uses its original artwork
  without a second color tint. The paperdoll checkbox reads `Slots`; aetheria slots
  follow login/live PropertyInt 322 (`AetheriaBitfield`), not character level.
- Item inspection retains DAT spell descriptions and renders the item fields
  supplied by the appraisal: tinkering, workmanship adjective, imbue properties,
  defense/mana conversion bonuses, wield/activation requirements, elemental caster
  bonuses against monsters/players, spellcraft, mana, mana consumption rate, and
  spell descriptions. References: `ItemExamineUI::Appraisal_ShowMagicInfo`,
  `Appraisal_ShowShortMagicInfo`, and its workmanship/imbue/requirement formatting.
  Inscribable items use the authored centered, black `uninscribed` prompt state.
  These focused caster/item corrections do not certify every appraisal type or
  the editable-inscription workflow.
- Terrain radius 5 is the retail `LScape` constructor default: 11x11 = 121 terrain
  blocks. Outer blocks use coarse terrain LOD; nearby scenery preloads within
  radius 2. Keep that horizon coverage to avoid reintroducing town pop-in.
  Progress and large connected environment-cell sets now log at Verbose, while
  a terrain queue making no progress for 30 seconds with loading enabled still
  warns. Deliberately paused outdoor loading while indoors is not a stalled load.

Validation: `Launch.bat --build-only` succeeded. All 32 `ACE.RetailParity` tests
passed in 117.62 seconds (30 clean, two with the existing intentional missing-effect
and unsafe-world-entry warnings). Reports: `Saved/Automation/2026.09.08.6-Final/`
and `Saved/Logs/2026.09.08.6-Final.log`. The inventory test replays login before
player creation, reversed object arrivals, equipped membership, cross-bag moves,
failed moves and root/side contents refreshes. UIScreens exercises real mouse
input for both directions of bag insertion, item reordering, acknowledgements
without forced UI invalidation, short/overflow hotbars, scrolling followed by
casting, end-slot drops, innate item casting, network aetheria unlocks, main/side
pack capacity and a caster inspection that scrolls without artificial padding.
Rendered captures in `Saved/Automation/RetailParity/` were visually inspected;
the half-full pack has its authored yellow lower-half fill and the inscription
prompt uses black centered text.
Live cross-client logout/login comparison and every item appraisal type still
require acceptance; isolated tests do not alter the user's server inventory.

## Changes in 2026.09.08.5

- Selection now tests visible mesh triangles before falling back to selection
  bounds, using the same path for hover, click, identify, and drag. Retail
  `Render::GetMouseSelectionObjectID` / `GfxObjUnderSelectionRay` gives polygon
  hits priority over broad bounds. A regression puts an NPC in front of a statue
  whose oversized selection proxy extends closer to the camera; the NPC wins.
  World walls still limit selection distance.
- Grounded movement and step-up use the retail walkable normal threshold, while
  airborne landing retains the separate landing threshold. Floor probes begin
  within step reach instead of above the character's head, where an upper floor
  could hide the final basement tread. Penetration recovery accepts incremental
  movement out of the same deep contact but rejects a second wall. This addresses
  pedestal trapping without allowing recovery through neighboring geometry.
  Retail references: `CTransition::step_up`, `OBJECTINFO::get_walkable_z`.
- Bench traversal now separates horizontal movement from the downward step and
  retains lower-sphere support at narrow edges, following `CTransition::step_down`.
  Previously, a diagonal sweep cut through the seat edge, then a center-ray slide
  dropped the feet to the floor and penetration recovery pulled the body back onto
  the seat. Step-up candidates also require progress in the movement direction.
  `WorldEntry` walks the actual town-hall bench setup `02000120` in eight cases:
  both axes, both directions, and indoor/outdoor movement modes. Each case crosses
  the seat, rises and descends once, returns to the floor, and never penetrates
  the seat. This isolates the cooked bench; it does not simulate the whole hall.
- Translucent world surfaces now cast ordinary dynamic shadows through the
  material's alpha-mask shadow pass (cutoff 0.1); visible opacity stays continuous.
  This is the user's chosen ordinary shadow, without colored transmission. Sky,
  weather, and particle material bases do not enable this shadow pass.
  `RuntimeActors` compares the same half-transparent surface with casting disabled
  and enabled: 2,817 receiver pixels darken, with summed RGB loss 231,187. The
  fixture excludes black background noise and uses an actual DAT surface material.
- Cell filtering now excludes an unrelated room's geometry components instead
  of its entire actor. Furniture and stairs can cross cell boundaries even when
  owned by a neighboring cell, as in retail `CPhysicsObj::add_obj_to_cell` /
  `calc_cross_cells_static`. The reported town-hall stair setup `02000623` belongs
  to `0134010F`; all 506 sampled upward treads remain available from `0134010E`.
- Reversing direction during a running jump charge clears a temporarily held
  JumpCharging pose before restoring locomotion. Portal exit stretch no longer
  widens the separate portal camera: retail `gmSmartBoxUI` stretches the world
  projection, not the CreatureMode tunnel camera. The tunnel now covers the frame
  throughout that exit phase.
- The Yaraq bench setup `02000368` / mesh `01000EB3` has two seat faces exactly
  coplanar with room `7D64013A`: polygon 15 at X=95 and polygon 16 at Y=81.5.
  Retail `PView::DrawCells` draws room geometry before furniture with LEQUAL
  depth. Unreal now recesses room render depth by 0.0025 AC to preserve that
  tie-break despite opaque draw reordering. Authored geometry and collision stay
  unchanged. The runtime fixture includes the actual upper-floor benches and a
  GPU comparison of coplanar room/furniture materials under tiny camera shifts.
- Sky geometry retains DAT vertices, winding, and scale relationships. Removed
  reversed shells, radial cloud remapping, and interpolated replacement headings;
  `SkyDesc::GetSky` uses discrete replacement mesh/heading and interpolates only
  authored brightness/transparency values. DAT visibility replaces the hardcoded
  night cutoff. Sky layers preserve authored draw order without depth writes,
  while world geometry occludes them in Unreal's later translucency pass.
  `Weather` captures all 20 sky groups at three clock samples and four camera
  headings (240 renders), verifies foreground depth occlusion against an HDR
  reference, and retains rain motion/indoor gating and distance-fog coverage.
- General's F7DE transport was already distinct from local Speech; the incoming
  formatted message was incorrectly wrapped in another local-speech prefix.
  The UI now displays formatted Turbine and legacy channel messages once.
  `UIScreens` exercises the actual General dropdown and entry, checking a
  server-assigned room id, raw message text, and absence of a local speech action.
- `/r` uses the last player teller and a name-based tell that works across
  landblocks. NPC speech/emotes cannot replace that recipient. Added retail
  squelch/unsquelch argument handling, account/reply/category switches, `/chat`,
  `/notell`, networked filters, `/on`, `/off`, `/clist`, `/index`, legacy channel
  aliases, and allegiance management commands. Default squelch is AllChannels=1,
  not Broadcast=0. Fellowship, patron/vassal, co-vassal, and broadcast formatting
  follows `ClientCommunicationSystem`; typed payloads are checked against the
  corresponding server readers. Allegiance MOTD/name, bans, officers/titles,
  lock/bypass, house permissions, and chat moderation are covered by loopback
  packet fixtures. The DAT's 309 pose-command entries all resolve to motions.
- No live-account commands or server moderation actions were sent. The exact
  Sanamar, basement, and town-hall walkthroughs still require in-game acceptance;
  these offline fixtures do not establish complete retail collision or command
  parity.

Validation: `Unreal/Launch.bat --build-only` succeeded and the complete
`ACE.RetailParity` suite passed all 32 tests (zero failed or skipped) in 82.35
seconds. The report has warnings in the ParticleTiming unresolved-effect fixture
and WorldEntry failure/recovery fixtures. Final report:
`Unreal/Saved/Automation/2026.09.08.5-Final/index.json`; runtime log:
`Unreal/Saved/Logs/2026.09.08.5-Final.log`; build log:
`Unreal/Saved/Logs/Build-2026.09.08.5.log`. The coplanar GPU depth fixture resolves
furniture at 180.479 cm and the room at 180.729 cm; their combined render retains
the furniture depth across all three camera shifts. Eight bench walks have zero
seat penetrations, one rise-to-descent reversal, and end at floor height.

## Changes in 2026.09.08.4

- Enemy position corrections now replace the prediction anchor on every accepted
  F748, including corrections smaller than two metres. Collision sweeps constrain
  extrapolated movement from the previous predicted position; they no longer block
  interpolation from the displayed body to an authoritative position. This avoids
  trapping the visual creature behind a local obstacle while targeting, projectiles,
  and its subsequent corpse use the server position. Retail reference:
  `CPhysicsObj::MoveOrTeleport` / `InterpolateTo`. `RuntimeActors` verifies both
  creature blocking during prediction and correction across that blocker, plus a
  75 cm correction during locomotion.
- Worn Mukkir Wings use ClothingTable `10000867`, part 29 `01004D80`, and clip-map
  surface `08001906`. Their U coordinates extend from 0.893786 to 1.94818. Clamping
  to the first tile sampled the black edge and removed the wing membranes. Setup
  materials now wrap each axis whose UVs extend beyond the unit tile, with that
  addressing mode included in the material/texture cache key. Untiled face-patch
  edges remain clamped. This also preserves tiled slippers and other clothing.
  `AppearancePlacement` builds the actual wings with the Hoary Mattekar over-robe
  and Bunny Slippers and produces `Saved/Automation/MukkirWings.png`.
- Town terrain previously wrote depth 48 cm behind its physical surface (22 cm
  in the alternate floor-priority mode). The `WorldEntry` rendering regression
  measured 548 cm at a Yaraq floor physically 500 cm from the capture camera.
  Landblock and chunk terrain now use only a 0.25 cm coplanar tie-break, preserving
  contact depth for shadows and grounded characters. This addresses a rendering
  source of the apparent floating without shifting the model below its collision
  origin. The actual player tick settles the reported `7D64000D` Yaraq position
  to 1200 cm, matching the DAT floor and visual origin. The authored robe hem is
  approximately 4.18 cm above that origin; it has not been artificially lowered.
- Equipment definitions are preserved as SQL **data fixtures only** under
  `Tests/Fixtures`, sourced from ACEmulator/ACE-World-16PY-Patches:
  [Mukkir Wings](https://github.com/ACEmulator/ACE-World-16PY-Patches/blob/master/Database/Patches/9%20WeenieDefaults/Clothing/Clothing/52193%20Mukkir%20Wings.sql),
  [over-robe](https://github.com/ACEmulator/ACE-World-16PY-Patches/blob/master/Database/Patches/9%20WeenieDefaults/Clothing/Armor/45031%20Hoary%20Mattekar%20Over-robe.sql),
  [slippers](https://github.com/ACEmulator/ACE-World-16PY-Patches/blob/master/Database/Patches/9%20WeenieDefaults/Clothing/Clothing/34022%20White%20Bunny%20Slippers.sql).
  No database writes or live-account tests were performed.
- Build and runtime version are `2026.09.08.4`. Launch build evidence:
  `Saved/Logs/Build-2026.09.08.4.log`. Final offline regression evidence:
  `Saved/Automation/2026.09.08.4-Final/index.json` and
  `Saved/Logs/2026.09.08.4-Final.log`: all 32 tests passed in 94.25 seconds
  (30 clean, two with the five expected particle/recovery fixture warnings).
  The final appearance fixture also passes independently in
  `Saved/Automation/2026.09.08.4-Wings/index.json`.
  Rendered Yaraq ground depth is now 500.25 cm at a physical distance of 500 cm.
  The exact live Reedshark encounter and visual ground contact require in-game
  acceptance; these
  regressions do not establish complete retail movement or visual parity.

## Changes in 2026.09.08.3

- Khayyaban arrival: the preserved `ACUnreal-user-20260908-ParityReview.log`
  identifies `9F44001A (90,24.553,31.890)`. The destination is below the DAT
  outdoor terrain (36.54606 m). Outdoor placement now begins its support sweep
  at or above the decoded terrain, retaining the server XY and requiring cooked
  support, a clear body capsule, and a valid cell. `WorldEntry` loads this actual
  landblock and verifies the destination after collision cooking completes.
- Creature prediction uses swept DAT body dimensions and wall sliding instead
  of moving directly through other actors. Runtime coverage moves one Drawohan
  setup toward another and checks that it stops before the other body. Server
  position corrections remain authoritative. Local penetration recovery checks
  the return path against a second wall; step-up checks both the lateral tread
  adjustment and descent before accepting a landing beside an object. These
  retain retail `CTransition` body-collision intent; this is not a transcription
  of the complete retail transition solver.
- Close-range spell effects: retain projectile actors through the server's
  NoDraw/Cloaked physics update so the following Explode script has an owner.
  The real server `SpellProjectile::ProjectileImpact` sends that state before
  its impact script. Object deletion still destroys the projectile normally,
  consistent with `CObjectMaint`; no artificial post-delete lifetime is added.
  `ParticleTiming` covers a real bolt setup, this message order, effect creation,
  and final deletion. It does not simulate live combat damage.
- Object translucency is applied once in the material scalar rather than also
  being baked into resolved texture alpha. Transparent animation hooks can now
  replace that scalar without multiplying an already-faded texture. This affects
  translucent Shadows and devices. Resolved appearance textures clamp at their
  borders so filtering does not sample the opposite edge of a face patch.
  The material fixture checks preserved source alpha, 0.5 scalar opacity and
  both address modes. Full face-seam and Shadow visibility acceptance is visual.
- Default animation frames replace bind rotations, matching the normal animated
  pose path. Particle attachment offsets no longer receive the actor scale a
  second time. The Fianhe `02001A16` / `33001285` fixture verifies head-part eye
  offsets at scale 1.25 against the DAT. Retail `ParticleEmitter::SetParenting`
  and `Particle::Init` preserve this authored offset. DefaultScriptPart now
  dispatches the attached child's script/intensity instead of the wielder's
  setup script, following `CPhysicsObj::play_default_script(part_index)`.
- Base outdoor fill was raised modestly. Scenery remains active through the
  two-landblock Chebyshev ring while terrain keeps its separate LOD policy,
  reducing near-town streaming pop-in at a bounded additional memory cost.
  Sky rendering excludes PhysicsBSP-only sections and applies the authored
  global rotation before heading, as `GameSky::Update`, `CalcFrame` and
  `Frame::grotate` do. Weather tests cover the rotation axes. The reported flying
  sky sheets and the Arwic south traversal have not been visually reproduced.
- Restored vendor/panel selected and unselected DAT tab states; pressed toolbar
  icons use available DAT state media when the requested state has no image.
  Fixed visible-page lookup when a hidden keyboard page shares a name with the
  Character options page. Inventory/tooltips use a dark body and gold border.
  Explicit selection triggers a short brightness pulse in world materials and
  inventory icons; health/name refreshes do not retrigger it. Repeated clicks on
  the same object do. The pulse preserves the original material and texture.
- Contract journal rows select from either the name or status column. Names,
  stages, notes, contacts and locations use the DAT contract table and server
  contract state. Relative server timers count down from receipt time. Journal,
  component and social lists grow with the panel; footer controls retain their
  authored bottom anchors. The inactive personal-notes page no longer paints
  over contracts. Personal journal/page-list editing is still unimplemented;
  this change does not claim those retained DAT tabs are functional.
- Keyboard options now edit up to three chords for the 28 implemented actions,
  with defaults, revert, cancel and persistent save/load. Gameplay input pauses
  during capture. The fixture uses an isolated INI and verifies disk readback.
  Video options control supported resolutions, display mode, quality, frame cap
  and VSync through `UGameUserSettings`. Tests verify the UI without changing the
  user's display configuration. Full retail action coverage, named key maps,
  every video option and exact retail control art remain unfinished.
- Runtime UI captures cover vendor tabs, spellbook/components, contracts,
  character options, keybindings, video, social tabs, character information,
  map, effects, vitae, link status, books, chess and assistance pages. Existing
  inventory/equipment, skills/titles and character-creation coverage is retained.
  These are offline fixtures, not proof that every live control and server
  response matches retail. In particular, book/chess/support pages and some
  social layouts still need full content and interaction comparison.
- Build version and configured window title both read `2026.09.08.3`.
  `Launch.bat` continues to build `ACUnrealEditor` before launching and aborts
  on compilation failure. Build evidence: `Saved/Logs/Build-2026.09.08.3.log`.
  Final regression evidence: `Saved/Automation/2026.09.08.3-Final/index.json`
  and `Saved/Logs/2026.09.08.3-Final.log`. The final Launch build succeeded in
  9.63 seconds, and the loaded DLL reported build `2026.09.08.3`. All 31 tests
  passed in 73.93 seconds: 29 clean and two with expected fixture warnings
  (unresolved effect-table and deliberately unsafe world-entry cases); zero
  failures or skipped tests. Final contracts and friends-page captures were
  visually checked after correcting page visibility and scrollbar placement.
  Live Khayyaban travel, crowded combat,
  translucent creatures/devices, face/eye appearance, sky movement and Arwic
  streaming remain user acceptance checks; no live account was accessed.

## Changes in 2026.09.08.2

- Dungeon portal arrival: the preserved `ACUnreal-user-20260908-DungeonPortal.log`
  shows destination `0179010D` streaming successfully, then holding at
  `spawn-placement` for 45 seconds before the one-shot lifestone recovery. This
  cell has no room collision triangles; its floor is static setup `02000898`
  (GfxObj `01001CD6`). Static objects were deferred until room visibility, and
  the portal-space branch activated only the destination's empty collision mesh.
- Prepare the same bounded collision neighborhood during portal space as during
  indoor movement, including static objects, while keeping rendering hidden.
  This follows retail `CTransition::build_cell_array/check_other_cells` and
  `CEnvCell::find_collisions` (environment plus static-object collision).
  Collision-required stabs also load when the room is outside the camera view.
  Readiness includes deferred static objects and collision-neighbor static work;
  newly created animated stabs inherit their room's hidden state.
- `ACE.RetailParity.WorldEntry` now loads the reported dungeon cell and its six
  direct portal neighbors from real DATs. The hidden-arrival assertion failed
  before the fix. It now drains the asynchronous floor setup, validates a clear
  capsule at `(100,-100,0)`, checks cooked neighbor collision and hidden geometry,
  and retains the previous Yaraq placement and recovery wire tests. The fixture
  position is the DAT cell origin; the old log did not record exact arrival XYZ.
  Portal hold diagnostics now include destination cell and position.
- A live server dungeon portal/relogin has not been exercised by automation.
  The user's account was not accessed for these tests.
- Validation: `Launch.bat --build-only` compiled successfully, runtime logs loaded
  build `2026.09.08.2`, and all 31 `ACE.RetailParity` tests passed (29 clean,
  two with expected negative-fixture warnings). Report:
  `Unreal/Saved/Automation/2026.09.08.2-Final/index.json`; build log:
  `Unreal/Saved/Logs/Build-2026.09.08.2.log`.

## Changes in 2026.09.08.1

- Pedestal entrance: the eye-to-player cell sweep could return a room that did
  not contain the camera after hitting the entrance stair wall. Check endpoint
  containment and adjacent rooms before choosing the outside landcell. This
  keeps the exterior terrain visible at the aperture while retaining indoor
  visibility clipping. The Yaraq entrance orbit fixture reproduced 43 wrong
  room assignments before the correction and none afterward.
- Inventory: server `ContainId` acknowledgements now own ordered membership.
  Removed speculative placement shifts that ran twice across request/ack;
  honor the old container until its ordered move arrives. Restore both item
  and pack order from PlayerDescription and bag ViewContents even when object
  creates arrive later or in reverse order. Slot drops/sort send dense retail
  list positions and retain server state on rejected moves. The loopback test
  decodes actual reliable outgoing actions and replays login/container data;
  a live account logout/login and database persistence have not been exercised.
- Particles: implement the missing A4R4G4B4 image decoder. Destroyed portal
  surface 080012FA is an authored black alpha shadow, not a white fallback
  card or additive glow. Preserve alpha blend flags independently of luminosity,
  use a two-sided translucent particle material with one opacity multiplication,
  and remove synthesized luma alpha from additive-only images.
- Particle orientation now follows the first GfxObjDegradeInfo draw mode,
  using the camera-to-particle heading where the retail draw mode requires it.
  Weeping Wand uses mode 2; the destroyed portal retains its authored frame.
  DAT dimensions, size variation and particle acceleration remain intact.
- Match `ParticleManager::CreateParticleEmitter` / `CreateBlockingParticleEmitter`:
  numbered slots replace immediately regardless of emitter-info DID, blocking
  creates leave occupied slots alone, and zero-ID creates allocate independently.
  This removes old buff particles that previously survived the next equipment
  enchantment. `ScriptManager` FIFO and delayed CallPES hooks preserve hook order;
  pending PlayScriptType, PlayScriptID and sound messages retain arrival order.
- Validation: `Launch.bat --build-only` succeeded and all 31 ACE.RetailParity tests
  passed on the final binary (29 clean, two with expected negative-fixture warnings).
  Runtime log confirms build 2026.09.08.1. Evidence:
  `Saved/Logs/Build-2026.09.08.1.log`,
  `Saved/Logs/RetailParity-2026.09.08.1-Final.log`, and
  `Saved/Automation/Build2026.09.08.1-Final/index.json`.
  Coverage includes actual DAT entrance/material checks, inventory wire/login
  replay with partial/reversed creates and root/side-bag refresh, real enchantment
  slot replacement, and mixed-opcode queue replay. Side-by-side live retail
  acceptance remains outstanding.

## Changes in 2026.09.07.10

- Verified server time against `NetworkSession.WriteOptionalHeaders` (TimeSync,
  `Timers.PortalYearTicks`), `GameTime::CalcDayBegin` / `UseTime`,
  `SkyDesc::CalcPresentDayGroup`, and the retail RegionDesc DAT. Weather is a
  deterministic DAT schedule selected by the server's date, with per-time
  opacity and lighting changes. It is not a separate server rain message.
  One Dereth day lasts 7,620 seconds (127 real minutes); servers with the same
  clock and DATs intentionally show the same weather.
- Preserved user-log evidence in `Saved/Logs/ACUnreal-user-Parity9-network-weather.log`: the .9 connection supplied
  `302951982.695954`, selecting day 39757, group 7 (`Rainy`), phase about .951.
  This group rains at noon too. Earlier logs include Sunny, Clear, and Cloudy
  groups and advancing phases. The captured rainy night is consistent with
  retail time; the available evidence does not show a globally frozen clock.
- Fixed actual lifecycle gaps: disconnect/failure clears the old clock and sky,
  a new connection waits for its own authenticated timestamp, and a valid
  zero epoch is distinct from unknown time. Character switches on one server
  and portal travel preserve the clock. Queued TimeSync packets retain their
  receipt timestamp so waiting for missing packets does not hold time back.
  Non-finite and negative samples are rejected. Day-number and time-of-day
  consumers now share the retail epoch calculation instead of duplicate code.
- Added `ACE.RetailParity.NetworkWeather`, using cleartext handshake and
  ISAAC-authenticated TimeSync datagrams through the real receiver, DAT meshes,
  sky Tick, world-light updates, and terrain-presenter weather gating. Covers
  same-day rain cessation, stars/daylight, forward/backward corrections,
  extrapolated midnight rollover, portal entry/exit, indoor/outdoor travel,
  disconnect/reconnect, character switching, delayed/reordered packets,
  invalid checksums/timestamps, and the user's exact captured clock.
  Expected sky-build diagnostics now use normal log severity.

Validation: `Launch.bat --build-only` succeeded and the loaded client DLL reported
build 2026.09.07.10. All 29 `ACE.RetailParity` tests passed in 76.15 seconds
(27 clean, 2 with existing fixture warnings, 0 failures). NetworkWeather passed
without warnings, including the exact reported timestamp. Evidence:
`Saved/Logs/Build-2026.09.07.10.log`,
`Saved/Logs/RetailParity-2026.09.07.10.log`, and
`Saved/Automation/Build2026.09.07.10/index.json`.
No new live account login was performed; server-side clock behavior was checked
from local protocol code and saved user logs, and client transitions were
exercised with offline packet fixtures.

## Changes in 2026.09.07.9

- Reproduced the reported collision failure with the actual Small Creepy Statue
  (`0200042B`) and Drawohan setup (`02000A0B`): a capsule resting on terrain hit
  the supporting floor at time zero, masking the obstruction later in a lateral
  sweep. The movement query now clears that bottom contact while retaining the
  full body radius and crown. It never ignores the floor component, which can
  also contain building walls. Exact ground contact, 0.1/1 cm penetration,
  joined floor/wall geometry, downward support, NPCs, and actual streamed Yaraq
  building PhysicsBSP are covered. Authored ethereal objects remain passable.
- Doorway spill now starts 100 cm inside the actual aperture and uses a bounded
  outward spotlight with dynamic shadows. Closed doors and walls can occlude
  it. Room walls retain hidden shadows and share its lighting channels. The
  separate light actor survives portal visibility changes; distance fading now
  spans 20 m before its 40 m cutoff. Uniform interior illumination remains.
  Offline render checks compare the actual Yaraq terrain with spill off/on,
  a procedural door blocking the aperture, and the room culled. A static-cube
  test fixture did not render reliably; the procedural fixture matches runtime
  doors. A negative-control run ruled out an additional renderer-bounds change;
  no such change is included.
- Compared `Particle::Init`/`Update`, `Frame::rotate`,
  `SmartBox::HandlePlayScriptType`, and the reported items' actual DAT scripts.
  Fixed local-to-world transform order, virtual part transforms, axis-angle
  rotation, axial-vector handedness, explosion direction normalization, and
  swarm/implode initial positions. Parent-local behavior follows each emitter's
  DAT flag. Removed the unsupported filter on portal disc gfx `01003DE3`.
  Weeping Wand `02000F1C` runs `33000DAA` with `3200056C` and all three `32000743`
  drip hooks; Destroyed Portal `020019E4` runs `3300126D`, including virtual
  part 6 and the `320009C2` spinning disc. Independent decoder output is saved
  in `Saved/Logs/Parity9Reported{Setups,Scripts,Emitters}.txt`.
- Network effects run on the addressed object's own physics-effect table.
  Removed implicit replay on a weapon's wielder or the local player, which
  could turn item messages into extra body buffs. Assault Orb `02000EC3` and
  explicit body-target controls are covered separately. Projectile lights are
  brighter (10-20 unitless intensity) and follow the missile head rather than
  the average of its old trail particles; bounded light counts are retained.
- F/the hand action now merges the selected owned item into compatible stacks
  sequentially and places survivors/remainder at the front of the main pack.
  It waits for both authoritative quantity updates before another merge;
  repeated input, timeout, character changes and trade state cannot launch a
  second stale merge. No stack quantities or object removals are predicted.
  Loopback packet tests cover partial/full merges across packs and final order,
  following `ACCWeenieObject::UIAttemptMerge` and `CM_Inventory` payloads.
- Paper doll now uses retail `gmPaperDollUI` heading 191.3679, the correct
  heritage camera, UI enum group 7 animation, and held frame 1. The camera is
  independent of character scale, and the preview actor is positioned after
  its root exists. The checkbox uses its DAT child rectangle, textures, text
  margins, and pressed/selected state above the model capture. Slot art,
  nine-region DAT silhouette selection, clothing/armor layering, drag/drop,
  shortcut overlays and wield/unwield packet paths are exercised by UIScreens.

Validation: `Launch.bat --build-only` succeeded for 2026.09.07.9, and the loaded
DLL reported that exact build stamp. `ACE.RetailParity` completed all 28 tests:
26 successes and two successes with warnings, zero failures/skips. The warnings
are intentional negative controls: unmapped item effects stay unresolved rather
than becoming body buffs, and unsafe spawn placements are rejected. Evidence:
`Saved/Logs/Build-2026.09.07.9.log`, `Saved/Logs/RetailParity-2026.09.07.9.log`,
and `Saved/Automation/Build2026.09.07.9/index.json`. The door fixture removed
added spill from 30,340 terrain pixels. These are isolated render/physics tests
and loopback protocol tests, not a live server login. In-game confirmation is still
needed for actual open/closed animated doors, moving fire/acid lighting, Assault
Orb equip's complete single-blue-effect sequence, and side-by-side timing/size
for all particle assets. Full one-to-one retail parity is not claimed. The prior
bench underside flicker remains unproven outside the diagnostic camera fixtures.

## Changes in 2026.09.07.8

- World-object appearance was using unlit materials while streamed outdoor
  scenery used DefaultLit. Opaque and masked world textures now use the same
  ambient, diffuse and shadow response as scenery. This includes the local pawn,
  remote players, NPCs and network props. Authored luminous/translucent effects
  and UI previews retain their separate material behavior. Occupied interior
  cells retain uniform illumination; returning outside restores current weather
  lighting. Per-appearance material parameters preserve script fades and palette
  textures without unsupported dynamic-material parents.
- Collision now reads Setup cylinders and spheres, previously skipped by the
  Unreal DAT parser. Retail `CPhysicsObj::FindObjCollisions` prefers actual part
  PhysicsBSP, then cylinders, then spheres; `CPartArray::CacheHasPhysicsBSP` checks
  the actual gfx BSP pointers. Empty procedural collision sections no longer
  disable a prop's fallback body. The Small Creepy Statue (`0200042B`) has no
  physics polygons but authors a cylinder at `(0.02,-0.1,0)`, radius `1.384`,
  height `4.198`. It now uses that body independently of its selection volume.
  Retail `CCylSphere` uses flat ends, represented here by a 64-sided convex
  cylinder (radial error below 0.13%), rather than a rounded capsule. PhysicsBSP
  props keep their authored polygon collision; server ethereal state still
  disables blocking. This does not claim analytic equivalence of Chaos contact
  resolution and retail's complete transition solver.
- Entering a town room previously disabled pawn blocking for every building
  shell. Only the occupied building now yields to its EnvCell collision. Other
  buildings, including those in neighboring streamed landblocks, remain solid.
- Preserve uniform lighting deeper indoors, blending the final 1.8m near each
  exterior aperture toward the entrance light. The blend is bounded by aperture
  width and height, with independent per-cell parameters. Doorway spill is now
  intensity 18, radius 425cm, with the existing two-light limit and distance
  culling. This transition and spill are the requested visual extension, not a
  claim that retail used dynamic doorway lights.

Evidence: `diag/InteriorAudit` independently decodes the statue, Pool
(`02000117`), Drawohan (`02000A0B`), and Yaraq building physics from the retail
DATs; output is `Saved/Logs/InteriorAudit-Parity8.log`. Runtime regression renders
isolate each model as a light/shadow receiver, compare cast shadows with identical
visible geometry, sweep both simple and complex queries against the statue, and
check actual wall traces and movement sweeps for all eight Yaraq buildings.
Rendered threshold comparisons confirm spatial blending while the day/night
interior test retains uniform brightness. The player's `.7` log is preserved as
`Saved/Logs/ACUnreal-user-Parity7-light-collision.log`.

Build: `Launch.bat --build-only`, recorded in `Saved/Logs/Build-Parity8.log`.
`Saved/Automation/Parity8Verified/index.json` records all 28 parity tests passing
(27 clean, one warning-bearing test). No gameplay/editor session was driven
against the live server for this pass; the user's exact nighttime camera views
still need in-game acceptance. Build header and displayed version are
`2026.09.07.8`. The earlier unresolved bench-underside flicker remains open.

## Changes in 2026.09.07.7

- Preserve the accepted uniform interior illumination. Doorway glow now uses
  explicit unitless falloff, intensity 8 and a 350cm radius; the former intensity
  1.25 / 250cm radius barely reached the terrain below the aperture centre.
  The actual Yaraq terrain regression failed on the old settings (9 visibly
  brightened pixels) and passes with the stronger, bounded light. There remain
  at most two lights per exterior-connected cell, culled at 20m, without shadow
  maps, specular highlights, volumetric light or translucent-particle lighting.
- Doorway lights have a separate visible actor owned by the cell. Hiding room
  geometry through PView must not extinguish a glow that remains visible outside.
  Cell reload, destruction and EndPlay explicitly release that actor. Regression
  coverage checks exterior illumination with the room hidden, and light cleanup.
- Improve nearby shadow resolution while retaining the common character/building
  shadow path and density. Four 4x cascades reserve approximately the first 7m
  for close detail while retaining the 600m range. The old three 2x cascades gave
  the first map roughly 86m. Module startup and engine configuration agree on
  four cascades. No color-depth, tone-mapping or indoor brightness change is made.
  Engine source: `FDirectionalLightSceneProxy::ComputeAccumulatedScale` /
  `GetSplitDistance`. A real DAT character's rendered shadow is compared with
  the old configuration on a neutral receiver, independently of texture colors.
- Bench flicker remains open: independently decoded the reported building's
  static placements and bench `02000368` / `01000EB3`; no duplicate placements
  were found. Retail `D3DPolyRender::ConstructMesh` emits both windings for the
  bench's CullMode.None seat, as does the current builder. Five underside views
  compare local and actual Yaraq coordinates; seven balcony camera views compare
  PView against all room geometry. The user clarified that only the underside
  texture flashes, while the legs remain visible. Its texture already has a full
  trilinear mip chain. An optional 24-view comparison with PixelDepthOffset removed
  changes only 0Ã¢â‚¬â€œ4 edge pixels; it does not reproduce the missing seat. An isolated
  120-engine-frame moving-camera sweep with temporal anti-aliasing also reports
  zero missing interior pixels. These fixtures have not reproduced the user's
  live in-building flicker, so this issue remains unresolved. No speculative mesh
  displacement, depth-offset removal or blanket two-sided-material change is made.
  Diagnostic switches for RuntimeActors: `-ACEBenchDepthProbe` and
  `-ACEBenchTemporalProbe`; reports are in `Saved/Automation/Parity7BenchDepth`
  and `Saved/Automation/Parity7BenchTemporal`.

Build and validation results are recorded in `Saved/Logs/Build-Parity7.log` and
`Saved/Automation/Parity7Verified/index.json`: all 28 parity tests pass (26 clean,
two with warnings). Warning-bearing tests include the engine's
`r.MotionVectorSimulation` thread-access warning and deliberate unsafe-spawn /
failed-recall fixtures; there are no failed tests. The subsequent optional bench
diagnostics also pass. Launch.bat builds the current
Editor target before launch and refuses to open stale binaries after a build
failure. Build header and project/displayed version are `2026.09.07.7`.

## Changes in 2026.09.07.6

### Reported gameplay, appearance, and interface regressions

- Tutorial spawn marker: the red arrow in cell `8C0401AD` is a DAT Stab
  (`02000C39` / `010028CA`), not a network object carrying HiddenAdmin. Its
  `11000118` degrade table gives the visible model a maximum distance of zero.
  `FACESetupMeshBuilder` now honors that authored no-draw range, preserving its
  physics data. Existing network visibility flags remain in force. Mesh format
  121 and disk schema 8 invalidate geometry caches containing the old marker.
  Source: retail `GfxObjDegradeInfo::get_max_degrade_distance` and the independent
  `diag/InteriorAudit` DAT decoder. This is not a model-ID or object-name blacklist.
- Character changes: restore a script's temporary fade materials before applying
  new appearance data. Clear part material overrides, script effects, and the
  reused pawn's appearance when returning to character selection. Otherwise the
  old character's saved material instances could replace the next character's
  textures after login. The regression exercises restoration followed by a new
  material assignment and effect cleanup.
- LFG logout message: map channel enter/leave codes `051B` and `051C` to their
  retail text and normal system chat. A channel departure is no longer shown as
  an unknown error. An incoming packet fixture checks the decoded text and channel.
- Skills: render the icon DID from each SkillTable entry, using the canvas DAT
  resolver even when the binder does not have a player controller. Keep the name
  to the right of the icon and preserve the existing base/buffed calculations.
- Equipment: use retail `gmPaperDollUI::CreateClickMap`'s `25000010` map at the
  authored `PaperDollDragMask` origin. Hidden slot boxes no longer intercept body
  clicks. The nine colored body regions select the upper equipped item using
  `InventoryPlacement` priorities, including the incoming clothing-priority field.
  Dropping clothing on the model accepts retail's body mask `08007FFF`; peripheral
  slots retain their own targets. Existing 24 empty-slot DIDs and coordinates were
  checked against layouts `21000023` and `21000037`. Slot selection and accept/reject
  feedback now use `06004D09`, `060011F9`, and `060011F8` respectively.
- Examination: retain the typed appraisal property tables instead of discarding
  fields. Player details now include allegiance/monarch/followers, body armor,
  damage ratings, arrival date, age and deaths when supplied. Item details include
  armor material resistance descriptions, workmanship, tinkering, spellcraft,
  mana, activation/wield requirements, defense/mana-conversion modifiers, uses,
  cooldown and crafter information. Mana conversion is a zero-based bonus, unlike
  defense multipliers. Source: `AppraisalSystem`, `ItemExamineUI`, and server
  `AppraiseInfo`. The UI uses the supplied data; absent fields are not fabricated.
- Interior illumination: room geometry and opaque/masked DAT furnishings use
  uniform full illumination independently of outdoor ambient and camera-cell
  transitions. This implements the requested fully lit presentation; it deliberately
  replaces the earlier 0.2 ambient approximation. A maximum of two short-range,
  unshadowed warm lights per outside-connected cell provides the requested doorway
  spill. Lights belong to the cell and follow its visibility/lifetime. Interior
  furniture material variants retain culling and live fog updates.
- Shadows: moving entities now use the same cascaded directional shadow projection
  as building geometry, with separate inset shadows disabled. This removes an extra
  shadow path that gave entities a different density. Receiver textures and ambient
  light can still make apparent darkness differ; a matched live comparison remains
  necessary.
- Portal exit: apply `gmSmartBoxUI`'s two projection-distance stages: stretch the
  tunnel for one second, switch to the destination world with a stretched projection,
  then restore the normal world projection over one second. Use the retail
  `GetAnimLevel` table rather than interpolating the FOV angle. Interrupted transitions
  and character changes clear the temporary projection. Existing early network
  completion and collision readiness handling are preserved.

Build and validation:

- `Launch.bat --build-only` compiled and linked successfully. Build header,
  project version and displayed title are `2026.09.07.6`; the updated DLL is in
  `Plugins/ACEClient/Binaries/Win64`. See `Saved/Logs/Build-Parity6.log`.
- `Saved/Automation/Parity6Verified/index.json`: all 28 tests passed, none skipped.
  The run recorded three intentional blocked-spawn/failed-recall warnings in
  `WorldEntry` and one invalid-parent warning in the new palette test fixture.
  That fixture now clones from the base material and copies uniform parameters.
  After the final rebuild, `Saved/Automation/Parity6RuntimeVerified/index.json`
  confirms `RuntimeActors` passes with no warnings or errors. Both test processes
  exited 0. No gameplay source changed after the successful full-suite run.
- The actual Yaraq room/furniture render remains stable when outside ambient
  changes from dark blue to warm daylight (mean RGB difference below 1/255 on
  visible room textures). Captures were visually inspected for uniform walls,
  stairs and ceilings. Detailed item/player text, skill icons and selected
  equipment-slot captures were also inspected.
- Engine startup still reports its editor-layout migration, missing non-Windows
  SDKs and `r.MotionVectorSimulation` console-variable warning. They are not
  suppressed or represented as resolved client issues.

Rendered fixtures include `GameplayCookingBuffed`, `GameplayPlayerInspection`,
`GameplayDetailedItemInspection`, `GameplayEquipmentSelected`,
`GameplayEquipmentModelTargets`, and `YaraqUniformInterior_Day/Night` under
`Saved/Automation/RetailParity`. The body-map test uses real canvas coordinates,
selects outer armor over an undershirt, drags it from the model, and validates the
outgoing equip packet through an isolated UDP socket. No live account is mutated.

Still requiring live acceptance: switching two real characters through logout,
creation into the tutorial dungeon, giving/equipping multilayer outfits, comparing
player/item examinations with retail on the same objects, night-time doorway spill,
entity/building shadow density, and portal arrival under network delay. The equipment
model's retail blinking part-selection illumination and the complete set of item-type
specific appraisal layouts are still checklist work. These fixes do not establish
complete retail parity for the entire client.

## Changes in 2026.09.07.5

### Character creation preview and name-entry regressions

- The creation canvas is focusable while its binder is attached. The earlier test
  invoked `KeyChar` directly and inspected the mouse reply, so it missed the actual
  non-focusable Slate widget that prevented typing. The new fixture focuses a real
  Slate window and routes character events through `FSlateApplication`.
- The examination sequence uses absolute retail part frames, without multiplying
  Setup placement rotations into them. Face view seeds the authored frame before
  pausing; clothes and summary use the mapped animation at 30 frames per second.
  Examination updates bypass gameplay-camera distance culling. The test now registers
  a gameplay camera and checks individual part transforms at several animation times.
- Bare preview actors now receive their location after their root component exists.
  Unchanged appearances retain their meshes and animation phase; hidden previews stop
  ticking. Scene capture retains rendering history between captures.
- Clothing controls, replacement models/textures, and subpalette ranges/shades were
  checked against the independent `ACE.DatLoader` decoder: 20,400 combinations across
  the 35 starter clothing tables agree. Swatch and shade mouse tests also verify the
  generated appearance reaches the displayed model, with rendered examples of both.
- Dedicated examination materials reproduce `CreatureMode`'s 0.3 ambient light and
  `gmCG3DView`'s intensity-2 distant light, clamped at vertices before texture modulation.
  The old preview light did not affect its unlit world materials. The preview no longer
  inherits world fog or filmic tone/color adjustments, and uses separate material instances.
- Template skylight real-time capture is disabled before waiting for world entry,
  addressing the warning displayed on the creation pages.
- DAT handles now permit the retail client's existing write handles while remaining
  read-only themselves. Both retained and temporary reads support concurrent clients;
  the previous sharing violation prevented the parity tests from loading retail assets.

Build and verification artifacts:

- `Saved/Logs/CreationPreviewFixBuild.log`: `Launch.bat --build-only` compile/link.
  Build header and project display title/version are all `2026.09.07.5`.
- `Saved/Automation/PreviewParityVerified/index.json`: all 28 retail regression tests
  passed (27 without warnings; `WorldEntry` emits three expected blocked-spawn/recall
  fixture warnings). No failed or skipped tests; the editor test process exited 0.
- `Saved/Automation/RetailParity/CreationClothesColor2.png` and
  `CreationClothesColor8.png`: rendered color-control examples.
- `diag/CreationAppearanceDump`: independent read-only DAT oracle and Python comparison
  against `Saved/Automation/CreationClothingActual.csv` emitted by the creation data test.
  The final run matched all 20,400 garment/setup/color/shade combinations.

The rendered clothes and Summary examples were visually inspected for assembled limbs
and visible palette changes. Engine startup still reports editor-layout migration and
uninstalled non-Windows SDKs; the suite also logs UE's `r.MotionVectorSimulation`
render-thread console-variable warning. These are not suppressed or counted as client
fixes. The creation fixture itself completed without warnings or errors.

Actual character creation/persistence on the intended server and a direct retail visual
comparison of every heritage remain live acceptance tasks. Local loopback tests do not
mutate a live account. Character deletion, restoration and credits remain checklist gaps;
this pass prioritized the reported creation regressions.

## Changes in 2026.09.07.4

### Character creation

The Create Character button now opens all six retail pages, backed by the original
`21000038` layout, `2100004C` control templates, DAT fonts, textures, portraits,
tooltips and confirmation/error dialogs. The model reads CharGenData `0E000002`,
SkillTable `0E000004`, language strings and DID mappers directly from retail DATs.

- All 13 heritages and both genders, including the Olthoi page restrictions.
- Profession templates and custom attributes, locks, borrowing unspent points,
  derived vitals, skill training/specialization, refunds, free heritage skills,
  and profession fitting after custom changes.
- Face strips, hair styles, bald eye textures, skin/hair/eye palettes, all four
  starting clothing slots, clothing color templates and shade sliders. Compatible
  appearance choices survive heritage/gender changes. Clothing composition follows
  the retail hat/trousers/shirt/footwear order.
- Authored heritage preview rooms, face rest pose, animated full-body preview,
  rotation and 0.6-second zoom; an isolated distant light uses the retail direction
  and intensity, with fixed exposure instead of adapting to clothing colors.
- Starting-town map and heritage restrictions; name editing, selection, dragging,
  clipboard, 32-character limit and retail formatting; complete scrollable summary,
  page-specific Random behavior, exit confirmation and unspent-attribute warning.
- Finish sends the edited draft through the authenticated reliable UI queue
  (`F656`). It retains the draft on rejection and handles `F643` success using the
  server-assigned identity before returning to character selection. Pending requests,
  duplicate Finish, disconnected transport, malformed replies and full accounts are
  guarded. The request never asks for admin/sentinel privileges.
- String16L now explicitly uses Windows-1252 bytes and byte-counted padding. The
  independent server decoder exposed its own UTF-8 `ReadChars` mismatch; its shared
  reader now consumes exactly the declared byte count and decodes the same code page.
  This does not expand the retail character-name input alphabet.

Implementation references: `gmCharGenMainUI`, the six `gmCG*Page` classes,
`gmCG3DView`, `CharGenState` (templates, constraints, randomization and fitting),
`ACCharGenData::FormatName`, `CPlayerSystem` character creation, and
`Source/ACE.Server/Network/Handlers/CharacterHandler.cs`.

Validation artifacts:

- `Unreal/Saved/Logs/CharacterCreationBuild.log`: successful compile/link through
  `Launch.bat --build-only`, the same build path used for user launches; both build
  stamp and ProjectVersion are `2026.09.07.4`.
- `Unreal/Saved/Automation/CharGenFinal/index.json`: creation data and GPU/UI tests,
  including real mouse routing, attributes/skills, appearance constraints, names,
  tooltips, Olthoi navigation, Finish over local UDP, rejection/retry and acceptance.
- `Unreal/diag/chargen-server-validation.txt`: all 26 heritage/gender wire fixtures
  accepted by the independent `ACE.Entity.CharacterCreateInfo` parser, with DAT
  validation of class, towns, attributes, skills, styles, palettes and hues.
- `Unreal/Saved/Automation/ServerCharGen/CharacterCreationStrings.trx`: two server
  String16L tests pass, including extended-byte boundaries and truncated input.
- `Unreal/Saved/Automation/RetailParity/CharacterCreation1.png` through `6.png`:
  rendered and visually inspected native pages at 1600x1200.
- The broader retail suite has 26 other successful results; WorldEntry includes
  expected warnings from the deliberately blocked-spawn and failed-recall fixtures.
  The initial creation-screen failure assumed random templates left skill credits;
  the test now chooses Custom explicitly. Both creation tests pass in the final
  focused rerun, including the added Finish-button network integration checks.

Live acceptance remains: create a character on the intended server, reconnect,
and verify the persisted appearance, stats, trained skills, starter equipment,
profession title and starting location in-world. No live account was mutated by
these tests. Retail's Keystone Help plugin is not hosted by Unreal; Help currently
shows the original DAT instructions in a native dialog. Preview lighting uses UE's
renderer and still needs a direct retail screenshot comparison. These limits prevent
claiming complete visual and live-server acceptance from automated tests alone.

### Character statistics and titles

The attribute/skill panels now distinguish invested ranks, computed base values and
buffed values, including attribute contributions, enchantment stacking, vitae and
augmentation modifiers. Live quality changes invalidate the displayed values and
raise costs use cumulative XP tables. The Titles tab uses the retail layout and
title list, supports sorting/selection, and sends the selected title to the server.
These changes have focused `CharacterStats` and native UI regression coverage;
the reported Cooking increase and live title change still need user acceptance.

## Changes in 2026.09.07.3

- `Launch.bat` / `OpenACUnreal.bat` now run an incremental ACUnrealEditor
  Win64 Development build before opening Unreal. Build errors stop the launch;
  the launcher no longer falls back to an older engine than the project's 5.8.
  `Launch.bat --build-only` validates the same path without opening the editor.
- Update the compiled build stamp and ProjectVersion together. The Play window
  title also shows 2026.09.07.3 so the test build is visible without opening logs.
- The user's launches at 06:48:34 and 06:53:05 UTC already loaded .2, as recorded
  by the compiled module in ACUnreal.log and its 06:52:44 backup. Those reports
  cannot be attributed to stale binaries; unresolved visual behavior remains
  subject to the acceptance items below.

Validation: `Launch.bat --build-only` compiled and linked successfully; a normal
`Launch.bat` run then reported the target up to date and opened the same project.
ACUnreal.log at 06:58:49 UTC confirms the loaded module's build stamp and the
engine ProjectVersion are both 2026.09.07.3; engine initialization completed.
The gameplay changes retain the .2 regression results below; .3 changes only
launch/build identification.

## Changes in 2026.09.07.2

- Restore server mesh visibility after object initialization and deferred DAT
  appearance creation. Collision/fallback setup previously re-enabled the mesh
  after `NoDraw` had been applied. Appearance restoration now respects NoDraw,
  Hidden, Cloaked, and HiddenAdmin at the part boundary. Effect-only setups retain
  no fallback body after physics updates. A cloak update cancels queued spawns;
  an uncloak update can recreate a previously removed actor without requiring
  another ObjectCreate. Reference: retail `CPhysicsObj::set_nodraw`,
  `CPhysicsPart::SetNoDraw`, and server `SerializePhysicsData`.
- StopParticle/DestroyParticle match the authored emitter slot, not the emitter
  resource ID or our runtime serial. Retail PlayScript 0x76 creates slots
  1000..1013; 0x75 stops those slots and fades the body in over 0.75 seconds.
  Arrival bubbles now stop emitting and drain at their DAT lifespans. Tests
  exercise both the body fade and eventual absence of all arrival emitters.
- Room draw meshes use the union of DrawingBSP InPolys instead of the polygon
  dictionary minus a guessed portal set. The dictionary includes unused blue and
  black connectivity faces. Conversely, Town Network ceiling caps 000701A6/A7
  are portal polygons that also occur in InPolys and must render. Compare retail
  `ACRenderDevice::DrawEnvCell` / `BSPTREE::draw_check`. EnvCell cache schema is 15;
  independent ACE.DatLoader fixtures cover 4,255 cells and 159 GfxObjs.
- Preserve authored dimensions for ordinary particle geometry rather than
  normalizing small cubic meshes. Sample scale and translucency endpoint jitter
  with the clamps in `ParticleEmitterInfo::GetRandom*`. The Weeping Wand uses
  a flat authored quad; its reported apparent size still needs live comparison,
  and no wand-specific size multiplier has been introduced.

Validation: ACUnrealEditor Win64 Development compiled successfully. Offscreen
GPU suite `ACE.RetailParity` completed all 25 groups (24 clean, one with three
intentional blocked-spawn warnings, zero errors). Report:
`Saved/Automation/RetailParity20260907-visibility2/index.json`.
This does not establish live visual acceptance for the reported admin markers,
interior lighting, doorway transitions, or the remaining movement/UI issues.

## Changes in 2026.09.07.1

- Pedestal Weak Spot (weenie 16919, Setup 02000D55) uses motion table 090000F9,
  not the Setup's empty default motion table. Its 03000919 transition plays at
  +30/-30 FPS and runs directional TransparentPart hooks on part 0: 0 -> 1 in
  0.26666668 seconds when opening, and 1 -> 0 when closing. Retail
  `TransparentPartHook::Execute` delegates this to the physics part translucency
  transition. Our script component could sleep before receiving that hook;
  adding a scalar tween did not wake it. Start the authored fade and enable
  ticking when the hook arrives. Remove the pedestal-specific closed bind-pose
  workaround. Apply settled visual hook endpoints for already-open ObjectCreate
  messages without replaying sounds or particle bursts. Preserve server reset
  timing; the client does not invent an automatic close timer.
- Retain materials replaced by temporary dissolve materials through Unreal GC.
  Convert only the affected part for TransparentPart, and restore its original
  lighting/blend material when it becomes opaque again. This also restores solid
  shadow behavior after opening/closing or leaving portal space.
- Dungeon residency now includes the player's current landblock even when the
  previous outdoor keep ring remains cached. Preserve remote players and items
  in that dungeon, release the outdoor ring in pure dungeon landblocks, and avoid
  reloading the same cells at every internal cell transition. Destroy separately
  spawned scenery children when their landblock, terrain chunk or room is
  destroyed. This fixes a concrete route for birds and other scenery to accumulate
  after leaving and returning. No live Viridian Rise frame-time improvement is
  claimed without measuring the same traversal.
- Cell connectivity is not equivalent to a draw aperture. Preserve ordinary
  CellPortal polygons unless DrawingBSP identifies a PORT aperture or the portal
  leads outside. Town Network 000701A6/000701A7 otherwise lost their sole ceiling
  polygon. Keep the Yaraq outside blue 080006FD faces excluded. EnvCell cache
  schema is 14; independent ACE.DatLoader fixtures now include 4,255 cells and
  159 GfxObjs. Remove the extra 80-meter building aperture cutoff: the resident
  full-detail ring, portal side and clipped view already bound this work.
- Remote movement no longer discards elapsed time beyond 0.05 seconds per frame.
  Position prediction consumes the whole interval (including acceleration), so
  a 10 Hz renderer covers the same distance as 60 Hz. Nonmissile objects carrying
  velocity now use the ordinary appearance creation budget; velocity alone does
  not identify a projectile. Zero-framerate DAT motion segments hold their authored
  frame rather than falling back to 30 FPS, including ATOYOT frame 19.
- Apply ObjDesc biology subpalettes to matching original surface/texture palettes,
  following `CPartArray::SetPalette`, without suppressing them based on a guessed
  headwear count. Preserve the final texture remap's precedence. Indexed human
  head pixels are compared with real palette 0400008E. This is fixture evidence;
  all heritage/headwear combinations still require live visual acceptance.
- Remove Weeping Wand size/birth-rate overrides and fountain/torch/Still-emitter
  lifetime substitutions; use authored emitter scale, timing and motion. Preserve
  zero initial scale, and apply the missing Explode Z acceleration. Hidden/UnHide
  physics effects drive the portal bubble emitters, with body transparency
  independent of the particles' opacity. Blood retains its no-light behavior.
- Apply server NoDraw/Hidden to networked bodies regardless of item type, while
  allowing their portal particles to render. Item/admin marker visibility still
  depends on the actual flags sent by the server; all special marker classes have
  not been inspected live.
- Probe world-entry floors with the full character radius instead of an 8 cm
  sample. Sloped teleports at 15/30/45 degrees can now find a valid capsule position
  at the requested XY. Evaluate placement before waiting indefinitely for server
  unhide. Compute appearance foot offsets in root-local space to avoid applying
  pawn scale twice.
- Honor DAT minimum/maximum control dimensions and separate the jump meter fill
  from its text layer. Show the authored Height label and respect the 810-pixel
  maximum bar width. World drops use viewport pixels for picking, fixing the DPI
  coordinate mismatch in player/NPC give targets. Live player-to-player giving
  still requires acceptance with a second account.
- Move successful build stamps and portal/sky/head diagnostics to Log/Verbose.
  Keep actual failures and unsafe-spawn warnings visible. The user's latest
  preserved session, `Saved/Logs/ACUnreal-user-20260907-dungeon-streaming.log`,
  ends with normal shutdown; it is not evidence of a new crash stack.

Pedestal reference data is saved as `Tests/Fixtures/PedestalWeakSpot.sql`, from
[ACE World weenie 16919](https://github.com/ACEmulator/ACE-World-16PY/blob/master/Database/3-Core/9%20WeenieDefaults/SQL/Door/Misc/16919%20Pedestal%20Weak%20Spot.sql).
This is reference data only; no database migration was run. Reproduce the motion
inspection with `dotnet run --project Unreal/diag/UIInventory -- --pedestal-probe
"C:/Turbine/Asheron's Call"`. The primary animation values come from the local DAT.

Validation: ACUnrealEditor Win64 Development compiled successfully. Build
**2026.09.07.1** passed all **25 D3D12 offscreen groups**: 24 clean, one with
intentional unsafe-spawn warnings, zero failed/skipped/unfinished. The final
RuntimeActors test forces GC while the pedestal is faded out, verifies the
replaced material remains alive, and confirms the identical original material
is restored on each close. The run also covers already-open creation, actual
10/60 Hz movement, distant building admission, 130 Yaraq rendered views, dungeon
ceilings/residency, palette pixels, held emote frames, particles and jump UI.
Report: `Saved/Automation/RetailParity20260907-dungeon5-final/index.json`.
Log: `Saved/Logs/RetailParity20260907-dungeon5-final.log`. No NaN-bounds,
nearly-zero normals/tangents, or degenerate tangent warnings appeared. Remaining
startup warnings come from the engine layout service and third-party MCP plugin.
Reviewed capture: `Saved/Automation/RetailParity/GameplayJumpCharge.png`.

Live acceptance still required for this build: repeated pedestal use/reset and
re-entry; dungeon arrival with another player and loot; remote hair/eyes and held
emotes; player giving; sloped admin teleports and bubble exit; cold and repeated
Viridian Rise/Yaraq traversal with frame-time, actor-count and memory measurements.
The larger account/gameplay/interface checklist below remains open.

## Changes in 2026.09.06.8

- Investigated the actual inventory crash in
  `Saved/Crashes/UECC-Windows-1E429759433EF2919573C4884C46441A_0000`.
  Its stack is `RefreshSpellHotbarOverlays` -> `CollapseSpellBarWidgets`.
  `BuiltInSpellIconBorders` lacked a reflected reference: removed or unplaced
  widgets could be garbage-collected while still cached. Retain that array with
  `UPROPERTY(Transient)`. The regression unparents the cached widgets, forces GC,
  and refreshes the spell bar. Summoned-creature kills were concurrent with the
  user's crash; the stack does not implicate the creature death code.
- Camera-near doorway visibility: portal traversal must start at the eye, even
  though rendered triangles clip at the render near plane. Moving the camera to
  2cm/32cm inside the reported Yaraq portals reproduced missing room/stair pixels
  with the previous 10cm portal near-plane clipping. Move only the root traversal
  near plane to the eye; retain side, far and recursive aperture planes. Compare
  these views with the same all-resident geometry, including exterior terrain.
- The blue entrance had a second source in EnvCells 7E650100..106, environment
  0D000405: their CellPortal polygon IDs also referenced opaque blue 080006FD.
  Exclude these aperture polygons from EnvCell draw sections, just as retail
  `CEnvCell`/`BSPPORTAL` draws them through PView. Keep physics, portal vertices,
  and cell bounds. Bump EnvCell disk-cache schema to 12 and regenerate independent
  ACE.DatLoader drawable-side fixtures. Both shell and cell blue planes are tested.
- Runtime static meshes were being rebuilt by Unreal's editor once per material:
  post-build `SetMaterial` invokes `PostEditChange`, despite those same materials
  already being assigned before the fast build. Remove that redundant loop.
  This also removes the path producing NaN bounds and tangent warnings, and avoids
  repeated generation work. Verify finite bounds, preserved physics/materials,
  and `IsCompiling()==false` on completed runtime meshes. Cold live traversal FPS
  has not been measured by this change.
- Inventory shortcuts now match `gmToolbarUI::CreateShortcutToItem` and the
  inventory drop branch: remove an item's previous binding, assign the new slot,
  then put a displaced item into the first free slot to its right (with wrap).
  Support side packs, items inside them, and the main backpack/player shortcut;
  activating the latter opens inventory locally. Paint the same DAT numeral on
  occupied toolbar, inventory, equipped and pack icons. Native press/move/release
  coverage includes a refresh during drag and the user's larger viewport size.
- Resizing a window exposed stale inventory overlays: their cache included size
  but not the window's position. The chrome moved while item icons and click
  geometry stayed behind. Include viewport/scale and root-window geometry in
  inventory, vendor and loot caches.
- Mana/item targeting uses `ClientUISystem::UpdateCursorState` and DidMapper
  2500000F entries 39/40/41 (06004D73, 06005E6B, 06005E6A), with the authored 14,14
  hotspot. Match `ItemHolder::IsTargetCompatibleWithTargetingObject` for the cursor
  preview, including ownership, player-wide use and trade-offered exclusions.
  Give the software cursor an explicit Slate desired size so its drawn hotspot
  matches the pointer. The server remains authoritative about actual item use.
- Inventory paperdoll uses `gmPaperDollUI::PostInit`, `UpdateHeritage`,
  `RedressCreature`, and `Render::SetFOVInternal`: (.12,-2.4,.88) meters before
  coordinate conversion, 191.3679-degree creature heading, 45-degree vertical FOV,
  and heritage-specific camera distances/heights. Rotate the creature rather than
  its attached camera; invalidate the portrait when heritage changes. Render a real
  human DAT model through the live inventory rig to check geometry and framing.
  Full equipped-character lighting and all heritage appearances still need live
  side-by-side acceptance.
- Optional mouse turning now uses captured relative deltas immediately instead
  of dropping short movements until five consecutive nonzero samples. Preserve
  legacy movement when that option is off; distinguish orbit drags from right-click
  item use by accumulated pixel travel. Unit coverage exercises the actual camera
  delta path, pauses, short movements and pitch limits. Physical mouse capture and
  follow behavior still require live gameplay verification.
- Add the ACEWorldBake -> ACEClient plugin dependency declared by its module.
  Move routine successful loading, sky/particle telemetry and source-authored empty
  physics messages to Log/Verbose. Keep actual missing assets/build failures as
  warnings; do not suppress engine or third-party warnings globally.

Validation: ACUnrealEditor Win64 Development compiled successfully. The final
D3D12 offscreen run loaded build **2026.09.06.8** and passed all **25 groups**:
22 clean, three with warnings, zero failed/skipped/unfinished. Evidence:
`Saved/Automation/RetailParity20260907-final/index.json` and
`Saved/Logs/RetailParity20260907-final.log`. There were no NaN-bounds,
nearly-zero tangent/normal, or degenerate tangent warnings. The remaining test
warnings are portal-camera/sky diagnostics and intentionally failed world-entry
recovery scenarios; third-party plugin and editor-layout notices occur at startup.

The larger-viewport native backpack test failed before the overlay cache correction
(`RetailParity20260907g`) and passed afterward (`RetailParity20260907h`), including
main-pack activation and displaced bindings. The portal-near reference renders
failed before the frustum correction (`RetailParity20260907b`) and passed afterward.
The preserved user crash log is `Saved/Logs/ACUnreal-user-20260906-inventory-crash.log`.

Ready to try through `Unreal/Launch.bat`. Live acceptance still needs the user's
inventory/summon sequence, physical mouse capture/follow behavior, repeated
entrance traversal, equipped portraits and cold landscape frame times. No live
account was logged in during this verification. The complete client acceptance
checklist remains open; these fixes do not certify every retail feature.

## Changes in 2026.09.06.7

- Doorway and stairwell camera classification: use the actual player view origin
  for both PView and viewer-cell resolution. Classify the collision-resolved camera
  endpoint in resident cell BSPs before attempting the feet-to-camera path. That
  second path could hit a stair/floor and select the wrong room. Added the reported
  shop's 7E650100..10A cells and adjacent player/camera combinations: eleven of
  those combinations failed before this fix (`RetailParity20260906w-camera`).
  The scene fixture now compares 76 room views with all resident geometry.
- Blue doorway: GfxObj 010014C3 uses opaque blue surface 080006FD for a PORT
  polygon. Retail `CGfxObj::InitLoad`, `BSPPORTAL::portal_draw_portals_only` and
  `PView::DrawPortal` replace that plane with the portal view. Keep the aperture
  geometry for visibility traversal but remove PORT polygons from ordinary shell
  draw sections. Physics polygons are unchanged. The independent ACE.DatLoader
  fixture exporter now separately counts PORT apertures and drawable sides;
  the pedestal retains its two textured surfaces and black PORT aperture.
- Lighting: `SmartBox::SetWorldAmbientLight` uses raw RGB coefficients, and
  `LScape::calc_object_light` includes 0.2 times sunlight magnitude. Apply those
  values to cached and newly created shell/scenery materials, eliminating the
  previous shell ambient mismatch between creation and updates. `CellManager`
  supplies constant 0.2 white ambient to sealed cells; outdoor-visible rooms use
  the current outdoor ambient. Avoid resending unchanged ambient material values.
  Creature/player shadow casting is no longer explicitly disabled on entering an
  EnvCell. UE directional shadow maps remain an approximation, not certified
  equivalence to every retail shadow/lighting case.
- Movement/animation: retain MoveTo flags, walk/run threshold, speed and run rate.
  `MovementParameters::get_command` controls the gait; a large run multiplier
  does not turn a walk-only path into running. Read authored motion-cycle velocity
  for each model and clamp the final approach step. Cache motion tables by DID
  and use cached animation references instead of copying all frames per sample.
  StreamingCost exercises table alternation and walk/run transitions; a cold
  landscape walk still requires live frame-time acceptance.
- Native UI input: honor DAT panel-page tab IDs and Menu hit targets, and route
  Slate's distinct double-click event through inventory activation. Actual canvas
  press/release tests cover Say, vendor tabs/filter, mana stones and trade controls.
  Trade now displays names, totals, item backgrounds/counts, scrollable horizontal
  offers, acceptance state and the authored `Trade_ClearAllButon` action. Preserve
  DAT dimensions and scale overlay positions with the canvas.
- Inventory: restore the held caster's innate spell and caster icon to BuiltInSpell;
  draw bound shortcut numerals on equipped slots and side packs as well as grid
  items. Equipment slot coordinates and empty icons were checked against the
  resolved DAT templates. Equipped-slot tool drops target that item; the blank
  doll targets the player. Double-clicking a mana stone arms targeting without
  discharging it onto self. `ItemHolder::TargetAcquired` blocks draining Retained
  items; an empty stone requires the retail Yes/No template and destruction prompt
  before sending UseWithTarget. Dialog height follows the DAT glyph layout.
- Optional mouse-turning controls: stationary RMB orbits; movement turns the body
  smoothly toward the view while keeping camera heading continuous. RMB+LMB runs;
  A/D strafe in this mode. This is the user's requested control behavior, not a
  claim about original retail input. Unsupported ledges use support-boundary
  projection instead of alternating sideways nudges. CameraAndEdges checks frame
  rate independence and repeated stable edge sliding.

Preserved user log: `Saved/Logs/ACUnreal-user-20260906-ui-lighting.log`.
The revised UI, world-DAT and scene groups passed in `RetailParity20260906z`;
build 2026.09.06.7 compiles as ACUnrealEditor Win64 Development. Final D3D12
offscreen validation passed all 25 groups: 14 clean, eleven with warnings, zero
failed/skipped/unfinished (`Saved/Automation/RetailParity20260906aa-final/index.json`,
`Saved/Logs/RetailParity20260906aa-final.log`). That process loaded build .7 and
verified 76 Yaraq views, native mana confirmation text, trade actions, indoor
creature shadow retention and sealed/outdoor ambient transitions. Existing engine
transient mesh-bounds/material diagnostics and intentional recovery warnings remain.
Use `Unreal/Launch.bat` to try it.
These checks do not certify all inventory, trade, vendor, animation, lighting,
world traversal or networking behavior as a complete retail replacement.

## Changes in 2026.09.06.6

- Traversal investigation: preserved the user's session as
  `Saved/Logs/ACUnreal-user-20260906-traversal-weather.log`. Initial appearance
  application coincides with long frame gaps; warmer models cost less. The mesh
  builder decoded the same appearance-dependent texture and palette for every
  polygon side. Retail `D3DPolyRender::ConstructMesh` shares surfaces between
  subsets. Resolve once per surface in each part, retaining authored geometry,
  UVs, vertex colors and distinct front/back sides. The actual DAT human fixture
  now makes 23 surface resolves instead of 383 for 409 triangles. The isolated
  cold build fell from 42.85 ms to 2.80 ms; warm from 43.88 ms to 2.51 ms. Baseline:
  `RetailParity20260906k-baseline`; fixed: `RetailParity20260906l` in Saved/Automation.
  These numbers measure CPU mesh preparation, not total frame time.
- Loading cadence: the player's landblock now charges the same shared 4 ms
  scenery budget as its neighbors; failed/pending attempts also stop at the
  deadline. Ready terrain application stops starting more blocks once 4 ms have
  elapsed, preventing the old multiple-block burst on one frame. Retail
  `LScape::PreFetchCells` and `update_block` use resident cached blocks; retain
  the existing asynchronous DAT builds and residency ring. Added slow appearance
  stage timings (`ACEAppearance cost`) to distinguish decode/component work from
  mesh/material application in subsequent live traces. Individual synchronous
  texture uploads, collision work and changes to terrain LOD can still overrun a
  budget; cold long-walk frame-time acceptance remains open.
- Shadows: UE 5.8 `FScene` constructs `PrimitiveOctree` with
  `UE_OLD_HALF_WORLD_MAX` (about 10.5 km). AC positions extend to about 49 km;
  Yaraq was outside its shadow-culling root. A control scene cast shadows near
  the origin but failed at its real coordinates. Set
  `r.Shadow.UseOctreeForCulling=0` in startup and config to use Unreal's alternate
  primitive gather with frustum/distance checks, retaining AC coordinates.
  Actual DAT shells now cast shadows at Yaraq across all 232 keys in 20 day
  groups; instanced foliage 02000322 passes independently. This Unreal culling
  correction is not a claim that CSM reproduces retail's entire shadow renderer.
  Corrected the earlier shadow test: unsigned pixel changes and suppressed shell
  color could pass from noise/depth artifacts. It now keeps identical geometry,
  requires net darkening, and separately isolates building receivers.
- Rain restart: environment `StopAllEffects` was restoring the sky actor's mesh
  materials, cloning the MIDs still referenced by the sky animation slots.
  Streaks returned on exit but UV updates went to stale materials. Weather owns
  its particle visuals, so stopping it no longer modifies the sky host materials.
  GPU checks now verify visible, animated rain after entering and leaving a room.
- Fog: follow `RenderDeviceD3D::SetFFFogProperties` and its linear range fog.
  Remove the invented 0.65 power curve and unconditional 900 m cap; honor
  interpolated DAT start/end, including zero start. Update cached outdoor lit
  scenery as well as terrain and shells. Outdoor fog remains active when seen
  from indoors; EnvCell materials use their own indoor state. Tests cover cached
  and newly created materials across weather/indoor changes. Per-cell fog and
  exact retail sky/depth compositing still need broader visual acceptance.
- Particles: follow `ParticleEmitter::UpdateParticles/RecordParticleEmission` and
  `ParticleEmitterInfo::ShouldEmitParticle`: update existing particles, then emit
  at most once and reset the emission time to now. Remove the 30 Hz minimum birth
  interval and hitch catch-up bursts; newborns retain age zero for their birth
  frame. Update each visual's transform once instead of three separate updates.
  The DAT wand fixture 320005A8 tests birth timing under a 200 ms hitch. Blood
  stays non-luminous; spell/buff/weapon effects retain lights. The decompile omits
  the x87 comparison in distance-based emission; that path, light strengths,
  long-hitch clocks and all particle equations are not yet certified 1:1.

Build 2026.09.06.6 compiles as ACUnrealEditor Win64 Development. Full D3D12
offscreen validation passed all 24 groups: 14 clean, ten with warnings, zero
failed/skipped/unfinished (`Saved/Automation/RetailParity20260906r/index.json`,
`Saved/Logs/RetailParity20260906r.log`). A subsequent test-only correction waits
for the foliage shader before measuring leaf cutouts; RuntimeActors passed again
in `RetailParity20260906s`. Isolated building receiver darkening was 3,595,893 RGB
units and textured foliage shadow darkening 714,757; capture pairs are under
`Saved/Automation/RetailParity/YaraqBuildingReceivers_*` and `YaraqFoliage_*`.
The final suite measured human preparation at 3.08 ms cold / 2.70 ms warm.
Existing engine mesh bounds/material warnings and deliberately exercised failure
paths remain. No live account or live traversal FPS test was used. Launch with
`Unreal/Launch.bat`; live cold-cache movement remains an acceptance check.

## Changes in 2026.09.06.5

- Indoor login deadlock: the preserved log
  `Saved/Logs/ACUnreal-user-20260906-blocked-entry.log` shows cell 7D630112
  loaded, followed by more than 76 seconds waiting on `cell-collision`. Portal
  visibility returned before collision activation; readiness waited for that
  activation before releasing portal space. Destination physics now prepares
  while its rendering remains hidden. WorldEntry tests the actual DAT room at
  local (36.71, 89.43, 9.20), through the presenter's portal visibility path.
- Safe world entry: check walkable support and a non-overlapping player capsule
  before arrival/reveal and again at the camera cut. Try nearby supported
  placement within four AC units; retain the validated position through reveal.
  Collision/placement failure after 45 seconds requests one server TeleToLifestone
  (0x63), using the character's server-owned Sanctuary. This requested recovery
  behavior is an extension; it is not attributed to retail. Existing server recall
  eligibility, animation delay and mana cost apply. A new teleport destination
  resets readiness and LoginComplete state without allowing another recall. If
  no destination arrives within 45 seconds, or the lifestone also cannot load a
  safe placement, disconnect and explain the failure on login rather than reveal
  missing terrain or loop forever. WorldEntry verifies the actual loopback packet,
  one-attempt limit, destination reset, missing floors, obstacles and both failure
  paths. Live server acceptance of recall is still a manual check.
- Portal movement: match `gmSmartBoxUI::PostInit/UseTime`,
  `UIGlobals::Init/GetAnimLevel` and `CreatureMode::SetCameraDirection_Degrees`.
  Use the 100-entry quantized sine-integral easing table, direct numeric angle
  interpolation, eye (0.24, -2.7, 0.88), and AC-to-Unreal roll handedness. Keep DAT
  animation 030005AC, start frame 1 at 40 fps, interpolating display frames. One
  owner advances the appearance clock across repeated portal visits. PortalSpace
  verifies curve samples, eye, roll, rate, repeated visits and rendered captures.
- Animation stepping: a .04 transform tolerance was also used for quaternion
  components, discarding several degrees of rotation. Translation retains its
  .04 cm tolerance; rotation and scale have separate .00001 tolerances. The
  portal regression now observes 120 distinct poses in 120 simulated display
  frames. This also removes that quantization from creature part animations.
- Loading cadence: active editor play clients opt out of Unreal's 3 fps background
  throttle via a delegate scoped to their lifetime, without editing global user
  preferences. Portal scenery uses the normal shared 4 ms budget and includes the
  player landblock in that budget; rooms use 2 ms and distant chunks one per call.
  Appearance work has a 2 ms/two-object portal cap and a 4 ms normal cap even when
  boosted. Individual synchronous asset builds/collision cooks may exceed a budget;
  live cold-load and long-walk frame times still require measurement. Portal
  illumination and complete rendered comparison against a running retail client
  remain open; the camera/animation tests do not establish complete portal parity.

Final validation: ACUnrealEditor Win64 Development rebuilt successfully as
2026.09.06.5. The complete D3D12 offscreen suite passed all 22 groups: 13 clean,
nine with warnings, zero failures, skipped or unfinished tests. Final report:
`Saved/Automation/RetailParity20260906j/index.json`; log:
`Saved/Logs/RetailParity20260906j.log`. Existing engine mesh/material warnings
remain, and the recovery tests deliberately exercise logged failure paths.
Portal captures are `Saved/Automation/RetailParity/PortalSpace0.png` through
`PortalSpace2.png`. The exact user destination resolves to UE capsule center
(-2403671.094, 1909742.969, 1008.500), retaining the original XY and supported feet.
The final teleport check also keeps autonomous position reporting disabled until
the validated destination is revealed. No live account was used in the tests.
Use `Unreal/Launch.bat` for the rebuilt editor client. Verify real lifestone recall
and cold-cache landscape traversal in play; complete retail parity remains open.

## Changes in 2026.09.06.4

Final validation: ACUnrealEditor Win64 Development compiled successfully. All
20 groups passed with D3D12 offscreen rendering (13 clean, seven with warnings,
zero failed/skipped) in `Saved/Automation/RetailParity20260906f/index.json`;
log `Saved/Logs/RetailParity20260906f.log`. Warnings include transient static-mesh
bounds during engine serialization and material/cache diagnostics. The completed
mesh bounds and collision assertions pass; those engine warnings remain open.
`Unreal/Launch.bat` opens the rebuilt editor project for manual testing.

- Building-entry crash: the actual .3 minidump resolves to interior ISM instance
  insertion after failed body initialization. DAT decorations without PhysicsBSP
  now keep physics disabled in both interior and outdoor ISMs; solids retain
  collision. See [crash evidence](CRASH_20260906_BUILD3.md). A new regression
  repeatedly enters the three crash rooms and adds their real DAT furniture.
- Streaming work: remove full ISM physics recreation after every instance;
  combine room collision triangles before a single synchronous cook; update
  portal stencil render state only when it changes. Remove the landscape
  presenter's competing cache trim (192 setups), leaving the DAT subsystem's
  existing centralized budgets (2,048 setups). These eliminate identified
  repeated work; live long-walk frame times still need measurement.
- Setup lifetime: the first full .4 run exposed an array-iteration failure in
  building mesh creation when background completions changed the cache during
  material work. Setup cache entries now have stable shared ownership. Static,
  procedural, portal-space and sky builders retain their geometry while doing
  work that may pump game-thread tasks. Late duplicate builds cannot replace a
  completed setup; stale generations cannot consume a new pending request.
- Effect lighting: exclude the twelve directional blood splatter PlayScript IDs
  `0x5B..0x66` in both particle-light selection and SetLight hooks. Remove the
  color heuristic and blanket player-owner exclusion, retaining lights for red
  spells, buffs, weapon and wand effects on players and other actors. Source:
  retail `PScriptType` and server `PlayScript`. Particle brightness inferred from
  art remains an approximation and requires further retail comparison.
- Building shadows: shell shadow passes use two-sided casting for thin DAT
  polygons; main-pass material sidedness stays authored. The original scene test
  was insufficient evidence of working shadows. See .6 for its correction and
  the actual world-coordinate shadow-culling fault.
- Creature inspection: use the nine `2100006B/10000166` attribute rows in retail
  order, their fonts/margins/alignment, health percentage format, unknown values,
  and appraisal highlight/color masks decoded from the server's ushort fields.
  Creature types resolve through `25000001 -> 2200000E`; enum underscores become
  spaces as `AppraisalSystem::InqCreatureDisplayName` does. Restore the Character
  and Level labels using the English DAT strings. This is the basic creature
  pane, not complete player/allegiance or extra-rating inspection.
- Item inspection: retain value/burden presence instead of showing unknown as
  zero. Insert those fields into the body when optional controls are absent,
  following `ItemExamineUI::SetAppraiseInfo`. Preserve weapon/armor lines following
  spell lists. Updating the same appraisal retains its scroll widget/offset;
  a new target starts at the top. Actual scroll-box ancestors clip the bitmap
  text; a DAT clip projected from moving child geometry no longer erases the
  lower part of scrolled descriptions.

## Changes in 2026.09.06.3

- Window paint order: generated text, inventory items, chat, sliders, radar, and
  other overlays follow their owning gameplay window when it is raised. Cached
  content also moves with the window. DAT chrome and content share an ordering
  band; popups and drag images have their own top layer. Canvas Z values remain
  within exact single-precision integer range, as Slate stores them as floats.
  Basis: `UIRegion::DrawChildren` and the retail element hierarchy.
- Input order: a foreground window consumes clicks on its empty body. Covered
  inventory cells cannot be selected through either widget hit testing or the
  coordinate fallback. Covered sliders, paperdoll controls and item drop targets
  reject the hit. Unsupported drops over a window do not target the world behind
  it. Loot double-click activation waits for release, allowing select-then-drag;
  returning a drag to its source does not arm a new double-click.
- Say menu: uses classic_chat 21000006, popup 1000001C and entry 1000001E, including
  191x17 cells, two columns, bitmap font 40000001, text margins and DAT state art.
  Menu insertion follows `gmMainChatUI::InitTalkFocusMenu`: squelch selected,
  monarch, selected, patron, Say, vassals, fellows, allegiance, General, Trade,
  LFG, Roleplay, Society, Olthoi. Selected-player tell/squelch are wired and
  disabled without an eligible selection. Membership/listening-dependent state
  and all localization strings still require complete parity validation.
- Inventory activation: owned-object decisions now use retail's combat-use,
  WieldOnUse/WieldLeft flags, equipment masks and salvage ItemType. Charges no
  longer override targeted use or block advertised wielding. Explicit No and
  wield-only useability are respected; owned container shortcuts open their
  contents locally. Ordinary activation of equipped items retains the retail
  PlaceInBackpack branch. Source: `ItemHolder::DetermineUseResult/UseObject`,
  `ItemUses`, and `CPlayerSystem::UsingItem`. UIScreens checks actual constructed
  Use/Wield packets through an isolated loopback socket.
- Transport: incoming fragment data is held locally until checksum validation and
  contiguous packet ordering succeed. Partial-message assembly occurs during
  ordered delivery, preventing damaged, duplicate, or early packets from mutating
  live reassembly. Fragment indexes/counts and count arithmetic are validated.
  TimeSync likewise takes effect after authentication and ordered delivery.
- Outgoing long messages: every 448-byte fragment is sent in its own independently
  sequenced/retransmittable UDP packet, respecting the 464-byte payload budget.
  Previously all fragments were concatenated into one oversized packet.
  Basis: `FlowQueue::EnqueueBlob`, `Source/ACE.Server/Network/PacketFragment.cs`,
  and `NetworkSession` packet processing and assembly.
- World-entry handshake: F7C8 waits for F7DF before sending F657, as
  `CPlayerSystem::LogOnCharacter` does. Unknown and pending-deletion characters
  cannot enter. Duplicate readiness does not resend entry. Rejected entry returns
  to character selection so another attempt can be made.
- Mesh creation finalizes cache-version invalidation before obtaining a setup
  cache pointer. Tests now check the completed static render/collision meshes for
  finite bounds, separately from engine build/serialization warnings.

## Acceptance still required

### Account and character lifecycle

Character creation is implemented and has the offline validation listed above;
live creation and first-world-entry acceptance remain outstanding. Delete
confirmation, restore and credits actions remain explicit stubs in
`ACEUICharSelectBinder::OnElementActivated`. Selection of deleted characters, restore availability,
slot capacity, countdowns, and failure dialogs must follow
`gmCharacterManagementUI::UpdateButtons` and its dialog handling. The layout
manager currently forces RestoreCharacterButton hidden; this must be removed as
part of the functioning restore workflow. DDD asset patch negotiation is not a
complete replacement for retail patching; current setup uses server-side DDD off.

### Landscape, buildings, collision, and streaming

The geometry fixes have focused DAT and runtime coverage: camera-based PView
updates, DrawingBSP PORT apertures, terrain masks, room lighting, PhysicsBSP
collision, room residency and scenery reuse. The scene suite now checks 130 Yaraq
views, including cameras next to doorways and stairwells, with surrounding terrain
and resident cells in its reference. This is not exhaustive coverage of all buildings,
dungeons, water, slopes, portals, and transitions. Reproduce threshold crossing
in both directions with both the camera and player, test basements and stairs,
and measure frame time, memory, load queues, and visible pop-in on long walks.
No measured retail-equivalent traversal performance claim has been established.

### Animation, movement, combat, item use, and effects

Death actions, held corpse poses, cancellation of idle fidgets, and elapsed-time
animation throttling have regression coverage. Complete live acceptance still
includes melee facing and repeat/height/power controls, missile timing/ammunition,
spell windup/release/cancel, death/resurrection, motion links during approach,
movement prediction and correction, portal transport, swimming/jumping/falling,
item cooldowns, and interactions under network delay/loss.

The user has subsequently reported summoned creatures killing enemies, but all
summoning paths have not been accepted end to end. Earlier traces showed pet essence
use without an ensuing pet ObjectCreate. `PetDevice::ActOnUse/SummonCreature` can fail
before creation; those traces alone did not prove the cause. Capture
the client request, matching server outcome, UseDone/error/cooldown/structure
updates, and resulting pet creation before claiming it fixed. Particle/light,
weather, sound, emitter lifetime and animation-hook parity need live comparison
beyond the current fixture coverage.

### Interface and gameplay feature coverage

Audit every reachable retail panel and control, including inspection detail
formatting, equipment/preview, inventory/containers/stack splitting/merging,
shortcut binding, trade, vendors, salvage/crafting, spellbook/components,
attributes/skills, effects/vitae, fellowship/allegiance/friends/squelch, housing,
quests/contracts, books/inscriptions, options/key bindings and modal dialogs.

Owned inventory activation now follows the focused retail decision tree, but the
complete `ItemHolder` behavior is not yet transcribed. Remaining work includes
force-use, full target compatibility and target acquisition, craft/volatile-rare
confirmations, component-pack rules, ground and
vendor contexts, and the complete AutoWear/AutoWield conflict sequence. The existing
mana-stone targeting, charge flag and destruction confirmation now have native
input and packet coverage; live application/consumption remains to be accepted.
Editable text controls, popup enabled states, localization,
context actions, tooltips, drag cursors, and interaction timing are not all proven
1:1. Appraisal uses the authored layout, but layout agreement alone does not prove
that all text/content generation matches `gmExaminationUI`. In particular, the
item appraisal formatting still uses an interim summary for several fields.
Full weapon/armor/magic/requirements formatting remains on this checklist.
Character/creature inspection now has the focused layout, header, detail-row and
rendered-preview coverage recorded above; live and rare-profile acceptance is
still pending. Editable inscriptions now
have input/protocol coverage; live server persistence remains to be accepted.

### Networking and release gate

Chat editing was compared directly with retail `ChatInterface` and
`ClientCommunicationSystem` for Quest 41 / Windows 2026.09.17.51. Focused entries
now receive Up/Down before Slate consumes them, retain separate 100-line window
histories including duplicates, and expand reply shortcuts while typing. Retell
uses the outgoing recipient; patron/monarch replies retain their own last sender.
Native Quest text callbacks and the PC VR keyboard share this behavior. Actual
Slate input and isolated loopback packets cover history, reply recipient stability,
tells, channel aliases, and unknown command forwarding. See
`Quest/Validation/v41-chat-parity.md` at the repository root for sources,
test results, and native-keyboard acceptance limits.

NetworkTransport now covers real datagram parsing, CRC failures, ordering,
duplicates, malformed fragment indexes, authenticated time, world entry and
loopback fragmentation. Still exercise live session loss/recovery, encrypted
retransmission/key windows, multi-queue delivery, movement timestamps, logout and
relogin, all gameplay responses, and server error paths. Large-message delivery
must remain within packet budgets for character generation and bulk operations.

A release can be called a retail replacement only after the account lifecycle and
all required gameplay/UI flows work against an unmodified compatible server,
with comparison evidence and no open blocking discrepancies. A numbered build
provided for testing does not meet that gate merely by compiling or passing the
current suite.
