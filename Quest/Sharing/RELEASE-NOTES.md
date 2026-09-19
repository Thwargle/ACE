# AC:VR Quest release 49

Native Quest: `2026.09.19.quest.49`, Android version code 49, installer revision 6.

## Install or update

Extract the entire ZIP. Put Android platform-tools beside the installer, connect
the Quest with a reliable USB data cable, and accept USB debugging in the headset.
Run **Update-Quest.cmd** if game data is already installed. For a first installation,
run **Install-Quest.cmd** and provide your own Asheron's Call DAT files.

The app appears as **AC:VR** in Unknown Sources. Do not uninstall the existing app:
updating retains DAT files, saved accounts, and settings. The installer migrates
the old runtime folder when necessary. See README.txt for setup and troubleshooting.

## Changes since release 48

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
needs testing. The default 10.0.0.26 address is private to the host's LAN.

This bundle contains no server, credentials, saved settings, SDK tools, or retail
DAT files. Each recipient supplies their own data and Android platform-tools.

## Validation and remaining checks

The Android ARM64 package built successfully and its signature and release
version were verified. All 15 Windows installer regression cases and the three
profile-migration checks on Quest passed. Shared PC/Quest source is synchronized.

The shared desktop login, server-profile, handshake, portal-reveal, Sanctuary
loading, and interior-streaming regressions passed. The broad world-entry suite
retains the previously recorded bench-stepping failures. Native Quest interaction,
multiplayer gameplay, and comfort require playtesting after installation.

This remains a test release. Consistent 90 FPS VR has not been established, and
this release does not claim a new measured frame-rate improvement. Landscape
textures retain full-resolution retail blends and uncompressed GPU pixels.
