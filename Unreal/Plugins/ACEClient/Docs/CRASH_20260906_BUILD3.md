# Building-entry crash in build 2026.09.06.3

Fixed in 2026.09.06.4. The saved reproduction is
`Saved/Crashes/UECC-Windows-D57CB12941F05263DA672B8107253188_0000`.
Its `ACUnreal.log`, `CrashContext.runtime-xml`, and `UEMinidump.dmp` are retained,
along with the matching .3 plugin DLL/PDB in `Symbols-2026.09.06.3`.

## Evidence

The crash occurred September 6 at 21:19:52 UTC, about 114 seconds after startup.
The occupant moved through Yaraq cells `7D63010D`, `7D63010E`, and `7D630112`.
The log repeatedly reports `Instance Static Mesh Component unable to create
InstanceBodies!` before an access violation reading address `0x4018`.

The crash-report worker stack is not the original call site. GameThread 10308 in
the minidump contains `memmove`, then these engine/plugin RVAs:

- Engine `0x129f47f`: instance-body array insertion.
- Engine `0x12b6724`: `SetupNewInstanceData`.
- Engine `0x1264fd2` / `0x1264d9a`: `AddInstanceInternal` / `AddInstance`.
- ACEClient `0xb93ef`: `AACEEnvCellActor::TrySpawnOneStaticObject + 0x54f`,
  matching .3 source line 685 (`Hism->AddInstance`).
- ACEClient `0xb8c8e`: `AACEEnvCellActor::Tick + 0x30e`, source line 483.

The plugin locations were resolved against the preserved matching PDB using
`diag/resolve_pdb.cpp` and DIA. Engine source and disassembly identify the body
insertion path; an installed private Engine PDB was not available.

## Cause and correction

A DAT setup with no PhysicsBSP deliberately yields a visual static mesh without
a BodySetup. Interior scenery nevertheless enabled QueryAndPhysics on its ISM.
UE's body initialization exits early without a BodySetup, leaving an empty
instance-body array with physics state marked created. Adding another instance
then inserts at an index greater than that empty array's size. Repeated full
physics recreation after each instance compounded the broken state and cost.

Interior and outdoor ISMs now enable physics only when their mesh has collision
data. Default collision inheritance is disabled explicitly. Collision toggles
preserve that distinction. Empty PhysicsBSP remains non-colliding; the fix does
not invent a collision hull or disable real furniture collision.

`AddInstance` manages its own body/render updates; the redundant full-pool
physics recreation after every addition has been removed. Room PhysicsBSP
sections are combined into one hidden collision section before cooking, retaining
the source triangles and immediate collision readiness while avoiding a recook
for every material section. Portal stencil state is reapplied only when changed.

## Regression and limits

`ACE.RetailParity.InteriorStreaming` loads the three actual crash rooms, toggles
collision off/on four times, and continues adding their DAT scenery after entry.
It exercises five solid ISM pools, two pools without collision, and 32 instances.
Assertions check body-slot counts for solids and the absence of partial physics
state for decorations. The full suite also checks authored indoor floors, setup
collision and the earlier camera/occupant doorway visibility cases.

This reproduces the crash's DAT assets and component sequence in an isolated
physics world. It is not a replay of the user's live network session or proof
that every possible building-entry crash is eliminated.

## Additional fault caught before delivery

The first full .4 suite (`Saved/Logs/RetailParity20260906e.log`) exposed a separate
`Array has changed during ranged-for iteration` ensure at
`UACEDatSubsystem::GetOrCreateSetupStaticMesh`, while the Yaraq building test was
creating materials. Background setup completions could relocate a by-value
cache entry or replace its geometry while a renderer retained raw part pointers.

Setup cache values now use shared immutable geometry, and builders retain a
reference across material/physics creation. A late duplicate background result
keeps an already completed setup. Generation checks precede pending-key removal.
`MeshApplication` checks cache growth, duplicate completion, eviction while held,
and stale-generation completion in addition to its actual render/collision tests.
