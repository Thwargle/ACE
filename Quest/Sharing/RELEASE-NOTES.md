# AC:Unreal / AC:VR release 127

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.20.127**. Android version code: 127. Quest installer revision: 9.

## Action Deck

- Add Action Deck as an optional bundled desktop plugin, with its own icon and
  Plugin Manager checkbox. Its movable hotbar has three rows of twelve slots.
- Drag inventory items, spellbook entries, or spell-bar icons onto the deck.
  Click a slot or press its shortcut to use the assigned action.
- Default shortcuts are 1 through = on the bottom row, Ctrl plus those keys on
  the middle row, and Shift plus those keys on the top row. The gear opens
  row-modifier and individual slot-key settings, including attack-height actions.
- Override reserves the deck's shortcuts ahead of native item and spell hotbars,
  including empty or unavailable slots. Typing retains priority. Settings pause
  action activation while keeping native shortcuts blocked.
- Lock the deck's position and hide its frame while keeping slots interactive.
  Save actions, shortcuts, and override state separately per character and server.
- Healing kits target the player unless another eligible player is selected.
  Depleted or missing items are dimmed until matching supplies return to inventory.
  Spell and attack availability follows the required combat stance.
- Use retail item underlays, enchantment effects, and composed spell icons without
  white mask frames.
- Show the moving icon above plugin windows when dragging into or within the deck.
  Move actions into empty slots or swap occupied slots while keys remain in place.
  Dropping outside the window removes the shortcut; dropping on its title,
  toolbar, or settings leaves it in place. Inventory items are unaffected.

## Validation and distribution

- Regression coverage exercises shortcut precedence through Slate and native
  input polling, healing targets, depletion/rebinding, persistence, and stance
  checks, plus rendered drag previews and pointer-driven rearrangement/removal.
- Action Deck is a desktop interface. The shared client remains available for
  PC VR and Quest; these packages do not add a world-space Action Deck interface.
- Automated and packaged checks do not replace live-server or physical-headset
  acceptance.
- Game DATs, server files, saved accounts/settings, and optional AC-WoW Transmog
  assets are excluded from client downloads.
