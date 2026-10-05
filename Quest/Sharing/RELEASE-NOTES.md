# AC:Unreal / AC:VR release 94

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.05.94**. Android version code: 94. Quest installer revision: 7.

## Plugins and UCM

- Added client plugin management at login and in game, plus a desktop plugin bar
  for showing and hiding individual windows.
- Added **Unattended Combat Manager (UCM)** with automatic skill-aware buffs,
  Force Buff, configurable buff requests from other players, combat equipment
  selection, health/stamina/mana recovery thresholds, routes, metas and loot rules.
- Added route/profile discovery and supported Virindi file conversion. Imported
  commands use UCM equivalents; conversion preserves the original files and
  reports unsupported behavior or missing dependencies.
- Routes display connected world-space lines and waypoint markers. Starting or
  changing a route joins its nearest eligible waypoint, independent of camera
  direction, then follows the route's loop or reverse settings.
- Improved obstruction checks, attack ranges and route door handling. Added the
  optional **Peace mode when idle** setting, off by default.
- Fixed repeated casting-tool swaps, redundant item buffs, Force Buff instruction
  exhaustion, and loot-rule memory/work-budget failures.
- Fixed active loot-profile persistence, stale container state and corpse-owner
  checks that prevented approaching/opening corpses. Looting preserves combat
  stance. The loot editor shortcut now clearly identifies itself as an editor.

## Waypoint and maps

- Added clickable chat coordinates, a movable direction arrow, distance display,
  and manual destinations through the Waypoint plugin.
- Added zoomable terrain tiles, live nearby portal markers and dungeon outlines.
  Maps support north-up or player-facing orientation, and a transparent pinned
  dungeon view centered on the player. Arrow and pinned-map placement persist.

## Gameplay and interface

- Improved distant monster selection, automatic combat targeting, and selection
  marker colors for doors, signs and other world objects.
- Held melee/missile attacks restart their charge after movement instead of
  losing the held request. Jump landing movement includes authored recovery motion.
- Added rebindable desktop controller inputs. Improved camera adjustment speed,
  mouselook through portals and cursor-position restoration.
- Improved collision sliding around creatures and camera behavior inside
  buildings with floors below the outdoor terrain.
- Expanded item examination formatting, including bow modifiers, shield details
  and spell ingredients; corrected spell-description sizing.
- Corrected character-creation skill icons that repeated inside their slots.
  Improved Game Play option spacing and link-quality warning colors.
- VR inventory allows stick movement while open and repositions on reopening
  unless pinned. Improved custom-object updates and local particle presentation.
- Reduced repeated visibility, object-update, UI, plugin and map work. Snow-effect
  setup data is reused to reduce repeated loading work.

## Compatibility and validation

UCM uses ordinary client/server actions. Decal and UtilityBelt DLLs cannot run
unchanged in this client. Virindi import coverage is substantial but incomplete:
unsupported files remain explicit failures. Recorded routes do not calculate
new paths around arbitrary walls. No full unattended-run parity or new live
Quest/Linux performance measurement is claimed for this release.

## Updating

Use the client updater or download the matching platform package from the
website. Installing an update closes the game; on Quest this is expected while
Android installs the replacement. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually.
