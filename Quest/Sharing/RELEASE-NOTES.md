# AC:Unreal / AC:VR release 90

Windows desktop, PC VR, native Linux desktop, and standalone Quest:
**2026.10.01.90**. Android version code: 90. Quest installer revision: 6.

## Changes since public release v89

### VR controls and interface

- Add analog-trigger fallback alongside trigger-click events, with hysteresis and
  duplicate-event suppression. Improve casting input across controller profiles.
- Prevent hidden UI source surfaces and oversized panel margins from silently
  blocking world casts. Show feedback when a visible panel blocks casting.
- Keep a cyan casting pointer visible for every spell type, including over menus.
  Add a soft visual halo without dynamic lights and draw the pointer above the
  interface while hovering a panel.
- Add Head, Body, and World inventory anchor modes. World placement persists when
  closing and reopening the inventory during the session; dragging and resizing
  continue to update the native menu and inspection panel together.
- Use dedicated, bounded anisotropic filtering for VR panel textures. Preserve
  immediate mip refresh and add desktop temporal motion-vector/responsive-AA
  support. These changes target blur sources; live headset smearing is not yet
  confirmed resolved.

### Inventory, inspection, skills, and chat

- Allow the main backpack to be dragged into vendor and salvage lists for bulk
  selection, matching additional packs in desktop and VR interfaces.
- Match retail inspection paragraph grouping and description precedence across
  desktop and VR. Preserve authored line breaks, avoid duplicated descriptions,
  and retain failed-appraisal messages and short-description fallback.
- Calculate skill XP progress from the current and next rank thresholds instead
  of the total skill-cap budget, correcting bars that appeared permanently full.
- Make Tab toggle chat/game focus while preserving the draft and caret position,
  including the last active floating chat window. Existing custom Tab bindings
  remain respected; the chat-toggle action is rebindable.

### World objects and animation

- Start authored idle animations for stationary props with motion tables. This
  restores animation-defined vertical offsets, including elevated crystal props.
- Wake dormant prop animation updates when an action arrives, allowing levers to
  play their authored use animation.
- Refine building click obstruction against visible polygons so NPCs can be
  selected through window openings while solid walls still block selection.
- Preserve server translucency through temporary fades, unhiding, and effect
  cleanup. Retain authored DAT transparency in unmodified multipart models.
  Newly created corpse objects continue to use their server-supplied appearance.
- Cache repeated material classification and reject irrelevant picking geometry
  before detailed polygon checks.

## Updating

Windows and Quest users can use **Updates** in the launcher or download from the
site. Supported updaters detect release 90. Accounts, settings, and DAT files are
retained; do not uninstall first. **Installation closes the game.** On Quest,
confirm Android's installation prompt, wait for completion, then reopen AC:VR.
Optional automatic updating remains off by default.

Linux users should close the game, extract the new archive into a new folder,
and run its `AC-Unreal.sh`. Linux updates use manual extraction. See
`README-LINUX.txt` for setup requirements.

No retail DAT files, saved accounts, or server configuration are included.

## Validation scope

Windows, Linux, and Quest compilation and 14 targeted regression suites passed.
Coverage includes trigger handling, pointer layering, UI mip refresh, inventory
anchoring, chat focus, skill XP, inspection, salvage/vendor offers, real DAT prop
and lever animations, window picking, and transparency.

Steam Frame casting and Quest/PC VR motion clarity still require live headset
acceptance. The automated sharpness capture was inconclusive and is not evidence
of a measured visual improvement. Daralet's exact crystal setup and Colier's live
lever interaction require confirmation on the affected server. Native Linux
runtime/GPU acceptance remains separate from cross-compilation and packaging.
