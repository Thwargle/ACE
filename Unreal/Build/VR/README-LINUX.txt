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
The login screen checks for Linux releases and opens the Linux download in your
browser. Close the game, download and extract the new archive into a new folder,
then launch its AC-Unreal.sh. Saved settings are stored separately; keep your
DAT backup. In-game automatic installation is not available on Linux.
SHA256SUMS.txt contains release file hashes. From a folder containing the archive
and checksum file, use sha256sum --ignore-missing -c SHA256SUMS.txt.
See RELEASE-NOTES.md for validation and remaining preview limitations.
https://thwargle.com/unreal/
