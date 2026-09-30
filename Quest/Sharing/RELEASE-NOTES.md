# AC:Unreal / AC:VR release 88

Windows desktop, PC VR, native Linux desktop, and standalone Quest:
**2026.09.30.88**. Android version code: 88. Quest installer revision: 6.

## Changes since public release v87

### Native Linux desktop

- Add a native x86_64 Linux download alongside the Windows and Quest packages.
  Extract the archive and run `./AC-Unreal.sh`; Wine and Proton are not required.
- Save Linux server profiles, account names, and settings separately from the
  extracted application. Passwords are not saved on Linux; enter them again after
  restarting. Supply your own updated retail DAT files through Game files.
- Detect new Linux releases in the launcher and open the matching archive in the
  browser. Linux updates use manual extraction; automatic installation is not
  available. This first Linux release targets desktop play; Linux VR is unverified.
- Keep release numbers aligned across all platforms and publish Linux download
  instructions and checksums on the website.

### VR inventory and inspection

- Add rebindable controller shortcuts: A uses or equips the selected item and B
  inspects an item or spell. Show inspection beside the main panel with its own
  close control, rather than requiring a separate navigation page.
- Keep panel geometry stable as selection changes. Add spell icons and explicit
  hotbar drop slots, with drag/drop and button-based add/remove actions.
- Organize attributes and skills using retail grouping and icons, including
  specialized, trained, and untrained skill sections.
- Correct gaps in the classic inventory background and improve selected-item
  names, material prefixes, health/mana indicators, and label fitting.

### Salvage and world objects

- Implement Ust salvage in desktop and VR interfaces: select eligible items,
  review the salvage list, remove entries, and submit through the server's salvage
  transaction. Respect retained items and report server results.
- Handle custom object descriptions and deferred world appearance across relogs
  and visibility changes so server-defined objects can reappear reliably.
- Correct close-range visibility and bounds for very large scaled models.

### Effects and crowded-scene performance

- Smooth rotating particle presentation between simulation updates, including
  Aetheria effects on slopes.
- Prevent weather particles such as snow from creating inferred scene lights.
- Share a bounded pool of effect lights across a world and prioritize nearby
  sources. Limit overlapping light energy to reduce the combined bright glow from
  crowds with illuminated weapons while retaining their authored particles.
- Cache repeated model-light estimates and avoid particle-position scans for
  sources that are outside the active light budget.

## Updating

Windows and Quest users can use **Updates** in the launcher or download from the
site. Existing supported updaters detect release 88. Accounts, settings, and DAT
files are retained; do not uninstall first. **Installation closes the game.**
On Quest, confirm Android's installation prompt, wait for completion, then reopen
AC:VR. Optional automatic updating remains off by default.

Linux users should close the game, extract the new archive into a new folder,
and run its `AC-Unreal.sh`. See `README-LINUX.txt` for requirements and setup.

No retail DAT files, saved accounts, or game-server configuration are bundled.
No new game-server deployment is required.

## Validation scope

Release validation covers platform builds, archive contents, synchronized versions,
update metadata, salvage, VR menu controls, selection data, custom object lifecycle,
large-model visibility, and particle/light regressions. Crowd benchmarks and light
comparison captures are controlled tests, not measured live gameplay FPS gains.
Live multiplayer acceptance, headset comfort, and Linux desktop/GPU compatibility
remain checks for users' hardware and server configurations.
