# AC:Unreal / AC:VR release 119

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.15.119**. Android version code: 119. Quest installer revision: 9.

## Pinned plugin backgrounds

- Keep the configured background opacity when plugin windows are pinned, including
  Supply Ledger and Hunt Ledger. Pinning hides the title bar and border while
  retaining the background behind text, icons, and counts.
- Apply individual and global opacity settings in both pinned and unpinned states.
  Quick-show restores window controls without changing background opacity or
  shifting the contents. Text and icons remain fully readable.
- Update plugin documentation and add regression coverage for pinning, unpinning,
  Quick-show, global overrides, and rendered transparent/partial/opaque backgrounds.

## Compatibility and validation limits

- **AC-WoW Transmog, its provider, converter and model assets are excluded.**
- Updates preserve accounts, settings, profiles and DAT files. Linux updates are
  downloaded and extracted manually. Game DAT files and a server are not included.
- Builds and automated checks do not establish live-server, Linux desktop, or
  physical-headset acceptance.
