# Screenshots, door use, scrolling, and spell selection

## Retail references

Reviewed the local retail client source in `C:/dev/retail_client_source`:

- `ClientUISystem.cpp` / `Device.cpp`: capture-screenshot action 0x55 saves a numbered image and reports the filename to chat. The shared input catalog already maps this action to numpad Multiply and exposes it in the existing keybinding window.
- `ItemUses.cpp` / `ItemHolder.cpp`: direct use is disallowed when the low `ItemUseable` bit (`Usable.No`) is set. Undefined useability is not equivalent to that explicit prohibition. Door activation through keys, switches, or scripts is distinct from direct player use.
- The runtime DAT attribute list and scrollbar artwork provide the existing window, arrows, thumb, and chain track. These are retained rather than replaced with a new UI.

## Changes

- Screenshot requests now save numbered PNGs in Unreal's platform screenshot folder, include the game UI, and report the actual filename after the engine finishes writing. Missing viewport, folder creation failure, concurrent requests, and failed saves produce feedback. The existing Capture screenshot keybinding remains editable/importable through the retail keymap interface.
- Client direct door use checks the item's `ItemUseable` property before approaching or sending a use action. Public property updates refresh this flag. ACE server direct-use handling checks it both before approach and immediately before activation. Existing server quest requirements, locks, keys, switches, and scripted activation remain authoritative.
- Resized attribute lists scroll through all nine rows with the wheel, arrow buttons, or scrollbar track/thumb. Selection and raising an attribute use the underlying row identity after scrolling. The thumb reflects the visible fraction and disappears when all rows fit.
- Vertical and horizontal chain scrollbar tracks repeat the native artwork instead of stretching it; unrelated artwork retains its original rendering mode.
- Each spell tab remembers its spell and scroll position for the current gameplay session. First visits start on the first spell. Next/previous wraps, and selection changes reveal the selected slot. Manual scrolling remains available. Reordering retains selection by spell ID where possible.
- Desktop spell tabs, the VR wrist bar, and the VR spell wheel share selection restoration without casting or changing combat stance. Logout clears the session memory; portal travel retains it.

## Validation

- Win64 editor/client source build: passed.
- Android Development build: passed; shared Quest source synchronization reports zero differences.
- ACE server Release build: passed, zero warnings/errors.
- `ACE.RetailParity.UIInteractions`: passed, including an actual saved PNG, screenshot feedback/rebinding, direct-use packet suppression and property updates, rendered resized attribute lists, scrollbar interaction, and tab selection/wrapping.
- `ACE.RetailParity.UIScreens`: passed.
- `ACE.VR.RigAndMenus`: passed, including wrist/wheel tab restoration and unchanged combat/action state.
- Rendered attribute-panel captures inspected for row layout and repeated chain artwork.

Test artifacts are under ignored `Unreal/Saved/UIInteractionAudit`. This pass does not publish releases, install a headset build, or restart a live server. Physical-headset and live quest-door testing remain to be done; server enforcement requires deploying the updated ACE server.
