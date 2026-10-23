# AC:Unreal / AC:VR release 131

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.22.131**. Android version code: 131. Quest installer revision: 9.

## Movement and rendering

- Correct remote-player prediction for independent forward/strafe rates,
  remembered run speed, and retail travel constraints to reduce rubber-banding.
- Stabilize object placement while terrain and building support load, including
  statues and dropped items. Reduce repeated work during world entry.
- Keep stationary character body parts aligned when their world transform changes.
- Honor texture-quality settings for runtime DAT textures and terrain.
- Improve portal illumination and expose the effect-light budget in video settings.
- Add a saved option to disable custom object highlighting.

## Combat and Action Deck

- Prevent repeated UCM item-enchantment buffs and respect the item-buff setting
  when returning from buffing to combat equipment.
- Preserve an in-progress cast when changing spell tabs, and redirect ranged
  auto-attacks when the selected target changes.
- Allow the main backpack on Action Deck to select the player.
- Use healing kits on the player when no valid friendly target is selected.
- Let unassigned Action Deck keys fall through to the native hotbars.

## Interface and interaction

- Keep vendor category clicks ahead of scrollbars and controls behind the menu.
- Allow ground packs to be opened through Use and picked up with the pickup key.
- Put Item Tools' item count on a separate line above the changing hand-in status.
- Add movable, lockable FPS display controls and retail busy/targeting cursors.
- Keep hover text clear of enlarged cursors and restore the selection toolbar's
  normal background after its temporary highlight.
- Improve fullscreen resolution transitions, small-screen options access, and
  graphics recovery from the launcher.
- Select newly created characters and preserve the logout transition before
  returning to character selection.

## Validation scope

- Automated checks use retail DATs and protocol/physics fixtures. They do not
  certify every live-server interaction or complete retail parity.
- Linux validation uses WSL with software Vulkan; physical Linux GPU and Quest
  headset gameplay are not certified by this release run.
- Downloads exclude game DATs, server files, saved accounts/settings, and optional
  transmog assets.
