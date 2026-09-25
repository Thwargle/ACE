# AC:Unreal / AC:VR release 74

Native Quest: `2026.09.24.quest.74`, Android version code 74, installer revision 6.
Windows desktop / PC VR: `2026.09.24.80`.

## Changes since v73

- Correct close-up armor geometry using retail's highest-detail model selection,
  including the chest shape of Pathwarden scale armor.
- Preserve the selected combat target when applying a healing kit to yourself
  through the backpack/hotbar.
- Lift a spell out of its original slot while dragging and show retail's green
  insertion marker. Allow smooth spell-bar width resizing.
- Skip empty spell slots when moving to the next or previous spell, wrap between
  populated entries, and keep the selected spell in view.
- Validate spell targets using retail spell formulas. Other spells no longer
  fall back to the caster; incompatible targets disable Cast and explain why.
  Preserve VR free aiming for projectile spells.
- Save Windows screenshots to Documents\Asheron's Call by default. Add a
  screenshot folder setting under Options > Config > Screenshots.
- Keep outdoor ambience audible near connected building entrances and fade it
  deeper inside. Correct the one-based landcell lookup for ambient sound tables.
- Make vendor Add use the whole available stack by default while respecting
  explicitly selected quantities and stock limits.
- Show a selected NPC's name in the Say menu's Tell option and allow direct tells
  to retail-compatible creature targets.
- Stop chat text and entry fields receiving clicks through foreground windows,
  including the vendor and spell bar. Exposed chat remains selectable/editable.
- Reduce copies and temporary allocations during portal visibility traversal
  using shared doorway geometry and reusable clipping buffers.

## Updating

Clients running v73 or later check for this release at the account launcher.
Open Updates, choose Download update, then Install. Windows restarts in the same
desktop or PC VR mode. Quest asks for Android installation confirmation; if
prompted, allow AC:VR to install updates and press Install again. Do not uninstall
first. Older clients need the website installer or Quest USB bundle once.

Accounts, settings, and retail DAT files are retained. Keep a separate backup of
retail DAT files. No retail DATs are bundled with the release.

## Server compatibility

This release updates the clients; publishing it does not update a game server.
Custom VR combat and pose replication require this project's VR-enabled ACE
server. Existing atlatl/thrown release and restricted-door server changes remain
required for those server-side behaviors.

GDLE login has been verified through character selection. The v64 packet-timing
correction is included. A live Seedsow item-interaction retest remains pending;
automated protocol checks do not establish full in-world GDLE compatibility.
Bundles contain no server, credentials, saved settings, SDK tools, or retail DATs.

## Validation and limitations

Regression coverage includes armor geometry, healing-kit selection, spell-bar
interaction and targeting, screenshot settings, Slate window input order, vendor
quantities, NPC tells, and portal traversal. Entrance audio reach was checked
against 86 real exterior portals and synthetic connected/sealed room cases.

A warmed desktop portal-traversal benchmark improved from 44.33 to 29.34
microseconds. This is a routine-level CPU measurement, not a measured Quest FPS
increase. Headset listening and Android update confirmation remain manual
acceptance checks. Download hashes are in SHA256SUMS.txt.
