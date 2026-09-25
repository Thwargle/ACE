# AC:Unreal / AC:VR release 78

Native Quest: 2026.09.25.quest.78, Android version code 78, installer revision 6.
Windows desktop / PC VR: 2026.09.25.84.

## Changes since public release v75

### Movement and collision

- Improve shared wall and corner recovery without pushing players through a
  second wall on the same mesh. Add coverage for the reported center-post trap.
- Improve grounding around creatures so running through crowds does not lift
  the player into an unintended airborne state.
- Extend stair coverage to both edges, ascending, descending, and stepping off
  the side, using actual training, Fort Teth, and Mosswart Fort geometry.

### VR interface and combat feedback

- Move and angle panels in full 3D. While holding Move, use the right thumbstick
  up/down to push the panel farther away or bring it closer. Resize remains a
  separate handle, and editing does not turn the player or scroll a menu.
- Improve Body-anchor following with separate start/stop thresholds and detach
  Body/World panels from head-tracking updates. Head-pinned panels retain headset
  late updates. These changes address jitter near the follow boundary.
- Add an optional native fellowship panel with placement and size controls.
- Add configurable VR action-button assignments in VR Options.
- Show position-based compass coordinates inside buildings and remove redundant
  marker chevrons.
- Filter floating combat notices to the local player's events, restore outgoing
  spell-damage notices, and size the cards to avoid clipping text such as Evaded.
- Send the chosen melee power/missile accuracy to servers that advertise support.
  This requires the matching server update; existing servers retain their prior
  combat behavior. Physical bow draw continues to control launch velocity.

### Retail interface

- Highlight selected vendor items, honor selected stack quantities, and improve
  cart quantities, individual buy/sell actions, prices, and alternate currency.
- Allow moving spells between hotbar tabs by hovering over or dropping onto a tab.
- Use appraisal enchantment flags for beneficial/harmful stat colors and display
  effective armor resistance values.
- Separate buffs and debuffs using the spell's flags.
- Add a cursor scale slider and restore a higher bird's-eye camera view.

### Performance and packaging

- Reuse animation pose buffers and avoid copying motion arrays on every tick.
- Replace animation loop wrapping whose CPU cost grew with session length with
  bounded work, while preserving motion hooks and one-shot behavior.
- Share portal-view material parameters instead of updating every material
  instance, avoid redundant mask bindings, and skip hidden-part transform work.
- Reduce hidden retail-interface work when only native VR panels are visible.
- Generate and verify runtime materials before the Quest cook. This fixes the
  missing terrain material and repeated loading attempts in the v76 test build
  that caused the green floor. Test builds v76 and v77 were not public releases.

## Updating

Clients running v73 or later check for this release at the account launcher.
Open Updates, choose Download update, then Install. Windows restarts in the same
desktop or PC VR mode. Quest asks for Android installation confirmation; if
prompted, allow AC:VR to install updates and press Install again. Do not uninstall
first. Older clients need the website installer or Quest USB bundle once.

Accounts, settings, and retail DAT files are retained. No retail DATs are bundled.

## Validation and limitations

Automated mobile-preview and packaged Windows tests cover animation lifetime,
motion parity, VR panels and notices, runtime materials, portal masking, and
stair/corner regressions. Release checks also exercise the launcher/updater,
installer preservation, and Quest signing compatibility.

90 FPS on Quest is still a target, not a measured result for this release. Native
headset performance, panel comfort, and the latest visual fixes need further
in-headset confirmation. Experimental mobile actor caching and local-light shader
policy changes remain disabled by default on Quest pending native GPU/stereo tests.

This download updates clients only. It does not deploy or restart game servers.
