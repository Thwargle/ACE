# AC:Unreal / AC:VR release 91

Windows desktop, PC VR, native Linux desktop, and standalone Quest:
**2026.10.03.91**. Android version code: 91. Quest installer revision: 7.

## Changes since public release v90

### Movement and combat

- Preserve post-jump landing momentum using the server's friction value instead
  of immediately stopping the character on contact.
- Make melee and missile attacks wait for a held attack button to be released.
  Quick clicks still charge to the requested power; longer holds use the charged
  power. Cancel pending and repeating attacks when moving or changing combat mode.
- Restore keyboard attack-power adjustments while retaining spell-tab controls
  in magic mode.

### Interface and inventory

- Prevent disabled skill/attribute raise buttons from spending remaining XP when
  there is not enough for the next point, in desktop and VR interfaces.
- Allow selection and copying of multiple lines in the desktop chat history.
- Expose Toggle Chat Entry in keyboard settings and support its retail keymap
  name while preserving the chat draft and caret when toggling focus.
- Make Examine toggle an already-open inspection window for the same selection.
- Make Pick Up remove selected worn clothing and jewelry into inventory.
- Include missing item ratings in inspection text.

### Custom objects

- Preserve server-authored creature spawn heights when no ground-contact state
  is supplied. This fixes object-looking NPCs being pulled beneath pedestals.
- Initialize animated NPC poses before their first draw and include elevated
  artwork in click detection without enlarging authored movement collision.
- Verify the supplied Daralet Tou-Tou crystal setup, clothing, animation, and
  0.75 scale against retail DAT data, including repeated updates and recreation.
  No custom server names, weenie IDs, or placement offsets are hardcoded.

### Installation and Linux

- Include Quest installer revision 7: use ADB's file-transfer protocol for large
  DAT files, verify size and SHA-256 inside app storage, and preserve existing
  accounts, settings, and verified data during repair.
- Disable Linux PSO precaching as a workaround for the reported Radeon/Mesa ACO
  shader-compiler abort. First-use shader compilation may still cause a hitch.
- Document launching Linux with `bash ./AC-Unreal.sh` when archive extraction
  or the file manager prevents direct script execution.

## Updating

Windows and Quest users can use **Updates** in the launcher or download from the
site. The updater detects release 91. Accounts, settings, and DAT files are
retained; do not uninstall first. **Installation closes the game.** On Quest,
confirm Android's installation prompt, wait for completion, then reopen AC:VR.
Optional automatic updating remains off by default.

Linux users should close the game, extract the new archive into a new folder,
and run `AC-Unreal.sh` (or `bash ./AC-Unreal.sh`). Linux updates require manual
extraction. See `README-LINUX.txt` for setup requirements.

No retail DAT files, saved accounts, or server configuration are included.

## Validation scope

Windows, Linux, and Quest packages built successfully. Ten targeted gameplay
regression suites and the packaged Windows launcher test passed. Coverage includes
combat input, landing momentum, chat, keybindings,
inspection, XP spending, custom-object rendering and selection, remote movement,
and stair support. Release packaging, website metadata, Windows updater, and
seven Quest data-transfer checks passed. The Quest DAT transfer repair was also previously verified
with the full portal DAT on Quest 3 using Windows PowerShell 5.1.

Daralet's live placement and interaction still require server-side confirmation;
the supplied weenie does not include its spawn coordinates. Linux GPU runtime
and live headset acceptance remain separate from compilation and offline tests.
