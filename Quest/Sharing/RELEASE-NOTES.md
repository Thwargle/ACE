# AC:Unreal / AC:VR release 82

Windows desktop, PC VR, and native Quest: 2026.09.27.82.
Release number / Android version code: 82. Quest installer revision: 6.

## Changes since public release v81

### Network movement and mixed desktop/VR play

- Desktop clients can receive and display VR head, hand, body, and equipment
  movement without having a headset. Standard movement and animation updates
  continue to work in both directions.
- Smooth network corrections without smoothing away normal running speed.
  Reduce sideways correction noise, follow slopes, and stop predicted movement
  at unsupported edges instead of running into empty space and snapping back.
- Send current feet positions with tracked poses; retain head/hand movement
  history when equipment changes. Restore ordinary movement after tracking loss
  without overwriting newer position corrections with older buffered poses.
- The receive-only VR pose protocol and server-side landing/interaction changes
  require the accompanying ACE server changes. They do not update third-party
  servers or add tracked hand animations to the original retail executable.

### Collision and interactions

- Use authored creature collision spheres for passage and remote prediction
  instead of oversized model bounds, including Phyntos Wasps, Tuskers, and Virindi.
- Improve wall sliding, stair-edge contacts, and bounded recovery from embedded
  positions. Upper-body contacts no longer become false floor support.
- Suppress generated scenery in building-occupied outdoor cells, including the
  unwanted Mosswart Fort courtyard tree; rebuild outdated scenery caches.
- Release stale door/NPC interactions after failed approaches, portals, or a
  missing completion timeout. Timed-out actions are not automatically repeated.
- Send the actual touchdown position before VR actions. The updated ACE server
  verifies pending landings before rejecting a grounded player as airborne.
- Changing overhead camera angle preserves the chosen zoom distance.

### VR interface

- Show only other fellowship members' names and vitals in the optional VR
  fellowship panel, without the full grey background or duplicate self vitals.
- Organize VR settings into Movement, Panels, Combat, Controls, and Graphics,
  with consistent controls and separate scrolling for each section.
- Filter compass, vitals, and fellowship panel textures to reduce shimmer during
  small head movements. Keep World panels fixed and reduce tiny Body-anchor motion.

### Updating

- Add an optional automatic-update checkbox, off by default. When enabled, the
  launcher checks periodically and downloads verified updates while idle.
  Login/gameplay prevents automatic installation; cancelling pauses automation
  for the current app session.
- Show an eight-second cancellable notice explaining that installation closes
  the game. Windows reopens in the same desktop/VR mode. On Quest, confirm the
  Android prompt, wait for installation, then reopen AC:VR from the library.
- Keep manual updating available. Existing v73+ clients detect v82 in Updates;
  earlier clients need the website installer or Quest USB bundle once.

Accounts, settings, and retail DAT files are retained. Do not uninstall first.
No retail DATs, saved accounts, or server configuration are bundled.

## Validation and limitations

Automated validation covers decoded movement/pose packets, actual DAT avatar
rendering, slopes and ledges, creature passage widths, reported stair/wall
fixtures, interaction recovery, updater state, and VR panels. Release package,
installer-upgrade, and public-download checks are recorded in the check-in notes.

A live mixed-client playtest and standalone headset panel comparison remain
necessary. This release does not claim a measured 90 FPS result or complete
resolution of every reported collision case.
