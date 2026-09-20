# AC:VR release 56

Native Quest: `2026.09.20.quest.56`, Android version code 56, installer revision 6.
Windows desktop / PC VR: `2026.09.20.65`.

This update makes buffing another player a single trigger action in native Quest
and PC VR.

## Install or update

Extract the entire ZIP. Put Android platform-tools beside the installer, connect
the Quest with a reliable USB data cable, and accept USB debugging in the headset.
Run **Update-Quest.cmd** if game data is already installed. For a first installation,
run **Install-Quest.cmd** and provide your own Asheron's Call DAT files.

The app appears as **AC:VR** in Unknown Sources. Do not uninstall the existing app:
updating retains DAT files, saved accounts, and settings. The installer migrates
the old runtime folder when necessary. See README.txt for setup and troubleshooting.

## Included changes

- With an "Other" buff selected in magic stance, close menus, aim at a visible
  player, and pull the action trigger. That click selects the player and casts
  the buff, even if a different object was previously selected.
- The aiming highlight previews the buff recipient without changing selection
  until the trigger is pressed. Both left- and right-handed controls work.
- Spell classification uses retail spell data. Self spells, fellowship spells,
  projectiles, and explicitly selected debuffs retain their targeting rules.
  Open menus and world geometry still block pointing at a recipient.

## Changes retained from earlier releases

- Expanded dungeon corner recovery for overlapping room meshes and beveled
  stair/wall contacts, shared by desktop, PC VR, and native Quest.
- Addressed the confirmed Android low-memory kill when loading Shoushi. Terrain
  textures now reuse their shared source pixels instead of retaining another CPU
  mip chain. Native Quest keeps a smaller surrounding terrain area (radius three
  landblocks) to bound memory use; desktop and PC VR retain their existing range.
  Landscape resolution, uncompressed pixels, and mip filtering are unchanged.
- Hold X for about a second to open VR options on Quest controllers, including
  Quest 2 through Steam Link when the runtime consumes Menu. Tapping X continues
  to toggle inventory; the normal Menu binding remains available.

- Fixed movement getting permanently stuck at beveled wall and stair corners,
  reproduced against the academy dungeon's actual collision geometry.
- Desktop players now wade below water surfaces using retail water depth, matching
  VR. Verified the reported DC56001F location at 90 cm below the surface.
- Released the held death animation when respawning, including remote players.
- Corrected the offhand Academy Cestus orientation using the authored attachment
  frame supplied by the server.
- Enter reopens chat after sending; slash opens command entry. Chat output supports
  selecting and copying text, with right-click actions for copying a message or the
  whole chat window. The input supports normal copy/paste.

- Restored the character model and heritage backdrop in the character creator's
  Appearance and Summary pages. The preview now uses the inventory model's
  scene-capture transparency handling, including on native Quest's mobile renderer
  and when the UI is rendered into a PC VR panel.

- Fixed character creation in native Quest and PC VR: controller buttons retain
  their input routing, the name field opens the correct keyboard, and Enter/Done
  submits the current name through the normal validation and confirmation flow.
  The PC VR keyboard opens only for text entry and stays below the creator, clear
  of navigation and Finish. Interrupted controller gestures release their capture.

- Branded Windows setup wizard and a branded Quest quick-start guide using the
  AC monogram. New player setup instructions at thwargle.com/unreal and /unreal-vr.
- Run-speed parity fixes shared by desktop and VR: character scale, retail's exact
  800 Run override, and burden/stamina calculations. Removed desktop correction
  feedback that could incorrectly reduce predicted speed.


- New login lobby with saved servers, a community server browser, custom server
  editing, and website/Discord links. Saved accounts are shared across servers.
  Credentials remain encrypted on the device.
- Configurable DAT folder and responsive login layout for desktop and VR.
  Corrected the offscreen login-panel placement. Quest uses native text input.
- Corrected GDLE authentication and connection acknowledgement. Server-list ports
  are preserved exactly, and login rejection messages appear without waiting for
  a timeout. ACE authentication remains supported.
- Restored terrain, buildings, and scenery after portal loading, including the
  repeatable invisible-world login at the Caulcano tower.
- Portal loading begins earlier and spreads destination work across frames.
  Exteriors continue loading when arriving inside open buildings such as Sanctuary.
- Steep-slope movement preserves falling momentum instead of repeatedly resetting
  gravity. Equipped ammunition stays hidden until loaded for firing, preventing
  the stray arrow visible on login with a wand equipped.

## Server compatibility

Choose a server in the lobby and use your own account. Custom VR combat and pose
replication require this project's updated VR-enabled ACE server. GDLE login has
been verified through character selection; full in-world GDLE compatibility still
needs testing. Use the server directory or the host and port supplied by your server owner.

This bundle contains no server, credentials, saved settings, SDK tools, or retail
DAT files. Each recipient supplies their own data and Android platform-tools.

## Validation and remaining checks

The Other-buff regression checks passed in the editor: retail Strength Other,
Heal Other, and Armor Other; both dominant hands; selecting and casting from
an empty or different selection; preview/cast agreement; menu and wall blocking;
and unchanged self, debuff, and projectile selection. The VR protocol suite also
passed. Windows and Android ARM64 builds compiled successfully. The packaged
Windows executable also passed VR rig/menu input, VR protocol, material loading,
and landscape texture fidelity (four passed, zero failed).

Earlier v55 Windows packaging passed material loading and landscape texture
fidelity checks. The retained gameplay fixes have the v54 results below.

For v54, the expanded academy test passed 2,417 blocked approach/retreat cases
using both desktop and tracked VR movement, at 60 FPS and 15 FPS simulation
steps. Wall sliding, stair ceilings, ledges, doorway movement, and interior
network movement regressions passed. Cold world arrival rendered all 121 desktop
blocks and all 49 blocks in the Shoushi Quest budget. Pixel comparisons verified
unchanged terrain source pixels and every mip, including texture recreation.
VR input tests verify short X presses, held X, release, and tracking loss.
The packaged v54 Windows executable passed academy corners, landscape texture
fidelity, texture budgets, and VR rig/menu input (four passed, zero failed).

The Shoushi terrain budget calculates roughly 2.3 GiB of source pixels plus GPU
textures, versus about 5.2 GiB including duplicate CPU mips at the former range.
This is terrain-only storage, not a measured total headset memory reading.
Live Shoushi loading and Quest 2/Steam Link controls still need player validation.

Earlier release validation:

Ten targeted editor regression suites passed across corner recovery, wading,
stairs, doorways, network movement, equipment, respawn, UI, and chat. The chat
checks exercise repeated send/reopen cycles and OS clipboard copy/paste.
The v53 packaged Windows executable also passed the chat, UI screens, academy
corner, and movement/equipment regression suites (four passed, zero failed).

The creator preview passes rendered mobile ES3.1/LDR and desktop checks that
verify the character contributes visible pixels to the final UI, including the
Appearance and Summary pages. Desktop creator controls, VR input lifecycle,
VR rig/controller routing, and all eight native keyboard callback cases pass.

The Android ARM64 package built successfully and its signature and release
version were verified. All 15 Windows installer regression cases and the three
profile-migration checks on Quest passed. Shared PC/Quest source is synchronized.

Prior releases passed the shared desktop login, server-profile, handshake,
portal-reveal, Sanctuary loading, and interior-streaming regressions. The broad world-entry suite
retains the previously recorded bench-stepping failures. Native Quest interaction,
multiplayer gameplay, and comfort require playtesting after installation.

This remains a test release. Consistent 90 FPS VR has not been established, and
this release does not claim a new measured frame-rate improvement. Landscape
textures retain full-resolution retail blends and uncompressed GPU pixels.
