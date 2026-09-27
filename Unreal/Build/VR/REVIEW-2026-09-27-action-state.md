# Door/NPC interaction recovery and VR landing state

Reported symptoms: desktop door/NPC interactions remain "too busy" until relog;
VR attacks/casts sometimes report airborne even when the player has landed.

## Findings and changes

- Retail's `ClientUISystem::Handle_Item__UseDone` releases its busy counter;
  `OnEndCharacterSession` clears it. The Unreal session kept its use gate until
  UseDone or disconnect, including after an unsuccessful approach.
- Failed approaches now release pending use after the controller stops movement.
  Portal entry and character exit also cancel obsolete interactions. An outstanding
  use with no completion for 30 seconds releases only the local interaction gate
  and asks the player to retry. It never automatically repeats a use or clears an
  unrelated equipment transaction. Queued network completions are processed before
  the timeout; truncated completions cannot clear an active use. Recovery logs
  include source, target, reason, and age.
- ACE deferred cancelled approach completion until the player became stationary,
  but checked that callback only inside the moving/animating physics branch.
  Already stationary approaches can now complete once and release their use.
- The shared client landing path flushed a grounded AutonomousPosition before
  committing the resolved feet position. It now sends the committed touchdown
  position, and VR actions preserve actual local contact rather than always
  claiming grounded.
- On ACE, a VR action can arrive after AutonomousPosition but before the physics
  tick consumes that position. A pending same-cell landing is now processed before
  the attack's airborne check. Normal movement validation remains in place.
- Full physics can retain contact from its old simulated position; zero-distance
  transitions also skip collision insertion. If still marked airborne after the
  queued position is applied, a bounded 0.5 cm downward collision probe checks
  real support and runs normal landing hooks. It requires a walkable surface,
  the same cell, and a resolved position within the probe distance. The client's
  contact bit alone never grants permission to cast; unsupported air positions
  remain rejected. Portal and cell transitions keep their existing scheduling.

## Validation

Artifacts: `Unreal/Saved/ReleaseValidation/sep27-action-state/`.

- 82 server VR-combat/physics tests passed, with no skips. New live-DAT fixtures
  cover exactly-once stationary approach completion, immediate landing actions in
  PK and NPK physics, unchanged-position contact refresh, and false grounded claims
  while airborne. Fixtures do not start a shard or save characters.
- Four client automation suites passed: InteractionRecovery, VR.Protocol,
  VR.StairCeiling, and RetailParity.WorldEntry. They cover timeout/cancellation,
  completion ordering, equipment-lock isolation, real touchdown packet positions,
  and position-before-action ordering. Two suites deliberately exercise rejection
  paths and complete with expected warning logs.
- Windows editor, Windows client, and Quest Android development builds succeed.
  Shared Unreal/Quest sources are synchronized.

The fixes include both client and ACE server code; the server-side changes require
an updated ACE deployment and do not alter third-party servers automatically.
No release was published or installed. These are automated reproductions; the
original intermittent user session has not been reproduced in a live playtest.
