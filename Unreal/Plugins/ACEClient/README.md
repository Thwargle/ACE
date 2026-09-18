# ACEClient — Unreal Engine 5 plugin for ACE

UDP client for the retail Asheron's Call protocol (client **1802**) used by [ACE](https://github.com/ACEmulator/ACE).

## Scope and current status

The project targets a complete retail 1802 client replacement, including gameplay,
networking, DAT content, controls and interface behavior. Graphical enhancements
follow feature completion. The client is under active development and is not yet
feature complete. See [retail parity acceptance and evidence](Docs/RETAIL_PARITY.md)
for the current implementation, explicit gaps, reference sources and release gate.

Implemented paths include login/world entry, object replication, local movement,
DAT terrain/buildings/interiors and collision, character appearance/motion,
combat and item action requests, social/chat features, and DAT-derived interface
panels. Their presence is not a claim that every path matches retail. Regression
coverage and build-specific results are documented in [Tests/README.md](Tests/README.md).

## Install

1. Copy `Unreal/Plugins/ACEClient` → `YourProject/Plugins/ACEClient`
2. Generate project files, enable the plugin, build
3. Engine modules: `Sockets`, `Networking`, `UMG`, `ProceduralMeshComponent`
4. Place retail DAT files under `C:/Turbine/Asheron's Call` (or set `ACEDatSubsystem.DatDirectory`)

## Server config

- Valid account (or auto-create)
- Prefer **`DDD.EnableDATPatching = false`**
- Ports **9000** (C2S) / **9001** (S2C)

## Fastest playable setup

1. Create a Game Mode that uses **`ACEPlayerController`**
2. Give it a simple pawn (empty or capsule)
3. Add **`ACEWorldPresenterComponent`** + **`ACETerrainPresenterComponent`** to the Game Mode (or a persistent level actor)
4. Use **`ACEPlayerController`** so your pawn gets the server character mesh on login
5. Play: login → Enter World → you and nearby players should match their ACE appearance

Optional: create a Widget Blueprint child of `ACELoginWidget` and assign named widgets (`HostBox`, `PortBox`, `AccountBox`, `PasswordBox`, `LoginButton`, `EnterWorldButton`, `StatusText`, `CharacterListLabel`, `CharacterListBox`). If none are bound, a basic C++ layout (dim backdrop, centered card, clickable character rows) is generated automatically.

## Blueprint API (`ACE Client Subsystem`)

| Event | Meaning |
|-------|---------|
| On Character List | Account characters ready |
| On Entered World | Local player guid + spawn position |
| On Object Created | Decoded world object |
| On Object Deleted | Guid removed from range |
| On Position Update | Any object moved |
| On Chat Message | System / speech text |
| On Log Message | Protocol debug |

| Call | Meaning |
|------|---------|
| Login / Logout | Session |
| Enter World / By Name | Character select |
| Send Movement / Stop | Locomotion |
| Get World Objects | Snapshot of registry |

## World presentation

- `AACEWorldEntityActor` — Setup + **ObjDesc** (face/clothing) from ObjectCreate; idle MotionTable
- `UACECharacterAppearanceComponent` — same appearance builder; auto-attached to the **possessed pawn** for your character
- `UACEWorldPresenterComponent` — other objects (`bSkipSelf` defaults true so self uses the pawn)
- `AACELandblockActor` / `UACETerrainPresenterComponent` — outdoor terrain
- `UACEDatSubsystem` — DAT load + mesh / motion / landblock

Local character looks come from ObjectCreate ModelData (server already merges CharGen + clothes). Clothing changes use `ObjDescEvent` (`0xF625`).

### DAT files

Point `UACEDatSubsystem::DatDirectory` at your client folder (default `C:/Turbine/Asheron's Call`):

- `client_portal.dat` — Setups, GfxObjs, Surfaces, MotionTables, RegionDesc (required)
- `client_highres.dat` — optional high-res textures
- `client_cell_1.dat` — landblocks (required for terrain)

See [Docs/DAT.md](Docs/DAT.md).

## Still ahead

- Full `UTexture2D` materials (vs vertex-sampled colors)
- EnvCell indoor environments
- Combat / inventory GameActions
- Styled UMG character picker

## Movement keys (`ACEPlayerController`)

| Key | Action |
|-----|--------|
| W/S | Forward / back |
| A/D | Turn left / right |
| Q/E | Sidestep left / right |
| Shift | Run |

## Protocol map

| Step | Notes |
|------|--------|
| LoginRequest → ConnectRequest → ConnectResponse | Cookie + ISAAC seeds; S2C on port+1 |
| CharacterList / EnterWorld / LoginComplete | Auth → world |
| ObjectCreate `0xF745` | ModelData + PhysicsDesc + WeenieHeader |
| UpdatePosition `0xF748` / ObjectDelete `0xF747` | Tracking |
| MoveToState `0xF61C` / AutonomousPosition `0xF753` | Locomotion |

## Layout

```
ACEClient/
  ACEClient.uplugin
  Docs/DAT.md
  Source/ACEClient/
    Public/   session, subsystem, DAT, widgets, world actors, protocol
    Private/  implementations
```
