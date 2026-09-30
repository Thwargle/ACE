# AC:Unreal / AC:VR release 87

Windows desktop, PC VR, and native Quest: **2026.09.30.87**.
Release number / Android version code: 87. Quest installer revision: 6.

## Changes since public release v86

### VR inventory and gameplay menus

- Add a controller-friendly gameplay interface with a mirrored, animated player
  model that faces the viewer. Native panels support moving, rotating, changing
  distance, resizing, and saving placement through the existing panel controls.
- Show the entire selected pack in a stable grid instead of Previous/Next pages.
  Center item icons in their slots and use pack icons along the right edge.
- Support dragging items between slots and packs, merging or splitting selected
  quantities, equipping in compatible slots, and dropping into the world. Fix
  full-stack drops incorrectly requesting a split.
- Keep Use / Equip, Inspect, Give, Drop, and targeted item use together. Clear
  hidden item context when changing pages while preserving unrelated world targets.
- Arrange equipment slots by body location. Separate Attributes and Skills, with
  +1/+10 training controls, current XP costs, and skill-credit requirements.
- Show spell icons, descriptions, explicit hotbar tabs, and add/remove/reorder
  actions. Newly added spells append after the last spell rather than inserting
  at the front of the bar.
- Fix fellowship creation and provide member selection, recruitment, leadership,
  dismissal, openness, leaving, disbanding, and fellowship preference controls.
- Clarify vendor buying/selling lists with item icons, offered quantities, prices,
  removal controls, and the same inventory slot and pack styling.
- Resolve native-menu pointer hit testing, overlapping HUD input, and inventory
  icon composition. Update inventory views on data changes rather than unrelated
  world-object updates. Distinguish combat pointer feedback from normal pointing.
- Resolve self-targeted spells before pointer selection so caster-only buffs do
  not require selecting the player.

### Desktop controls and retail interactions

- Add rebindable Show / hide interface, defaulting to Alt+Z. Keep camera zoom
  working while the interface is hidden.
- Allow mouse-wheel directions to be rebound to other actions instead of zoom;
  keep zoom actions bindable and retain imported keymap filenames across restarts.
- Preserve mouse-look and held movement when using targeting hotkeys. Add an
  optional toggle mode for mouse-look and confine the cursor in fullscreen modes.
- Exclude player-owned summons from monster-targeting cycles and correct UI-tab
  action routing.
- Honor the current/previous recipient when giving items, prompting for a target
  only when needed. Support removing individual entries from the vendor sell list.
- Recover rejected world-object use transactions instead of leaving a stale busy
  state. Use the shared combat-to-interaction handling for doors, NPCs, and portals.
- Apply retail tooltip templates, colors, and fonts; correct spellbook inspection
  sizing and suppress duplicate inspection scrollbars.
- Prevent overhead map view indoors and use the retail camera collision radius.

### Custom servers, movement, and crowded areas

- Accept server-authored object scales and fresh custom NPC descriptions, including
  a defeated creature replaced by a usable reward NPC under the same identifier.
- Build item examination text from server appraisal properties and the player's
  installed DAT content. Respect DAT vital formulas, gear health bonuses, and
  enlightenment when updating shared desktop/VR vitals.
- Match retail's most-recent opposing movement command behavior, backward movement
  and turn rates, and movement interruption of the local casting animation. Keep
  the corresponding movement and action packets consistent with server behavior.
- Batch repeated creature-overlap and floor queries in crowded scenes. Retain wall,
  stair, and creature collision checks while reducing repeated physics work.
- Add regressions using low carenzi, medium gromnies, and elevated wasps, including
  mixed crowds, wall contacts, stairs, and the reported dungeon location.

## Updating

Use **Updates** in the launcher or download the current installer from the site.
Existing supported updaters detect release 87. Accounts, settings, and retail DAT
files are retained; do not uninstall first. **Installation closes the game.**
On Quest, confirm Android's installation prompt, wait for completion, then reopen
AC:VR from the library. Optional automatic updating remains off by default.

To reposition a VR panel, unlock it and hold **Move** or **Resize**. While holding
Move, the right thumbstick pushes the panel away or brings it closer.

No retail DAT files, saved accounts, or game-server configuration are bundled.
No new game-server deployment is required.

## Validation scope

Automated coverage exercises controller clicks and drags, mirror orientation,
inventory and fellowship packets, spellbar placement, XP costs, desktop input,
custom object handling, and varied creature collisions using actual DAT geometry.
Crowd-query benchmarks are synthetic; no new measured headset FPS gain is claimed.
Live headset comfort and multiplayer/server acceptance remain separate checks.
