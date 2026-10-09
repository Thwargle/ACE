# AC:Unreal / AC:VR release 104

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.09.104**. Android version code: 104. Quest installer revision: 7.

## Movement and targeting

- Improve landing, standing, jumping again, and strafing off creatures, including
  low carenzi, medium gromnies, and flying wasps. Preserve wall-contact protection.
- Limit nearest/next/previous target selection to the current retail radar range:
  75 meters outdoors and 25 indoors. Previously seen distant monsters no longer
  stay eligible after the player moves away.

## Interface and inspection

- Restore retail skill hover descriptions and attribute formulas.
- Correct Character Information augmentation descriptions and summoning mastery
  labels, including Naturalist, and the displayed chess rank.
- Update an open desktop or VR inspection panel when the selected target changes.
- Improve examination spacing, requirements, ratings, enchantments, portal details,
  item experience, salvage workmanship, and weapon-property presentation.
- Keep spell enchantment flags from the appraisal packet while preserving
  normalized spell IDs for plugins.

## UCM

- Keep recovery and combat available while waiting for a portal or recall result,
  instead of blocking other activities for the entire pending teleport timeout.

## Performance

- Remove per-frame clip-list allocations during casting and death animations.
- Calculate dense particle bounds on workers alongside vertex generation. The
  desktop CPU benchmark improved dense particle updates by approximately 3-4%
  with identical geometry and bounds; this is not a whole-game FPS estimate.
- Retain full textures, model detail, particle counts, and collision accuracy.

## Validation and installation

Focused regressions cover inspection, character information, skill tooltips,
UCM scheduling, target range, creature landing, textures, model caches, particles,
and lighting. The performance review passed 30 tests plus three final particle
checks. Automated fixtures do not replace live-server or headset acceptance.

Installing an update closes the game. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files are not included.
