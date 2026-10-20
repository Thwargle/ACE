# AC:Unreal / AC:VR release 126

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.19.126**. Android version code: 126. Quest installer revision: 9.

## Retail rendering, physics, and networking

- Restore retail's 50-metre distance offset before selecting authored clothing
  detail levels, preventing nearby vendors and players from abruptly changing models.
- Cache dungeon cell distances during streaming plans to reduce repeated DAT
  reads, including Facility Hub. Add focused streaming diagnostics and regressions.
- Correct overlapping mansion floor presentation and let gravity-enabled objects
  settle after their room collision is ready, including after landblock reloads.
- Restore house-hook drag/drop and removal controls, and horizontal scrolling in
  external containers such as mansion chests.
- Deliver received network packets without waiting behind an unrelated missing
  UDP packet; preserve ordered game events per owner, reject stale property
  updates, and bound receive work across frames.
- Use replicated elasticity and inelastic collision behavior, and retain valid
  lower-body landing contacts at large world coordinates.
- Use authored low, medium, and high attack/aim motions without blending attack
  poses or falling back to another weapon style. Preserve queued stamped attacks.

## Third Eye and maps

- Add Third Eye, an optional bundled spawn watcher with its own eye icon and
  Plugin Manager checkbox. Save exact/partial name watches per character/server;
  show nearby matches, local alerts, selection, and optional world/map markers.
- Provide searchable, paginated Third Eye lists with compact page controls and
  adaptive page sizes, without a panel scrollbar.
- Give pinned maps a black background controlled by the opacity setting. Save
  separate outdoor/interior zoom levels, optionally fill walkable floor polygons
  in blue, and show the active UCM route and current waypoint.
- Keep portal names visible consistently, including overlapping Town Network labels.

## Plugins, combat, and interface

- Distinguish active tabs, prevent mid-word label wrapping, and improve retail
  bitmap font sampling so thin letters remain readable.
- Restore retail green dropdown artwork and selected-row styling.
- Preserve the user's appraisal target for server commands such as /getinfo and
  /vendordump while plugins perform background inventory appraisals.
- Automatically stow incompatible arrows, quarrels, or darts before equipping a
  different ranged weapon, for both manual actions and UCM. Wait for confirmation
  of each removal and retain compatible ammunition.
- Recover from missing spell acknowledgments without dropping the queued next
  buff action. Show a fixed-size Current status with a full-text tooltip.
- Add UCM Micro F / CF controls for Force Buff and Cancel Force Buff, FC to follow
  a selected character, and C to follow observed footsteps without skipping corners.
  Record follow positions during casting/equipment waits and pause on lost targets
  or exhausted movement history.

## Validation and limits

- Regression coverage exercises network loss/reordering, dungeon streaming,
  mansion placement, collision, attack heights, armor distances, plugin interfaces,
  map routes, appraisal context, and ammunition switching.
- Third Eye can only detect objects already known to the client. Route lines show
  recorded waypoints; corner following uses observed positions, not unseen paths.
- Automated and packaged checks do not establish full retail parity, live-server
  soak acceptance, or sustained physical-headset frame rates. Intermittent stalls
  and location-specific performance reports still need live confirmation.
- Game DATs, server files, saved accounts/settings, and optional AC-WoW Transmog
  assets are excluded from client downloads.
