# AC:Unreal / AC:VR release 121

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.16.121**. Android version code: 121. Quest installer revision: 9.

## Login and profiles

- Add an optional alias for each saved account. Separate account entries can use
  the same username with different passwords or aliases.
- Preserve each account's identity when editing or selecting it for a server.

## World and retail behavior

- Recover remote players and monsters whose server position corrections were
  blocked by dungeon walls, stairs, or floors, leaving their models in the wrong place.
- Allow keyboard targeting of eligible off-screen targets and targets in culled
  dungeon rooms throughout the existing targeting range.
- Apply the server's ground-placement pose when items are dropped, including
  shields and bracelets. Preserve the custom VR drop behavior.
- Match retail vendor category names and item groupings.
- Show installed backpacks and available backpack slots in external containers,
  including house chests.
- Match retail full-palette override precedence and appearance merging for
  clothing. Preserve server-supplied body-part and texture layering order.

## UCM and Item Tools

- Correct the icon and overlay values exposed to imported vTank loot rules, so
  rules can distinguish aetheria and cloak levels correctly, including custom icons.
- Allow transfer of bonded, non-attuned salvage and items across all packs;
  continue excluding attuned, retained, equipped, or offered items.
- Confirm vendor note proceeds against live inventory before restocking, so
  selling a trade note can continue into purchasing the missing supplies.

## Network recovery

- Bound ordered gameplay packet processing when delayed packets release a backlog,
  while keeping transport acknowledgements responsive and preserving packet order.
- Add receive/dispatch timing and backlog details to `/acueperf` diagnostics.
- Add simulated wasp-dungeon lag tests through 23 seconds of round-trip latency,
  plus dungeon visibility and blocked-position regression coverage.

## Compatibility and validation limits

- **AC-WoW Transmog, its provider, converter and model assets are excluded.**
- Updates preserve accounts, settings, profiles and DAT files. Linux updates are
  downloaded and extracted manually. Game DAT files and a server are not included.
- The repeatable Glenden Wood 1 FPS room and intermittent white polygons have
  not been reproduced end to end. The network changes address confirmed backlog
  processing behavior; this release does not claim every reported dungeon stall fixed.
- Automated checks and packaging do not establish physical-headset or live-event
  acceptance, or reproduce the unspecified live tailoring outfit.
