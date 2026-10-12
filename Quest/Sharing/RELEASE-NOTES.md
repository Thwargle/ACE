# AC:Unreal / AC:VR release 110

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.12.110**. Android version code: 110. Quest installer revision: 7.

## Retail movement, combat, and camera

- Align player turning, mouse-facing rate limits, movement hold flags, grounded
  position updates, and airborne movement packets with retail behavior.
- Correct melee/missile approach and rotation handling, manual attack/cast stop
  ordering, missile readiness, and stale queued movement around combat actions.
- Preserve held emote poses during strafing and turning. Use retail's pose and
  transition checks for jumping instead of forcing the character to stand up.
- Keep the camera's chosen heading when switching from left-button orbit to
  right-button mouselook; turn the character toward that heading at retail speed.
- Correct collision and cell transitions for open building passages, including
  entrances and areas beneath walkways. The fix uses shared authored geometry
  rules rather than a special exception for Bandit Castle.

## Chat and plugin API

- Route pet-attack and killing-blow messages through the appropriate retail chat
  filters in main, secondary, and VR chat windows.
- Retain large chat bursts for Lua plugins instead of discarding messages beyond
  the newest 128. Deliver ordered batches independently to each plugin, including
  /myquests results with UCM disabled.
- Bound the shared chat backlog at 32,768 events or 8 MiB of UTF-8 message text,
  expose pending/overflow counts, and clear unread pages between logins.
- Update API documentation and the quest-tracker example for paged chat delivery.

## Maintenance and upstream

- Merge ACEmulator/ACE master through 7715c052 (database version marker update).
- Resolve shared plugin-source unity-build naming collisions and update regression
  code to current Unreal APIs without the reported deprecation/float warnings.
- Keep Windows, Linux, PC VR, and Quest source/version metadata synchronized.
- **AC-WoW Transmog, its provider, converter, and model assets are excluded.**

## Installation and validation

Validation covers the changed movement, combat, emote, camera, collision, chat,
plugin API, and packaging paths. Linux and headset gameplay require live acceptance;
packaging and automated checks alone are not live gameplay certification.

Installing an update closes the game. Accounts, settings, and DAT files are
retained. Linux updates are downloaded and extracted manually. Game DAT files
and a game server are not included.
