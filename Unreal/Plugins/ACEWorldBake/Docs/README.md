# ACE World Bake

Offline DAT export and UE import for **landscape tiles**, **building landblocks**, and **scenery** — ported from [ACUnreal](https://gitlab.com/landelare/acunreal).

Use this to build a **World Composition** Dereth map with distance-based tile streaming (same workflow as acunreal), then play in ACEViewer on baked static meshes + landscape instead of runtime ProcMesh.

## Requirements

- UE 5.8, Visual Studio 2022
- **32 GB RAM** recommended for full-world import (or import one landscape column at a time)
- Retail DAT files (`cell.dat`, `portal.dat`, `highres.dat`, `locale/english.dat`)

## DAT path

Set in **Project Settings** or `Config/DefaultEngine.ini`:

```ini
[ACEWorldBakeDatTools]
DatFileDirectory=C:/Turbine/Asheron's Call
```

Or in-editor: call `UACEWorldBakeEditorFunctionLibrary::SetDatFileDirectory`.

## Quick start (editor)

1. **Regenerate project files** and build `ACEViewerEditor`.
2. Open ACEViewer in the editor.
3. Use **Editor Utility Widget** or **Blueprint** nodes on `UACEWorldBakeEditorFunctionLibrary`:

| Step | Function |
|------|----------|
| Export models/images | `ExportPortalResources` |
| Export landscape + T3D | `ExportLandblocks` |
| Import textures | `ImportTextures` (`Saved/Images` → `/Game/Images`) |
| Import FBX models | `ImportModels` (`Saved/Models` → `/Game/Models`) |
| Import surface materials | `ImportSurfaces` |
| Import landscape parent materials | `ImportLandscapeMaterials` |
| Per-tile landscape MICs | `ImportLandscapeMaterialInstances` |
| Buildings + scenes | `ImportLandblockInfo` / `ImportLandblockScene` |

4. **Headless export** (landblocks only):

```bat
UnrealEditor-Cmd.exe ACEViewer.uproject -run=ACEWorldBakeExportResources -LANDBLOCKS
```

## Automated full world build

One-shot export + import + Dereth map + 64 landscape tiles + landblock T3D queue:

```bat
Unreal\BuildDerethWorld.bat
```

Or manually:

```bat
UnrealEditor-Cmd.exe ACEViewer.uproject -run=ACEWorldBakeBuildWorld -DATPATH="C:/Turbine/Asheron's Call" -unattended
```

In-editor: call `UACEWorldBakeEditorFunctionLibrary::BuildEntireWorld()`.

**Commandlet switches:**

| Switch | Effect |
|--------|--------|
| `EXPORTONLY` | DAT → `Saved/` only |
| `IMPORTONLY` | Skip export |
| `SKIPLANDSCAPE` | Skip tiled landscape + MICs |
| `SKIPSETUP` | Skip setup blueprint import |
| `SKIPMODELS` | Skip FBX model import |
| `SKIPLANDBLOCKS` | Skip T3D landblock/scene import |
| `RECONFIGUREWC` | Nest landblock/scene tiles under landscape parents and save all WC levels unloaded (fast fix for editor memory) |
| `NODEFAULTMAP` | Do not update `GameDefaultMap` |
| `DATPATH=` | Override DAT directory |

Requires parent materials at `/Game/Materials/M_Diffuse*` and `/Game/Landblocks/LandscapeMaster` (copy from acunreal Content or create stubs). Landscape MIC parent defaults to `/Game/Landblocks/LandscapeGCF_0`.

## World setup (Dereth map) — manual fallback

Follow the acunreal README steps:

1. Create `Content/Maps/Dereth/Dereth.umap` (basic template map).
2. World Settings: **Enable World Composition**, disable **World Bounds Checks**.
3. **Import tiled landscape** from `Saved/Landblocks/Tile_x*_y*.r16` (exclude `Tile_All.r16`):
   - Tile offset: `-4, -4`
   - Components `17×17`, Quads `15×15`, Scale `2400 × 2400 × 12800`
4. Load all 64 tiles, create WC layer (~250000 streaming distance).
5. **Apply Landscape Textures** per column → **Import Landblocks** per column.
6. Nest `_Landblocks` and `_Scenes` sub-levels under each `Tile_x*_y*` parent.
7. Save persistent level with tiles **unloaded**.

Tile grid: **8×8** UE tiles, **32×32** AC landblocks per tile.

## Runtime tile helpers

`UACEWorldBakeFunctionLibrary` (Blueprint):

- `UnrealVectorToTile` — player position → tile index
- `UnrealVectorToACLandblock` — position → landblock X/Y
- `GetACEWorldBakeWorldSize` — tile count and tile size

## Opening Dereth in the editor (without loading the whole world)

The bake creates **383** sub-levels (64 landscape + landblocks + scenes). Opening Dereth with everything loaded will exhaust RAM.

**After a full build (or `RECONFIGUREWC`),** tiles are saved **unloaded** and landblock/scene levels are **nested under** their landscape tile (`Tile_x0_y7_Landblocks` → child of `Tile_x0_y7`). Only landscape tiles use distance streaming (~2500 m).

1. Do **not** set `EditorStartupMap` to Dereth (the build commandlet only updates `GameDefaultMap`).
2. **File → Open Level** → `/Game/Maps/Dereth/Dereth`.
3. **Window → World Composition** — move the editor camera to the area you care about; WC streams in nearby landscape tiles only.
4. **Levels** panel — double-click e.g. `Tile_x0_y7` to load one tile; its `_Landblocks` / `_Scenes` children load with the parent.

**Fix an already-built map without re-importing** (~seconds):

```bat
UnrealEditor-Cmd.exe ACEViewer.uproject -run=ACEWorldBakeBuildWorld -unattended -RECONFIGUREWC -log
```

## Using baked world in ACEViewer

After Dereth is built:

1. Set `GameDefaultMap` / `EditorStartupMap` to `/Game/Maps/Dereth/Dereth`.
2. Move `PlayerStart` to Yaraq area (acunreal default: `X=800000, Y=-1000000, Z=9500`).
3. Disable runtime terrain presenter for baked regions (future hybrid flag in `ACETerrainPresenterComponent`).

Until hybrid mode lands, disable `ACETerrainPresenterComponent` on GameMode or set load radius `0` when testing pure baked maps.

## Source layout

| Module | Role |
|--------|------|
| `ACEWorldBakeDatTools` | DAT read, export to `Saved/` |
| `ACEWorldBakeEditor` | Import, T3D paste, WC level creation |
| `ACEWorldBake` | Tile/landblock coordinate Blueprint library |
