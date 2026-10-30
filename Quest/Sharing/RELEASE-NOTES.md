# AC:Unreal / AC:VR release 143

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.10.29.143**. Android version code: 143.
Quest installer revision: 9. These notes cover all changes since v142.

## Combat movement

- Correct desktop and touch forward/backward movement during spellcasting,
  power-up gestures, and missile aim/reload transitions. Movement now respects
  the active retail animation sequence instead of predicting a full-speed run
  underneath it.
- Resume held movement correctly after combat control returns to the player,
  including when movement was already held before the cast. Preserve separate
  turning/sidestepping and existing melee, stance-change, and airborne rules.

## Chat and UCM

- Add a per-window **Plugin chat** filter in Options > Chat for local plugin
  notices and UCM loot announcements. Preserve existing colors and closed-window
  history, and migrate saved filter preferences.
- Expose **Announce item ID matches** in UCM Configuration and Loot settings.
  Disabling it clears pending match notices without affecting looting or the
  separate pickup-announcement option.

## Packs inside chests

- Allow packs in an external container's pack list to be dragged back into
  inventory. Clicking still opens a pack; right-clicking examines it.
- Return the loot view to the parent chest after the server confirms removal,
  while retaining the removed pack's contents.
- Include nested packs in touch/VR loot menus with separate open and take
  actions.

## Route creation

- Accept route/profile names containing spaces, such as **Academy Loop**, and
  include them in both the Route and Overview dropdowns.
- Show creation errors beside the name field, with clear duplicate-name and
  file-write explanations. Failed creation preserves the active route and
  existing saved data.
- Add end-to-end UI coverage for creation by button or Enter, automatic
  selection, waypoint saves, both dropdowns, disk reloads, and failure handling.

## Validation and packaging

Focused tests cover retail DAT combat sequences, movement packets, chat routing,
loot notifications, container interactions, touch/VR menus, and route persistence.
Build and package verification results accompany the release locally.
Motion fixtures accept the configured retail DAT directory on Windows and Linux.
Live-server and device testing remain distinct from automated coverage; this
release has not been newly accepted on a physical phone or headset.

Packages do not include retail DATs, personal settings, server data, or generated
MeshCache files. Android phones/tablets remain a preview and use a separate APK
from Quest. Existing Android signing identities and installer revision are kept.
