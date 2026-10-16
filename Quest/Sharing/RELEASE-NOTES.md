# AC:Unreal / AC:VR release 120

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.16.120**. Android version code: 120. Quest installer revision: 9.

## Login and character creation

- Replace the login screen's red buttons with quiet neutral controls, ivory text,
  retail bronze frame artwork, and a muted green Launch button.
- Fit the main login interface without scrolling, including narrow windows and
  error messages. Server, account, and directory lists scroll within their panels.
- Keep login backgrounds black after logout, put Quit at the top right, and move
  credential editing to the Accounts tab. Remove the redundant welcome message.
- Use the retail full-body idle when zoomed out in character creation, and the
  held rest pose for close-up inspection. Clarify Olthoi body, eye and markings
  color controls and hide unsupported skin-style arrows.

## UCM

- Fill empty mana stones from suitable unwanted magical loot picked up by UCM;
  no manually selected donor pool is needed. Normal loot rules take precedence,
  and protected items and existing carried equipment are excluded.
- Expose the minimum mana required in source loot, with a default of 1,000.
- Put Prioritize loot and Prioritize navigation together on Overview.
- Remove constantly changing action text from UCM Micro; the main UCM window
  continues to show the current action.

## Performance and networking

- Retire stale contents of closed loot containers after the retail grace period,
  preventing discarded corpse loot from accumulating during long hunting runs.
- Reuse materials and textures still held by visible characters when bounded
  caches turn over, reducing repeated appearance rebuilds in crowded scenes.
- Reject already-received encrypted packets before checksum-key processing,
  matching retail duplicate handling without disrupting ordered packet delivery.

## Compatibility and validation limits

- **AC-WoW Transmog, its provider, converter and model assets are excluded.**
- Updates preserve accounts, settings, profiles and DAT files. Linux updates are
  downloaded and extracted manually. Game DAT files and a server are not included.
- Regression tests reproduce the corrected cache, stale-loot and duplicate-packet
  behavior. The reported Hoshino event crash and multi-hour UCM freeze have not
  been reproduced end to end; this release does not claim those reports resolved.
- Builds and automated checks do not establish physical-headset or live-event
  acceptance.
