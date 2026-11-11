# AC:Unreal / AC:VR release 155

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.11.155**. Android version code: 155.
Quest installer revision: 10. These notes cover changes since v154.

## Retail chat commands

- Fix /pkarena and /pklarena recalls, including /pka and /pla aliases.
- Add /consent and /permit commands for corpse-looting permissions.
- Add /corpse and /cor to report the last outdoor corpse location supplied by
  the server, including after relogging.
- Add /render fov and /render radius with saved settings. Native Android retains
  its terrain memory limit.
- Correct housing permission/list commands, boot aliases, and available-house
  type selection. /house alone now displays help instead of recalling.
- Restore retail confirmations for /die and /house abandon.
- Correct AFK on/off behavior and custom message limits/resetting.
- Restore missing chat aliases and improve quoted-name and whitespace handling.
- Expand local help to cover all 130 public retail command names, aliases, and
  help topics. Use /help commands or /help followed by a command name.
- Keep graphics controls bound to the correct settings when new options are added.

## Verification and scope

Validation covers Editor and packaged Windows/native Linux command, network and
interface tests, APK contents/signatures, and an isolated Windows upgrade.
Live-server command execution and physical Quest/phone acceptance remain unverified.
The Quest installer continues to include Diagnose-Quest.cmd for startup diagnostics.

Packages exclude retail DATs, personal settings, server data and generated MeshCache
files. Android phones/tablets remain a preview APK.
