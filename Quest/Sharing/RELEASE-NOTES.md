# AC:Unreal / AC:VR release 117

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.15.117**. Android version code: 117. Quest installer revision: 9.

## UCM and plugin performance

- Reduce stalls when starting UCM, adding waypoints, or editing monster settings
  with a large loot profile selected, including while looting is disabled.
- Reuse unchanged loot-rule validation, cache regex validation, and avoid repeated
  whole-profile JSON conversions and duplicate saves.
- Allow automatic Void combat to select a wand when usable curses are available
  without an eligible direct-damage spell. Refresh curses according to their timers.
- Keep combat failure details visible during retry delays.
- Show minimum ring targets as a whole-number count from 1 to 10, without a percent
  sign. Global and per-monster priority controls now range from -1 (ignore) to 10.
  Existing imported values are preserved until edited.

## Pickup and effects

- Keep F-key pickup active through the server-directed approach and arrival.
  Remove unsolicited movement commands that could cancel pickup and require a
  second press. Deliberate movement still cancels the approach.
- Preserve Aetheria surge emitters when spellcasting reuses numbered particle
  slots. Keep authored effect timing, orbit, and explicit stop/destroy behavior.
- Correct desktop SDR additive-particle blending after tone mapping so brightness,
  color, and overlapping glows better match retail. Apply alpha only once and
  preserve RGB for additive-only surfaces.

## Installation and validation

- **AC-WoW Transmog, its provider, converter and model assets are excluded.**
- Updates preserve accounts, settings, profiles and DAT files. Linux updates
  are downloaded and extracted manually. Game DAT files and a server are not
  included.
- Automated checks cover the changed behavior. Controlled timing measurements
  do not establish live FPS gains. Desktop particle composition targets SDR;
  HDR, stereo VR and mobile retain their respective rendering paths. Packaging
  and offline checks do not establish live-server or physical-headset acceptance.
