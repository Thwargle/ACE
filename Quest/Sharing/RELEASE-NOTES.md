# AC:Unreal / AC:VR release 137

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.25.137**. Android version code: 137. Quest installer revision: 9.

These notes cover all changes since **v136 / 2026.10.25.136**.

## New optional plugins and target plates

- Add **Dereth Foundry**, a Lua content workshop for server administrators,
  disabled by default. Seven tabs cover items, placement, landblocks,
  quests/recipes, generators/encounters, local content files, and help.
- Foundry uses the shared plugin styling and a new anvil icon. Persistent or
  destructive changes require confirmation. Existing server permissions still
  apply; local files are not automatically uploaded to a remote server.
- Add **Dereth Lens**, an optional Lua terrain overlay for impassable slopes,
  deep-water landblocks, radar boundaries, and dungeon illumination. Walkable
  slopes and wadeable coastal water remain uncolored. It provides terrain
  guidance, not a guarantee about bridges, dynamic obstacles, or server rules.
- Add per-entry target plates to **Third Eye**, with known role, level, vitals,
  and timers for the player's confirmed debuffs. Track eligible off-selection
  targets with bounded background assessment and health requests. Unknown
  server data stays hidden; other players' debuffs and early dispels are not inferred.
- Extend the Lua API with terrain overlays, target tracking, and restricted
  administrative content tools. World overlays follow objects every frame,
  including stereo views, without requiring Lua to redraw every frame.

## UCM reliability and combat

- Fix out-of-memory failures with large custom loot profiles and full snapshots.
  Increase the bounded Lua heap from 8 to 32 MiB, contain callback failures,
  and add useful memory diagnostics.
- Prune expired hunt timers and departed-item retry history during long sessions
  while preserving retries for live objects.
- Fix automatic mana-stone filling losing its reserved donor during pickup
  reassessment, causing repeated junk looting instead of filling the stone.
- Check the target's live position and projectile clearance around corners
  before UCM casts. Recheck immediately before sending the cast so stale
  positions do not permit shots into walls. Manual casting is unchanged.

## Retail animation, visibility, and tooltips

- Fix Apostate Gateway and Aetherium Deposit objects falling far below their
  reported location. Objects without movement spheres retain their authored
  position instead of receiving unsupported gravity movement.
- Restore authored golem rising animations and resting-to-combat transitions,
  including activation during a lay-down animation.
- Finish the authored local walking/running stop transition when stopping or
  focusing chat, with collision handling and position reporting retained.
- Keep inventory and plugin tooltip text clear of scaled cursors, including
  enlarged UI, viewport edges, and embedded game viewports.

## Resource use and diagnostics

- Bound retained material caches, preserve visible material reuse and lighting,
  and clear stale outdoor materials when DAT resources reload.
- Avoid copying decoded texture pixels for surface classification and avoid
  rebuilding unchanged Dereth Lens overlay geometry while stationary.
- Expand `/acueperf` and slow-frame diagnostics with effects subcategories,
  cache-maintenance timing, and DAT resource counts.
- Expand `/acueobject` with server-versus-rendered positions, bounds, visibility,
  and camera/material details, and save the report to the client log.
- **The reported intermittent effects hitch remains under investigation.**
  Controlled tests did not reproduce the 126 ms effects spike and did not
  establish an overall FPS improvement; do not interpret these changes as a
  confirmed fix for that report.

## Validation and distribution

- Expand regressions for long UCM hunts, large loot profiles, donor reassessment,
  firing corridors, authored motion transitions, quest-object placement,
  scaled tooltips, plugin capabilities, target tracking, and cache lifetimes.
- Ship matching Windows, Linux, and Quest packages without personal settings,
  account data, server databases, or retail DAT files.
- Automated fixtures and packaged-client checks do not replace live server,
  native Linux GPU, or physical Quest/PC VR acceptance. Foundry server imports
  and deletions were not performed against a live database during validation.
