# AC:Unreal / AC:VR release 146

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.01.146**. Android version code: 146.
Quest installer revision: 9. These notes cover all changes since v145.

## Movement and chat

- Restore the freer pre-v144 movement while casting. Keep physical melee,
  missile, and combat stance transition movement rules intact.
- Preserve running/walking momentum and animation after landing from a jump
  while the movement key remains held.
- Fix enlarged chat windows leaving most of their space unused after text
  scaling. Keep the log, scrollbar, entry field, and Send button aligned.

## Lua plugin graphics and controls

- Add native per-frame pulse, blink, rotation, opacity, image halos, and text
  outlines for plugin graphics, without requiring frequent Lua updates.
- Add layered frames, alignment, fixed dimensions, clickable graphics, and
  aspect-preserving custom PNG images for bars, gauges, and countdown displays.
- Support overlaid progress labels with separate text and fill colors.
- Let plugins hide controls while pinned, configure their Start/Stop controls,
  and participate in Quick-show. Include a runnable Graphics Gallery example.
- Add permission-controlled playback of bundled short PCM WAV sounds with
  bounded playback and memory budgets.

## Plugin chat and data

- Filter chat subscriptions before delivery to Lua and retain each plugin's
  delivery cursor across refreshes, with bounded inboxes and gap reporting.
- Support chat history opt-out, bounded pattern filters, sender metadata,
  explicit window routing, and per-message colors while respecting chat filters.
- Make inventory-only snapshots avoid unrelated spell, vendor, and combat work.
- Add permission-controlled background identification and item/spell action
  lifecycle events with object metadata. Completion releases the action gate;
  it is not proof that a cast or item effect succeeded.

## Action Deck

- Allow a fourth row to be added even when proposed hotkeys conflict. Preserve
  existing bindings and leave conflicting new slots unbound for manual rebinding.

## Validation and packaging

Automated checks cover movement, chat layout, Action Deck, item tools, plugin
graphics and chat, API permissions, and update behavior in the editor and
packaged desktop clients. Android packages are checked for signing continuity,
16 KiB alignment, identity, version, and bundled plugin contents. Package checks
do not replace live-server or physical-device acceptance testing.

Packages exclude retail DATs, personal settings, server data, and generated
MeshCache files. Android phones/tablets remain a preview using a separate APK
from Quest. Existing signing identities and installer revision are retained.
