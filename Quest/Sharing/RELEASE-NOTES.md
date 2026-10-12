# AC:Unreal / AC:VR release 109

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.11.109**. Android version code: 109. Quest installer revision: 7.

## Lua plugin API 2

- Let companion plugins run alongside UCM, read chat independently, save state
  across logins, exchange messages, and register commands.
- Expose UCM's game snapshots and native action pipelines, reusable UCM and Item
  Tools policies, profile services, target selection, and navigation services.
- Add interactive plugin windows with tabs, sections, buttons, editable fields,
  sliders, lists, progress indicators, icons, and local PNG images.
- Let plugins place markers over world objects and on maps, and use the existing
  navigation arrow. Include API documentation and working quest-tracker,
  target-list, and API-workbench examples. Retain API 1 compatibility.

## UCM profiles and targeting

- Add New and Delete controls for independent Character, Route, Loot, Vendor,
  and Monster profiles. New routes start empty.
- Add New setup with Default profiles and Save current setup. Keep profile
  selection available on the setup page without duplicate copy controls.
- Import Virindi routes and loot files into named profiles while preserving
  original files. Remove the duplicate route dropdown.
- Rename Rules to Monsters, add the selected monster with default settings,
  and group its settings in a collapsible section.
- Move Protections and Banes to Buffs, and keep log access in the Log tab.
- Restore acquisition of eligible monsters outside missile/spell attack range
  but within target acquisition range, with approach and route-return handling.

## Item Tools and retail interface

- Handle partial NPC item acceptance and busy-recipient responses, refresh
  inventory after removals, and continue confirmed bulk transfers without
  stalling on items such as Brown Rat Tails.
- Restore quickslot selection borders and item cooldown indicators, including
  shared cooldowns and server-reported cooldown state.
- Correct chat-filter hit testing at different UI scales so category selections
  apply to the intended main or secondary chat window.
- Improve the shared local cosmetic host's fitting, previews, and catalog
  handling for independently installed plugins. **AC-WoW Transmog, its provider,
  converter, and model assets are excluded from this release.**

## Installation and validation

Windows, Linux, and Quest use shared sources and version checks. Release
validation covers plugin APIs, UCM policies and profiles, Item Tools, chat,
quickslots, and packaging boundaries. Linux and headset gameplay still require
live acceptance; packaging alone is not live certification.

Installing an update closes the game. Accounts, settings, and DAT files are
retained. Linux updates are downloaded and extracted manually. Game DAT files
and a game server are not included.
