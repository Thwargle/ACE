# AC:Unreal / AC:VR release 68

Native Quest: `2026.09.23.quest.68`, Android version code 68, installer revision 6.
Windows desktop / PC VR: `2026.09.23.74`.

## Changes since v67

- Add 40 retail keyboard actions for player/fellowship/radar selection, opening
  unopened corpses, inventory transfers, panels, chat windows, camera control,
  screenshots, logout, and emotes.
- Improve custom retail .keymap compatibility: the reported example now imports
  156 action bindings and 17 explicit DoNothing overrides. Disabled chords no
  longer accidentally trigger fallback movement. Mouse wheel zoom and the
  keyboard Menu key import correctly.
- Match keyboard zoom direction to its retail action and let imported wheel
  bindings control zoom. Held mouse look also works when ordinary mouse turning
  is disabled, without appraising an object on release.
- Follow retail target ordering and preserve combat-specific binding priority.
  Panel shortcuts select the intended tab, and split-stack focuses an editable
  quantity that is clamped to the actual stack size.
- Show a scrollable import report that identifies unsupported controls separately
  from native UI/system contexts. Review the mapping draft, then press OK to apply
  it or Cancel to discard it. Existing profiles are not reset by this update.

## Keymap compatibility limits

Legacy DirectInput joystick indices, browser Back, and retail's distant map-camera,
Help and plugin-manager actions are not implemented. Separate system, text-editing,
mouse/widget, targeted-use and alternate-camera maps retain the application's
existing behavior rather than being remapped. Save As exports supported bindings;
keep the original file if it is also used with retail. The import report lists
exact controls that were not loaded instead of implying complete compatibility.

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

Windows and Android development builds passed. Automated input tests use the
reported custom keymap and cover modifier priority, disabled chords, combat
contexts, persistence and export/import. Desktop and ES3.1 mobile UI tests cover
selection, stack quantity, panel shortcuts and the rendered import report. Camera
regressions verify held mouse look and ordinary mouse controls.

Both release packages built successfully. The packaged Windows client passed
KeyboardBindings, CameraAndEdges and UIScreens. All 15 Quest installer regression
cases passed under Windows PowerShell. Shared source and runtime materials were
synchronized before Quest packaging.

These checks do not replace live gameplay acceptance on public servers or headsets.
No physical legacy joystick was tested. There is no new FPS benchmark in this
release; sustained 90 FPS VR and 144 FPS desktop remain targets.
Download hashes are provided in SHA256SUMS.txt.
