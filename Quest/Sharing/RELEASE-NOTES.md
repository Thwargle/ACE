# AC:Unreal / AC:VR release 92

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.03.92**. Android version code: 92. Quest installer revision: 7.

## Changes

- Missing or empty game DAT files now open the Game files page before login,
  with the missing filenames, searched folder, and instructions to recover.
  Quest users receive instructions for running Repair-Quest.cmd. This message
  uses packaged fonts and remains readable without the DAT assets.
- The launcher checks both client_portal.dat and client_cell_1.dat before
  connecting. Selecting and saving a complete installation clears the error.
  client_highres.dat remains optional.
- Fixed a remaining placement error for creature-based objects: later grounded
  network updates could pull an elevated object down to distant terrain.
  Ground correction now respects the model's scaled DAT step-down distance.
  This applies to custom content and stock creature-based props, without
  hardcoded server names or object IDs.

## Validation

- Missing, empty, and partial DAT installations, recovery after choosing a valid
  folder, persistent error text, and launcher layout checks passed.
- Scaled Tou-Tou crystals retain their elevation and selection after repeated
  position updates. Stock Fishing Hole, Nexus Crystal, and Fir Tree fixtures
  passed with their retail DAT models.
- Remote movement, stair support, and movement review regression suites passed.
- The exact Daralet tower scene still needs an in-game recheck. No new live
  headset or Linux runtime acceptance is claimed for this release.

## Updating

Use the client updater or download the matching platform package from the
website. Installing an update closes the game; on Quest this is expected while
Android installs the replacement. Accounts, settings, and DAT files are retained.
