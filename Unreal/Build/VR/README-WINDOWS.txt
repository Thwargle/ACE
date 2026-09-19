AC:Unreal / AC:VR - WINDOWS CLIENT

1. Extract the complete AC-Unreal-and-AC-VR-Windows folder. Keep ACUnreal.exe,
   ACUnreal, and Engine together. Unreal Editor is not required.

2. Have your own Asheron's Call data installed at:
      C:\Turbine\Asheron's Call
   Required files: client_portal.dat, client_cell_1.dat,
   client_local_English.dat. client_highres.dat is optional.
   Game data is not included in this download.

3. Desktop play (AC:Unreal): run AC-Unreal.bat.
   ACUnreal.exe is the shared runtime executable.

   PC VR: start the Meta PC software and connect your headset. For a Quest,
   enter its PC Link/Air Link connection first. Start SteamVR and wait for the
   headset and controllers to connect. Set SteamVR as the current OpenXR
   runtime in its settings, then double-click AC-VR.bat (AC:VR).
   The existing Launch-VR.bat shortcut also works.

4. Enter the server host/address, port, and YOUR account and password.
   Ask the server owner for these. The packaged LAN default is not an Internet
   address. The host needs this project's VR-enabled ACE server for VR combat
   and motion replication. Login uses UDP 9000; world traffic uses UDP 9001.

Do not copy another person's Saved folder into this package. Your own login
and preferences are saved locally after you enter them. No shared account is
included. See RELEASE-NOTES.md for changes and known limitations.
