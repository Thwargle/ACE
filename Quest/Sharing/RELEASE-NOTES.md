# AC:Unreal / AC:VR release 98

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.07.98**. Android version code: 98. Quest installer revision: 7.

## UCM automation

- Recoverable activity failures no longer switch off all of UCM. Blocked
  navigation and failed loot transfers leave combat and vital recovery available.
- Retry obstructing doors, including doors used earlier along the route.
- Added automatic summon refills using carried Encapsulated Spirits, automatic
  mana-stone filling, and equipment mana replenishment controls.
- Summoning retains combat stance instead of unnecessarily entering peace mode.
- Reduced repeated loot evaluation and appraisal waits, with bounded regex work
  and improved evaluation caching.
- Simplified the UCM Micro combat checkbox label.

## Retail interface and interaction

- Updated Friends and Squelch layouts, selection states, scrolling and buttons.
  Squelches filter incoming speech, emotes, tells and chat channels before delivery
  to chat or plugins, preserving the server's channel masks and account requests.
- Restored retail chat destination captions and menu wording.
- Improved allegiance/fellowship presentation and inventory stack controls.
- Fixed keybinding capture, vendor trophy-count refresh, and main-pack salvage
  selection and material scoping.
- Corrected local Aetheria surge effect handling.
- Added the retail barber interface, appearance preview and Apply/Cancel protocol.
- Added the retail house purchase and maintenance interface, payment handling,
  confirmations and authoritative transaction refresh.
- Restored spellbook filter persistence and decoding of older saved spell bars.
- A failed retail HUD load returns to login with a data error instead of opening
  an unrelated fallback interface.

## Networking

- Corrected player movement incarnation and server-control acknowledgements,
  rejected stale motion, and prevented local autonomous echoes replaying movement.
- Retained valid movement received before an object's complete creation message.
- Rejected malformed or nonfinite physics messages without consuming their sequence.
- Handled unavailable retransmissions so one unrecoverable packet no longer
  indefinitely blocks later received updates.
- Corrected encrypted retransmission requests and retry checksums.
- Preserved complete fragment identities and retail ephemeral-message ordering;
  bounded incomplete-message storage and validated message queues.
- Processed validated keepalive and latency headers promptly despite gameplay
  packet reordering.

## Validation and installation

Focused automated regressions cover the changed UCM, interface and protocol paths.
These changes do not certify complete retail parity. Live multiplayer loss/recovery,
custom-server edge cases and headset/Linux gameplay acceptance remain necessary.

Installing an update closes the game. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files are not included.
