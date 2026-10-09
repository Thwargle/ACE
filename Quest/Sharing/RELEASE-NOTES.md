# AC:Unreal / AC:VR release 103

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.08.103**. Android version code: 103. Quest installer revision: 7.

## Gameplay fixes

- Honor the server's ethereal flag for pets and other creatures after spawning,
  physics updates, and visibility changes. Pets no longer become solid obstacles.
- Fix custom vendors whose zero catalog quantity prevented Add to List and Buy.
- Keep typing in Loot Editor and other editable text fields from triggering
  gameplay movement or shortcuts.

## UCM

- Limit fast-buff backward movement to a brief 0.12-second pulse, independent of
  delayed spell results. Send both movement and release through normal networking
  while casting is busy; repeated spell messages cannot restart the bump.
- Add direct attack spell selection and Void damage-over-time controls. Correct
  Nether damage handling and conversion of imported VT profiles.
- Add an optional desktop-only setting to suspend the 3D world view while UCM
  runs. Interface, simulation, and networking continue; stopping UCM restores
  the view. This does not pause the game or apply to VR.

## Waypoint map

- Keep nearby portal markers visible while decluttering their labels. Shorten
  the "Portal to" prefix in map labels while retaining full names on hover.
- Minimize the pinned map independently of the direction arrow. Preserve its
  expanded size and position, and keep the minimize control usable while locked.
- Add a thin gold map border and adjustable map opacity.
- The existing Show / hide interface binding remains configurable under
  Keyboard > UI, allowing an alternative to Alt+Z.

## Validation and installation

Targeted editor regressions cover text focus, Void selection and imports, map
rendering and persistence, buff movement and release, custom vendor catalog
quantities, creature collision, and VR wall contact. These checks do not replace
live-server or headset acceptance testing.

Installing an update closes the game. Accounts, settings and DAT files are retained.
Linux updates are downloaded and extracted manually. Game DAT files are not included.
