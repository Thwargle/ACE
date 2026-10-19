# AC:Unreal / AC:VR release 124

Windows desktop, PC VR, native Linux x86_64, and standalone Quest share version
**2026.10.19.124**. Android version code: 124. Quest installer revision: 9.

## Inventory, vendors and world objects

- Preserve the server's item order in side packs, including login snapshots that
  arrive before the backpack itself. Keep unresolved item slots in their original
  positions as descriptions arrive.
- Prevent unsolicited inventory snapshots from being treated as newly opened
  ground containers and discarding another pack's contents.
- Keep renewed vendor item descriptions alive after reopening a shop, preventing
  old cleanup timers from turning listed merchandise into "Unknown" items.
- Fall back to the authored resting pose when a corpse's motion table has no
  requested On/Off transition, instead of displaying an upright bind pose.

## Hunt Ledger

- Count the first luminance reward when login omits an initial zero balance,
  including characters who unlock luminance during the session.
- Show available XP and luminance separately from earnings, mark a full luminance
  balance with "(cap)", and show "--" while waiting for a starting balance.
- Track credited luminance directly from server balance updates. Spending does
  not subtract earnings; bonuses and sharing are already reflected in the credits.
- Keep the reset button clear of the resize handle.

## Linux updates and shutdown

- Download, verify, install and restart Linux updates from the launcher with the
  included Python 3 helper. Preserve saved settings, DAT files and user plugins.
- Validate archive paths and executable versions, wait for the requesting client
  to exit, and restore previous files if installation fails.
- Stop repeated automatic installation attempts after a failed update until the
  player checks again. Close other clients using the same installation first.
- Allow 45 seconds for requested exit or fatal-error cleanup, then terminate a
  stuck process. Normal gameplay, minimization and loss of focus do not trigger it.
- **Upgrading from v123 or earlier on Linux still requires one manual extraction.**
  The new installer becomes available after launching v124. It requires
  `/usr/bin/python3` and a writable installation folder.

## Validation and remaining limits

- Regression coverage includes side-pack order, vendor description lifetime,
  supplied corpse fixtures, luminance packets and UI, updater rollback and restart,
  and bounded Linux shutdown. Platform packaging does not establish live playtest
  or physical-headset acceptance.
- The exact custom-server decoration selection mismatch remains unresolved.
  Usable quest bodies remain interactive; this release does not hide all corpses.
- The Linux safeguard addresses hangs during exit/fatal cleanup. The original
  cause of every reported ghost process has not been reproduced.
- Luminance blocked by its cap or deposited into an unreported custom bank cannot
  be measured from balance updates. The reported all-session zero has not been
  reproduced on a live server; no custom `/lph` commands are issued.
- Game DATs, server files, saved accounts/settings and optional AC-WoW Transmog
  assets are excluded from client downloads. No new sustained Quest FPS claim is made.
