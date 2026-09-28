# AC:Unreal / AC:VR release 84

Windows desktop, PC VR, and native Quest: **2026.09.28.84**.
Release number / Android version code: 84. Quest installer revision: 6.

## Changes since public release v83

### Movement, camera and world loading

- Strengthen grounded ledge detection while preserving valid stairs, slopes,
  deliberate jumps and airborne portal arrivals.
- Prevent repeated jump presses in midair from restarting the jump animation.
- Stabilize mouse camera orbit near the overhead pole to prevent inversions.
- Build bridge and platform collision independently of initial cell visibility,
  fixing the missing collision after portal entry seen in Empyrean Rescue.
- Initialize animated scenery's first pose and visibility before its first
  rendered frame. Hidden parts such as the Bind Stone spikes no longer flash
  into view during initial loading, and activation animations can still reveal them.

### Inventory and interface

- Support selected stack quantities when splitting, moving, giving and selling
  items, including dragging a partial stack into the vendor sell list.
- Honor the selected destination bag when picking up items unless
  "Pick up items into main pack" is enabled.
- Apply mana stones to the player when dropped on the paper doll's body view;
  target the specific equipment item when dropped on a visible equipment slot.
- Restore main player vitals resizing and use the retail fill textures at their
  intended height instead of stretching or cropping them vertically.
- Cover all 480 known server failure codes with an explicit display policy.
  Full-health healing kit use now reports the readable full-health message.
  Internal failures retain a generic user message and detailed diagnostic logging.
- Expand regression coverage for monster-only versus item-only target cycling.

### Installation and updating

- Skip Visual C++ x64 and GameInput installation when a compatible or newer
  runtime is already present, avoiding unnecessary prerequisite prompts.
- Keep the same release version across Windows, PC VR, Quest and the installers.

Use **Updates** in the launcher or download the current installer from the site.
Existing supported updaters detect release 84. Accounts, settings and retail DAT
files are retained; do not uninstall first. Installation closes the game.
On Quest, confirm Android's installation prompt, wait for completion, then reopen
AC:VR from the library. Optional automatic updating remains off by default.

No retail DAT files, saved accounts or game-server configuration are bundled.
This release updates the clients; no new game-server deployment is required.

## Validation scope

Regression checks cover real DAT bridge geometry and animated scenery, ledge
and stair movement, repeated jumps, camera limits, inventory stack operations,
paper doll targets, pickup destinations, vitals layout, error messages and updates.
Live gameplay remains useful for confirming reported routes on different servers.
No new measured headset FPS improvement is claimed by this release.
