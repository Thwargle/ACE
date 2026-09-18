# Using Asheron's Call `.dat` files in Unreal (via ACViewer / ACE.DatLoader)

ACViewer ([ACEmulator/ACViewer](https://github.com/ACEmulator/ACViewer)) and this plugin both follow **ACE.DatLoader** layouts.

## Client files

| File | Role |
|------|------|
| `client_portal.dat` | Setup, GfxObj, Surface, SurfaceTexture, Texture, Palette, MotionTable, Animation, RegionDesc |
| `client_highres.dat` | High-res Texture (`0x06`) |
| `client_cell_1.dat` | CellLandblock (`xxxxFFFF`), LandBlockInfo, EnvCell |

Default directory: `C:\Turbine\Asheron's Call`

## Pipelines

### Object → mesh + texture

1. `ObjectCreate.SetupId` → Setup (`0x02`) → GfxObj parts (`0x01`)
2. GfxObj polygon `PosSurface` → Surface (`0x08`) — one `ProceduralMeshComponent` section per unique Surface
3. Image surface → SurfaceTexture (`0x05`) → Texture (`0x06`) [+ Palette `0x04` for INDEX16/P8]
4. Decode DXT1/3/5, RGB, INDEX16 → `UTexture2D` via `FACEDatTextureResolver::GetOrCreateUTexture`
5. `UACEDatSubsystem::GetOrCreateTexturedMaterial` wraps it in a `MaterialInstanceDynamic` (parent
   `/Engine/EngineMaterials/Widget3DPassThrough_Opaque`, an always-available UMG engine material
   with an unlit `SlateUI` texture parameter — no plugin content asset needed) and assigns it
   per-section using the GfxObj's real UVs. Flat-color (non-image) surfaces, and any part whose
   ObjDesc has dye/texture-swap overrides (equipped gear, dyed skin), still use the vertex-color
   fallback (`GetVertexColorMaterial`) since the per-instance swap/dye isn't baked into the
   cached-by-SurfaceId texture.

### Motion

1. `ObjectCreate.MotionTableId` (or Setup `DefaultMotionTable`) → MotionTable (`0x09`)
2. Idle: `DefaultStyle` → `StyleDefaults[stance]` → `Cycles[(stance<<16)|(motion&mask)]`
3. First `AnimData` → Animation (`0x03`) `PartFrames[frame][part]`
4. Each Setup part is a `UProceduralMeshComponent`; tick updates relative transforms

### Outdoor terrain

1. Player `CellId` landblock → CellLandblock `(LB<<16)|0xFFFF`
2. 9×9 height indices → RegionDesc `0x13000000` `LandHeightTable[256]`
3. 8×8 cells triangulated with AC2D `GetSplitDir` diagonal choice
4. `UACETerrainPresenterComponent` loads a Chebyshev neighborhood around the player
   (`LoadRadius` default 5 = retail `LScape::mid_radius` → 11×11 heightfield).
   Buildings/stabs/RegionDesc flora spawn only at Chebyshev ≤ 1 (retail
   `get_block_orient` size=1 → `side_cell_count==8`). Outer rings subsample the 9×9
   heightfield (`poly_size` 2/4/8) and stitch edges with `TransAdjust`.

### Character appearance (ObjDesc)

1. ObjectCreate carries **ModelData** = server-resolved face + clothing (not CharacterList)
2. PhysicsDesc `SetupId` + AnimPartChanges swap GfxObjs on Setup parts
3. TextureChanges remap SurfaceTextures per part; SubPalettes dye INDEX16 skins/hair/eyes
4. Local player: `ACEPlayerController` applies this to the possessed pawn (`UACECharacterAppearanceComponent`)
5. `ObjDescEvent` refreshes clothes / barber without a full recreate

## Module map

| Class | Role |
|-------|------|
| `FACEDatDatabase` | B-tree + sector read |
| `FACEDatTextureResolver` | Surface → RGBA / `UTexture2D` |
| `FACESetupMeshBuilder` | Multi-part, per-surface sections |
| `FACEDatMotionPlayer` | Idle MotionTable evaluation |
| `FACELandblockMeshBuilder` | Outdoor heightfield |
| `UACEDatSubsystem` | Caches + Blueprint apply helpers |
| `AACEWorldEntityActor` | Textured parts + idle anim |
| `AACELandblockActor` / `UACETerrainPresenterComponent` | Terrain |

## Terrain surfacing (implemented)

Terrain uses the retail `RegionDesc.TerrainInfo.LandSurfaces.TexMerge` blend, baked on the CPU
per landblock (`FACELandblockMeshBuilder::BakePCode`): base `TerrainTex` plus up to three terrain
overlays and two road overlays, with alpha maps picked from `CornerTerrainMaps` /
`SideTerrainMaps` / `RoadMaps` by the retail PRNG on the cell's palette code. Cells are grouped
into one mesh section per palette code. `GetTerrainTypeColor` is only a fallback if the bake
fails. Triangulation uses the retail `GetSplitDir` hash, so diagonals match the original client.

The GPU `Texture2DArray` path (`FACELandSurfaceAtlas`, `bEnableGpuLandMaterial`) is written but
disabled — the Custom HLSL blend is unvalidated and its UV layer packing does not yet carry the
third terrain overlay or second road alpha.

## EnvCell storage and identification

Cell.dat has three landblock-scoped types (same routing idea as tooling `IDatReaderWriter`):

| File short ID | Type |
|---------------|------|
| `0x0001`–`0x0040` | Outdoor `LandCell` (8×8 grid of 24×24 AC units in a 192×192 landblock) |
| `0x0100`–`0xFFFD` | **EnvCell** (building interiors + dungeons) |
| `0xFFFE` | `LandBlockInfo` |
| `0xFFFF` | `LandBlock` / CellLandblock heightfield |

Full ID = `(landblockKey << 16) | shortId`. ACEClient indoor test is
`(CellId & 0xFFFF) >= 0x0100` (`IsIndoorCell`); LBI loads as `lbKey | 0xFFFE`.

Outdoor landcell from local XY (editor/`LandblockDocument` math; same grid retail uses):

```
cell = floor(x/24)*8 + floor(y/24) + 1   // 1..64
```

### Building → EnvCell linkage

Each `LandblockInfo.Buildings[]` entry has doorway portals (`PortalCellIds` /
`CBldPortal`). Interior membership is a BFS from those portals following
`EnvCell.CellPortals` (and often `VisibleCells`). Tooling duplicates that BFS for
blueprint extraction; ACEClient uses the same graph in
`GetOrBuildBuildingInteriorFootprints` (flora / grade masks).

**Outdoor draw/load does not BFS.** It uses the DAT precomputed `CBldPortal.StabList`
(cells visible through that doorway from outside). Full BFS over-pulled street-invisible
rooms. Indoor stream still walks portals + `VisibleCells` + budgeted `NumCells`.

`CBldPortal` seed (persisted): `Flags`, `OtherCellId` (dest EnvCell), `OtherPortalId`
(reverse portal index on that cell), and `StabCells`. Streaming loads the full StabList;
**draw** requires a nonempty frustum-clipped aperture (retail copied PView) — see Cell visibility.

Editor-only (N/A for client): when a building moves/places, VisibleCells entries that
point at outdoor LandCells (`0x0001`–`0x0040`) are remapped via
`PositionToOutdoorCell` so `find_transit_cells` stays valid across outdoor cell boundaries.

### Building interior vs dungeon (tooling vs ACEClient)

Reference render prep (`EnvCellManager.PrepareLandblockEnvCells`) classifies each EnvCell:

| Class | How | Z vs terrain |
|-------|-----|--------------|
| Building interior | In `buildingCellIds` (portal BFS), else fallback `SeenOutside` | Small **positive** Z offset so floors sit above LScape |
| Dungeon / terrain-only | Not in any building; landblock may have `Buildings.Num()==0` | Pushed **far below** terrain |

ACEClient:

- Outdoor EnvCell stream only if `Info.Buildings.Num() > 0` (StabList path) — dungeon-only
  LBs are not peeked from the street (≈ `isDungeonOnly`).
- Look-out: `PView::DrawInside` draws `LScape` with `Render::PortalList = outside_view`
  (0xFFFF portal polygon clips). ACE keeps the heightfield submitted and discards
  fragments whose camera ray misses the doorway rectangle. Do not hide the land actor
  (black void through the door) and do not unhide unclipped plaza (grass shop floors).
- Building EnvCells: **draw-only** +Z (~12 cm) from footprint BFS; collision stays authored.
- Land **PixelDepthOffset** (~22) backs outdoor look-in.

### World transform

Matches tooling and retail: landblock origin `(blockX*192, blockY*192)` → Unreal; EnvCell
actor root at that origin; cell `Frame` as `CellMesh` relative transform.
`CellToWorld = CellFrame * Translate(LandblockOrigin)` (UE multiply order). Wrong order
breaks `point_in_cell` and silently disables indoor residency.

### Load path

Indoor: `LandBlockInfo.NumCells` → fetch `lbKey | (0x0100 + i)` (budgeted; portal/
VisibleCells seeded first). Outdoor: grab_visible on the full-res 3×3 (not the
11×11 LOD ring). Caps (~384 indoor / ~2048 outdoor safety). Residency cut is **player CellId**, not camera
`FindCameraCell` (camera hysteresis left LScape under shop floors). Outdoor **draw**
admission still uses the camera frustum against building apertures.

## Cell visibility (implemented)

Outdoor→indoor draw follows retail building portals: each `LandblockInfo` building portal
carries `OtherCellId`, `OtherPortalId`, and a `StabList` of EnvCells visible through that doorway
(`FACEDatBuildingPortal`). Indoors, the draw set is the current EnvCell plus its `VisibleCells`
and non-outside `CellPortals`.

### Camera outside an EnvCell (two cases — both FullCell)

Treat “camera outside an EnvCell” in two distinct ways; both submit the **complete** CellStruct
mesh (`FullCell`). Portal geometry controls admission and draw order; it is **not** a GPU
fragment clip on the EnvCell shell.

1. **Outdoor camera looking into an EnvCell** — retail sequence approximated in Unreal:
   building aperture (side test + frustum clip) → admitted StabList EnvCells → FullCell shell
   → Setup exterior (DrawingPortal holes). Depth-only doorway apertures write Z before shells
   (building phase-order stand-in). A portal opens only when the copied aperture is nonempty;
   streaming still loads the full StabList. `LandblockInfo` is cached so per-tick admission
   stays cheap.
2. **Interior-associated eye outside CellBSP** (doorway edge) — keep the full VisibleCells +
   CellPortals draw set; never pick a DrawingBSP leaf from an invalid outside point.

Retail never CSG-punches the heightfield (`ConstructPolygons` keeps both land tris).
`SmartBox::RenderNormalMode` is binary by residency:

| Viewer cell | Draw |
|-------------|------|
| Outdoor (`cell & 0xFFFF < 0x100`) | Full `LScape` + building Setup shells; EnvCell StabList doorway peeks (aperture-gated) |
| Indoor (EnvCell) | **No landscape pass** except land visible through `0xFFFF` doorway portals (EnvCell walls occlude; do not CSG-punch land) |

This client residency draw:

| Viewer | Draw |
|--------|------|
| Outdoor (player CellId) | Full **unpunched** opaque LScape + Setup shells + **aperture-admitted** StabList peeks. Land under the nearest peeked EnvCell floor XY is shader-discarded so look-in floors aren't plaza grass. |
| Indoor + 0xFFFF doorway (PortalList) | Land **stays drawn**; fragments kept only if the camera ray hits a doorway rectangle (retail `Render::PortalList` / `cliplandscape`). EnvCell floors are the indoor ground. |
| Indoor sealed dungeon | **Hide LScape** (`outside_view.view_count==0`) |

Player CellId is the cut point for indoor/outdoor residency (collision + LScape). Outdoor
**aperture admission** uses the camera frustum/eye (retail outdoor PView). **Banned forever:**
FloorTris CSG punch, portal half-space `clip()` on EnvCell materials, CustomStencil look-out
land, and EnvCell +30 cm collide lift. Depth-only doorway apertures (no main-pass color) are
allowed — they are not EnvCell clip planes.

**Basements / land under buildings (retail vs ACE):** Retail never punches the heightfield.
When the viewer is **indoor**, `LScape::draw` is skipped entirely except fragments admitted
through `0xFFFF` doorway rectangles (`Render::PortalList` / `cliplandscape`). Sealed dungeons
submit no land at all (`outside_view.view_count==0`). The DAT mesh still dips into basement
mouths outdoors — that is fine because outdoor viewers draw the full heightfield; indoor viewers
never see it except through doorways. `FloorTris` in ACE are **collision/flora only** (suppress
outdoor land support under EnvCell floors while still outdoor-resident) — not a visual CSG punch.
Indoor + doorway: PortalList shader clip + building footprint AABB (`TryGetBuildingLandClipBox`
from EnvCell floor XY) so dipped heightfield under the occupied building does not paint the
basement floor. Outdoor look-in: admitted peek cell AABB only (never the whole landblock).

**EnvCell Z:** collision and draw at authored DAT height (no mesh lift). Coplanar LScape
loses via land material PixelDepthOffset so indoor floors win look-in / look-out.

**Stairs:** Stab force-draw includes ClipMap wood treads; EnvCell draw→collision keeps
upward ClipMap; indoor `TryStepUp` probes lateral ±45°/±90° + feet ring for L/quarter turns.

Sky (`AACESkyDomeActor` format v50 — retail GameSky):
camera-follow root; uniform `WorldScale * SkyDistanceScale` (default 1);
Properties `0x1` after-pass / `0x4` weather / `0x8` no Z-fix;
weather Z = −120 AC; `SkyObjReplace` gfx swap + MaxBright;
CalcFrame: heading about +Z, then rotation about −Y (BeginAngle→EndAngle, not negated);
cloud cubes tessellated + spherized with authored UVs (no lat-long remap);
WorldFog flag gated; fog clamp default 0 (DAT only).
Land unlit emissive follows DAT DirBright/AmbBright (`SetWorldEmissiveScale`).

## Cell collision (implemented)

Retail has no "world holes": interiors work because the collision candidate set is
cell-partitioned (`ObjCell::find_cell_list`). Inside an EnvCell the set is that cell plus
portal-linked cells, and outdoor land is only added when the sphere crosses an outside portal
(`OtherCellId == 0xFFFF`). Outdoors the set is the land cells plus the *building transit* cells
found through `BuildingObj::find_building_transit_cells`.

The client mirrors that via `ACECellTransit` (`FindCellList` / `ResolveTransitCellId`):

| Player cell | Collides |
|-------------|----------|
| Outdoor | Outdoor heightfield **always** (retail `add_all_outside_cells` is unconditional) + **sphere-tested** doorway entry EnvCells |
| Indoor | Transit EnvCells from portal growth; outdoor land only if an outside portal is crossed |

Pawn sweeps ignore EnvCell actors outside the transit set; outdoor terrain is ignored only when
the player is indoor-resident. Doorway StabList peek cells stay **draw-only**. EnvCell blocking
uses PhysicsBSP (`CellCollisionMesh`) when present — draw-mesh collision is a fallback only.
Building exterior stairs win over grade via the raised-walkable step preference, not by
suppressing land: land support stays valid under building footprints while outdoor-resident,
exactly as retail.

**Cell-local containment transform.** Retail `CEnvCell::point_in_cell` maps the global point
into cell-local space with the cell Frame, then walks the CellStruct cell BSP
(`point_inside_cell_bsp`: Behind → outside, Front/Close → PosChild, leaf → inside; NegChild is
never walked). The Unreal equivalent must compose transforms in UE order (`A * B` applies A
first): `CellToWorld = CellFrame * Translate(LandblockOrigin)` — matching how
`AACEEnvCellActor` places `CellMesh` (actor at landblock origin, cell Frame as the component's
relative transform). Composing the other way rotates the landblock origin by the cell
orientation and breaks containment for every rotated cell, which silently disables indoor
residency (no interior collision, terrain lid never hides, basements unreachable).

**Retail render modes** (`SmartBox::RenderNormalMode`): viewer in a land cell
(`cellid & 0xFFFF < 0x100`) → full `LScape::draw` landscape pass; viewer indoor → **no
landscape at all**, only `DrawInside(viewer_cell)` walking the portal graph (with the LScape
viewpoint updated through the outside cell when `seen_outside`). There is no terrain hole
geometry anywhere — "holes" into lower levels exist because the landscape simply is not drawn
while the viewer is indoor-resident.

## Still later

- Per-instance ObjDesc texture-swap/dye baked into real (not vertex-color) textures
- Validate and re-enable the GPU land material (full overlay/road layer packing)
- Optional: building-only EnvCell draw Z bias from footprint BFS membership (tooling
  EnvCellManager pattern) — collision stays at authored Z; no land punch
