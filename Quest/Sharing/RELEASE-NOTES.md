# AC:Unreal / AC:VR release 83

Windows desktop, PC VR, and native Quest: **2026.09.27.83**.
Release number / Android version code: 83. Quest installer revision: 6.

## Changes since public release v82

### Movement and portals

- Improve escape from overlapping creatures, including Tuskers and Virindi.
  Walking away or along a contact can free the player while walls, other
  creatures, and movement deeper into a creature remain blocked.
- Improve remote players' stair ascent and descent using the shared player-body
  sweeps, reducing stops at risers and feet intersecting descending treads.
- Prevent crowded portal arrivals from being rejected just because creatures
  occupy the destination. Architecture must still be clear and loaded.
- Preserve intentional airborne portal destinations and let the player fall to
  the landing. Only confirmed obstruction with no valid nearby placement can
  trigger automatic lifestone recovery; unfinished loading is not treated as
  proof that the player is trapped.

### Shared performance improvements

- Run gameplay UI refresh once per frame across desktop and VR render paths.
- Reduce repeated inventory scans, item copies, sorting, vendor filtering and
  equipment checks. Skip radar brush changes when its appearance is unchanged.
- Reuse immutable landblock metadata instead of copying nested building and
  portal data during movement, visibility and scenery queries.
- Reuse identical ground-support queries within a remote movement substep and
  use existing entity lookups for selection markers and flashes.
- Reuse VR gait pose storage and blend animation poses directly into body parts.
- Apply the final VR body placement once per update, avoiding duplicate updates
  to body parts and attachments. This also benefits desktop observers of VR users.
- Add rendering diagnostics for recurring mesh recreation. Quest actor draw
  caching stays disabled because the native comparison did not show an advantage.

## Updating

Use **Updates** in the launcher or download the current installer from the site.
Existing supported updaters detect release 83. Accounts, settings and retail DAT
files are retained; do not uninstall first. Installation closes the game.
On Quest, confirm Android's installation prompt, wait for completion, then reopen
AC:VR from the library. Optional automatic updating remains off by default.

No retail DAT files, saved accounts or game-server configuration are bundled.
This release updates the clients; no new game-server deployment is required.

## Performance and validation notes

The prior Yaraq headset captures confirmed one gameplay UI refresh per frame and
less CPU work in inventory/radar/hotbar refresh. The latest actor-cache comparison
was approximately 46–50 FPS with menus closed and changing clocks/head views.
This release does **not** claim 90 FPS or a measured headset FPS gain from the
newest multipart changes. Those still need an installed-build comparison.

Automated checks cover actual DAT bodies and portal geometry, crowded arrivals,
intentional drops, remote stairs, animation/attachment equivalence, UI refresh,
inventory queries, rendering and updater compatibility. They do not establish
that every live server route or collision report is resolved.
