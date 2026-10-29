# AC:Unreal / AC:VR release 142

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the new
Android phone/tablet preview share **2026.10.28.142**. Android version code: 142.
Quest installer revision: 9. These notes cover all changes since v141.

## Android phone and tablet preview

- Add a separate optional Android ARM64 client with touch-friendly login,
  character creation, inventory, spellbook, vendors, trade, loot, and inspection.
- Add a responsive movement D-pad, separate left/right turning keys, swipe-look,
  pinch zoom, simultaneous movement/jump input, and a jump-charge meter.
- Move, resize, rebind, mirror, and hide touch controls with saved layouts and
  reserved space that prevents overlap with menus and HUD panels.
- Add a caster spell wheel, selected-spell display, retail peace/combat and
  ability icons, and retail-textured vitals. Preserve icon proportions as buttons
  resize; retry missing inventory artwork without rebuilding the inventory.
- Include bundled plugins, a quick-access plugin rail, persistent plugin HUD
  panels, and a choice of full-screen or pinned chat.
- Correct Vulkan device-profile selection and refresh Android's inbound network
  mapping more often to address the observed early disconnects. Size the mobile
  texture pool from physical RAM instead of using the previous fixed budget.
- Keep phone updates separate from Quest updates. The APK requires Android 10+,
  ARM64, Vulkan/ASTC support, and separately supplied retail DAT files. Initial
  setup uses USB debugging and the repository's Mobile installation helper.

## Chat, fellowship, and UCM

- Select and copy part of a chat message by dragging, or double-click a word.
  Paste into the chat input; whole-message/window copying remains available.
- Preserve matching chat history while a chat window is closed, honoring each
  window's filters and bounded scrollback.
- Show all server-supplied fellowship members, with scrolling beyond nine rows.
- Add optional local UCM messages when looted items match a rule, and when a
  selected/assessed item matches a rule. Separate Loot settings control these
  notices; they are not sent to other players or server chat.

## Inventory and interface fixes

- Clear the spent equipped stack when the last arrow, dart, or thrown missile is
  consumed, allowing ammunition replacement without relogging. Reconcile ACE's
  separate ownership and world-deletion messages while preserving normal unequip.
- Make Dereth Foundry creation and other administrative commands use ACE's
  required wire prefix. Server permissions and command confirmations still apply.
- Enable Abandon for selected journal contracts, including available and
  completed entries. Keep the entry until the server confirms its removal.
- Flip the vendor category dropdown arrow when the list opens and closes,
  using the retail DAT artwork.

## Validation and preview limits

The release uses automated protocol, UI, plugin, mobile-layout, and updater
regressions, platform builds, signature checks, and archive checksum validation.
The Android preview has been played on a Galaxy Z Fold7, including a connection
check beyond the previous disconnect interval. The latest HUD and texture-pool
changes have not yet been tested on that phone. Other phones/tablets, sustained
thermal/memory behavior, fold transitions, live Frostcull ammunition handling,
and physical Quest/PC VR acceptance still require hardware/server testing.

Packages do not contain personal settings, accounts, server databases, or retail
DAT files. The phone build is a sideloaded testing preview, not a Play Store app.
