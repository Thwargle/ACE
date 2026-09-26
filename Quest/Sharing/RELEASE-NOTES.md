# AC:Unreal / AC:VR release 79

Windows desktop, PC VR, and native Quest: 2026.09.26.79.
Release number / Android version code: 79. Quest installer revision: 6.

## Changes since public release v78

- Use the same displayed version across Windows desktop, PC VR, and Quest, and
  reject release builds if the platform version declarations disagree.
- Fix overhead camera controls moving the camera below the world. Raising and
  lowering retain the downward angle; sideways rotation restores the normal
  camera distance and collision before turning, following retail behavior.
- Allow a smooth handoff from held forward movement to autorun when releasing
  the movement key.
- Fix missing tab and button text at scaled/windowed resolutions, including
  2048x1536 at 125% UI scale.
- Allow the spell hotbar to expand to the available viewport width.
- Improve key-binding capture for left/right Shift, Control, and Alt, and ignore
  orphan key-release events that could assign the wrong modifier.
- Prevent a delayed account-launcher refresh from covering character selection.
- Restore retail vendor quantity defaults, preserve manually chosen stack
  quantities, and correct trade-note pricing and item-name/plural captions.
- Fix duplicate application of remote-player velocity after grounded position
  updates, which could make players move above slopes before snapping down.
- Improve support at convex stair and ramp edges, including the center-post
  stairwell, while retaining full step and headroom checks at steep risers.
- Extend regression coverage for keypad camera controls, retail run/jump
  formulas, crowded movement, Fort Teth, Mosswart Fort, and stair-edge traversal.

## Updating

Clients running v73 or later check for this release at the account launcher.
Open Updates, choose Download update, then Install. Windows restarts in the same
desktop or PC VR mode. Quest asks for Android installation confirmation; if
prompted, allow AC:VR to install updates and press Install again. Do not uninstall
first. Older clients need the website installer or Quest USB bundle once.

Accounts, settings, and retail DAT files are retained. No retail DATs are bundled.

## Validation and limitations

Regression coverage exercises camera socket positions at 30/90 FPS, UI clipping,
key bindings, autorun, vendor transactions, launcher state, remote grounding, and
collision against actual retail DAT geometry. The reported higher center-post
trap reproduced and passes with the support fix. Other reported wall-clipping
cases did not reproduce in the automated routes and still need live confirmation.

Remote animation and hardware-specific keyboard behavior also need confirmation
from affected players. This release does not change retail jump height or speed;
the tested arcs match the existing retail formulas. New in-headset performance
and installation-confirmation acceptance have not been performed for v79.

This download updates clients only. It does not deploy or restart game servers.
