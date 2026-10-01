# AC:Unreal / AC:VR release 89

Windows desktop, PC VR, native Linux desktop, and standalone Quest:
**2026.10.01.89**. Android version code: 89. Quest installer revision: 6.

## Changes since public release v88

### Movement and interaction

- Correct creature-contact sliding at large world coordinates so legal movement
  alongside a creature is not repeatedly rejected. Preserve collision against
  walls and the creature's authored body size.
- Match retail interaction distance in three dimensions, including slopes and
  target body dimensions. Improve automatic approaches to corpses and objects,
  retain the server's use transaction, and respect its charge-distance limits
  and error messages.
- Add negotiated equal-speed VR stick movement while keeping the avatar facing
  the player's head direction. Forward, backward, sideways, and diagonal inputs
  use consistent movement speeds on compatible servers. Replicate that movement
  through standard motion updates so desktop and VR observers see the same
  motion. Servers without this capability retain their existing movement rules.

### Camera and custom world content

- Preserve the selected field of view in overhead sky mode while correcting
  scenery, particle, and FOV-based detail culling at high camera distances.
- Honor server-provided hidden-from-UI flags during hover, selection, and live
  property changes. Keep gravity-free props at their server-authored height and
  respect placement frames and object scale instead of forcing them to ground.
- Expand shared inspection details from server properties, including healing
  tools, consumables, containers, books, locks, and selling restrictions.

### Inventory, salvage, and spells

- Correct material-prefixed item names across inspection, inventory, salvage,
  and VR labels. Display salvage-bag progress rather than a permanently full bag.
- Restore retail slot backgrounds in the salvage list.
- Correct spell school and level filters, use retail display ordering, and add
  clear-filter controls and useful empty-result messages in the VR spellbook.

### VR interface

- Add a movable, lockable, resizable native chat window. The left grip button
  toggles it by default and can be rebound. Place chat beside the vendor interface
  during vendor interactions.
- Keep VR windows in front of world particles and lighting, and improve window
  interaction focus. Correct the brief black flash when submitting text through
  the VR keyboard.
- Move equipment navigation to a player icon above the pack icons. Use the bright
  retail item-selection frame, centered icons, and simpler attribute/skill labels.
- Match desktop inspection content, retail text colors, wrapping, and inscription
  styling in the VR inspection panel.
- Add a native VR jump-charge display instead of showing the desktop jump bar.

### Linux desktop

- Correct pointer coordinate and confinement bounds so the top and left portions
  of the game window remain reachable across windowed and fullscreen modes.

## Updating

Windows and Quest users can use **Updates** in the launcher or download from the
site. Existing supported updaters detect release 89. Accounts, settings, and DAT
files are retained; do not uninstall first. **Installation closes the game.**
On Quest, confirm Android's installation prompt, wait for completion, then reopen
AC:VR. Optional automatic updating remains off by default.

Linux users should close the game, extract the new archive into a new folder,
and run its `AC-Unreal.sh`. Linux updates use manual extraction. See
`README-LINUX.txt` for requirements and setup.

Equal-speed VR movement requires the corresponding server capability. Existing
servers remain compatible and keep their established movement behavior until
updated. No retail DAT files, saved accounts, or game-server configuration are
bundled in the client downloads.

## Validation scope

Source regression coverage includes creature sliding, swarm/wall contact, stairs,
slopes, interaction approaches, camera behavior, custom object properties,
inspection presentation, salvage, spell filtering, pointer bounds, VR menus,
chat, and negotiated movement. Repeated-contact tests cover low, medium, and high
creature bodies, including Carenzi, Gromnies, and Phyntos Wasps.

Controlled tests do not certify every custom-server object or live multiplayer
condition. The Daralet crystal/pedestal example, live headset comfort, and Linux
desktop/GPU behavior still require acceptance on the affected hardware and server.
