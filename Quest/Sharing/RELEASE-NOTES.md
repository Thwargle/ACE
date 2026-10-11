# AC:Unreal / AC:VR release 108

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.11.108**. Android version code: 108. Quest installer revision: 7.

## Unattended Combat Manager

- Switch to the next live target promptly after a kill in melee, missile, and
  spell combat, while retaining vital-recovery priority.
- Keep checking health recovery after further damage. Confirm healing from
  action results rather than net health gain, and retry other eligible spells
  when one fails. Add configurable emergency stamina-to-health recovery.
- Support summon devices across levels using their actual requirements,
  cooldowns, charges, and available inventory items.
- Fill available side packs when the main pack is full. Pause only looting when
  no eligible container can accept an item; other activities keep running, and
  inventory changes allow looting to retry.
- Wait for server-confirmed combat stance before casting. Finish cancellation
  of an active melee/missile attack before changing stance or equipment, and
  correctly recognize equipped offhand ownership.
- Match route movement animation to actual movement input, including slow
  approaches and direction changes.

## Reusable profiles and Item Tools

- Split UCM setups into linked Character, Routes, Loot, Monsters, Metas, and
  Vendors JSON profiles. Reuse the same character or loot configuration across
  hunts, identify the active file on editing tabs, save independent copies,
  and reload linked files from disk.
- Migrate existing bundled setups with backups and collision-free names.
  Preserve imported Virindi files and reject conflicting stale edits.
- Add Item Tools for bulk giving, vendor buying/selling, profile-based item
  transfers, keyed chest collection/distribution, and per-character restocking.
  Include stack quantities, trade-note funding, eligibility checks, and
  server-confirmed transfers.

## Retail interface, combat, and movement

- Restore the Friend Name, Friend, and Status labels in the Friends panel.
- Open the spellbook, select, and reveal a newly learned spell, including in VR.
- Use the correct train-skill control without the inappropriate +10 option.
- Show wield-requirement failures and related transient notices in the retail
  top-center message area; align chat colors and combat wording with retail.
- Remember the selected NPC as the Give recipient when an inventory item is
  subsequently selected.
- Replay crafting/combine animations for successive new actions without
  requiring the character to move.
- Improve remote monster approach, retreat, and melee attachment prediction
  so creatures respond when the player moves inside their preferred distance.

## Plugin controls

- Minimize the plugin bar to a movable strip and respect the UI lock for moving
  it. Preserve its placement and collapsed state.
- Simplify the plugin manager and rename the options entry to Manage Plugins.
- Include the reusable local cosmetic service API for independently installed
  Windows plugins. **AC-WoW Transmog and all of its tools and model assets are
  excluded from this release.**

## Installation and validation

Windows, Linux, and Quest use shared sources and version checks. Release
validation covers UCM decisions and action synchronization, linked profiles,
Item Tools, retail messages and panels, movement, and plugin boundaries.
Linux and headset gameplay still require live acceptance; packaging is not
live certification.

Installing an update closes the game. Accounts, settings, and DAT files are
retained. Linux updates are downloaded and extracted manually. Game DAT files
and a game server are not included.
