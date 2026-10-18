# AC:Unreal / AC:VR release 122

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.17.122**. Android version code: 122. Quest installer revision: 9.

## UCM and inventory

- Separate summoning essences from weapon selection so selecting an essence does
  not exclude usable wands or interrupt combat. Prioritize essence appraisal and
  allow automatic summoning with Combat disabled, including a UCM Micro toggle.
- Recover from missing corpse contents and containers that close after the final
  pickup. Preserve completed-corpse history across UCM Stop/Start.
- Fill empty mana stones from eligible junk without requiring those items in the
  consumables pool; preserve confirmed source-item and mana checks.
- Preserve backpack slot order after bulk transfers and subsequent item drags.
- Restore keyboard pickup and pack-slot drops of portable backpacks on the ground.
- Add an explicit Item Tools option for servers that pay vendors from a custom
  bank. Explain missing inventory proceeds instead of repeatedly selling notes.

## World, targeting and chat

- Improve cliff, roof and stair support checks using retail polygon footprints,
  terrain support and step-down rules; prevent unsupported outward edge creep.
- Preserve authored decorative poses when the server supplies no motion table,
  and exclude unnamed fixed decoration from selection and examination.
- After a kill, select the nearest eligible creature within radar range.
- Preserve the chat reading position while messages arrive or old history expires,
  including scrollbar dragging and unread indicators in extra chat windows.

## Login and plugin development

- Add an eye button to show or hide the saved-account password while editing it.
- Remove the shared host notice from the top of unrelated plugin windows.
- Add permission-gated Lua chat filtering by window, sender, type and text, while
  keeping messages available to plugin readers. Include a documented example.
- Let custom plugins choose separate, toggle or hidden Start/Stop controls,
  customize their labels, and optionally hide Settings and profiles.
- Expose composite inventory item icons with backgrounds, underlays, overlays
  and enchantment effects through the Lua UI API.

## Performance and diagnostics

- Exclude unused default engine plugins and editor import pipelines from game
  packages, avoiding a Niagara type-registration crash found in release testing.
- Batch rejected crowd contacts during floor support checks and stop repeating
  identical blocked movement steps. A synthetic 120-creature hallway comparison
  improved from about 30 to 40 FPS on the development machine.
- Add per-frame CPU subsystem timings to `/acueperf` and slow-frame reports,
  plus moving-crowd and prolonged simulated-lag regression coverage.
- Avoid repeated VR arm render-proxy updates when controllers are untracked.
- Defer hidden desktop HUD presentation in native VR and refresh it when shown.
- Strengthen Quest performance capture validation for refresh-rate and resolution
  changes. These changes do not establish a sustained 90 FPS target.

## Compatibility and validation limits

- **AC-WoW Transmog, its provider, converter and model assets are excluded.**
- Updates preserve accounts, settings, profiles and DAT files. Linux updates are
  downloaded and extracted manually. Game DAT files and a server are not included.
- The full live-server dungeon/Hoshino freeze and leftover-process crash reports
  remain unconfirmed. The ten-minute synthetic crowd/lag test stayed around
  32-34 FPS, but it does not reproduce every live server's workload.
- Custom-bank purchasing and multiplayer collision behavior still require live
  acceptance. Automated tests and packaging are not physical-headset acceptance.
- Added packet-level tests for the standard fellowship horn health bonus,
  including damage, refresh, stacking and expiry. The reported Conquest
  693/683 display has not been reproduced and is not claimed fixed.
