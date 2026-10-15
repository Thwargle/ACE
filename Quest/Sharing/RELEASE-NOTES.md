# AC:Unreal / AC:VR release 116

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.14.116**. Android version code: 116. Quest installer revision: 9.

## UCM and navigation

- Selecting an imported meta enables its rules. Show the active state, waiting
  conditions, and linked profiles, with controls to restart or change state.
- Respect negative monster priorities as exclusions, and allow live settings
  changes to supersede earlier meta overrides. Preserve authored start conditions.
- Add zoom-in and zoom-out controls to the pinned waypoint map.

## Retail behavior and presentation

- Preserve loose items at their server-provided elevation on tables and platforms.
  Send hand/F pickup requests directly to the server and clear rejected transfers.
- Apply server-provided inventory capacity updates, including the extra-pack
  augmentation, and include Enlightenment bonuses in trained skill displays.
- Correct looping scenery animation at frame boundaries, including windmill blades.
- Align particle scheduling, stopping, attachment lifetime, distance checks,
  brightness, and trails with retail behavior. Remove blanket cast cleanup that
  could prematurely remove active effects such as Aetheria surges.

## Performance

- Share common native data within each batch of Lua observer callbacks while
  preserving separate chat cursors, saved state, and mutable Lua tables.
- Cache spellbook filtering and sorting until spells or filters change.
- Cache Hunt Ledger text and calculations, refreshing on gains or once per second.
- Add regression benchmarks and CPU profiling markers for these update paths.

## Installation and validation

- **AC-WoW Transmog, its provider, converter and model assets are excluded.**
- Updates preserve accounts, settings, profiles and DAT files. Linux updates
  are downloaded and extracted manually. Game DAT files and a server are not
  included.
- Automated checks cover the changed behavior. Controlled CPU benchmarks do not
  establish live FPS gains. Live Linux, headset, and server-session acceptance
  are not claimed by packaging or offline tests.
