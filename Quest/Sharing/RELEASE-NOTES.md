# AC:Unreal / AC:VR release 152

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.07.152**. Android version code: 152.
Quest installer revision: 9. These notes cover changes since v151.

## Spell inspection

- Show the correct component requirements when the matching school's foci is
  in a player backpack slot, or the character has its infused augmentation.
- Refresh an open spell inspection when foci ownership or augmentations change.
- Correct prismatic taper counts for formulas with multiple scarabs while
  retaining required scarabs and chorizite.

## Lua plugin APIs

- Add a native color picker with hex entry, color wheel, brightness and opacity.
- Add scale and X/Y offsets for plugin controls, including entire panel contents.
- Add custom nameplate text, backing and vital-bar colors, plus scale and offsets
  with independent VR placement overrides. The selected actor's tint is unchanged.
- Add item selection in owned packs and open chests/corpses, with current
  container membership checked before selecting; selection never uses or moves items.
- Fix clipping of plugin text when controls are scaled, moved or rotated.
- Expand the Lua API documentation and machine-readable catalog; include an
  optional Appearance Controls example demonstrating saved preferences.

## Validation and scope

Automated checks cover spell formulas and login ordering, plugin input/rendering,
saved preferences, container-selection packets, packaged clients, APK contents,
upgrade preservation and public download integrity. Results accompany the local
release artifacts. Quest and phone hardware were not acceptance-tested for v152.

The intermittent Levistras casting rejection with foci remains unconfirmed; the
inspection correction is not a claimed fix for that server-side warning.

Packages exclude retail DATs, personal settings, server data and generated
MeshCache files. Android phones/tablets remain a separate preview APK.
