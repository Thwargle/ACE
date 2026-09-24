# Retail UI layout commands

## Retail behavior reviewed

Primary reference: the local retail client source in `C:/dev/retail_client_source`.

- `ClientCommunicationSystem.cpp`: `DoSaveUI`, `DoLoadUI`, `DoSaveAutoUI`,
  `DoLoadAutoUI`, `DoLockUI`, their command registration and help text.
- `gmGamePlayUI.cpp`: `CreateScreenLayoutPath`, `SaveScreenLayout`,
  `LoadScreenLayout`, `RecvNotice_PlayerDescReceived`, and
  `ListenToGlobalMessage` (resolution-change reload).

Retail writes one rectangle per window, for example:

```text
<CHAT> X:35 Y: 640 W: 520 H: 240
<TBAR> X:1500 Y: 920 W: 310 H: 132
```

There are 16 tags: SBOX, CHAT, FCH1-FCH4, EXAM, VITS, SVIT, ENVP, PANS,
TBAR, INDI, PBAR, COMB, RADA. This is geometry only: it does not contain
spells, inventory shortcuts, visibility, chat filters, keybinds, or lock state.

Named files are `<name>.txt`. An empty name resolves to `UI-Default.txt` in
CreateScreenLayoutPath. One retail help string claims the unnamed save is
character-specific, but the actual command/path code uses the default file.
The implementation follows the executable code.

`saveautoui` explicitly saves a snapshot under a server/character/resolution name.
Retail loads that snapshot after the player description and on resolution changes.
It does not continually overwrite it as windows move. `loadautoui` manually
restores the same snapshot.

## Shared client implementation

- `/saveui [name]` and `/loadui [name]` save/restore named snapshots in
  `Saved/UILayouts`. The default is `UI-Default.txt`; names accept up to 16
  characters, with quotes for spaces and an optional `.txt` suffix.
- `/saveautoui` and `/loadautoui` use
  `Saved/UILayouts/Auto/<Desktop|PCVR|Quest>/UI-<server>-<character>-<width>-<height>.txt`.
  Unsafe identity characters are escaped without conflating distinct names.
- `/lockui` toggles the existing lock implementation. Both `/` and `@` command
  prefixes work. All commands are local and send no game action to ACE or GDLE.
- Help topics explain file locations and snapshot behavior. Save/load errors and
  the full successful file path are reported in chat.
- Auto loading waits for character identity and gameplay geometry. Subsequent
  HUD ticks compare the cached session/player/mode/size without filesystem I/O.
  Returning to a saved size reloads its snapshot. Dragging does not overwrite it.
- Existing retail files can be copied into `Saved/UILayouts` and loaded by name.
  Export uses the same tagged rectangle format.

## Adaptations and boundaries

- Width and height refer to the effective logical UI canvas. Desktop UI scaling
  therefore selects a fitting layout without changing the user's scale setting.
  Auto files are separated by desktop, PC VR, and native Quest display modes.
- Saved world-view rectangle SBOX is accepted and exported but is not applied:
  Unreal continues to render the world to the full viewport, including stereo VR.
- The commands restore the retail window layout, not the independent VR wrist,
  pinned-vitals, menu-plane poses, or VR comfort settings.
- Loaded sizes respect window constraints. Fixed-height vital artwork stays fixed;
  toolbar height supports one or two shortcut rows. Hidden windows are also clamped
  to the canvas, so reopening one cannot leave it unreachable.
- The existing last-used layout cache remains active independently of snapshots.
  Radar/power-bar placement and non-chat widths now participate in that cache.
- Recognized malformed/duplicate records reject the complete file before mutation;
  unknown tags are ignored. File size is capped at 64 KiB and names cannot escape
  the layout directory. Saves replace a completed temporary file instead of
  truncating the previous snapshot before the write completes.

## Verification

`ACE.RetailParity.UILayoutCommands` covers the retail text format, geometry round
trips, hidden-window clamping, vital height, hotbar rows, invalid files, names,
chat dispatch, auto loading, explicit snapshot preservation, context separation,
and absence of server packets. The existing UIScreens suite checks shared layout,
resize, and rendering behavior. Validation outputs are in ignored
`Unreal/Saved/UILayoutAudit`.

Passed: Win64 editor build, Android Development build, desktop UILayoutCommands
and UIScreens, ES3.1 mobile-preview UILayoutCommands and UIScreens, and the
fresh-session geometry regression. The Quest source mirror matches the shared
implementation. Live headset testing has not been performed for this change.

These changes are source updates; no new release has been published for this task.
