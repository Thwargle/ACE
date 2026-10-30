# AC:Unreal Mobile

Android phone and tablet development target. This is separate from the Quest app:
`com.acecommunity.acunreal.mobile`, landscape, ARM64, Android 10+, Vulkan/ASTC.
Compatibility and performance still require testing on physical devices. No
Google Play publication is configured. From v142, the website offers a separate
phone/tablet preview APK, and in-game updates use the Android artifact rather
than the Quest package. USB setup with the helper below is still required for DATs.
Update notices identify AC:Unreal Mobile and explain Android installation and
permission prompts for phones and tablets. After installation, reopen the app
from your apps; existing DAT files, accounts, and settings are kept.

First hardware target: Samsung Galaxy Z Fold7. Layout tests include its landscape
cover display (2520×1080) and unfolded display (2184×1968), with resize support
and safe-area margins. Display dimensions: [Samsung specifications](https://news.samsung.com/ca/samsung-galaxy-z-fold7-raising-the-bar-for-smartphones).
Folding/unfolding cancels active touches before controls move to the new layout.
Control positions, sizes, bindings, and hidden/added buttons persist across app
restarts. Folding or opening pinned panels can temporarily fit controls to the
available space without replacing your preferred layout. Valid drag positions
also save if the app goes into the background before you lift your finger.

## Install the downloaded preview

Download the Android preview APK from [AC:Unreal downloads](https://thwargle.com/unreal/#android-setup).
On a Windows PC, download/extract this repository (or clone it), install PowerShell 7,
and install Google's Android SDK platform tools and build tools. You do not need
Unreal Engine to install an existing APK. From the repository folder, run:

```powershell
& 'C:/Android/platform-tools/adb.exe' devices -l
./Mobile/Install-Android.ps1 -Serial 'YOUR_DEVICE_SERIAL' `
  -Apk 'C:/Users/YOU/Downloads/AC-Unreal-Android-v142.apk' `
  -AdbPath 'C:/Android/platform-tools/adb.exe' `
  -AaptPath 'C:/Android/build-tools/35.0.1/aapt.exe' `
  -DatDirectory "C:/Turbine/Asheron's Call"
```

Replace the example paths and serial with your own. Keep `Mobile/` and `Quest/Sharing/`
in their original repository structure: the installer uses the shared DAT-transfer helper.
Unlock the device and authorize USB debugging when prompted. Later APK updates retain
the copied DATs and settings; initial DAT setup still requires this USB helper.

## Build from source

From the repository root in PowerShell 7:

```powershell
./Mobile/Build-Android.ps1
./Quest/AndroidSDK/platform-tools/adb.exe devices -l
./Mobile/Install-Android.ps1 -Serial 'YOUR_DEVICE_SERIAL' -DatDirectory "C:/Turbine/Asheron's Call"
```

The build uses UE 5.8, the same Android SDK/NDK/JDK as Quest, and the shared client
source and generated DAT runtime materials. Override the script parameters for
different tool locations. The output is
`Mobile/Packaged/Android_ASTC/ACUnreal-arm64.apk`.

Enable USB debugging and authorize the computer on the device before installing.
The installer validates the app ID, checks Android/ARM64 support, and verifies
DAT file sizes and SHA256 hashes before replacing files. It preserves settings.
The APK contains the client; supply your own retail DATs separately. Required:
`client_portal.dat`, `client_cell_1.dat`, `client_local_English.dat`.
`client_highres.dat` is optional. The current development APK supports `run-as`
for installing these files; a future store build needs a platform file importer.

## Touch controls

- The D-pad moves and strafes at retail run speed as soon as a direction is touched.
  Drag between directions for diagonals; hold Walk for walking. Separate left/right
  keys turn the character; camera rotation remains a swipe gesture.
- Swipe empty world space to orbit the camera. Pinch with two fingers to zoom.
- Hold Jump to charge, then release. A footer meter shows the game's actual jump charge.
  Different fingers can move, turn, and jump together. Turn keys have at least 64 logical
  pixel targets separated by a 32 logical pixel neutral gap.
- Tap a creature/object to select it; Use interacts and Attack uses the equipped combat mode.
- Spells opens the active spell bank. Choose a tab/page and spell, then tap Attack in magic
  mode or a button rebound to Cast. Selecting a spell does not cast it.
- Menu → Combat settings changes height, power/accuracy, auto repeat, and auto target.
- Menu → Edit control layout lets you drag, resize, rebind, and hide buttons. Touch settings
  adds buttons, restores hidden buttons, mirrors the layout, and adjusts camera sensitivity
  and opacity. Layouts persist locally. Reserved menu space and overlapping controls are blocked.
- Actions can use any of the 18 item shortcut slots or the first 12 slots of the active spell bank.
- The D-pad's transparent corners remain available for camera gestures.
  Mode uses retail peace/combat artwork; item and spell buttons follow their current bindings.
  The footer shows the selected attack spell, and its reserved space cannot be covered by controls.
- Icons retain their native proportions. Resizing scales the artwork, labels, padding,
  corner radii, and borders together. D-pad keys have equal frames and gaps; turn keys
  remain square at every size. Short labels fit within their reserved text area.
- Plugins opens the installed plugin browser. Enabled plugins also appear in a scrollable
  left rail with their own icons. Waypoint readouts, ledgers, UCM Micro, Action Deck, and
  companion readouts can remain visible in a horizontally scrollable HUD area. Its space
  is reserved above movement/action controls; each panel title opens its full settings.
  Touch settings and each plugin page offer a separate Show/Hide on screen option.
- Chat can use the full-screen phone panel or a pinned transcript with an inline message
  field. Pin/Unpin in Chat or change the mode in Touch settings. Typing cancels held movement;
  merely displaying the transcript does not. Chat mode and plugin visibility are saved locally.
- Vitals use the retail DAT meter caps, backgrounds, and fills. Inspect opens the
  selected object's appraisal in a touch panel and is also available for rebinding.
- Missing inventory artwork retries in place without rebuilding the bag or losing selection.
- Inventory uses paged, finger-sized slots. Select an item for Use/Equip, inspection, quantity,
  left-hand equipment, give, drop, or moving to another bag. Moving uses the existing stack-merge handling.
- Spellbook and Hotbars configure the shared banks without requiring drag-and-drop.
- Loot, vendor, and trade open a full-screen touch panel when the server opens them.
- Books and server confirmations open readable touch dialogs, including accept/decline actions.
- Menus block world touches and release held movement/attacks. Backgrounding, capture loss,
  rotation/resizing, logout, and portal transitions also clear input.
- Character creation uses the retail DAT model, validation, appearance rendering, and protocol.

The mobile HUD replaces floating desktop windows while active. Its safe-area layout
keeps the top menu accessible; vendor/inventory/settings occupy modal panels. Desktop
plugin windows are replaced by mobile HUD panels and the Plugins menu. Plugins retain
their authored widgets and live data through the same host, without a second script VM
or snapshot loop. Larger panels scroll within their reserved area. Modal panels, the
spell wheel, and login screens hide these in-game readouts.

Android phone/tablet builds choose a bounded texture streaming pool from physical RAM:
one eighth, rounded down to 64 MiB, within 256–1024 MiB. The Fold7's recorded RAM selects
1024 MiB instead of its previous 600 MiB pool. This does not disable texture streaming
or suppress memory warnings; texture quality still controls DAT mip resolution. Quest
and desktop budgets are unchanged. Memory pressure and sustained performance still
need validation on the phone after this change.

## Development checks

Use `-ACEMobile -nohmd` on the desktop client to preview the phone presentation
without changing desktop preferences. `ACE.Mobile` automation covers layout,
saved rebinding, simultaneous fingers, cancellation, spell-wheel selection,
menu refresh costs, character creation, launcher sizing, and rendered phone/tablet
screens. Run the existing `ACE.VR` suites after changing the shared menu widgets.

Before distributing a mobile release, test on real phones/tablets: login/native
keyboard, notches, multi-touch movement/combat, bag transfers and stacks, looting,
buy/sell/trade, suspend/resume, reconnect, and sustained thermal/memory behavior.
Desktop rendering and automation do not establish Android device compatibility.

The initial Fold7 development package passed the Win64 editor and Android ARM64
builds, eight targeted automation suites, APK signature verification, and 16 KiB
alignment checks for every packaged native library. Its manifest enables window
resizing and screen-size changes without requiring a headset. The development APK
was installed over USB on a Galaxy Z Fold7 (SM-F966U, Android 17); all four DATs
were hash-verified and the mobile login screen was confirmed on the device.
The Android device-profile selector is explicitly enabled because this project's
game target disables implicit engine plugins. Without it, the generic Android
profile disables Vulkan even on supported GPUs. The Fold7 now selects
`Android_Adreno8xx_Vulkan` and initializes Vulkan successfully. The user has since
confirmed in-world play; fold/unfold behavior and sustained performance still
require physical testing.

The first gameplay report exposed repeated receive timeouts about 81 seconds after
login. Android now refreshes the inbound UDP mapping every 20 seconds using the
existing retail connectionless NOP. Desktop retains its 110-second cadence. The
loopback regression verifies wire bytes, sequence/cache isolation, and simulated
60-second NAT expiry for ACE and GDLE. The user confirmed the updated Fold7 build
stayed connected during the requested three-minute check, beyond the previous
81-second disconnect window. Longer sessions and other network paths remain untested.

The October 28 touch update passed `ACE.Mobile.LayoutAndUpdates`,
`ACE.Mobile.TouchAndMenus`, `ACE.Network.ReceivePortKeepalive`, `ACE.VR.Protocol`,
and `ACE.VR.RigAndMenus`. Rendered fixtures cover 1280x720, 2520x1080, 2184x1968,
and 1024x768, including the circular sticks, retail ability icons, spell wheel,
plugin browser, and embedded UCM panel. The APK was installed over USB and its
on-device SHA256 matched the built artifact. All four existing DATs were verified.
On October 28, the user confirmed that the updated build stayed connected and that
the new controls, icons, and Plugins button appeared correctly on the Fold7.

The subsequent October 28 D-pad update replaces analog movement and the turn stick.
Touch-down sends the same full-rate directions as retail keyboard movement, avoiding
an initially slow analog rate being captured by the walking transition. Saved layouts
are migrated to finger-sized D-pad keys and gain Inspect without resetting bindings.
Enabled plugins have a reserved left rail; inventory icon loads retry with bounded
work and backoff; vitals use the end-of-retail `RootGameplay_FloatyVitals_Field` artwork.
`ACE.Mobile.LayoutAndUpdates`, `ACE.Mobile.TouchAndMenus`, `ACE.VR.Protocol`,
`ACE.VR.RigAndMenus`, and `ACE.RetailParity.IconRendering` passed. Tests cover immediate
movement/turning, independent touch capture, plugin-bar hit testing, appraisal requests,
delayed icons without inventory rebuilds, and phone/tablet layouts. Win64 editor and
Android ARM64 builds passed; the signed, 16 KiB-aligned development APK was installed
on the Fold7 with matching SHA256. In-world feel still needs the user's hardware check.

The October 28 button-scaling follow-up preserves rectangular DAT icon proportions
and scales control content and frames together. Both `ACE.Mobile` suites passed,
including geometry checks from the smallest to largest supported controls and
rendered default/enlarged layouts on phone, unfolded Fold7, and tablet resolutions.
The Win64 editor and Android ARM64 builds passed. The signed, 16 KiB-aligned APK
was installed over USB with its on-device SHA256 verified; all four DATs and saved
settings were retained. Physical gameplay confirmation of this visual update is pending.

Mobile updates use the manifest's `android` artifact and `androidVersion`, named
`AC-Unreal-Android-vN.apk`. A missing mobile artifact is reported as unavailable;
the mobile app never downloads a Quest APK as a substitute.

The October 28 HUD follow-up widens turn-key spacing, adds the actual jump-charge
meter, restores persistent plugin readouts, and offers pinned or full-screen chat.
Both `ACE.Mobile` suites passed, including real Slate touch routing into pinned chat,
plugin window visibility, and collision checks for the reserved HUD area. Rendered
fixtures cover 1280x720, 2520x1080, 2184x1968, and 1024x768. Win64 editor and Android
ARM64 builds passed; APK signature and 16 KiB package alignment checks passed.
The phone was disconnected, and the user requested continuing without it. This APK
has not been installed or tested on hardware. The RAM-based texture pool adjustment
is included, but whether it resolves the reported warning remains unverified.
