AC:Unreal / AC:VR - WINDOWS CLIENT

SETUP
Use AC-Unreal-Setup to install the desktop and SteamVR clients with the AC logo
and Start menu shortcuts. Install the included Microsoft Visual C++ x64 and
GameInput runtimes on the final screen if this is your first installation.
These Microsoft components may require administrator approval.

For the portable ZIP, extract the complete folder. Keep ACUnreal.exe, ACUnreal,
and Engine together. Run AC-Unreal.bat for desktop or AC-VR.bat for SteamVR.
Install Microsoft Visual C++ x64 if needed:
https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
Unreal Editor, ThwargLauncher, and Decal are not required.

GAME DATA
Provide your own updated Asheron's Call DAT files. Required: client_portal.dat,
client_cell_1.dat, client_local_English.dat. client_highres.dat is optional and
recommended. Retail download and update guide: https://emulator.ac/how-to-play/
Keep all files in one permanent folder, for example C:\Turbine\Asheron's Call,
and select that DAT folder in the login screen. Game data is not included.

CONNECT
Use Browse servers in the login screen, or add a custom server with the address,
port, and ACE/GDLE type supplied by its owner. Imported servers retain their
listed ports. Choose an account or save your own, press Launch, select a
character, and Play. Server websites/Discords explain registration requirements.
Never share your password or another player's Saved folder.

PC VR
Connect the headset using Steam Link, or Meta Link/Air Link and SteamVR.
Wait for the headset and controllers to connect. Select SteamVR
as the current OpenXR runtime in its settings. Launch AC VR (SteamVR) or
AC-VR.bat. Native Quest uses the separate Quest installer, not this package.
Full physical VR combat and motion replication require a VR-enabled ACE server.
GDLE authentication has been verified; full in-world compatibility needs testing.

On Quest controllers, hold X for about a second to open VR options, including
through Steam Link when the Menu button is reserved by the runtime. Tapping X
still toggles inventory. Menu also opens VR options when available to the game.

UPDATES AND HELP
From v73 onward, use Updates in the login screen: Download update, then Install
and restart. The client reopens in the same desktop or PC VR mode. Older builds
need one manual update to enable this. Website installers remain available. Saved accounts and
settings are stored separately; keep your DAT backup. The community Windows
installer is unsigned; SHA256SUMS.txt contains file verification hashes.
See RELEASE-NOTES.md for exact validation and remaining preview limitations.
https://thwargle.com/unreal/
https://thwargle.com/unreal-vr/
