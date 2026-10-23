# AC:Unreal / AC:VR release 132

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.23.132**. Android version code: 132. Quest installer revision: 9.

These notes cover all changes since **v131 / 2026.10.22.131**.

## Loading and performance

- Replace expensive high-compression processing of retained world textures with
  fast, lossless compression. This reduces CPU work when entering new areas while
  preserving full DAT texture quality and restoration after changing Texture Detail.
- Limit terrain retry processing to one callback per frame. Missed timer intervals
  no longer accumulate into a burst of repeated loading work after a slow frame.
- Prevent duplicate streaming event subscriptions when presenters initialize again.
- Expand `/acueperf` with effective rendering settings, editor-play identification,
  and texture allocation totals. Add tracing and repeatable scene checks for texture
  retention, appearance creation, and loading costs.
- These changes address identified loading costs; they do not eliminate every
  first-load hitch or guarantee a particular frame rate on all hardware.

## Networked NPC turning

- Resolve a TurnToObject command from the target's current physics position when
  the command arrives, rather than using an older server position for a locally
  moving player. This addresses NPCs such as the Pumpkin Buffer turning forward,
  then briefly reverting while the player circles them.
- Preserve authoritative pending teleports and forced positions. If the target
  has not spawned, use the absolute heading supplied in the packet immediately.
- Keep retail's command-time target and DAT turn rate in the shared movement path;
  the change does not add continuous target tracking or extra smoothing.

## Character statistics and interface layering

- Include permanent augmentation gains, including Jack of All Trades and
  specialized luminance bonuses, in the displayed unbuffed skill baseline.
  Permanent gains no longer appear as green spell bonuses when no buff is active.
- Preserve actual buff, debuff, and vitae indicators and leave equipment eligibility
  and combat calculations unchanged.
- Translate retail's authored interface depth consistently into Unreal layer order.
  This fixes clipped allegiance and options icon edges under the toolbar trim.

## Action Deck macros

- Add a macro editor for each slot, with a name, hotkey, and selectable icon from
  existing item, spell, and 32-by-32 DAT icon artwork.
- Support multiple chat commands, optional spacing between sends, and local
  `/wait <milliseconds>` instructions. For example, `/hom`, `/wait 30000`, and
  `/f I'm home.` send the first command, wait 30 seconds, then send fellowship chat.
- Dispatch commands through the normal chat handling path without replacing text
  the player is currently typing. Only one macro runs at a time.
- Preserve pending delays through portals and cancel macros when logging out,
  changing character/session, or disabling Action Deck.
- Show the macro status and Stop button only while a macro is running. An idle
  deck no longer reserves an empty status footer.

## Action Deck layout and key hints

- Add an optional fourth row, giving 48 slots. The new top row defaults to
  **Alt+1 through Alt+=**; the existing Shift, Control, and unmodified rows retain
  their assignments. Row visibility and bindings save per character and server.
- Hiding the fourth row retains its contents and releases its hotkeys. Duplicate
  bindings are prevented and reported in settings.
- Reflect custom key assignments in both icon labels and hover hints immediately,
  including when the cursor remains over the same slot.
- Right-clicking a slot opens its settings and unpins the deck so settings can
  be shown and hidden easily.
- Collapse the empty top toolbar when pinned and locked without shifting the slots.
  Holding the pinned-movement modifier reveals the controls again.
- Preserve retail icon transparency and cutouts throughout the deck and icon picker.
- Keep icon-picker buttons visible and stable as the catalog loads incrementally,
  including on slower storage and Linux.

## Validation and distribution

- Add regression coverage for world texture retention, streaming retry budgets,
  command-time turn targets, augmentation presentation, toolbar layering, macro
  parsing/execution, custom key hints, and the optional fourth row.
- Ship matching Windows, Linux, and Quest versions. Packages exclude account
  settings, personal saves, server data, and the original game's DAT files.
- Linux package validation uses WSL with software Vulkan; physical Linux GPU and
  Quest headset behavior require device testing.
