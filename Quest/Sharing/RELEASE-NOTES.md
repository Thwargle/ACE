# AC:Unreal / AC:VR release 63

Native Quest: `2026.09.22.quest.63`, Android version code 63, installer revision 6.
Windows desktop / PC VR: `2026.09.22.69`.

## Changes since v62

- Preserve the player's current facing when delayed server position updates
  arrive, including forced position corrections. This addresses mouse and
  keyboard turning snapping backward under latency. Server position corrections,
  teleport destinations, and explicit server turn commands remain authoritative.
- Retain small mouse turns between frames, even below the movement packet threshold.
- Enable mouse turning by default, with a saved local preference when disabled.
- Keep physical numpad camera controls separate from player movement with Num Lock
  both on and off. Dedicated arrow keys retain their movement controls.
- Restore character deletion with the original retail warning and typed DELETE
  confirmation. Pending deletions can be restored; roster updates retain selection.
  Requests validate the current character slot and block conflicting operations.
- Implement the Credits button using the original DAT credits text, artwork,
  two-column layout, and scrolling presentation.
- Return creatures, including drudges, from completed throwing/aiming poses to
  their ready animation instead of leaving them frozen until their next action.
- Keep options dropdown labels alive while their menus are displayed, preventing
  selections from becoming blank after garbage collection.
- Correct link latency measurement so valid readings no longer repeatedly reset
  to 0 ms. Use a recent traffic window and time since the last packet for status.
  Restore the Link panel's retail text, font, and layout.
- Fit long inventory titles within the original retail header without text
  overflow, retaining the full title in the tooltip.

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

## Validation and limitations

Six packaged Windows suites passed: CameraAndEdges, CharacterManagement,
LinkTiming, MovementReview, UIScreens, and WorldEntry.

Regression coverage includes character-management packet handling and prompts,
credits and retail UI presentation, dropdown lifetime through garbage collection,
latency timing, delayed position updates, small mouse turns, and numpad controls
in both Num Lock states. Creature motion coverage exercises 2,408 original DAT
missile entries returning to Ready.

This release has not received live headset acceptance or a physical-keyboard
Num Lock check. No real character was deleted during automated validation.
There is no new FPS benchmark in this release; sustained 90 FPS VR and 144 FPS
desktop remain targets. Download hashes are provided in SHA256SUMS.txt.
