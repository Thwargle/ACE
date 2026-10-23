# AC:Unreal / AC:VR release 131

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.22.131**. Android version code: 131. Quest installer revision: 9.

Changes below are relative to **v130 / 2026.10.22.130**. These expanded notes
replace the initial condensed v131 changelog.

## Network movement, physics, and object placement

- Correct remote-player forward and strafe prediction so a faster sidestep does
  not accelerate the forward animation and repeatedly pull the player backward
  when server positions arrive. The forward movement state keeps its animation
  while sidestepping contributes its own movement.
- Preserve the last received run rate through walking, turning, stopping, and
  death. Position corrections use that remembered rate instead of changing speed
  when the current animation changes.
- Replace the custom two-second prediction limit with retail's accumulated-travel
  constraints, including different indoor and outdoor distances. This avoids
  prematurely stopping slow movers and handles changes of direction correctly.
- Reset movement constraints on accepted server position updates, rather than
  treating appearance refreshes as new movement updates. These changes use the
  shared movement path for remote players, NPCs, and creatures.
- Correct step-up height, upper-body clearance, and foot support checks for
  raised surfaces. Players can step onto the Academy table to reach its books,
  matching the reported retail behavior without increasing pickup range or
  adding an exception for those books.
- Wait for DAT setup and collision-body data before spawning world entities,
  preventing dropped objects from being created without the physics needed to
  settle onto a surface.
- Wait for terrain, buildings, furniture, and their collision data before
  applying gravity to props. This addresses statues falling through their
  pedestals and objects remaining incorrectly placed after login or streaming.

## Rendering, lighting, and performance

- Refresh transforms and bounds when cached character parts become visible
  again. This fixes an idle character appearing split into separated body parts
  after a visibility or position change until movement refreshed the model.
- Avoid rebuilding collision for visual-only clothing, body parts, and model
  detail changes. This reduces unnecessary work while characters load at login.
- Make Texture Detail affect runtime DAT world textures and terrain: Low uses
  quarter-resolution textures, Medium uses half resolution, and High or above
  preserves full DAT detail. Interface artwork remains sharp.
- Apply texture-quality changes to already loaded terrain, objects, and effects
  without restarting or logging in again. Retain compressed source mip data so
  increasing quality can restore the higher-resolution textures.
- Increase the reach and color strength of portal illumination so portals cast
  more visible light into the surrounding room.
- Add a saved **Effect light limit** slider from **0 to 64**, default **8**, for
  simultaneous desktop weapon, portal, and spell lights. Higher limits reduce
  light switching in crowds; lower limits reduce GPU work. Dynamic lights Off
  disables these lights, and Low caps the limit at two. Save with Apply.
- Make **Custom object highlighting** apply and save immediately. Turning it off
  removes the custom glow from selected and hovered objects, including the
  player's own character, in desktop and VR.
- Avoid rebuilding the cursor's brush and layout every frame when its artwork,
  size, and hotspot have not changed.

## UCM buffing and combat

- Fix the item-enchantment buff loop when switching from a casting wand back to
  a weapon and shield. Re-equipping the shield no longer invalidates all existing
  worn-item buff timers and sends UCM back through the same buffs indefinitely.
- Track equipment that still needs a buff separately from equipment already
  covered, retaining that information while an item is temporarily unequipped.
  Buff only the uncovered equipment instead of repeating the full armor cycle.
- Add **Item enchantment buffs** to UCM's Buffs settings. Disabling it excludes
  item-magic buffs from automatic choices, manually selected spells, and buff
  items, and immediately invalidates the previous buff plan.
- Switching spell tabs during casting no longer reapplies the combat stance and
  interrupts the spell already in progress.
- Changing the selected target during melee or ranged auto-attack cancels the
  previous target's repeat and redirects attacks to the new valid target. Ranged
  retargeting waits through reload/aim and the cancellation acknowledgement,
  preserving attack height and power instead of firing at the previous monster.

## Action Deck and Item Tools

- Allow the main backpack to be dragged onto Action Deck as a persistent
  **Target self** shortcut. The binding survives login and is usable in any
  combat stance.
- Activating that backpack shortcut selects the player, or completes an item
  already waiting for a target on the player without replacing the combat target.
- Clicking or pressing the hotkey for a healing kit uses it on the player unless
  another valid friendly player is selected. Respect target compatibility and
  player-killer status when choosing another recipient.
- Let unassigned Action Deck hotkeys fall through to the native hotbars, even
  with Override enabled. Assigned slots that are depleted or temporarily unusable
  still reserve their keys so they cannot trigger a different native action.
- Treat invalid saved Action Deck action types as empty slots rather than
  converting them into another action.
- Move Item Tools' **# / # items given** or **confirmed actions** count onto its
  own line above the changing hand-in status, keeping the count stable and readable.

## Retail cursors, tooltips, and interaction

- Restore retail cursor artwork for combat, spellcasting, Use, Examine, and
  using an item on a target, including the valid and invalid target states.
- Show the retail busy/hourglass cursor while waiting for use, spell, shopping,
  attack, manual appraisal, or allegiance requests. Clear it on the matching
  response, cancellation, disconnect, or timeout; background plugin appraisals
  do not make the cursor busy.
- Pressing Use or Examine with no selected object now enters the corresponding
  target mode. Clicking a world object, inventory item, or equipped item performs
  that action; Examine can also inspect a spell. Escape cancels targeting.
- Keep inventory and plugin hover text clear of the full enlarged cursor,
  including cursor changes while hovering, software-cursor scaling, and tooltips
  repositioned at monitor edges.
- End the selection toolbar's bright green flash after its retail quarter-second
  interval. Return to the normal or stacked-item background even when the object
  has no health or mana bar to replace the highlight.
- Give an open vendor category dropdown priority over scrollbars and item controls
  behind it. Clicking outside dismisses the dropdown without activating the
  underlying control.
- Allow packs on the ground to be opened with Use/double-click, while the explicit
  pickup action can still collect them. Ordinary loose items retain pickup on Use.

## Display settings and FPS overlay

- Use the current monitor's native resolution when moving from windowed mode to
  fullscreen unless another resolution was explicitly chosen. Borderless uses
  the monitor resolution, and unusably small requested modes receive a safe fallback.
- Add a centered **Keep / Revert** confirmation for resolution and window-mode
  changes, including Alt+Enter and F11. Enter keeps the change; Escape or a
  **15-second timeout** restores the previous mode and resolution.
- Avoid permanently moving saved UI windows during an unconfirmed display change.
- Keep Options reachable after a resolution change: reopening it brings the panel
  onto the screen even if off-screen windows are normally allowed. Below 800x600,
  temporarily scale the interface to fit without overwriting the saved UI scale.
- Add **Reset graphics** to both the desktop login screen and the in-game Options
  menu. Restore a usable windowed resolution and default graphics, brightness,
  contrast, field of view, UI scale, cursor scale, and light settings.
- Update individual graphics controls when selecting a quality preset, and honor
  subsequent individual changes when Apply is pressed.
- Make the FPS overlay draggable when the normal UI lock is disabled. Save its
  position, restore it on later launches, and keep it within the viewport when
  window constraints are enabled. A locked overlay lets clicks pass through.
- Temporarily reposition the FPS overlay for smaller viewports without replacing
  its saved position, so it can return to the chosen location at the normal size.

## Character selection and logout

- Automatically select a newly created character in the roster instead of
  reverting to the first name. Preserve the preferred selection when the list
  refreshes in both character-selection interfaces.
- Keep the server-driven logout motion visible before entering portal space,
  including the longer player-killer delay, then play the portal transition and
  audio while waiting for the server's character roster.
- Preserve an existing portal view when logging out from portal space, and stop
  movement and pending interactions during logout. Do not start destination
  streaming, briefly reveal the old world, or send an arrival notification as
  part of leaving the world.
- Keep incoming impact effects visible during the initial logout delay, retiring
  them when the portal view actually begins.

## Validation scope

- Windows desktop/PC VR, native Linux, and Quest packages use the same release
  version. Refresh the packaged runtime materials and verify download hashes.
- Release validation passed 11 editor, 23 packaged Windows, 17 packaged Linux,
  and one Linux SDL check. Regression coverage includes movement rates and
  constraints, tabletop movement, spawn support, idle body transforms, buff
  handoffs, combat retargeting, Action Deck, cursors, display recovery, texture
  quality, FPS placement, vendor input priority, and logout transitions.
- Automated checks use retail DATs and protocol/physics fixtures. They do not
  certify every live-server interaction or complete retail parity.
- Linux validation uses WSL with software Vulkan; physical Linux GPU and Quest
  headset gameplay are not certified by this release run.
- Downloads exclude game DATs, server files, saved accounts/settings, and optional
  transmog assets.
