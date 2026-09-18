# ACE VR test release 18 — September 14, 2026

Quest: 2026.09.14.quest.18. Windows: 2026.09.14.28.

This packages the version 17 gameplay fixes, with personal account defaults
removed from both clients. New installations start with empty account fields;
existing saved logins and settings remain on their own devices. Use these
packages for sharing instead of earlier bundles.

Included fixes:

- Fixed signs and floating portals keep the server's placed position instead
  of falling under a received velocity. Creature jumps and projectiles retain
  their movement. The Smithy fix was verified on the Quest in version 17.
- Colored sky rectangles and the moon border blend correctly. Version 16's
  modest foliage-edge improvement is retained.
- B inspects the hovered inventory item in VR.
- Existing VR combat, body/hand tracking, menus and saved settings are retained.

This is a development test release. Quest frame rate still needs improvement;
the latest live sign-area sample was about 28 FPS. This release contains no new
performance optimization beyond version 17. Later performance work will use a
different release number.

For native Quest, send ACE-Quest-Test-v18-installer-r4.zip. Extract it and read
README.txt, then run Install-Quest.cmd. A USB data cable, Developer Mode, Android
Platform-Tools, and the recipient's own Asheron's Call DAT files are required.
An existing installation can be updated without uninstalling.

For PC VR or desktop play, send ACE-Windows-Test-v18.zip. Extract the complete
folder and read README-WINDOWS.txt. Unreal Editor is not needed.

Both clients need a reachable ACE server and the recipient's own account. The
server must include this project's VR extensions for custom VR combat and poses.
The default private address 10.0.0.26 only works on the host's LAN or a routed
VPN; remote friends need the host's reachable address and UDP ports 9000/9001.
Game DATs, personal settings, logs, database files and server credentials are
not included. Server software is unchanged and is not bundled with the clients.

SHA256SUMS.txt provides checksums for the two downloads. Native Quest gameplay
acceptance applies to version 17's equivalent gameplay code; version 18's
credential-default change is validated through packaging and login regressions.
