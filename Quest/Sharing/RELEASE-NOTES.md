# AC:Unreal / AC:VR release 111

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.12.111**. Android version code: 111. Quest installer revision: 7.

## UCM combat, buffing, navigation, and salvage

- Clear obsolete combat approaches and queued actions when a target dies, and
  continue target selection without waiting for optional monster appraisal.
- Reduce unnecessary delays between activities while retaining server cast,
  equipment, and attack acknowledgments and vital recovery priority.
- Replace held backward fast-buff movement with a press/release in the same
  update, avoiding accumulated backward travel toward dungeon portals.
- Smooth route returns and advance already-reached walk points in one decision.
  Keep arrival latched so small position corrections do not restart steering.
- Apply Peace mode when idle before looting in clear areas, while nearby living
  monsters keep the player in combat stance.
- Detect available Usts across inventory for salvage rules and bag combining.

## Plugin windows and new ledgers

- Add Supply Ledger to track selected inventory items with their actual item
  icons and counts, and Hunt Ledger for experience and luminance totals and
  five-minute/hourly rates with a resettable run.
- Use compact original plugin icons with full names on hover, retail-style
  colors and borders, and consistent button sizing.
- Use the radar lock control for plugin pinning. Hold the rebindable Quick-show
  key (Left Control by default) to move pinned windows. Locked ledgers hide
  their title bars and editing controls.
- Make Supply Ledger narrower and resizable, retain its size and position, and
  use one add button plus per-item remove controls.
- Remove duplicate body titles from standalone plugin windows. Keep plugin-bar
  visibility in Manage Plugins rather than UCM's Overview tab.
- Restore loot-rule rendering and preserve the list position when selecting
  rules. Improve scaled text and tab/button layout.
- Correct the floating navigation arrow's heading while inside dungeons.
- Let Lua plugins supply a window title and icon through presentation metadata
  and runtime updates; document these controls and avoid redundant headings.

## Movement and maintenance

- Preserve position precision in ground support and building cell transitions
  to reduce catching and jitter at entrances and walkway intersections.
- Synchronize shared client sources and release metadata across platforms.
- **AC-WoW Transmog, its provider, converter, and model assets are excluded.**

## Installation and validation

Automated checks cover the changed UCM, plugin UI/API, movement, collision, and
packaging paths. Linux and headset gameplay require live acceptance; package
validation alone is not live gameplay certification.

Installing an update closes the game. Accounts, settings, and DAT files are
retained. Linux updates are downloaded and extracted manually. Game DAT files
and a game server are not included.
