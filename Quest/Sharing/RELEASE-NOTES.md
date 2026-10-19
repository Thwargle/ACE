# AC:Unreal / AC:VR release 125

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.19.125**. Android version code: 125. Quest installer revision: 9.

## Weapon lighting

- Give both locally held weapons priority within the existing effect-light budget,
  keeping nearby scenery and camera position from suppressing offhand illumination.
- Ignore black, non-emitting overlays when estimating particle light. Preserve the
  hue of small spark effects without dimming illumination a second time because
  their textures contain dark padding, including the Quintessence Sickle effect.
- Keep the existing desktop/mobile light limits and overlapping-light intensity cap.

## Armor distance rendering

- Use the retail DAT's authored model levels and distance thresholds for character
  armor, including per-view distance, object scale, and billboard draw modes.
- Preserve clothing layers, dyes, texture replacements, and the highest-detail
  collision geometry as other characters change visual detail with distance.
- Keep the local player and character previews at highest detail. No item-specific
  Celdon override is used.

## Accounts

- Drag the six-dot grip at the left of an Accounts row to reorder saved accounts.
  An insertion marker shows the destination; long lists scroll near their edges.
- Save the order automatically and use it on Play. Preserve account identity,
  aliases, passwords, selection, and unfinished form edits, including duplicate usernames.
- Remove the example placeholder from the optional account alias input.

## Validation and limits

- Regression coverage includes dual-wield lighting, light budgets, authored armor
  tables and rendered distance comparisons, clothing layers, and account drag/save behavior.
- The exact reported Celdon close-up mismatch was not reproduced; its existing
  close geometry already matched the retail DAT. This release adds general
  authored distance-level handling rather than claiming that specific case was reproduced.
- Packaging and automated tests do not establish live-server or physical-headset
  acceptance. No new sustained Quest frame-rate claim is made.
- Game DATs, server files, saved accounts/settings, and optional AC-WoW Transmog
  assets are excluded from client downloads.
