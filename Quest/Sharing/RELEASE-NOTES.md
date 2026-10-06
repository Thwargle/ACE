# AC:Unreal / AC:VR release 96

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.06.96**. Android version code: 96. Quest installer revision: 7.

## UCM buffing

- Buff cycles now start with Creature Enchantment Mastery, Focus, Willpower
  (Self), Mana Conversion and Life Magic Mastery, following Virindi Tank's
  opening order. Other trained magic schools precede the remaining buffs.
- Normal and forced buff cycles re-evaluate usable tiers as the server reports
  increased skills. Successful casts no longer retain a stale retry delay.
- Choose automatic highest usable spells or an explicit level for each of
  Creature, Life and Item magic. Unavailable or repeatedly failed buffs are
  skipped so the rest of the cycle can finish.
- Fixed repeated Tusker Leap casting: automatic maintenance uses sustained buffs
  instead of short burst spells. Explicitly selected short buffs remain usable;
  renewal windows cannot immediately mark a fresh buff as expired.
- Removed the unnecessary fixed action delay after acknowledged casts while
  retaining server busy checks and the minimum request interval.

## UCM vital recovery

- Added a preference for kits/consumables or spells, with fallback when the
  preferred method fails, times out or is unavailable. Explicit preference can
  override imported recovery handlers; imported ordering remains selectable.
- Mana spell recovery prefers Stamina to Mana over Mana Boost. Revitalize
  restores depleted stamina afterward; health retains first priority.
- Recovery supports all spell tiers, respects current skills and components,
  and preserves the server's component exemption for eligible characters.
- Empty supplies are skipped and failed recovery methods yield to alternatives.

## Server companion fix

The local ACE server update retries a failed player login placement once, five
millimeters above the original point, using normal collision validation. This
fixes exact-floor dungeon spawns without accepting invalid positions or rejecting
intentional airborne placements. This is a server change; public client packages
do not include server binaries or configuration.

## Validation and updating

DAT-backed regressions cover buff order, newly available tiers, Tusker Leap,
recovery priority and fallback, and Stamina to Mana followed by Revitalize.
Automated tests and packaging checks do not replace live Linux or headset
acceptance; no new live performance measurement is claimed.

Installing an update closes the game; on Quest this is expected while Android
installs the replacement. Accounts, settings and DAT files are retained. Linux
updates are downloaded and extracted manually. Game DAT files are not included.
