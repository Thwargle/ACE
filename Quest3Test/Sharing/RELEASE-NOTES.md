# ACE client release 42

Native Quest: `2026.09.17.quest.42`, Android version code 42, installer revision 6.
Windows desktop and PC VR: `2026.09.17.52`.

## Install or update

Extract the complete Quest ZIP. Put Android platform-tools beside the installer,
connect with a reliable USB data cable, and accept USB debugging inside the headset.
Run **Update-Quest.cmd** if game data is already installed. This preserves DAT files,
login entries, and settings. For a first installation use **Install-Quest.cmd**
and supply your own Asheron's Call DAT files. See README.txt for troubleshooting.

Extract the complete Windows ZIP and run **ACEViewer.exe** for desktop or
**Launch-VR.bat** for PC VR. See README-WINDOWS.txt for OpenXR setup.

Use your own account on the host's updated VR-enabled ACE server. These archives
contain no server, account data, saved settings, or retail DAT files.

## Changes since release 38

- Menu opens VR settings; clicking the left stick opens the spell wheel. The right
  stick chooses a spell, A/B change tabs, grips page long tabs, and trigger confirms.
  Movement remains available; a fresh trigger press casts the chosen spell.
- Magic stance shows the wrist spellbar. In peace, B can inspect the pointed world
  object. Deliberate two-hand contact can use doors, lifestones, and usable objects;
  brief contact, movement, menus, and combat cancel that gesture.
- Updated crossbow/bolt placement, thrown-stack handling, and atlatl aiming from
  the heel of the dominant palm. Projectiles ignore their own caster while retaining
  world obstruction checks. Neighboring-cell transitions no longer alone reject
  a current combat request.
- Combat feedback distinguishes incoming YOU events from outgoing TARGET events.
  Enemy health retains the heart/vial artwork without the exterior black backing.
- Updated water support and steep-slope sliding; vendor quantities, stable open
  vendor placement, and long-book scrolling received fixes. Cloud layers render
  over celestial objects.
- Chat Up/Down recalls the focused window's sent messages. `/r ` expands the last
  teller before sending; patron/monarch reply shortcuts and retell routing are fixed.
- Quest keyboard Enter/OK submits chat directly, without a second Submit click.
  It includes the final typed text, and duplicate confirmations cannot resend it.
  PC VR keyboard Enter and auxiliary chat windows are covered too.

## Validation and remaining checks

Windows and ARM64 Android packaging succeeded. The shared client source, input
configuration, and runtime materials match. Chat/input tests passed on desktop,
mobile preview, and the packaged Windows client; eight native keyboard action
scenarios passed with stub Android/JNI boundaries. Prior v40/v41 functional tests
cover the broader changes above.

This is a test release. Version 42 has not yet had a live headset acceptance check.
Please test keyboard Enter/OK, repeated chat sessions, spell-wheel controls,
missile ammo/aim, two-hand use, water/slopes, and cloud layering together.
Consistent 90 FPS in VR and 144 FPS on desktop are not established; dense areas
remain below target. Mobile shadows still have visible cascade transitions.
Some in-game build labels may lag package metadata; report the manifest version.
