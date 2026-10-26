# AC:Unreal / AC:VR release 138

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.25.138**. Android version code: 138. Quest installer revision: 9.

These notes cover all changes since **v137 / 2026.10.25.137**.

## Equipment and inventory reliability

- Fix client inventory state that could hide a weapon or other equipped item
  after unequipping until relogging. Physical detach and delayed public property
  updates no longer overwrite the player's ordered inventory receipts.
- Check bag capacity before unequipping. If the main backpack is full, use an
  available side pack; if no valid destination has room, keep the item equipped
  and explain why. A specifically selected full bag does not silently redirect
  the item elsewhere.
- Preserve early equipment receipts and wait for each item's own placement
  update when replacing equipment, instead of guessing where displaced items go.

## Simpler UCM route editing

- Creating a route immediately saves its file and selects it as the active route.
  Show the active filename, saved waypoint count, and autosave feedback.
- Make adding a waypoint easier to find, paginate long routes, and group less
  common steps and route behavior settings into expandable sections.

## Companion target plates

- Add independent plate and background opacity controls to the Lua world-plate
  API, with separate optional VR values. Text, vital numbers, bars, and icons can
  remain fully opaque without changing the selected creature's yellow tint.
- Update the target-plate example with four saved desktop/VR opacity sliders
  that companion authors can use in their own settings panels.
- Remove plates and their settings from Third Eye. Existing watch rules,
  alerts, and world markers remain; companion addons own their plate controls.

## Validation and distribution

- Expand regressions for full bags, delayed equip/unequip updates, early item
  receipts, route creation and selection, plate opacity, and Third Eye migration.
- Ship matching Windows, Linux, and Quest packages without personal settings,
  account data, server databases, or retail DAT files.
- Automated and packaged-client checks do not replace live-server reproduction,
  native Linux GPU testing, or physical Quest/PC VR acceptance. The original
  disappearing-item report lacked an item identifier or packet capture; these
  fixes cover the reproduced inventory-state failure paths.
