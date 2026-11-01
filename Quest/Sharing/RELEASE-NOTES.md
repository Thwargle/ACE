# AC:Unreal / AC:VR release 147

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.01.147**. Android version code: 147.
Quest installer revision: 9. These notes cover changes since v146.

## Chat button artwork

- Fix duplicated backgrounds on the main chat target selector and Send button
  when increasing chat text size. Each button now draws one complete scaled face.
- Scale the chat entry strip vertically with the input row while keeping its
  horizontal texture repeat, preventing repeated top/bottom borders.
- Preserve ordinary DAT border and scrollbar tiling, along with the auxiliary
  chat windows' text-only Send states.

## Validation and packaging

Regression checks cover all five chat fonts and sizes, enlarged/resized chat
windows, normal/pressed/disabled button states, panel layout, and update behavior.
Small, medium, and extra-large chat renderings were inspected during the fix.
Release checks run in the editor and packaged Windows/Linux clients. Android
packages are checked for identity, version, signing continuity, 16 KiB alignment,
and bundled plugin contents; the Windows installer is tested upgrading v146.

Packages exclude retail DATs, personal settings, server data, and generated
MeshCache files. Android phones/tablets remain a preview using a separate APK
from Quest. No physical-device or live-server acceptance test was performed.
