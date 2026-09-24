# AC:Unreal / AC:VR release 69

Native Quest: `2026.09.23.quest.69`, Android version code 69, installer revision 6.
Windows desktop / PC VR: `2026.09.23.75`.

## Changes since v68

- Add retail-style /saveui, /loadui, /saveautoui, /loadautoui, and /lockui
  commands. Save named window layouts and explicitly save automatic layouts for
  the current character, server, display size, and desktop/VR mode.
- Save numbered PNG screenshots with numpad * by default. Chat confirms the
  actual saved path or explains a failure. Rebind Capture screenshot through the
  existing keyboard mapping window.
- Respect the ItemUseable flag on quest/puzzle doors, including live property
  changes, and explain when a door cannot be activated directly. Existing server
  quest, lock, key, and switch requirements remain authoritative.
- Make resized attribute lists work with the wheel, arrows, and scrollbar dragging.
  Scrolled selection still refers to the correct attribute. Repeat the chain
  scrollbar artwork instead of stretching it.
- Remember each spell tab's selected spell for the session. Returning to a tab
  restores its highlight and scroll position; first visits select the first spell.
  Next/previous wraps at the ends and keeps selection visible.
- Synchronize spell selection across desktop tabs, the VR wrist bar, and the VR
  wheel without casting or changing combat stance. Portal travel retains tab
  memory; logging out clears it.

## Layout files

Named layouts are stored in Saved/UILayouts. These are window geometry only;
spells, inventory shortcuts, chat filters, and keybindings are not changed.
The automatic layout is a snapshot saved by /saveautoui, not a continuous
save of every window movement. /loadui without a name loads UI-Default.txt.

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
must include the v60 instant atlatl/thrown release changes. This source update
also adds a server-side direct-use guard for restricted doors; server owners
must deploy the updated server to obtain that additional enforcement. Client
packages cannot update a remote server. Keys, switches, and scripted activation
retain their existing paths.

GDLE login has been verified through character selection. The v64 packet-timing
correction is included. A live Seedsow item-interaction retest is still pending;
automated protocol checks do not establish full in-world GDLE compatibility.
Bundles contain no server, credentials, saved settings, SDK tools, or retail DAT files.

## Validation and limitations

Windows and Android Development source builds and the ACE server Release build
passed. UI regressions cover retail layout-file parsing, layout restoration,
attribute scrolling, chain artwork, spell-tab memory/wrapping, screenshot writing
and feedback, screenshot rebinding, and restricted-door action suppression.
The VR rig tests cover wrist/wheel selection restoration without stance changes.
The packaged Windows v69 client also passed UIInteractions, UILayoutCommands,
UIScreens, and VR RigAndMenus (four suites).

These checks do not replace live quest/puzzle or headset gameplay acceptance.
There is no new FPS benchmark in this release; sustained 90 FPS VR and 144 FPS
desktop remain targets. Download hashes are provided in SHA256SUMS.txt.
