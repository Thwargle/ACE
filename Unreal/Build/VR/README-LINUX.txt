AC:Unreal - NATIVE LINUX DESKTOP CLIENT (x86_64)

SETUP
Extract the complete AC-Unreal-Linux-v*.tar.gz archive into a writable folder.
Open that folder in a terminal and run ./AC-Unreal.sh. Keep AC-Unreal.sh,
ACUnreal, and Engine together. This package contains a native Linux ELF binary;
Wine, Proton, Unreal Editor, ThwargLauncher, and Decal are not required.

Use an x86_64 Linux distribution with glibc 2.28 or newer, Linux kernel 4.18.20
or newer, and a Vulkan-capable GPU. Ubuntu 22.04 or newer is a useful baseline.
Epic's UE 5.8 Vulkan driver baseline is AMD RADV 24.2.8+ or NVIDIA 570+.
Install graphics and audio drivers through your distribution's package manager.
Engine requirements: https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-requirements-for-unreal-engine
This initial Linux release targets desktop play. Linux VR is not validated.

AMD RADV compatibility: Linux disables pipeline precaching to avoid an ACO
store_deref shader-compiler abort. Pipelines compile on demand, which can
cause a brief first-use hitch. For older builds, launch with:
  bash ./AC-Unreal.sh '-ini:Engine:[ConsoleVariables]:r.PSOPrecaching=0'

GAME DATA
Provide your own updated Asheron's Call DAT files. Required: client_portal.dat,
client_cell_1.dat, client_local_English.dat. client_highres.dat is optional and
recommended. Retail download and update guide: https://emulator.ac/how-to-play/
Keep all files in one permanent folder and enter its absolute Linux path in
the login screen, for example /home/you/Games/AsheronsCall. Filenames are case
sensitive. You can also use the app DAT folder shown in Game files.
Game data is not included.

CONNECT AND SAVED SETTINGS
Use Browse servers in the login screen, or add a custom server with the address,
port, and ACE/GDLE type supplied by its owner. Enter your account credentials,
press Launch, select a character, and Play. Server websites explain registration.
Linux saves server details, account names, and settings, but does not save
passwords. Enter the account password again after restarting the client.
Saved data normally lives under ~/.config/Epic/ACUnreal/Saved/.
Never share another player's Saved folder.

UPDATES AND HELP
The login screen downloads and verifies Linux releases. Choose Install and
restart to update this installation and reopen the game. Python 3 (/usr/bin/python3)
and a writable installation folder are required. Close other clients using the
same installation first. Saved settings, DAT files and user plugins are retained.
Automatic updates are optional and only run in the launcher, never during play.
Older releases without the update helper need one manual archive extraction.
If installation fails, the previous files are restored and the client reports
the error on its next launch. Update results are in Saved/Updates.
Linux allows 45 seconds for requested exit/fatal-error cleanup before terminating
a stuck process. This does not monitor frame rate, window focus or minimization.
SHA256SUMS.txt contains release file hashes. From a folder containing the archive
and checksum file, use sha256sum --ignore-missing -c SHA256SUMS.txt.
See RELEASE-NOTES.md for validation and remaining preview limitations.
https://thwargle.com/unreal/
