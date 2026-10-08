# AC:Unreal / AC:VR release 100

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.08.100**. Android version code: 100. Quest installer revision: 7.

## UCM combat and recovery

- Target death, zero-health, and removal notifications wake UCM immediately,
  releasing the previous physical attack's retry delay before the next target.
- Health, stamina and mana changes trigger recovery checks during physical combat.
  Rechecking priorities does not repeatedly restart the same attack.
- Finished combat appraisals wake the next decision promptly. Dead debuff
  recipients no longer stall combat while waiting for an obsolete result.
- With Loot Before Combat disabled, nearby eligible enemies take priority over
  corpse approach and looting, including when a corpse is already open.

## UCM buffing

- Fast buff movement is enabled by default. Explicit saved or imported opt-outs
  remain respected. Movement stops on completion, failure or manual input.
- Confirmed buffs advance promptly, with guarded handling of late server
  acknowledgments and bounded retries when a server rejects a cast during recoil.
- Corrected confirmation of player-targeted banes that report results on worn
  armor. Timers include spell-duration augmentations, preventing premature repeats.
- Recognize healing and other recovery spell result messages correctly.
- Cast completion wakes scheduling without repeating background inventory and
  appraisal scans on every wake.

## Rendering performance

- Cache decoded texture surface information instead of copying full pixel data
  during repeated model construction. This reduces repeated Snow Tusker spawn
  and corpse setup work in Frozen Valley and benefits other shared models.

## Validation and installation

The Windows editor build and 30 plugin regression suites passed before packaging.
Tests cover target changes, recovery priorities, buff confirmations, delayed
acknowledgments, busy retries and combat/loot ordering. Targeted texture and
Snow Tusker performance checks were also completed during development.
The packaged Windows client passed 29 regression suites. Release packaging and
website metadata checks passed, including native Linux archive validation.
Live multiplayer, Linux gameplay and headset acceptance remain necessary;
these changes do not claim complete retail or Virindi Tank parity.

Installing an update closes the game. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files are not included.
