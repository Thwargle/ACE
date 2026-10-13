# AC:Unreal / AC:VR release 113

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.13.113**. Android version code: 113. Quest installer revision: 9.

## UCM profiles and interface

- Replace named setups with independent Character, Route, Loot, Vendors, Metas,
  and Monsters profiles, selected together from Overview's Configuration panel.
  Remember the current selections and migrate existing setup selections.
- Rename Profiles to Import and separate the vTank loot and route import actions.
- Add monster search and consistent expandable rule panels. Monster priority and
  attack range use whole-number controls with shared defaults and per-monster
  overrides.
- Give UCM Micro a full-width Start UCM / Stop UCM button.

## vTank compatibility and automation

- Import embedded meta routes and supported profile dependencies into reusable
  shared profiles. Preserve authored start conditions and existing user edits.
- Keep the active route when metas load character or loot settings; replace it
  only when loading navigation or meta data.
- Improve legacy vendor waypoint handling, restock batches, purchase limits,
  trade-note funding, and confirmation handling. Allow vital recovery while
  waiting at vendors and keep other UCM activities available if routing fails.
- Expose vendor closing through the Lua plugin API.
- Compatibility testing covers supported local and public meta/route fixtures;
  external plugin integrations that are not supported remain reported as such.

## Movement, rendering, and inventory

- Fix intermittent casting movement locks when using strafe and turn inputs.
  Manual movement correctly reclaims control and sends its release state while
  preserving unfinished cast links and normal spell timing.
- Avoid unnecessary terrain render-state rebuilds on indoor/outdoor transitions,
  reducing doorway and underpass stalls across the world, including Linux.
- Show the retail green accept and red blocked indicators when dragging items
  over salvage slots, using the same eligibility checks as the actual drop.

## Installation and validation

- **AC-WoW Transmog, its provider, converter, and model assets are excluded.**
- Automated checks cover UCM profiles, meta/vendor workflows, casting movement,
  salvage feedback, transition rendering, packaging, and updates. Live-server
  PvP under latency and headset acceptance remain separate checks.
- Updates preserve accounts, settings, profiles, and DAT files. Linux updates
  are downloaded and extracted manually. Game DAT files and a server are not
  included.
