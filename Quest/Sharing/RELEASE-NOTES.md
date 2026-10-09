# AC:Unreal / AC:VR release 101

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.08.101**. Android version code: 101. Quest installer revision: 7.

## Collision and portal landings

- Player and remote-creature movement now uses the retail DAT's individual body
  spheres, preserving their centers, radii and object scale. Removed human-size
  clamps and the shortened player body that changed clearance around obstacles.
- Ground support, stair recovery and portal placement use the same authored
  lower sphere, including its offset from the object's origin.
- Sliding follows the intersection of the ground and obstacle surfaces on slopes.
  Corrected terrain support that could leave the body penetrating an incline.
- Objects with explicitly empty collision geometry no longer become invisible
  obstacles through their enlarged selection volumes.
- Coastal-water portal arrivals use the DAT's wading support and nearby placement
  checks. This applies to supported water landings generally, including Withered
  Beach, rather than a destination-specific exception.

## Rendering performance

- Reuse setup, mesh and material information across repeated creature spawns and
  corpse construction, reducing duplicated work for Snow Tuskers and other models.
- Batch particle vertex preparation and use worker threads when the active
  workload is large enough. Retain particle counts and appearance; small workloads
  stay on the serial path to avoid unnecessary thread overhead.

## UCM

- Evaluate other loot while appraisal replies are pending, with a bounded queue
  and first-match rule ordering preserved.
- Timed navigation pauses no longer block combat or vital recovery.

## Retail interface and networking

- Use retail text fields and fonts in fellowship, friends, squelch and journal
  screens, including multiline editing and journal scrollbar handling.
- Restore journal column layout, timer formatting and sorting without changing
  stored page numbers or the selected page.
- Correct motion action-sequence handling and retain each follow-up animation's
  own speed. Stopping movement while airborne no longer reports a false landing.

## Validation and installation

Windows, Linux and Quest builds succeeded. All 46 packaged Windows regression
tests passed, along with 11 release-packaging tests and 6 website-release tests.
All three platform packages contain the current plugin files.

Collision validation covers 35 DAT body/scale cases and movement regressions for
crowds, stairs, slopes, ledges, portal arrivals, scenery and remote characters.
Live multiplayer, Linux gameplay and headset acceptance remain necessary; this
release does not claim complete retail or Virindi Tank parity.

Installing an update closes the game. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files are not included.
