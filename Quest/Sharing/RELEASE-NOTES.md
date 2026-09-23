# AC:Unreal / AC:VR release 67

Native Quest: `2026.09.23.quest.67`, Android version code 67, installer revision 6.
Windows desktop / PC VR: `2026.09.23.73`.

## Changes since v66

- Show retail's original move and directional resize cursors over usable window
  handles. Keep the appropriate cursor while dragging and respect the UI lock.
- Restore the toolbar's retail height range. With the UI unlocked, drag its bottom
  border up to show one shortcut row or down to show both rows. Its top stays in
  place, and its width remains fixed.
- Clip the second row cleanly as the toolbar shrinks, without stretching its icons
  or leaving hidden shortcuts able to intercept world clicks. Bindings are retained.
- Save the chosen toolbar height and restore it alongside the UI lock state.
  Loading the layout no longer overwrites saved window geometry with defaults.
- Share the toolbar behavior across desktop, PC VR, and standalone Quest. Cursor
  artwork is cached, and the shortcut clipping panel avoids needless visibility
  changes during steady gameplay.

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

Windows and Android development compilation and the desktop and ES3.1 mobile
native UI suites passed. Checks cover cursor artwork and drag capture, one and
two shortcut rows at 100% and 200% scale, partial-row clipping, hidden-slot hit
testing, resize limits, UI locking, and saved geometry. Rendered screenshots were
reviewed. Retail behavior was checked against the original UI data and client code.

The packaged Windows client passed the native UI regression suite, and all 15
Quest installer regression cases passed under Windows PowerShell.

These checks do not replace live gameplay acceptance on public servers or headsets.
There is no new FPS benchmark in this release; sustained 90 FPS VR and 144 FPS
desktop remain targets. Download hashes are provided in SHA256SUMS.txt.
