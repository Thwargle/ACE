# AC:Unreal / AC:VR release 62

Native Quest: `2026.09.22.quest.62`, Android version code 62, installer revision 6.
Windows desktop / PC VR: `2026.09.22.68`.

This release fixes repeated-portal memory growth, improves shared runtime
performance, and updates movement and the retail interface since public v60.

## Changes since v60

- Release the previous area's terrain textures before loading the next portal
  destination. Resource destruction runs across portal frames; nearby destination
  mesh caches remain reusable. Repeated Obsidian Rim trips passed on Quest 3.
- Retire departed outdoor and dungeon meshes, discard cancelled worker results,
  limit concurrent room builders, and remove redundant environment-data copies.
- Reuse the UI name index until the element tree changes, with correct updates
  after renaming, reparenting, removal, and reordering.
- Avoid repeated outdoor visibility bookkeeping and unchanged ceiling updates.
- Prepare terrain mip storage with a faster lossless encoder and cache the
  complete prepared mip chain. Terrain resolution, pixels, and mipmaps remain
  intact. Native UI panel textures use a single bilinear mip at their authored size.
- Improve escape and sliding when multiple collision surfaces meet, including
  the narrow Shoushi gap between Eiichi and the lifestone. Shared collision fixes
  apply to desktop, PC VR, and native Quest.
- Restore retail minimap marker masks, colors, ranges, selection squares, and
  click selection. Offscreen selected objects use the original DAT direction arrows.
- Keep the settings window and Apply button reachable after desktop UI scaling.
- Make VR enemy health bars shorter and hide them immediately at zero health,
  including enemies defeated in one hit.

## Install or update

Windows: run the AC:Unreal installer. It includes desktop and PC VR shortcuts.
Existing game data, accounts, and settings are retained. A portable ZIP is also
available. The Windows installer is not digitally signed.

Quest: extract the entire ZIP. Put Android platform-tools beside the installer,
connect the headset with a USB data cable, and accept USB debugging.
Run **Update-Quest.cmd** if game data is already installed. For a first installation,
run **Install-Quest.cmd** and provide your own Asheron's Call DAT files.

The app appears as **AC:VR** in Unknown Sources. Do not uninstall to update:
updating retains DAT files, saved accounts, and settings. The installer migrates
any old runtime folder when necessary. See the included instructions for setup.

## Server compatibility

Choose a server in the lobby and use your own account. Custom VR combat and pose
replication require this project's updated VR-enabled ACE server. Server owners
must include the v60 instant atlatl/thrown release changes; a client update cannot
change an older server's rejection behavior. This release adds no new server requirement.

GDLE login has been verified through character selection; full in-world GDLE
compatibility still needs testing. Bundles contain no server, credentials, saved
settings, SDK tools, or retail DAT files.

## Validation and performance

- Windows and Android builds succeeded. Regression coverage includes portal
  resource retirement, loading transitions, cancelled streaming work, terrain
  fidelity, UI lookup lifetime, minimap interaction, VR menus/health bars,
  Shoushi collision, and training-dungeon corners.
- On Quest 3, one login and seven portal transitions completed successfully;
  the tester confirmed repeated Obsidian Rim visits looked correct.
- Sampled maximum memory accounting fell from 5.26 GiB in the failing v61 run
  to 2.67 GiB in v62. Swap remained at 216 KiB instead of approximately 2.10 GiB.
  These are five-second samples from the recorded routes, not instantaneous peaks.
- Outdoor samples remained around 59–66 FPS. Sustained 90 FPS VR and 144 FPS
  desktop are still targets, not established performance claims.

This is a community preview. Additional hardware, multiplayer, and comfort
testing remains important. Download hashes are provided in SHA256SUMS.txt.
