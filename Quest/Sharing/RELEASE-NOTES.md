# AC:Unreal / AC:VR release 60

Native Quest: `2026.09.21.quest.60`, Android version code 60, installer revision 6.
Windows desktop / PC VR: `2026.09.21.66`.

This release improves first-login loading, reduces terrain CPU memory use, and
adds movement, missile, interface, and VR interaction fixes since public v56.

## Changes since v56

- Preload runtime materials asynchronously before world entry. Prepare gameplay
  UI in bounded batches and reuse it when leaving portal space. DAT indexing and
  cache work run on dedicated background workers to avoid blocking head tracking.
- Store terrain restore data and prepared mip levels with lossless CPU compression.
  Retail terrain resolution and GPU texture pixels are preserved. A captured
  170-texture scene used about 147 MiB less CPU restore storage.
- Update world fog through a shared material parameter collection instead of
  rewriting parameters across every cached material and actor. Interior and
  preview fog exclusions remain supported.
- Improve sliding across successive wall triangles and bends, retaining all
  contact constraints at corners. Shared movement covers desktop and both VR modes.
- Prevent outdoor jumps from landing below the DAT terrain or submerged river bed,
  including the reported Holtburg river bank. The visible water surface remains
  non-blocking, and jumping keeps its arc until reaching the actual walking surface.
- Correct missile release validation on the updated VR server: only bows require
  a physical draw. Crossbows, atlatls, and thrown weapons accept trigger releases.
  Bows honor the same recovery timer as other missiles; failed atlatl/thrown sends
  now report connection failure. Weapon placement and aiming are unchanged.
- Repair deliberate two-hand use in peace mode. The gesture checks usable range,
  visible surfaces, gaze, and a short hold using tracked hands, allowing doors and
  tall lifestones to work without forcing hands through collision. Withdraw hands
  before repeating the gesture.
- Add independent Invert Mouse X and Invert Mouse Y settings.
- Add desktop UI scale in 25% steps, constrained to fit the current window. VR
  keeps its own UI scaling controls.
- Add a frame-rate overlay setting showing FPS and frame time in desktop and VR.
- Replace the enemy health display with a clear rounded red bar, subtle dark
  track, and gold frame. No numbers or extra labels; VR sizing remains readable
  with distance and redraws occur only when health changes.
- Keep vitals lookups within their active UI container and clear stale values.
- Add Quit to the account launcher and the build version at the bottom of the
  login screen. Quest builds verify the displayed version matches the APK.

## Install or update

Windows: run the AC:Unreal installer. It includes desktop and PC VR shortcuts.
Existing game data, accounts, and settings are retained. A portable ZIP is also
available. The Windows installer is not digitally signed.

Quest: extract the entire ZIP. Put Android platform-tools beside the installer,
connect the headset with a reliable USB data cable, and accept USB debugging.
Run **Update-Quest.cmd** if game data is already installed. For a first installation,
run **Install-Quest.cmd** and provide your own Asheron's Call DAT files.

The app appears as **AC:VR** in Unknown Sources. Do not uninstall to update:
updating retains DAT files, saved accounts, and settings. The installer migrates
any old runtime folder when necessary. See the included instructions for setup.

## Server compatibility

Choose a server in the lobby and use your own account. Custom VR combat and pose
replication require this project's updated VR-enabled ACE server. Server owners
must update for the instant atlatl/thrown release validation; updating only the
client cannot change an older server's rejection behavior.

GDLE login has been verified through character selection; full in-world GDLE
compatibility still needs testing. Use the directory entry or host/port supplied
by your server owner. Bundles contain no server, credentials, saved settings,
SDK tools, or retail DAT files.

## Validation and remaining checks

- Server: 17 protocol/release unit checks and five projectile integration cases
  passed, including bow, crossbow, atlatl, arrows, and thrown-item ammunition.
- Mobile renderer: launcher, academy corners, ledges/stairs, VR protocol,
  controller/menus, and stair/river movement suites passed. River coverage includes
  80 desktop/VR jumping scenarios at 90 and 20 Hz, including a landblock crossing.
- Loading preparation, terrain fidelity, and rendered weather/fog checks passed
  during development on desktop and the mobile rendering path.
- The packaged Windows game passed seven suites: responsive launcher, material
  loading, landscape texture fidelity, academy corners, VR protocol, controller
  and menu input, and stair/river movement.
- Windows and Android ARM64 packaging succeeded. Installer contents exclude
  private settings and game data; release artifacts carry SHA-256 checksums.

This is a test release. First-login loading was smoother in the user's v58 test,
but sustained 90 FPS VR and 144 FPS desktop have not been established. The shared
fog optimization still needs a controlled headset frame-time comparison. Terrain
GPU textures remain full-resolution and uncompressed. Headset gameplay,
multiplayer behavior, and comfort still require playtesting.
