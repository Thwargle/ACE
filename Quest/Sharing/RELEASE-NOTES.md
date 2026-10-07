# AC:Unreal / AC:VR release 97

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.06.97**. Android version code: 97. Quest installer revision: 7.

## UCM and looting

- Added UCM Micro, a compact panel for toggling UCM and its major activities.
- Added a rebindable Toggle UCM action in keyboard settings (unbound by default).
- Manual movement releases automated steering without switching off UCM, allowing
  manual combat alongside automated looting and recovery.
- Improved route recovery after combat detours. Automatic combat chooses usable
  equipment according to trained skills and attack height according to the target.
- Repaired import of loot profiles missing their final empty record delimiters,
  including PhaelaeCustom_v6; disabled imported rules remain disabled.
- Auto-buy can redeem accepted, carried trade notes to fund pyreal purchases. It
  splits stacks when needed and waits for confirmed proceeds before buying.
  Retained notes, offered trade items and note types requested by the restock
  profile are excluded. Alternate-currency vendors do not trigger redemption.
- Added a Hide plugin bar option to UCM Overview. Plugins remain accessible from
  Game Play settings while the bar is hidden.

## Presentation and interaction

- Desktop selected and hovered objects now use the configurable selection glow,
  enabled by default. Mouselook can highlight and interact with the aimed object.
- Improved readability of the screenshot save-location field.
- Dungeon maps omit shared open-floor borders while retaining collision walls and
  distinct elevations, making connected walking areas easier to read.
- Fixed repeated melee attack power-bar fills and UCM attack height/power updates.
- Examination includes multiple wield requirements, additional activation
  restrictions, and missing descriptive information.
- Fixed a buried-house camera obstruction case using interior geometry checks.
- Scenery particle emitters start at their final world position instead of briefly
  allocating effects at the landblock origin.
- Usable, nonattackable reward NPCs can hold a Dead emote without losing selection
  or click collision. This covers creature-based quest remains without name or
  weenie-ID exceptions.

## Validation and remaining reports

Automated regressions cover UCM policies, note splitting/selling and purchase
resumption, inventory safety, map contours, selection, inspection, camera contact,
and network death-state handling. Actual DAT fixtures cover both supplied custom
reward-NPC models and different creature sizes.

The reported large-swarm airborne trap and severe Frozen Valley frame drops were
not reproduced in offline tests; this release does not claim those reports are
resolved. No new live Linux or headset acceptance is claimed.

Installing an update closes the game; on Quest this is expected while Android
installs the replacement. Accounts, settings and DAT files are retained. Linux
updates are downloaded and extracted manually. Game DAT files are not included.
