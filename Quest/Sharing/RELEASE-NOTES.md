# AC:Unreal / AC:VR release 105

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.09.105**. Android version code: 105. Quest installer revision: 7.

## Playable Olthoi

- Improve character creation previews, PvP target eligibility, and Olthoi chat
  channels and language handling using retail rules.
- Preserve the nonhuman player body in VR rather than applying humanoid hiding.

## Buff and debuff panels

- Preserve permanent equipment enchantments, Vitae, and cooldown entries when
  the server purges temporary enchantments on death.
- Keep beneficial and harmful enchantment purges consistent with server events.

## Chess

- Join world chess boards through the retail game protocol and open the board
  in desktop or VR. Reopen an active game through Game Center.
- Correct turn handling, confirmed and rejected moves, stalemate offers,
  resignation confirmation, and cleanup after a match or logout.
- Refresh the displayed chess rating immediately when the server changes it.
- The accompanying ACE source fixes checkmate, special moves, AI move search,
  and match-result handling. These server-side improvements require deploying
  the updated ACE server separately; client installers do not include a server.

## UCM scroll learning

- Match scroll-learning requirements to retail and ACE rather than casting
  difficulty. Levels II-VI require a trained school and 0/50/100/150/200 current
  skill respectively. Levels I and VII, plus ACE's VIII, retain their exemptions.
- Refresh unknown-scroll eligibility when the character's skills change.

## Validation and installation

Focused automated regressions cover Olthoi creation, targeting and chat; death
enchantment purges; chess UI, protocol and server rules; and scroll eligibility.
Live retail/Unreal chess matches, live Olthoi PvP, and headset gameplay remain
acceptance checks; successful packaging is not live gameplay certification.

Installing an update closes the game. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files are not included.
