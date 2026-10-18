# AC:Unreal / AC:VR release 123

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.18.123**. Android version code: 123. Quest installer revision: 9.

## Auto Craft

- Add a separate optional Auto Craft plugin with its own icon and Plugin Manager checkbox.
- Search categories and recipes, choose a quantity or Max available, and optionally
  make missing intermediate ingredients from materials in the character's packs.
- Show available inventory, missing materials and tools, batch quantities and
  space requirements before starting. Save selections per character and server.
- Confirm inventory changes before advancing; support pause/resume, stopping,
  stack preparation and safe coordination with UCM and other inventory automation.
- Include 11,199 production recipe pairs covering 4,744 output item IDs from the
  standard ACE World database. Server-specific recipes and item-modification-only
  actions such as tailoring are outside this manufacturing catalog.

## Inventory and movement

- Preserve dense backpack order when equipping and dequipping, including early
  weapon attachment packets. Do not invent a main-pack move during visual detach.
- Resolve drag destinations against current item IDs after transfers change the
  inventory. Keep partial-stack, cross-pack and empty-cell drops consistent.
- Keep inventory slot counts consistent with the displayed list while equips are pending.
- Correct grounded slope recovery so turning or casting does not introduce
  sideways movement down walkable slopes.

## Rendering, plugins and updates

- Avoid stale GPU occlusion results hiding dungeon room shells as the camera
  moves through doorways; retain current-frame portal visibility checks.
- Parse queued resting motions in initial object descriptions so decorative
  bodies can retain their authored dead, sleeping or sitting poses.
- Honor plugin text-size overrides and add separators to vendor category rows.
- Discover optional VR protocol support through the standard ping action, avoiding
  unhandled 0xF7D0 warnings on ordinary ACE servers. Servers offering the optional
  VR extension need the accompanying updated ping handler.
- Restart the Windows client visibly after updates in desktop and VR modes;
  keep the installer itself unattended.

## Validation limits

- Automated tests cover inventory order, drag placement, crafting, slope contact,
  dungeon visibility, initial resting poses, plugin UI and updater restart behavior.
- The exact custom-server Dead Adventurer selection report remains unresolved;
  added object diagnostics support further investigation. Not every selectable
  decoration or reported ghost-process crash is claimed fixed by this release.
- Live-server and physical-headset acceptance remain separate from build and
  automated-test success. No new sustained Quest FPS claim is made.
- Updates preserve saved accounts, profiles, settings and DAT files. Game DATs,
  server files and optional AC-WoW Transmog assets are excluded from client packages.
