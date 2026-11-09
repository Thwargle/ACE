# AC:Unreal / AC:VR release 153

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.09.153**. Android version code: 153.
Quest installer revision: 9. These notes cover changes since v152.

## UCM looting and salvage

- Remove the fixed three-second wait after successful salvage and salvage-bag
  combinations. Continue when the server confirms consumption of the inputs.
- Shorten the handoff after closing a corpse and queuing inventory salvage.
- Stop repeatedly retrying failed stack merges and salvage combinations that
  interrupt route navigation. Retry eligibility follows relevant item changes.
- Keep protected and unsupported salvage out of combination attempts, and
  prevent salvage bags from entering ordinary stack merging.

## Collision

- Fix the invisible obstruction around Shady's Cage Door in Protecting Picketed
  Pets. NPCs using model-based collision now use their authored collision mesh
  instead of an oversized selection capsule.

## Windows updates

- Retry temporary access/sharing errors when launching the installer or client.
- Report whether installation or restarting failed, preserving successful
  installation status if the client could not reopen.

## Verification and scope

Additional regression coverage checks corpse pickup-key interactions and
spellbook school/level filters. Those paths already behaved correctly in the
tested source; this release does not claim new gameplay changes for them.

Validation includes the Editor, packaged Windows and native Linux, Android APK
contents/signatures, isolated Windows upgrades, and public download integrity.
Quest/phone hardware, live quest hand-in and live-server loot timing are not
acceptance-tested. Packages exclude retail DATs, personal settings, server data
and generated MeshCache files. Android phones/tablets remain a preview APK.
