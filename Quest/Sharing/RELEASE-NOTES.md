# AC:Unreal / AC:VR release 114

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.13.114**. Android version code: 114. Quest installer revision: 9.

## UCM buffing and metas

- Refine automatic self buffs, trained-skill selection, spell-family exclusions,
  and buff-other requests to follow vTank's buffing policy more closely.
- Respect active enchantments and confirmed item-buff timers across login. New
  profiles rebuff five minutes before expiry and group nearby expirations;
  existing profiles retain their configured refresh threshold. Avoid immediate
  duplicate casts when result messages arrive before enchantment updates.
- Preserve unfinished casting transitions during fast buffing and subsequent
  casts. Fix instruction-budget failures when buffing transitions into combat
  with a large learned spellbook.
- Add Import All and bounded, scrollable, clearable import results. Reimporting
  the same source reuses its named profiles and preserves native edits instead
  of producing numbered duplicates. Remove anonymous Imported selections.
- Finish meta profile loads before executing subsequent commands, including
  nested action lists, so navigation, loot and character settings stay in order.
- Selecting a navigation file, including one already imported, enables route
  navigation for the next Start without starting movement immediately.

## Plugin and game interface

- Keep Lua plugin controls alive during live updates: open dropdowns, text-entry
  drafts, focus and collapsed sections survive changes elsewhere in the window.
  Plugin displays continue updating during mouse look.
- Remember which plugin windows were open or closed across login. Add a shared
  option to keep game and plugin windows on screen or allow offscreen placement.
- Add horizontal or vertical plugin-bar orientation, saved across logins.
- Reorganize UCM's buffing, combat, spellcasting, melee-hand and recovery sections,
  standardize Enable labels and initial expansion states, and match section
  styling to the native window theme.
- Make /fps toggle the meter and its options setting together.

## Housing and item animation

- Correct housing-tab and house-crystal purchase/maintenance layouts and text
  sizing. Accept eligible payments from dragged bags, Pyreals and trade notes,
  with matching drag feedback and reconciled inventory handling.
- Restore hooked-item animation and materials, including floating candles and
  chalkboards. Play destination animation cycles after activation transitions,
  including the house crystal's independently rotating parts.
- Avoid replaying animation hooks on stationary poses, and preserve casting
  sequence transitions when movement interrupts a held animation.

## Diagnostics and installation

- Use the /acue prefix for client-specific commands: /acuehelp, /acueperf,
  /acueobject, /acueplugins, /acueplugin, /acueucm and /acueitems. Previous
  spellings remain compatibility aliases; retail and server commands are unchanged.
- Add bounded frame-performance reports and a crowded-wasp regression scene.
  The reported live-server wasp slowdown is still under investigation; these
  diagnostics are not a confirmed fix for that stall.
- **AC-WoW Transmog, its provider, converter and model assets are excluded.**
- Updates preserve accounts, settings, profiles and DAT files. Linux updates
  are downloaded and extracted manually. Game DAT files and a server are not
  included. Live-server visual parity and headset acceptance remain separate
  from automated build and regression checks.
