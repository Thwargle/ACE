# AC:Unreal / AC:VR release 118

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.15.118**. Android version code: 118. Quest installer revision: 9.

## Plugin windows and controls

- Add background/frame opacity sliders with live previews and percentage readouts.
  Apply one value to every plugin or use per-plugin settings; text and icons remain readable.
- Pin plugin windows without their outer frame or background. Quick-show restores
  movement and resize controls without shifting the contents. Allow smaller windows.
- Add a gear icon to open Plugin Manager. Its window now has one title bar and
  supports moving, resizing, pinning, and saved open/closed state like other plugins.
- Drag plugin bar icons or Plugin Manager row grips to reorder plugins. The saved
  display order is shared by both views and does not change plugin execution order.
- Fix enabled checkboxes appearing empty on the initial draw while retail artwork
  loads. Refresh existing controls without rebuilding the window or closing menus.
- Expose opacity and pin defaults through Lua plugin presentation metadata, with
  player preferences taking precedence.

## Ledgers, maps, and effects

- Toggle every Supply Ledger item name together by clicking a row name or icon.
  Keep counts visible and in place; resize the window for a compact icon-only view.
- Keep the add-item control available when an empty Supply Ledger is pinned.
- Save supply lists, name visibility, plugin layout and pin preferences, and
  separate indoor/outdoor zoom levels for both the full and pinned waypoint maps.
- Keep held-weapon glow lighting attached to the current emitter position while
  moving or turning, including the light fade after an emitter stops.

## Validation and known limitations

- Extend the offline wasp stress fixture with configurable crowds and spell volleys,
  real movement and charged jumps, resolution control, and corrected frame timing.
  Windows testing reproduced brief large-volley stalls, but not the reported sustained
  1-2 FPS collapse. Bulk particle initialization remains a known performance issue.
- **AC-WoW Transmog, its provider, converter and model assets are excluded.**
- Updates preserve accounts, settings, profiles and DAT files. Linux updates are
  downloaded and extracted manually. Game DAT files and a server are not included.
- Builds and automated checks do not establish live-server, Linux desktop, or
  physical-headset acceptance.
