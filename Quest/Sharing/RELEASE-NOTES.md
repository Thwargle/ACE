# AC:Unreal / AC:VR release 81

Windows desktop, PC VR, and native Quest: 2026.09.26.81.
Release number / Android version code: 81. Quest installer revision: 6.

## Changes since public release v80

### Camera and field of view

- Preview desktop FOV immediately while moving the Config slider. Apply saves
  the value; Reset restores it. Leaving Config ends an unapplied preview.
- Use retail's 10–160 degree FOV preference range and aspect-ratio conversion.
  Existing FOV preferences migrate automatically.
- Correct keyboard and mouse-wheel zoom timing, near/far limits, first-person
  behavior, and distance smoothing using the retail camera calculations.
- Preserve the configured FOV in portal space and the return-to-world blend.

### Movement and collision

- Improve remote players' and creatures' ground following on slopes. Remove
  unwanted sideways drift from grounded slope collision and use authored motion
  speeds and model scale when predicting movement between network updates.
- Keep supported creatures grounded when horizontal velocity updates arrive.
  The Stuck pickup flag no longer prevents creatures from following terrain.
- Sweep ordinary position corrections against solids instead of allowing them
  to move the local player through walls.
- Improve stair-edge support and descent recovery when a side contact moves the
  player away from the original tread. Keep recovery bounded by nearby support.
- Keep solid world-object collision active when rendering culls the object,
  including authored invisible platforms. Moving objects can remain visible
  when their current bounds overlap a visible room despite an older cell ID.
- Allow charging the next jump while airborne. Hold through landing and release
  to jump again; releasing in the air does not launch an extra jump.

### Effects and interface

- Prevent repeated selection of a weapon's world model from accumulating glow
  brightness on its particle effects.
- Display Stored Mana, Efficiency, and Chance of Destruction for applicable
  mana charges and stones, including Titan Mana Charge.
- Disable Apply and Reset when the current options page has no pending changes.

## Updating

Clients running v73 or later check for this release at the account launcher.
Open Updates, choose Download update, then Install. Windows restarts in the same
desktop or PC VR mode. Quest asks for Android installation confirmation; if
prompted, allow AC:VR to install updates and press Install again. Do not uninstall
first. Older clients need the website installer or Quest USB bundle once.

Accounts, settings, and retail DAT files are retained. No retail DATs are bundled.
Headset projection remains controlled by the VR runtime; FOV options affect the
desktop camera.

## Validation and limitations

Automated development validation covers remote movement, real DAT stair and
interior fixtures, world-object collision and visibility, repeated weapon
selection, mana appraisal, options, camera input, FOV, and portal transitions.
Release packaging and updater verification are recorded with the check-in notes.

The exact Empyrean Rescue bridge and flying-pyramid route still need a live
playtest. No new headset acceptance, 90 FPS result, or live side-by-side retail
camera comparison is claimed. This release updates clients only.
