# AC:Unreal / AC:VR release 112

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.12.112**. Android version code: 112. Quest installer revision: 9.

## UCM

- Preserve equipment buff timers across login so restarting UCM can honor buffs
  that are still active, with invalidation when equipment or ownership changes.
- Improve target handoff after a kill without applying the previous target's
  retry delay to the next target.
- Correct automatic melee attack heights, enable automatic attack power by
  default, and use monster elemental preferences when choosing weapons.
- Pause automated inventory actions around manual item drags and equipment
  changes to prevent conflicting moves and items disappearing until relog.
- Improve scroll-learning confirmation and retry handling, including eligible
  unread scrolls already in inventory. Wait for Peace mode before reading.

## Movement and rendering

- Reduce catches and stalls at indoor/outdoor boundaries, including bridge
  underpasses and building entrances. Reuse collision bodies and render state
  instead of repeatedly rebuilding them during visibility transitions.
- Keep scenery rendering stable when terrain visibility changes. These fixes
  apply globally and have regression coverage on Windows and native Linux.
- Restore held scroll-reading animation behavior and stop existing movement
  when reading starts; a fresh movement input can interrupt the action.

## Inventory and interface

- Dragging the main pack or a backpack onto a closed vendor opens the shop and
  adds eligible contents to Sell. Offers wait for the matching vendor reply.
- The F key can fill a stack to its maximum and leave the remainder in its
  original pack. Combined-stack selection follows confirmed server updates.
- Remove unrelated waypoint-coordinate notices and empty footer space from
  default plugin windows.
- Add Hunt Ledger's estimated time to the next level below the totals, using
  the current run's XP rate and remaining level XP.
- Keep Supply and Hunt Ledger contents in place when locking hides their
  headers or Quick-show restores them, including near the screen edge.

## Installation and validation

- **AC-WoW Transmog, its provider, converter, and model assets are excluded.**
- Automated checks cover the changed UCM, inventory, UI, movement, rendering,
  and packaging paths. Native Linux transition regressions were exercised;
  live-server gameplay and headset acceptance remain separate checks.
- Updates preserve accounts, settings, profiles, and DAT files. Linux updates
  are downloaded and extracted manually. Game DAT files and a server are not
  included.
