# AC:Unreal / AC:VR release 66

Native Quest: `2026.09.23.quest.66`, Android version code 66, installer revision 6.
Windows desktop / PC VR: `2026.09.23.72`.

## Changes since v65

- Restore repeating interior wall textures, including the Town Network, while
  retaining the foliage texture-edge fix.
- Keep building interiors and stair collision loaded across their full height.
  This addresses stair lips blocking descent and stairs disappearing during jumps,
  including the reported Fort Teth towers, without location-specific exceptions.
- Handle corpse contents arriving before individual loot objects so newly created
  loot can be selected, appraised, and picked up consistently. Clear stale loot
  selections and close open loot windows when entering a portal or recalling.
- Queue equipment swaps until the server acknowledges each removal and equip.
  Inventory and hotbar swaps share this behavior. Swapping wands preserves combat
  intent through the temporary unarmed stance; explicitly choosing Peace wins.
- Restore both retail shortcut rows with 18 working slots, drag assignment,
  swapping and removal, selection, use, and inspection.
- Improve window dragging from native title/frame labels while respecting UI lock,
  buttons, tabs, and resize controls. Raised windows and file dialogs now cover
  underlying text and block clicks through them.
- Restore retail keybind buttons, three mappings per action, tabs, default bindings,
  and the existing Load File, Save As, Defaults, Revert, OK, and Cancel controls.
  Import and export retail .keymap files with modifier chords and mouse bindings.
  Keep unsupported imported bindings when saving and report unsupported entries.
  Remove duplicated key labels and fix file-dialog placement and text layering.
- Make fellowship controls reflect membership, leadership, recruitment, and sharing
  state, and use the native member-row layout and vital bars.
- Correct Spike Strafe sword orientation and preserve the intended placement for
  ring and wall spell projectiles.
- Keep creatures turning toward their server-designated target at close range,
  including when the local VR player moves within melee reach.
- Clip main-player vital fills to their native meter bounds and render empty meters
  without a leftover fill pixel, retaining the retail textures.
- Apply shared fixes to desktop, PC VR, and standalone Quest.

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

GDLE login has been verified through character selection. The v64 packet-timing
correction is included. A live Seedsow item-interaction retest is still pending;
automated protocol checks do not establish full in-world GDLE compatibility. Bundles contain no server,
credentials, saved settings, SDK tools, or retail DAT files.

## Validation and limitations

Windows and Android compilation and the relevant automated development regressions
passed. Checks cover equipment and loot packet ordering, GDLE interaction transport,
keyboard mapping round trips, native UI screens, world entry, movement, projectile
placement, interior texture sampling, and stair/corner collision. The Fort Teth
regression exercises six reported positions in desktop and VR at 30 and 90 FPS.
Native UI screenshots were also checked in desktop and ES3.1 mobile preview,
including file dialogs and main-player vitals at multiple scales and fill levels.

The packaged Windows client also passed KeyboardBindings, UIScreens,
InteriorSampling, and FortTethStairs. All 15 Quest installer regression cases passed.

These checks do not replace live gameplay acceptance on public servers or headsets.
There is no new FPS benchmark in this release; sustained 90 FPS VR and 144 FPS
desktop remain targets. Download hashes are provided in SHA256SUMS.txt.
