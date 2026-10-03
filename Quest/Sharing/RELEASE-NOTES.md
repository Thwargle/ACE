# AC:Unreal / AC:VR release 93

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.03.93**. Android version code: 93. Quest installer revision: 7.

## Changes

- Fixed delayed cancellation of a previous attack discarding the next held
  attack. Firing, cancelling, and immediately holding a different aim height
  now preserves the new request until release.
- Melee and missile power hotkeys now use retail's seven positions, including
  minimum, middle, and maximum. Mouse slider values round to the nearest step.
- Restored the authored jump landing recovery: kneeling back to Ready or
  transitioning into walking/running, while allowing the next jump to interrupt.
- Fixed keyboard focus after pressing Enter in an empty chat box. Normal chat
  returns control to the game; Stay in Chat retains a usable text entry.
- Gravity-disabled creature-based objects retain their server-supplied height
  even close to a floor. Ground/contact updates no longer pull them down.
- Added `/aceobject` diagnostics for selected object placement, model, scale,
  and physics flags to help investigate custom-server content.

## Validation and remaining checks

- Regression coverage includes delayed attack responses, queued releases,
  all seven power positions, DAT landing poses, repeated jumps, chat focus,
  gravity-disabled custom objects, and remote movement/support.
- Desktop and VR movement fixtures verify jump trajectories and landing coast
  against retail friction at 30, 90, and 144 Hz. Glide distance has not been
  artificially increased; the reported shorter glide needs a live comparison.
- The original gravity-enabled Daralet crystal scene remains unverified in-game.
  The placement fix covers the administrator's gravity-disabled configuration.
- No new live headset or Linux runtime acceptance is claimed for this release.

## Updating

Use the client updater or download the matching platform package from the
website. Installing an update closes the game; on Quest this is expected while
Android installs the replacement. Accounts, settings, and DAT files are retained.
Linux updates are downloaded and extracted manually.
