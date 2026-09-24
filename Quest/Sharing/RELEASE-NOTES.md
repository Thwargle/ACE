# AC:Unreal / AC:VR release 73

Native Quest: `2026.09.24.quest.73`, Android version code 73, installer revision 6.
Windows desktop / PC VR: `2026.09.24.79`.

## Changes since v72

- Add automatic version checking at the account launcher using the public website.
  An Updates page shows available versions, download progress, and installation actions.
- Updates are optional. An offline or unavailable website does not block login.
  Downloads run asynchronously, stream to disk, and can be cancelled and retried.
- Verify exact download size and SHA-256 before enabling installation. Reject
  malformed metadata, unexpected filenames, unsupported schemas, and older releases.
- Windows: Install and restart updates the current installation and reopens in
  the same desktop or PC VR mode. Portable installations keep their current folder.
- Quest: download the APK over Wi-Fi, then request the Android install confirmation.
  If asked, allow AC:VR to install updates and press Install again. The installer
  verifies the package identity, newer version code, and the existing signing key.
- Accounts, settings, and retail DAT files are retained. The USB Quest updater and
  website installers remain available as fallbacks.
- Extend release publishing with a direct Quest APK and versioned update metadata.
  New downloads are verified before the website switches to the new release.

## Enabling updates for the first time

Older clients do not contain the updater. Install v73 once using the Windows setup
or the Quest USB bundle. Future versions can then be downloaded from Updates in
this build's login screen. The updater never installs during gameplay.

Windows users select Download update, then Install and restart. Quest users select
Download update, then Install and confirm in the headset. Do not uninstall first.
Keep a separate backup of your retail DAT files. No retail DATs are bundled here.

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

Automated checks cover manifest validation, bounded downloads, corrupted/truncated
payload rejection, cancellation, launcher layout and controls, and desktop/VR
restart helper behavior. Windows and Quest packages are built before publication.
Live headset confirmation and a real version-to-version upgrade remain acceptance
checks; compilation and fixture tests alone do not certify every headset OS.
No new performance benchmark is claimed. Download hashes are in SHA256SUMS.txt.
