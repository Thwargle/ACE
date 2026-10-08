# AC:Unreal / AC:VR release 99

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.07.99**. Android version code: 99. Quest installer revision: 7.

## Movement and inventory

- Fixed an airborne lock when a nearby creature blocks recovery from a wall
  overlap. Falling recovery preserves architecture collision while excluding
  creatures from the support retry, following retail collision behavior.
- Improved ramp-to-floor transitions so tiny collision overlaps do not repeatedly
  roll the player back at a seam.
- Native UCM routes and imported VT routes share ground-following steering and
  three-dimensional arrival checks. Route lines follow ramps and elevation changes.
- Swapping weapons with a full main pack now stows displaced equipment in an
  available side pack and handles server container acknowledgements correctly,
  preventing weapons from disappearing until relogging.

## UCM buffing and equipment

- Improved fast-cast scheduling, stopped stale navigation movement during buffs,
  and kept Force Buff active until its enabled buff families are handled.
- Expanded configurable other-player buff requests for Heavy, Finesse, Light,
  Unarmed, Two-Handed and general melee roles, including supporting combat skills.
- Added a visible FIFO buff queue with tell replies for queue position, turn,
  completion and cancellation. Out-of-range requesters are removed so the next
  player can receive buffs.
- Added default main-hand/offhand weapon choices and honored the server's
  Left-hand Tether property in ordinary equipment handling and UCM selection.

## UCM salvage

- Added an explicit, default-off option to apply the active profile's salvage
  rules to items already in owned inventory, even with corpse looting disabled.
- Appraisal and safety checks preserve equipped, retained, traded, tinkered and
  inscribed items. Importing a profile alone does not enable inventory salvage.
- Corrected fractional workmanship groups, material overrides, value thresholds,
  full-bag exclusions, and CombineSalvage import/command handling.
- Inventory rule scans resume within the plugin budget and reuse completed
  results until relevant inventory, appraisal or profile data changes.

## Validation and installation

Automated coverage includes carenzi, gromnie and wasp collision shapes, desktop
and VR jumps at the reported dungeon location, landing position/contact state,
ramps, stairs, ledges, equipment swaps and the changed UCM policy paths.
This is not a claim of complete retail or Virindi Tank parity. Live multiplayer,
headset and Linux gameplay acceptance remain necessary. Four previously observed
stair/terrain assertions remain outside the fixes in this release.

Installing an update closes the game. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files are not included.
