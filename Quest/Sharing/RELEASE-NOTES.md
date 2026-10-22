# AC:Unreal / AC:VR release 130

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.22.130**. Android version code: 130. Quest installer revision: 9.

## Movement and corpse placement

- Correct remote NPC and player turning, walking start/stop transitions, and
  interrupted strides using the retail DAT motion tables.
- Use each creature's authored movement instead of human fallback speeds when
  animation position frames supply velocity. Includes rabbits, chickens,
  roosters and chicks through the shared movement path.
- Match retail animation-frame boundaries when entering cycles or death poses,
  including queued walk transitions and the jump-to-run landing displacement.
- Preserve authoritative corpse positions without mesh-bottom placement offsets.
  Keep physics and visible death animation on the same clock.
- Use authored collision spheres for slope support, preventing repeated sideways
  corrections. Preserve real jumps and actors with gravity disabled.

## Interface

- Keep inventory hover text clear of the mouse cursor, including near screen
  edges, while retaining the retail tooltip style.

## Validation scope

- Movement regression coverage includes 168 creature trajectories, 64 independent
  ACE physics death endpoints, and 96 corpse handoffs across flat ground, slopes,
  airborne placement, delayed appearance loading, and multiple frame rates.
- Retains v129's UCM performance, video settings, Action Deck, retail interface,
  inventory icon, mouse-release and shutdown fixes.
- Packaged checks do not certify every live-server interaction, native Linux GPU,
  or physical headset. Full live retail parity is not claimed.
- Game DATs, server files, saved accounts/settings and optional transmog assets
  are excluded from client downloads.
