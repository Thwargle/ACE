# AC:Unreal / AC:VR release 65

Native Quest: `2026.09.22.quest.65`, Android version code 65, installer revision 6.
Windows desktop / PC VR: `2026.09.22.71`.

## Changes since v64

- Correct dark lines around tree texture edges by following retail's clamped
  object-texture sampling. Faces explicitly marked for repeating textures still
  repeat. Full DAT texture resolution and the existing mip chain are retained.
- Preserve texture addressing through procedural meshes, static scenery, building
  and interior materials, and the disk cache. Clamped and repeating uses of the
  same image no longer share a conflicting sampler.
- Explain recognized ACE and GDLE login failures with actionable messages, including
  accounts already online, full servers, unsupported login methods, and character
  loading problems. Original error codes remain available for bug reports.
- Keep unknown rejections explicitly unknown instead of blaming the password.
- Apply these fixes to desktop, PC VR, and standalone Quest.

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

Development regression suites passed for login errors and packet handling, responsive
launcher layouts, foliage sampling, appearance placement, mesh application, polygon
sides, and texture budgets. Foliage checks cover 19 real DAT surfaces and pass with
both desktop and mobile-preview rendering. Login messages were visually checked at
VR-panel, 720p, and narrow-window sizes.

The packaged Windows client also passed LoginHandshake, NetworkTransport,
ResponsiveUI, and FoliageSampling.

This release has not received live gameplay acceptance for the reported tree edges
or duplicate-account login. Compilation and automated checks are not headset acceptance.
There is no new FPS benchmark in this release; sustained 90 FPS VR and 144 FPS
desktop remain targets. Download hashes are provided in SHA256SUMS.txt.
