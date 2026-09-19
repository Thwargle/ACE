# AC:Unreal / AC:VR release 48

Native Quest: `2026.09.18.quest.48`, Android version code 48, installer revision 6.
Windows desktop and PC VR: `2026.09.18.58`.

## Install or update

**Quest:** Extract the entire AC-VR-Quest ZIP. Put Android platform-tools beside
the installer, connect with a reliable USB data cable, and accept USB debugging
inside the headset. Run **Update-Quest.cmd** if game data is already installed;
for a first installation run **Install-Quest.cmd** and provide your own Asheron's
Call DAT files. The app appears as **AC:VR** in Unknown Sources. Update normally
over your existing installation; do not uninstall it first. Your data, login entries, and
settings are retained. See README.txt for setup and troubleshooting.

**Windows:** Extract the entire AC-Unreal-and-AC-VR-Windows ZIP. Run
**AC-Unreal.bat** for desktop or **AC-VR.bat** for PC VR. Unreal Editor is not
required. The shared runtime is ACUnreal.exe; Launch-VR.bat is also available.
See README-WINDOWS.txt for DAT and SteamVR/OpenXR setup.

Use your own account and a reachable server address. The default 10.0.0.26 is a
private LAN address. Custom VR combat and pose replication require this project's
updated VR-enabled ACE server. These downloads contain no server, account data,
saved settings, or retail DAT files.

## Naming cleanup in release 48

- Renamed the shared Unreal project, module, targets, executable and launch tools
  to ACUnreal, with AC:Unreal / AC:VR display names.
- Quest source and tools now live under Quest/. The public native installer
  contains AC-VR-arm64.apk.
- Existing login, settings, journal and game data migrate without replacing
  newer profile files. The Android installation ID remains stable for updates.
- Old class names remain only as compatibility redirects for existing maps.

## Changes since release 42

- The desktop product is **AC:Unreal**; PC VR and native Quest are **AC:VR**.
  Both have the new circular AC launcher icon and updated visible names.
- Crossbow grip/rotation, initial ammunition placement, atlatl and thrown-item
  trigger firing, and bow draw offset have been revised. Thrown items use their
  own stack. These changes also update the equipment seen by nearby VR players.
- VR body, head, hand, equipment, and ammunition poses share a common body root
  and interpolate between updates. Nearby remote bows display a string.
- Movement has a stick-only reference option. Full-stick running uses the retail
  speed formula without the old learned slowdown multiplier.
- Close-range melee validation and creature spacing have been revised. Own VR
  projectiles retain caster immunity while moving, including backpedaling.
- The spell wheel requires a caster, and selecting a spell does not change stance.
  Deliberate two-hand use checks interaction range instead of requiring contact
  through collision. Underwater views no longer black out against the water surface.
- Spells in Effect uses retail row layout and formatted durations. Vendor counts
  respect stock and stack limits. Damage labels and the enemy vial alpha are revised.
- World selection respects visible walls in desktop and VR. Teleporting clears
  stale targets and health feedback. The Quest scenery-culling correction addresses
  the right-eye peripheral popping identified during headset testing.
- Rain respects opaque walls and terrain. Quest shadows use the corrected
  receiver configuration and the distribution accepted during the headset comparison.
- Landscape textures retain full resolution, uncompressed GPU pixels, and retail
  blend arithmetic. Lossless disk caching remains enabled.
- CPU work has been reduced in equipment lookups, portal geometry, object metadata,
  particle updates, and UI lookups. Native FPS targets have not yet been reached.

## Validation and remaining checks

Windows and ARM64 Android packages built successfully. The final Windows UI
regression passed, including the product/version command; all 15 installer
regression scenarios passed. Shared source and runtime materials match. Packaged
app labels and launcher icons were verified. Earlier gameplay, rendering, mobile
preview, and server regression results are documented in the source review.

This remains a test release. Fresh multiplayer and headset gameplay acceptance
is still needed, particularly for weapon alignment, close melee, movement,
two-hand use, and vendor/chat interactions. Recent controlled Quest captures were
roughly 46–53 FPS; consistent 90 FPS VR and 144 FPS desktop are not established.
Full-resolution terrain needs a new performance comparison. The visible road
texture seam and a Coldeve-specific DX12 rendering crash remain under investigation.
Packaging success is not a claim that those outstanding issues are fixed.
