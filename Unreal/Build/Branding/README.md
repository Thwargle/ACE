# AC:Unreal / AC:VR branding

- **AC:Unreal** is the desktop client.
- **AC:VR** is the PC VR and native Quest client.
- `AC-Icon.png` is the original generated artwork, with transparent corners.
- `Export-Icons.py` exports Windows `Application.ico` and all Android launcher densities using Pillow.
- Android resources live in `Unreal/Build/Android/res`; `Quest/Sync-ClientSource.ps1` copies them into the generated Quest project before packaging.

Windows cannot use a colon in filenames. Display names are **AC:Unreal** and
**AC:VR**; launchers and public archives use `AC-Unreal` / `AC-VR`. Both clients
share the `ACUnreal` Unreal project/module, targets and executable. The native
Quest workspace is `Quest/`; its public installer contains `AC-VR-arm64.apk`.

Existing profile files migrate to the shared `ACUnreal/Saved` location without
replacing newer settings. Quest migration moves the data in place, preserving
the encrypted login and avoiding another DAT transfer. The Android application
ID remains `com.acecommunity.questtest`: changing it would create a second app
and break access to existing app data and Keystore credentials. The remaining
original-name strings are compatibility readers and Unreal class redirects.
The ACE server, protocol, DAT asset names and plugin namespaces are unchanged.

## Validation — 2026-09-18

Windows `2026.09.18.58` and Quest `2026.09.18.quest.48` packaged successfully
under the renamed project. Packaged Windows UI, profile migration and encrypted
login tests passed. All 15 installer cases and six binary DAT-transfer checks
passed under Windows PowerShell. Isolated Android filesystem fixtures verified
atomic profile migration, preservation of a newer profile, and repeat upgrades.
The current source audit allows original names only in compatibility readers,
class redirects and their tests; Android retains its installation identity.
Quest/Windows shared source, materials and icons match. These new binaries have
not been deployed as part of this cleanup; the installed headset build is unchanged.

Artwork was created with the built-in image-generation tool. Original prompt:

> Create one polished application launcher icon for AC:Unreal and AC:VR, a modern Unreal Engine port of the classic fantasy game Asheron's Call. Use exact letters AC as a custom intertwined monogram, clearly readable as A and C, with bold angular blackletter / gothic calligraphic forms inspired by the Unreal Engine U logo's typography, but an original AC design rather than a U. Place the letters within one substantial circular silver-white ring. Sophisticated, clean, iconic vector-like silhouette, sharp intentional contours, subtle medieval fantasy personality, excellent legibility at 32 pixels. White / pale silver monogram and ring on a flat very dark charcoal disk, with only very restrained dimensional shading. The disk fills almost all of a square canvas with a small even margin. Outside the circular disk is true transparent alpha, not a checkerboard. No words, no subtitle, no additional letters, no game scene, no tiny decorative runes, no mockup, no background shadow, no perspective. Centered, straight-on, premium finished app icon. Output a single square high-resolution icon.
