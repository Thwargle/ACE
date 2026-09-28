# AC:Unreal / AC:VR release 85

Windows desktop, PC VR, and native Quest: **2026.09.28.85**.
Release number / Android version code: 85. Quest installer revision: 6.

## Changes since public release v84

### Networked player movement

- Keep ordinary forward movement continuous when late position packets correct
  the predicted position, reducing backward steps while running beside friends.
- Apply visual position corrections through collision so remote players respect
  walls, stair risers, slopes and ledges. Selection follows the displayed body.
- Ignore stale airborne position corrections instead of replaying old jump
  velocity. Queue landing corrections until the simulated body reaches ground.
- Distinguish genuine portal teleports using the server's teleport sequence,
  including nearby destinations, while blending normal position corrections.
- Keep VR body movement on the same buffered timeline as tracked head and hands,
  removing an additional speed-dependent body delay.
- Blend tracking recovery into an existing avatar instead of snapping it into
  place. Newer landing information cannot pull a buffered VR jump to the floor.
- Prevent ground seating differences from turning small position noise into
  visible lateral correction.

### Building entrances and visibility

- Update movement cell ownership and collision residency promptly at building
  entrances to reduce the movement hitch when crossing between inside and outside.
- Keep terrain collision independent of camera visibility and avoid recreating
  physics bodies when only collision filters change.
- Allow airborne transitions into building interiors, including window jumps,
  so interior geometry remains visible after landing inside.

### Emote transitions

- Use retail's authored transitions from the actual current pose when leaving
  lying, sitting and other held emotes, including intermediate ready transitions.
- Finish queued pose transitions before starting subsequent gestures or movement,
  preventing the body from appearing to levitate upright.
- Preserve casting, jumping, death and locomotion behavior when an emote ends.

## Updating

Use **Updates** in the launcher or download the current installer from the site.
Existing supported updaters detect release 85. Accounts, settings and retail DAT
files are retained; do not uninstall first. **Installation closes the game.**
On Quest, confirm Android's installation prompt, wait for completion, then reopen
AC:VR from the library. Optional automatic updating remains off by default.

No retail DAT files, saved accounts or game-server configuration are bundled.
No new game-server deployment is required. Standard movement stays compatible
with retail servers; extended VR tracking requires a compatible server and client.

## Validation scope

Regression coverage includes delayed and jittered movement updates at 30, 90 and
144 FPS, jumps and landings, slopes, stairs, cliffs, selection alignment, VR
tracking recovery, desktop/VR observer compatibility, actual DAT building
entrances and authored emote transitions. Server integration checks verify
validated root positions and VR pose delivery to subscribed desktop and VR
observers, without sending extended tracking packets to retail clients.
These automated checks do not replace live multiplayer or headset acceptance.
No new measured headset FPS improvement is claimed by this release.
