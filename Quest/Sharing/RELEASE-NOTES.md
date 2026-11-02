# AC:Unreal / AC:VR release 148

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.01.148**. Android version code: 148.
Quest installer revision: 9. These notes cover changes since v147.

## UCM Mobs and attack profiles

- Add a compact Mobs table for Default and per-mob attack profiles, with visible
  names, quick attack/debuff switches, damage and pet choices, and column tooltips.
- Expand Details to configure the full per-mob attack profile: matching, attack
  modes, debuffs, weapons, offhand, damage, pet damage, priority, and range.
- Keep table and Details edits synchronized and saved. New native rows inherit
  Default; imported profiles retain their independent settings.
- Support essence summoning without automatic attacks, pursuit, weapon changes,
  or combat assessment when attack families are disabled. Explicit Ignore rules
  still exclude mobs, and older imported VT summon-only settings are recognized.
- Consolidate duplicate controls, rename visible Monster labels to Mob, and show
  the selected profile name plainly. Keep mob names visible while scrolling;
  hide the horizontal scrollbar when the table fits the window.

## Backpack interactions

- Fix dragging portable packs directly from the world and from their open
  container icon into inventory.
- Correct pack movement between player inventory and chest pack slots, including
  slot capacity, insertion order, and drag feedback.
- Prevent invalid nested packs and containment cycles; retain server-authoritative
  ownership and contents during moves.

## Validation and packaging

Release verification covers UCM decisions, summoning, imports, profile editing,
responsive table/details layouts, backpack interactions, and updater behavior in
the editor and packaged Windows/Linux clients. Android packages are checked for
identity, version, signing continuity, 16 KiB alignment, and bundled plugins.
The Windows installer is checked upgrading v147 while preserving saved data.

Packages exclude retail DATs, personal settings, server data, and generated
MeshCache files. Android phones/tablets remain a preview using a separate APK
from Quest. No physical-device or live-server acceptance test was performed.
