# AC:Unreal / AC:VR release 149

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.04.149**. Android version code: 149.
Quest installer revision: 9. These notes cover changes since v148.

## UCM combat and recovery

- Support custom wand augmentation requirements, including ILT's type-13
  64-bit stat checks. When a count is not sent to the client, allow the server
  to validate the normal equip attempt instead of rejecting the wand locally.
- Improve equipment eligibility diagnostics and refresh eligibility when relevant
  character stats change. Respect Magic/Melee/Missile restrictions for explicitly
  selected weapons; show caster element and bonus in the equipment pool.
- Add priority and Remove controls to the Mobs table. Move pet selection into
  Details alongside weapons and offhand selection.
- Keep native mob rows independent when Default changes and prefer the longest
  matching name prefix. Preserve imported VT rule ordering.
- Allow debuffs without enabling ordinary attacks. Keep bolt/arc, ring, streak,
  and Void attack selections independent.
- Fix the stamina-to-health threshold and prioritize vital recovery over an
  attack still waiting for its combat stance transition.

## Inventory, spells and input

- Highlight items offered in trade and remove transferred items from inventory
  when the server confirms the transfer, without requiring a relog.
- Restore spell-bar hover names and keyboard bindings for spell slots 10-12.
- Prevent hidden text fields from blocking Give Selected and Action Deck hotkeys.
- Smooth animation during slow mouselook turns.
- Prevent a stale mouse-turn release from cancelling a newly started Use/Give
  interaction, including scripted corpse interactions.

## Validation and packaging

Release checks cover the changed UCM policies, input, trade, spell UI, launcher
and updater in the editor and packaged Windows/Linux clients. Android packages
are checked for identity, version, signing continuity, 16 KiB alignment, and
bundled plugins. The Windows installer is checked upgrading v148 while preserving
saved data. Detailed results accompany the local release verification records.

Packages exclude retail DATs, personal settings, server data and generated
MeshCache files. Android phones/tablets remain a preview using a separate APK
from Quest. Live ILT wand acceptance, custom mob reward delivery and the rare
Action Deck report still require player verification; no physical-device or
live-server acceptance test was performed for this release.
