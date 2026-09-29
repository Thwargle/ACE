# AC:Unreal / AC:VR release 86

Windows desktop, PC VR, and native Quest: **2026.09.29.86**.
Release number / Android version code: 86. Quest installer revision: 6.

## Changes since public release v85

### Expired portals and stale creatures

- Match retail's 25-second retention of world objects outside the server's
  interest area, preventing expired summoned portals and old creatures from
  reappearing when returning to an area.
- Retire attached visuals, effects, collision and selection state together.
  Fresh server object descriptions restore valid objects normally.
- Use cell visibility data and neighboring outdoor landblocks for retention,
  independent of camera direction. Preserve the player, inventory and equipment.

### Movement through creature swarms

- Prevent overlapping creature spheres from forcing unrequested sideways
  movement during walking, running and jumping on desktop and in VR.
- Allow outward and tangential movement out of overlaps while continuing to
  check walls, other creatures, stairs and scenery.
- Apply the correction through shared collision handling, without special
  behavior tied to one dungeon or location.

## Updating

Use **Updates** in the launcher or download the current installer from the site.
Existing supported updaters detect release 86. Accounts, settings and retail DAT
files are retained; do not uninstall first. **Installation closes the game.**
On Quest, confirm Android's installation prompt, wait for completion, then reopen
AC:VR from the library. Optional automatic updating remains off by default.

No retail DAT files, saved accounts or game-server configuration are bundled.
No new game-server deployment is required.

## Validation scope

Automated coverage includes portal and creature retirement, server object
refreshes, inventory preservation, 2,761 deterministic swarm cases, and shared
desktop/VR wall, stair, ledge and crowded landing regressions using actual DAT
geometry. These checks do not replace live swarm or headset acceptance.
No new measured headset FPS improvement is claimed by this release.
