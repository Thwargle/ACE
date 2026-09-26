# Radar, targeting, vitals resizing, and panel shortcuts

## Changes

- Desktop radar and the fallback HUD enumerate server-known, radar-eligible objects by distance without applying camera/portal rendering visibility. The native VR compass no longer discards nearby objects solely because they belong to another landblock. Server radar flags, range limits, hidden objects, and inventory/equipment exclusions still apply.
- Selecting a radar object is independent of whether its actor is rendered. Portal visibility changes no longer clear the selected object. Actor culling and world picking remain intact.
- Fellowship membership overrides normal player/PK colors with retail bright green for radar blips, selection corners, and the offscreen direction arrow. Both the leader and ordinary members use green.
- Monster cycling requires an attackable creature. Doors, signs, corpses, portals, and other non-creatures remain available to item selection, without entering the monster cycle.
- Both desktop vitals arrangements use the DAT's horizontal resize grips and width limits while retaining their fixed retail heights. Child artwork and labels reflow with the frame. Unlock the UI and drag either side edge to resize.
- Attributes explicitly opens the Attributes tab. Skills, Spellbook, Components, Allegiance, and Fellowship explicitly select their own tabs. A shortcut to a different tab in the same panel switches tabs; repeating that tab's shortcut closes it. Default and rebound actions share the same dispatch path.

## Retail references

Source root: `ThirdParty/acclient-AI-RE/2013-09 11.4186/acclient-src/src`.

- `GAME/game_ui_misc/gmRadarUI.c`: `DrawObjects` uses player-relative planar distance; `GetBlipColor` applies fellowship color overrides.
- `GAME/game_ui_misc/VividTargetInd.c`: `SetSelected` obtains the indicator color from the radar color resolver; fellowship leader/member colors map to BrightGreen.
- `AC/accui_misc/PlayerSystem.c`: `SelectNext` separates monster, item, player, and compass selection categories.
- `AC/accui_misc/CombatSystem.c`: `ObjectIsAttackable` requires the creature item-type bit before checking attack permissions.
- Resolved retail layout `Plugins/ACEClient/Docs/UI/Resolved/0x21000005.json`: stacked vitals allow width 160–610 at height 58; horizontal vitals allow width 360–3000 at height 26. Left/right resize flags, meter edge anchors, and panel page identities come from this layout.

## Validation

Artifacts: `Unreal/Saved/ReleaseValidation/sep26-radar-input`.

- Windows editor compilation: `Unreal/Saved/Logs/sep26-radar-input-build.log`.
- Android ARM64 Development compilation: `android-build.log`.
- `Report1`: KeyboardBindings, UIInteractions, UILayoutCommands, VR.GesturesAndSettings, and VR.RigAndMenus passed. The initial UIScreens run identified test fixture issues addressed in the follow-up run.
- `InteriorReport`: InteriorStreaming passed, including selection of a culled room's object while its actor remains hidden and non-colliding.
- `Report3`: UIScreens passed. Coverage includes range-based radar visibility, fellowship colors, creature-only cycling, default/rebound panel shortcuts, and actual vitals edge drags at 100%/125% rendering scale. The rendered stacked and horizontal resized-vitals screenshots were visually checked.
- Quest shared-source synchronization and `git diff --check` passed.

This is development validation. No public release, updater manifest, website package, or installed headset build was changed.

Subsequent packaging and publication are documented in
[CHECKIN-2026-09-26-v80.md](CHECKIN-2026-09-26-v80.md).
