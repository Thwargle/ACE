# AC:Unreal / AC:VR release 106

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.09.106**. Android version code: 106. Quest installer revision: 7.

## Controls and display

- Preserve Shift, Ctrl, Alt, and combined keybindings even when the modifier is
  released first. Mouse buttons retain modifiers, and wheel up/down can be bound
  with or without modifiers. Controller Start can also be rebound.
- Restore saved desktop resolution, display mode, and window placement. Keep
  Alt+Enter transitions and the settings display synchronized.
- Make the radar movable from its handle and show the move cursor on hover.
- Escape clears a selected target before toggling gameplay options, without
  also closing the backpack.
- Clicking a chat name places the typing cursor after the tell prefix. Preserve
  server welcome and channel messages received before the gameplay HUD opens.

## Inventory and item presentation

- Right-clicking corpse contents selects the item as well as examining it, so
  the pickup key can take it immediately.
- Combine stacks one press at a time, select the resulting stack, and preserve
  its backpack until a subsequent pickup action moves it.
- Show retail drop-target indicators for inventory, backpacks, equipment, and
  vendor sales. Resolve world drops at the release position.
- Restore available skill credits when no attribute or skill is selected.
- Show equipped ammunition counts on the combat-mode button and omit the
  inappropriate Armor Level: 0 line on cloaks.

## Movement and archery

- Complete retail forward/backward walking steps after a short key tap, using
  authored motion and matching movement updates.
- Match strafe animation cadence to movement speed to reduce foot sliding.
- Correct bow-ready and reload transitions and ammunition attachment handling,
  including parent-event sequencing after relogging.

## Unattended Combat Manager

- Add a movable UCM activity log for diagnosing route failures and other actions.
  Keep the latest 1,000 events in a bounded file that survives client restarts.
- Improve returning to routes after long combat chases. Retain the traveled
  detour, search reachable waypoints over multiple updates, and distinguish
  unfinished reachability checks from a blocked route.
- Correct the Corruption, Destructive Curse, and Corrosion spell selections.
- Move Fast buff movement from Combat to Buffs, preserving saved preferences.

## Validation and installation

Automated regressions cover input capture, saved bindings, retail UI interactions,
inventory, chat, movement, archery, and UCM route recovery and logging. Windows,
Linux, and Quest packages use the shared source and version checks. Linux and
headset gameplay still require live acceptance; packaging is not live certification.

Installing an update closes the game. Accounts, settings, and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files and a game
server are not included.
