# AC:Unreal / AC:VR release 151

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.06.151**. Android version code: 151.
Quest installer revision: 9. These notes cover changes since v150.

## Interface and world visibility

- Keep large indoor scenery, including stairs, visible when its bounds extend
  into a visible room even if its origin cell is hidden.
- Restore horizontal resizing for vendor and salvage windows, sharing their width.
- Preserve spellbook school/level filters when learning a spell; reveal the new
  spell only when those filters admit it.
- Fix spell hotkeys 11 and 12 and display available-house replies from the server.

## UCM and plugins

- Use a player-targeted bane for equipped armor when refreshing partial coverage.
- Skip identified items without workmanship during automatic salvage.
- Make the enabled-plugin limit configurable; disabled plugins no longer consume
  the limit, and all discovered plugins remain available in the manager.
- Expand Lua API documentation, including loot matching, capabilities, settings,
  snapshots, custom graphics, and plugin limits.

## Display and updates

- Preserve the confirmed Linux resolution during delayed window resize events;
  use fullscreen when the requested decorated window cannot fit the monitor.
- Apply the selected texture filtering to terrain and world textures instead of
  forcing trilinear filtering.
- Replace the Windows updater's PowerShell dependency with a native helper for
  update/restart compatibility under Wine/Proton.
  Older builds with a broken updater may need one manual installation to acquire it.

## Diagnostics and validation

- Expand /acueobject with renderer-side draw state, bounds, geometry counts, and
  camera/interior diagnostics. The reported invisible Apostate Nexus Gateway is
  still under investigation; this release does not claim a confirmed gateway fix.

Automated checks cover the changed behavior, packaged clients, APK contents,
upgrade preservation, and public download integrity. Validation results accompany
the local release artifacts. Physical Linux 4K displays, a live Faugus restart,
Quest/phone hardware, and the affected gateway session were not acceptance-tested.

Packages exclude retail DATs, personal settings, server data, and generated
MeshCache files. Android phones/tablets remain a separate preview APK.
