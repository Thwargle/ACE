# Client updates

The packaged login screen checks `https://thwargle.com/assets/ac/release.json`
once per app launch. The Updates page also supports a manual check. A failed
check never prevents login. Download and installation are explicit user actions
and are restricted to the disconnected launcher.

Both Windows modes use the same installer. The helper waits for the game to exit,
verifies the installer again, installs into the existing directory, and restarts
in desktop or VR mode. Portable updates suppress shortcuts and uninstall
registration. No uninstall, DAT deletion, or settings reset is performed.

Quest downloads a direct APK into private app storage. Android owns the update
confirmation and package replacement. Installation requires the same package and
signing certificate and a newer version code. The player may need to grant the
app permission to request installations. The USB updater remains the fallback
for headset OS versions that do not expose that permission or confirmation UI.

## Publishing a newer version

1. Increment `ACEClientBuild::ReleaseNumber` and the Quest Android StoreVersion
   together. Use one `YYYY.MM.DD.ReleaseNumber` display version in
   `ACEClientBuild::Version`, both ProjectVersion settings, and the Quest
   VersionDisplayName. Windows desktop, PC VR, Quest, and the Windows installer
   must show the same version. Both build scripts run `Test-ReleaseVersion.ps1`
   to reject mismatches before compiling; release assembly also checks the APK.
2. Build and test Windows and Quest. Keep the Quest package ID and signing key.
3. Run `Quest/Build-SharePackage.ps1`, `Build-Release.py`, and
   `Build-WindowsInstaller.ps1` with the new release number.
4. Run `Stage-WebsiteRelease.py` against the completed release directory. It
   verifies the artifacts and adds schema 1 update metadata to the website.
5. Run the website's `_tools/check-ac-pages.py`, then its FTPS deployment helper.
   Upload and verify every versioned artifact before publishing `release.json`.
6. Verify the public HTTPS downloads and test Updates from the preceding build.

`updates.windows` describes the EXE installer; `updates.quest` describes the
direct APK, not the USB ZIP. Each includes `file`, `bytes`, and `sha256`. The
client constructs the URL from the fixed website host and numeric release. JSON
cannot provide an executable command, arbitrary URL, or local path. Payloads
stream to disk with a size limit and are SHA-256 checked before installation.
HTTPS authenticates the manifest; the hash detects corrupted downloads. It is
not an independent release signature if the publishing account is compromised.

Never replace bytes under a release number after distributing it. Publish a new
number instead. Clients skip equal/older releases; restoring older website links
does not downgrade an installed client. Keep old downloads for manual rollback.

## Verification

- Unreal automation: `ACE.Updates` and `ACE.Launcher` in editor and packaged builds.
- `Test-UpdateClient.ps1`: isolated fixture executables cover desktop/VR restart,
  quoted paths, portable arguments, and rejection of corrupt installers.
- Test an actual installer against an isolated copy of the previous portable
  release; verify saved configuration and DAT fixtures survive unchanged.
- Verify APK signatures match the preceding release and inspect the packaged
  install permission and explicit, non-exported result receiver.
- Headset acceptance must exercise permission, confirm/cancel, and reopening the
  updated app. Compilation alone does not establish OS confirmation behavior.

v73 is the first release with this updater. Earlier clients need one manual
Windows installation or Quest USB update to acquire it.
