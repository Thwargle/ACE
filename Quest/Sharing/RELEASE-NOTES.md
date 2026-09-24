# AC:Unreal / AC:VR release 71

Native Quest: `2026.09.24.quest.71`, Android version code 71, installer revision 6.
Windows desktop / PC VR: `2026.09.24.77`.

## Changes since v70

### Remote movement in desktop and VR

- Smooth heading and position corrections for remote players and creatures,
  including move-to actions and velocity updates. Preserve facing while strafing
  or jumping instead of automatically pointing the character along its velocity.
- Follow nearby terrain, stairs, and streamed interior floors during grounded
  movement, reducing horizontal movement above slopes followed by downward snaps.
  Preserve airborne movement and keep indoor actors off outdoor terrain support.
- Apply tracked VR root movement through one presentation path so the body,
  hands, and held equipment remain aligned while the root is smoothed.
  Reset interpolation across teleports and large position changes.
- Report the actual mouse turn rate instead of maximum turning speed, and send
  the stop when mouse turning ends. Reject stale or duplicate velocity packets
  while correctly handling sequence wraparound.

### Retail selection indicators and jump meter

- Use retail's selection sphere and original corner-arrow artwork. Place the
  arrows outside the projected object bounds, honor UI scaling and screen margins,
  and keep their placement correct when the camera is pitched.
- Allow the jump progress bar to be dragged by its fill, label, or border while
  the UI is unlocked. Retain its position through normal layout saving and
  saveui/loadui, and preserve its fixed height and existing appearance.

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
replication require this project's VR-enabled ACE server. Existing v60 instant
atlatl/thrown release and v69 restricted-door server changes remain required for
those server-side behaviors. This release changes client code; publishing client
packages does not update a game server.

GDLE login has been verified through character selection. The v64 packet-timing
correction is included. A live Seedsow item-interaction retest remains pending;
automated protocol checks do not establish full in-world GDLE compatibility.
Bundles contain no server, credentials, saved settings, SDK tools, or retail DAT files.

## Validation and limitations

The packaged Windows client passed movement, radar artwork, UI layout, and
rendered UI suites. Windows and Quest source builds also passed before packaging.

Eight relevant automation suites passed, covering movement, actor runtime,
interior streaming, idle actor cost, VR pose buffering, retail radar artwork,
UI layout persistence, and rendered UI screens. Movement checks include slopes
at 30, 90, and 144 FPS, indoor floors, airborne behavior, and packet ordering.

Live headset and multiplayer acceptance of these changes remains pending.
This release does not include a new FPS benchmark; sustained 90 FPS VR and
144 FPS desktop remain targets. Download hashes are in SHA256SUMS.txt.
