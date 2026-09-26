# Movement, collision, appraisal, and selection review — 2026-09-26

Shared fixes for desktop, PC VR, and standalone Quest. This is development validation, not a new published release.

## Remote movement

- `ObjectDescriptionFlag::Stuck` means an object cannot be picked up. Players and monsters can carry it; it does not mean that their feet should keep the server packet's old height. Only anchored non-creature scenery now bypasses ground following.
- A horizontal/non-rising ObjectCreate or VectorUpdate velocity does not by itself make a supported creature airborne. Preserve grounded locomotion when its feet are already at a valid floor; keep genuine upward impulses and unsupported bodies ballistic.
- Grounded collision slides preserve the horizontal travel direction while following a slope. Projecting the displacement onto the full slope normal was introducing sideways drift between server corrections.
- Interpreted WalkForward remains WalkForward even with an animation multiplier greater than one. Prediction uses the actor's DAT motion velocities and model scale instead of assuming every actor has human speed at scale one.
- Remote foot support accepts authored solid world-object geometry, including hidden platforms. Creature bodies and selection proxies remain excluded.

Relevant retail references: `PORTAL/cphysobj/CPhysicsObj.c` contact-plane/velocity handling and `update_position`; `PORTAL/cphysics/CTransition.c` support and step-down transitions. The local ACE equivalents are `Source/ACE.Server/Physics/PhysicsObj.cs`, `Transition.cs`, and `PartArray.cs`. The Stuck pickup check is explicit in `Source/ACE.Server/WorldObjects/Player_Inventory.cs`.

## Walls, stairs, and airborne charge

- Ordinary idle position corrections are swept against collision before updating the local pose. Explicit forced/teleport corrections keep their authoritative behavior.
- Foot support uses the lower sphere's contact at convex tread edges, with the authored inclined floor retained at ramp edges. A flat tread's distant edge cannot support an almost vertical contact indefinitely.
- If a downward capsule sweep moves sideways while clearing a stair lip, re-query support at its resolved position and sweep the remaining descent. Keep the original step-down depth budget and bound retries; do not snap through a wall or across a drop.
- Grounded traversal remains grounded in actual DAT stair fixtures at 20, 30, and 60 FPS. Tests cover center/edge offsets, both directions, wall penetration depth, open ledges, and headroom.
- Desktop and VR can charge jump while airborne. Releasing in the air does not apply another impulse. Holding through landing retains the charge so release can launch the next jump.

Relevant retail references: `CTransition::step_down`, `edge_slide`, and `CBSPTree` lower-sphere contact tests. Existing Fort Teth/Mosswart and center-post fixtures exercise shared collision behavior, without location-specific movement exceptions.

## World visibility and effects

- Camera/portal visibility no longer disables world-object collision. A solid object can be invisible (including NoDraw) and still support feet; ethereal, corpse, and missile collision rules remain explicit.
- An object's current bounds can keep it visible when they intersect a resident visible room even if its last network CellId describes a hidden room. This handles moving traps whose vector updates do not include a new cell. The check uses cached cells and does not load additional rooms.
- World-model selection flash now visits appearance parts only. Previously it also cloned/boosted particle materials; a live or pooled effect could retain that boosted material and accumulate brightness across clicks.

Relevant retail reference: `CPhysicsObj::find_bbox_cell_list` and cross-cell shadow/object registration. Visibility and physics occupancy are separate concerns.

The user identified the Empyrean Rescue quest but did not have exact bridge/hallway coordinates. The [quest description](https://loresraat.org/wiki/Empyrean_Rescue_Quest) and [dungeon description](https://loresraat.org/wiki/Empyrean_Rescue_Dungeon) identify the sequence and final hazards. The collision/visibility regressions use real DAT geometry and a stale-cell moving-object fixture; the exact quest bridge and flying pyramids still need a live route check.

## Options and appraisal

- Apply/Reset are disabled and dim when their page has no pending changes. Config compares against current applied settings, Character against its option snapshot, and Chat against filter/opacity snapshots. Returning a changed value to its original value disables the buttons again.
- Mana charges/stones show Stored Mana, Efficiency, and Chance of Destruction when those appraisal properties are present, including zero. Follow retail's spell-book exclusion and percentage conversion.
- Titan Mana Charge fixture: 5,000 stored mana, 100% efficiency, 100% destruction chance. Additional fixtures cover an empty reusable stone and spell-bearing items.

Relevant retail reference: `GAME/game_ui_misc/gmExaminationUI.c`, `ItemExamineUI::Appraisal_ShowManaStoneInfo` (properties 0x6B, 0x57, 0x89).

## Validation evidence

Output directory: `Unreal/Saved/ReleaseValidation/sep26-movement-review`.

- Baseline replay reproduced horizontal-vector terrain errors up to 303.87 cm without gravity and 10.24 cm with gravity.
- Updated straight/diagonal downhill replays use real creature flags, including Stuck, sparse position packets, and repeated vector packets. Test limits: less than 2 cm height error and 0.1 cm lateral drift. Separate cases retain airborne jump arcs and tracked VR roots.
- Repeated world selections exercise live Weeping Wand and Staff of Aerfalle emitters and assert that flashed components are appearance parts only; emitter counts remain stable.
- `ContactReport5/index.json`: FortTethStairs, LedgeStairs, WallContact, and StairCeiling all pass after the final support fix.
- `FinalReport/index.json`: all nine combined movement, collision, particle, interior, and UI suites pass (zero failed or skipped).
- `build-final.log`, `build-win64.log`, `build-android.log`: Windows editor, Windows game, and Android development builds succeed. Quest source/material parity check reports zero differing files.

Synthetic packet replay is not a substitute for a side-by-side live server/VR test with real latency. The exact Empyrean Rescue geometry and headset presentation remain live validation items.
