# AC:Unreal / AC:VR release 133

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.23.133**. Android version code: 133. Quest installer revision: 9.

These notes cover all changes since **v132 / 2026.10.23.132**.

## UCM shield and equipment buffs

- Fix an equipment-buff stall after selecting a shield for the left hand. ACE can
  mark inventory items as attackable; UCM incorrectly applied its monster
  line-of-sight check to those items' stale world positions and rejected the cast
  before sending it, leaving the policy waiting for confirmation.
- Apply the hostile-creature sight check and offensive-cast tracking only to
  actual hostile creatures. Owned equipment remains eligible for its normal buffs,
  including Olthoi's Bane on an equipped shield or one carried while using a wand.
- Preserve server-confirmed item timers and normal combat obstruction checks.
  Add regression coverage for the reported spell, shield ownership states,
  confirmation handling, and rejection of an obstructed hostile creature.

## ACE upstream merge (server source)

- Merge ACEmulator/ACE's default branch, `master`, through `7f87f9d8`.
- Revalidate inventory ownership and stack amounts after movement or pickup
  animations when giving or splitting items, preventing stale-item duplication.
- Improve trade-item recovery when a recipient cannot accept an escrowed item.
- Reject duplicate item entries and multiple vendor-service purchases in a single
  vendor request.
- Update Entity Framework Core to 9.0.21, Harmony to 2.4.3, and the upstream
  MSTest/test SDK dependencies and server build metadata.
- Bind the Docker database port to localhost in the default Compose configuration.
- These server changes are included in the Git fork. Client download packages do
  not include or deploy an ACE server.

## Validation and distribution

- The shield regression reproduced the blocked cast before the fix. The updated
  host and UCM policy tests cover confirmation, equipment handoff, and continued
  combat without repeating confirmed buffs.
- Ship matching Windows, Linux, and Quest versions. Packages exclude account
  settings, personal saves, server data, and the original game's DAT files.
- Automated checks do not replace testing on the affected player's live server
  or physical Linux GPU and Quest hardware.
