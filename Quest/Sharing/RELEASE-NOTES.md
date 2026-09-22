# AC:Unreal / AC:VR release 64

Native Quest: `2026.09.22.quest.64`, Android version code 64, installer revision 6.
Windows desktop / PC VR: `2026.09.22.70`.

## Changes since v63

- Correct packet timestamps to use retail's half-second network intervals instead
  of CPU clock ticks. The previous values could trigger GDLE's speed-hack
  disconnect check during ordinary gameplay, including item interactions.
- Use the same connection clock for retransmitted packets, reset it on reconnect,
  and preserve the protocol's 16-bit wraparound for long sessions.
- Apply the correction through the shared networking code in Windows desktop,
  PC VR, and standalone Quest; ACE and GDLE use the same interval format.
- Add regression coverage for using, moving, and equipping items, GDLE inventory
  replies, packet checksums, retransmissions, and the server's timing check.

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

GDLE login has been verified through character selection. This release corrects
a confirmed protocol error found while investigating item-interaction disconnects
on Seedsow. A live Seedsow retest is still pending; automated protocol checks do
not establish full in-world GDLE compatibility. Bundles contain no server,
credentials, saved settings, SDK tools, or retail DAT files.

## Validation and limitations

Development and packaged Windows networking suites passed: GDLEInteractionTransport, LoginHandshake,
NetworkTransport, and LinkTiming. Item coverage captures 480 use/move/equip packets
over loopback and checks successful inventory replies without leaving the world.

This release has not received live Seedsow or headset acceptance.
There is no new FPS benchmark in this release; sustained 90 FPS VR and 144 FPS
desktop remain targets. Download hashes are provided in SHA256SUMS.txt.
