# AC:Unreal / AC:VR release 150

Windows desktop, PC VR, native Linux x86_64, standalone Quest, and the Android
phone/tablet preview share **2026.11.05.150**. Android version code: 150.
Quest installer revision: 9. These notes cover changes since v149.

## Networking and combat UI

- Improve network catch-up during heavy traffic. When dispatchable updates wait
  at least 100 ms, temporarily increase the receive/dispatch budget from 0.5 ms
  to 8 ms per frame, retaining packet limits and ordered delivery.
- Expand /acueperf diagnostics to distinguish socket backlog, dispatch queues,
  and updates waiting for missing packets or objects.
- Restore the Recklessness range indicator on the melee attack bar for trained
  and specialized characters, using the end-of-retail 10-90% range.

## Linux

- Expand resolution selection beyond the saved 1080p entry: detect monitor modes
  and offer suitable 1440p, 4K, ultrawide, and common window sizes.
- Refresh the resolution list when opened, retain custom/saved selections, and
  default fresh installations to desktop resolution. Keep the 15-second
  confirmation/revert safeguard.
- Add optional password saving on the Accounts tab. Off by default; Linux
  passwords are stored unencrypted in a file restricted to the current user.
  Turning it off removes saved passwords.

## Validation and packaging

Automated validation covers sustained ordered network traffic, recovery from
backlogs, Recklessness/UI rendering, login persistence, resolution selection,
settings save/reload/revert, launchers and updates. Native Linux display mode
queries passed under WSLg X11 and Wayland. Controlled traffic tests drained all
events in order without duplicates; live crowded-event improvement still needs
player verification. No physical Linux monitor, Quest or phone acceptance test
was performed for this release.

Packages exclude retail DATs, personal settings, server data and generated
MeshCache files. Android phones/tablets remain a preview with a separate APK
from Quest. Detailed release validation results accompany the local artifacts.
