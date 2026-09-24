# AC:Unreal / AC:VR release 72

Native Quest: `2026.09.24.quest.72`, Android version code 72, installer revision 6.
Windows desktop / PC VR: `2026.09.24.78`.

## Changes since v71

### Resizable retail windows

- Fix the shared panel resize calculation for allegiance, fellowship, attributes,
  journal, map, and options. Lists grow with the panel and bottom controls stay
  inside the frame when it is enlarged or reduced repeatedly.
- Use the resolved retail layout and its minimum panel height. Remove duplicate
  per-tab positioning that moved controls beyond the window after resizing.
- Clip text and pointer hit regions to their actual parent panels, including
  offset list rows, hidden tabs, and overlapping windows.
- Retain the native map size, aspect ratio, paper frame, and centered placement.

### Fellowship and allegiance

- Fix fellowship rows intercepting clicks anywhere in the panel. Recruit,
  Open/Close, Make Leader, Dismiss, Quit, and Disband now receive their own clicks.
- Preserve retail leader/member permissions and live server-backed state.
- Add confirmation prompts for swearing allegiance, breaking with a patron,
  and removing a vassal. Confirmations retain the selected target and check that
  the action is still valid before sending it.
- Enable allegiance controls only for eligible players, an actual patron, or
  a selected current vassal. Stop the old fallback of breaking with the monarch.
- Make the vassal list scrollable with wheel, arrows, and scrollbar dragging;
  keep names and XP in separate columns and prevent row text overflowing.
- Use retail's original social button captions and column labels; correct
  checkbox placement when the canvas is scaled.

These shared UI changes apply to desktop, PC VR, and standalone Quest.

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

Rendered UI and interaction automation covers repeated panel resizing, footer
visibility, real pointer clicks, fellowship packet decoding and outgoing actions,
allegiance confirmations, list selection, scrolling, and saved layouts.

Windows and Quest builds and packaged Windows checks are verified before publication.
Live multiplayer and headset acceptance of this release remains pending.
No new performance benchmark is claimed. Download hashes are in SHA256SUMS.txt.
