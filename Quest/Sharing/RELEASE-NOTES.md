# AC:Unreal / AC:VR release 107

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.10.107**. Android version code: 107. Quest installer revision: 7.

## Controls and movement

- Bind controller button combinations such as RT+A in the keybinding screen.
  Save held-button modifiers, distinguish combinations from plain buttons, and
  retain correct behavior when buttons are released in different orders.
- Prevent movement and gameplay shortcuts while typing in retail text fields,
  including Journal, Page List, Fellowship, Friends, and Squelch.
- Clear stale running, action, and movement state during portal transitions so
  arrival immediately shows the appropriate idle or falling animation.
- Start strafing at the authored retail step phase without the initial glide.
  Preserve movement speed and matching local/remote animation cadence.

## Inventory and vendors

- Select the first merchandise item when opening a vendor. Preserve the chosen
  item when that vendor refreshes its stock during shopping.
- Keep the large toolbar backpack icon open while the inventory panel is open.
- Update the selected stack's displayed quantity after combining stacks while
  preserving manually selected partial quantities.
- Apply retail target-type, ownership, and trade restrictions before targeted
  item use. Mana stones no longer offer to destroy incompatible targets such as
  corpses, and confirmation rechecks that the target remains eligible.
- Update inventory-drag world highlighting from the current pointer and camera
  position instead of retaining the object beneath the initial drag location.

## Unattended Combat Manager

- Smooth route returns after chasing monsters by combining straight portions
  of the traveled path while preserving corners and elevation changes.
- Limit movement near return points to prevent overshooting, wake the route
  decision immediately on arrival, and remove the action delay from steering.
- Continue toward the pending waypoint after looting on the current route
  segment instead of walking back to the departure point. Off-route returns
  retain their observed path around obstacles and between floors.

## Validation and installation

Automated regressions cover controller combinations, text focus, drag targeting,
portal arrival, strafe timing, vendor selection, stack quantities, targeted item
use, and native/imported route recovery. Windows, Linux, and Quest packages use
the shared source and version checks. Linux and headset gameplay still require
live acceptance; packaging is not live certification.

Installing an update closes the game. Accounts, settings, and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files and a game
server are not included.
