# AC:Unreal / AC:VR release 136

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.25.136**. Android version code: 136. Quest installer revision: 9.

These notes cover all changes since **v135 / 2026.10.24.135**.

## Retail UI interactions

- Restore the retail green acceptance circles when dragging items onto native
  hotbar slots, and restore secure-trade acceptance/rejection feedback.
- Use the correct backpack drop artwork and allow dropping items into the main
  backpack while the inventory window is closed. Clear feedback when leaving,
  cancelling, or completing a drag.
- Activate buttons only when released over the original pressed control. Cancel
  pending clicks when controls are hidden, disabled, covered, or detached, or
  when the screen or mouse capture changes.
- Honor retail button hold-repeat timing. Moving off a held button pauses repeat;
  releasing it does not add an extra click, and frame hitches do not burst repeats.
- Keep the caret visible in long single-line text fields and align mouse selection
  with the scrolled text. Refreshing unchanged text preserves caret and selection.
- Correct scaled hit testing for scrollbars, stack quantities, and vendor/trade
  drop targets, including enlarged UI scales.
- Preserve window stacking order after repeated window raises.

## Network positioning and animation

- Drive authored turning animations for remote players, NPCs, and creatures,
  including target-facing movement, without restarting the animation each frame.
- Use retail viewer-distance rules for position correction instead of snapping
  nearby entities simply because their reported position differs by over 40 meters.
  Preserve immediate handling of actual teleports.
- Match retail position-change tolerance and reset position-report timing after
  chat emotes. Report the actual ground-contact state.
- Acknowledge forced local position corrections using authoritative coordinates
  while retaining local heading. Prevent older force-position epochs from rolling
  back state or applying duplicate corrections.
- Preserve server-supplied emote and pickup animation speeds. Queue distinct
  actions, return one-shot emotes to locomotion, and update active animation speed
  without restarting it.
- Restore authored loops for 23 persistent social emotes after their entry
  animation. Already-active emotes start in the proper cycle when an entity appears;
  still poses and death poses remain still.

## Validation and distribution

- Expand UI regressions for drag feedback, shared controls, scaling, text entry,
  window order, and retail screens.
- Expand protocol-to-rendered-animation coverage for 309 chat aliases, 74 distinct
  motions, both human body setups, multiple animation rates, and player/NPC/creature
  turning at 30, 60, and 144 FPS.
- Cover correction-distance boundaries, force-position ordering, airborne and
  teleport behavior, emote completion, persistent cycles, and action queues.
- Ship matching Windows, Linux, and Quest packages without account settings,
  personal saves, server data, or the original game's DAT files.
- Automated fixtures and packaged-client checks do not replace live simultaneous
  retail-client comparisons or physical headset acceptance.
