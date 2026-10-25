# AC:Unreal / AC:VR release 135

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.24.135**. Android version code: 135. Quest installer revision: 9.

These notes cover all changes since **v134 / 2026.10.23.134**.

## Inventory and Item Tools

- Moving an item into another bag first fills compatible stacks in that bag,
  up to their maximum sizes, before using a new slot for any remainder.
- Add **Merge All** to Item Tools to combine compatible inventory stacks,
  with progress, cancellation, and server-confirmed sequencing.

## Companion target data and world plates

- Expose object type, known creature class/title, level, assessed health, stamina,
  mana, and player-killer status to companion plugins. Unknown vitals remain
  unknown rather than displaying invented values.
- Expose debuffs by object ID from the same confirmed-cast timers used by Action
  Deck, including spell ID, DAT icon, and remaining duration. Timers remain
  available for tracked objects after changing selection.
- Add client-rendered world plates attached to objects, with brackets, role,
  name, level, optional vital bars, and debuff icons/countdowns. The client updates
  their placement every frame, including VR, independently of Lua callback rate.
- Add API documentation and a target-plate companion example. Timers describe
  the player's confirmed casts; they do not claim to detect other players' casts,
  early dispels, or server-specific duration changes.

## Companion performance

- Spread due companion callbacks across frames with round-robin scheduling.
- Allow companions to set update intervals from 0.5 to 60 seconds and request
  only the snapshot fields they need.
- Share native snapshot work between readers in a host tick and invalidate it
  before actions or state changes. Character-only readers avoid inventory and
  nearby-object work, while full-snapshot readers reuse the shared snapshot.
- Add sustained callback and memory regression coverage. These changes reduce
  repeated host work; third-party scripts can still perform expensive work in
  their own callbacks.

## Camera and movement parity

- Honor camera slope alignment without mouse movement, using movement history
  and terrain slope with retail-style filtering. Add the corresponding saved
  video option and correct the camera's alignment to the avatar's forward axis.
- Make camera stiffness control rotation and positional smoothing; full
  stiffness snaps to the requested camera state.
- Play authored turning animations when turning in place and finish the authored
  running step transition when releasing movement.
- Honor movement locks during melee actions and combat/peace stance transitions
  while retaining turning controls.

## UCM combat, looting, and summons

- Keep native auto-repeat from restarting plugin-owned attacks when selection
  changes, bypassing UCM's range and visibility checks.
- Clear the previous attack before automatic post-kill target selection so
  selecting another creature does not itself trigger a charge or shot. UCM's
  configured pursuit settings still apply.
- Resume corpse approaches when a delayed server distance update tightens the
  interaction range, including after pausing or before the initial turn finishes.
  Preserve the server's use distance and avoid duplicate interaction requests.
- Ignore hidden or stale pet objects when deciding whether a summoned pet is
  still active, so those entries no longer suppress later summons.
- Match UCM Micro's background opacity to the common plugin opacity setting.

## Appearance and UI

- Apply retail part-level palette selection across clothing surfaces, fixing
  Asenala's undyed head covering and other multi-surface palette replacements.
- Apply show/hide helm and cloak options immediately without re-equipping;
  cancelling options restores the previous appearance setting.
- Display inscription authors in the bottom-right signature area.
- Use the blue retail shortcut artwork for every empty second-row hotbar slot.
- Keep inventory and plugin tooltips clear of scaled mouse cursors, including
  at screen edges and larger UI scales, through final tooltip placement.

## Validation and distribution

- Regression coverage includes inventory merging, companion snapshots and plates,
  sustained plugin workloads, camera slope/stiffness, authored movement links,
  palette layers, appearance options, tooltips, attack ownership, visibility
  checks, and delayed corpse interaction distances.
- Ship matching Windows, Linux, and Quest versions. Packages exclude account
  settings, personal saves, server data, and the original game's DAT files.
- Automated builds and regressions do not replace live headset acceptance or
  verification with the affected third-party companion scripts.
