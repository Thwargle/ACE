# AC:Unreal / AC:VR release 70

Native Quest: `2026.09.24.quest.70`, Android version code 70, installer revision 6.
Windows desktop / PC VR: `2026.09.24.76`.

## Changes since v69

### Movement and networking

- Keep nearby exterior room collision active before the player crosses its cell
  boundary. Fix wall clipping at Fort Teth and Mosswart Fort through shared
  collision residency and filtering, rather than location-specific exceptions.
- Prevent creature collision bodies from becoming walkable floors or step-up
  targets. Improve falling and recovery beside walls in crowded dungeons.
- Release narrow stair-edge contacts into a proper fall and suppress tiny
  stationary slope corrections that made the VR camera shake.
- Smooth remote heading corrections, maintain movement prediction between sparse
  packets, and continue player animation beyond the previous distance cutoff.
- Buffer VR tracking samples so body, hands, equipment, and weapon draw share
  the same presentation time. Keep remote roots updating when ordinary actor
  movement ticks are suspended; reject stale samples and reset across transitions.
- Restore retail's periodic receive-socket keepalive on server port+1. This
  addresses an idle NAT-mapping failure consistent with the reported Coldeve
  disconnects; live confirmation on the affected user's connection is pending.
  Improve timeout diagnostics without changing gameplay packet sequencing.

### Inventory, chat, and retail interface

- Automatically merge picked-up stackable items into a matching owned stack
  when the entire pickup fits, including stacks in packs. Preserve intentional
  rearrangement and wait for authoritative server inventory updates.
- Append double-clicked spellbook spells to the end of the open spell tab.
- Reflow resized spellbook/effects pages: grow the list and retain bottom-anchored
  controls and descriptions. Remove faint underlying inspection scrollbar arrows.
- Restore retail Character option grouping and Apply/Reset/Defaults behavior;
  retain chat routing and opacity controls on Chat and local preferences on Config.
- Add the five retail chat font faces and five sizes using their actual DAT
  fonts on all platforms. Persist the selection and rewrap existing chat lines.
- Use shared DAT emote text for typed poses and emote keybinds, including
  one-shot/held gesture variants. Social gestures address the selected target,
  e.g. "Thwargle waves at OtherName." Noninteractive poses stay untargeted.
  Selected-target phrasing is an extension to retail's base emote table.

### VR interface and animation

- Add a native VR compass with nearby selectable markers, current coordinates,
  evenly spaced cardinal labels, and a larger decorative frame. Toggle it in
  VR settings without opening the desktop interface.
- Add native VR vitals with clearer labels, health/stamina/mana values, stance,
  combat timing, and existing placement/locking controls. HUD updates are bounded
  and unchanged vitals do not redraw continuously.
- Allow the wrist spellbar to be hidden while using the spell wheel.
- Separate incoming and outgoing combat notices with clearer labels, larger
  numbers, improved contrast, and distinct positions.
- Add subtle, smoothed torso bend/twist from tracked head and hand motion for
  local and remote VR avatars. Preserve neck, waist, and controller alignment.
- Use the retail Falling transition at jump takeoff, keep Ready/run while
  charging, and clear held jump state on landing. Shorter jump handoffs avoid
  a frozen-looking pose on rapid successive jumps.

## Install or update

Windows: run the AC:Unreal installer. It includes desktop and PC VR shortcuts.
Existing game data, accounts, and settings are retained. A portable ZIP is also
available. The Windows installer is not digitally signed.

Quest: extract the entire ZIP. Put Android platform-tools beside the installer,
connect the headset with a USB data cable, and accept USB debugging.
Run **Update-Quest.cmd** if game data is already installed. For a first installation,
run **Install-Quest.cmd** and provide your own Asheron's Call DAT files.

The app appears as **AC:VR** in Unknown Sources. Do not uninstall to update:
updating retains DAT files, saved accounts, and settings. The installer migrates
any old runtime folder when necessary. See the included instructions for setup.

## Server compatibility

Choose a server in the lobby and use your own account. Custom VR combat and pose
replication require this project's VR-enabled ACE server. Existing v60 instant
atlatl/thrown release and v69 restricted-door server changes remain required for
those server-side behaviors. This release changes client code; publishing client
packages does not update a game server.

GDLE login has been verified through character selection. The v64 packet-timing
correction is included. A live Seedsow item-interaction retest remains pending;
automated protocol checks do not establish full in-world GDLE compatibility.
Bundles contain no server, credentials, saved settings, SDK tools, or retail DAT files.

## Validation and limitations

Windows and Quest Development packages built successfully. The packaged Windows
client passed 11 suites covering UI interactions/screens, chat, targeted emotes,
Fort Teth stairs, movement, avatar animation, receive-port keepalives, VR rendering,
VR rig/menus, and wall contact. Editor-only inventory-order and VR pose-buffer
tests also passed (13 suites total).
Live headset and multiplayer acceptance of these changes remains pending.
This release does not claim complete Config-tab parity or a new FPS benchmark;
sustained 90 FPS VR and 144 FPS desktop remain targets.
Download hashes are provided in SHA256SUMS.txt.
