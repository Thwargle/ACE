# AC:Unreal / AC:VR release 139

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.27.139**. Android version code: 139. Quest installer revision: 9.

These notes cover all changes since **v138 / 2026.10.25.138**.

## Retail skill and attribute data

- Stop retired server skills such as Axe, Dagger, and Sword from appearing in
  the skill list when they have no definition in the loaded DAT skill catalog.
  Apply the same catalog to login data, live updates, desktop, VR, and plugins;
  preserve skills explicitly defined by custom DATs.
- Clear cached skill definitions and vital formulas when changing DAT files.
  Reject incomplete skill tables as a basis for filtering player skills.
- Log loaded DAT iteration numbers to help diagnose outdated game data.
  Verify the local end-of-retail DATs contain all 38 current skills.

## Network animation timing

- Honor slow and paused server animation rates for emotes and movement substates
  instead of forcing a minimum playback speed.
- Keep authored death movement synchronized with paused death animations.

## VR menus and FPS display

- Keep the native VR spellbook active when learning a spell, reveal the new
  spell within the current filters, and preserve working filter controls.
- Restore text-entry visibility when reopening spell search and fellowship pages.
- Show the FPS meter only during gameplay in desktop and VR. Hide it on login
  and character screens while preserving its settings for the next session.

## Validation and distribution

- Expand regression coverage for retired skill records, DAT reloads, slow and
  paused animation rates, VR spellbook controls, and FPS display lifecycle.
- Ship matching Windows, Linux, and Quest packages without personal settings,
  account data, server databases, or retail DAT files.
- Automated and packaged-client checks do not replace live-server comparison,
  native Linux GPU testing, or physical Quest/PC VR acceptance.
