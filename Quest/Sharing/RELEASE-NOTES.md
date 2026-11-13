# AC:Unreal / AC:VR release 156

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.12.156**. Android version code: 156.
Quest installer revision: 10. These notes cover changes since v155.

## UCM combat and equipment mana

- Fix UCM buffing and combat stalling on GDLE servers, including Reefcull, when
  the server does not echo a successful combat-stance change.
- Preserve server corrections and ACE combat-stance confirmation behavior.
- Fix equipment mana recharge while UCM is stopped, including low-mana jewelry
  being overlooked in large inventories.
- Prioritize fresh mana readings for equipped gear and used mana supplies while
  retaining the existing appraisal request rate limit.
- Recover from temporary recharge failures instead of leaving stopped-UCM
  equipment recharge disabled until restart.
- Refresh equipment mana after manual, Action Deck, and UCM mana-supply use.
- Cover mana stones, mana charges, and custom mana-stone-category supplies,
  including targeted and whole-suit recharge, in automated recovery tests.

## Verification and scope

Release validation covers Editor and packaged Windows/native Linux regression
tests, APK contents/signatures, and an isolated Windows upgrade. Live-server
acceptance and physical Quest/phone testing remain unverified for this release.
The Quest installer includes Diagnose-Quest.cmd for startup diagnostics.

Packages exclude retail DATs, personal settings, server data and generated
MeshCache files. Android phones/tablets remain a preview APK.
