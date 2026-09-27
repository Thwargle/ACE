# Desktop and VR movement compatibility

This follow-up verifies the shared observer protocol and closes a tracking-loss
handoff bug. It does not change the release version or deploy a server/client.

## Receive paths

AC:Unreal desktop negotiates receive-only VR poses on world entry. A local headset
is not required to receive or render another player's head, hands, held equipment,
or authoritative root. Enabling a headset adds local combat/UI feedback without
changing the remote-avatar renderer.

Ordinary position and motion broadcasts remain active for both kinds of sender.
VR subscriptions do not filter these broadcasts or mark a desktop player's hands
as tracked. Original retail clients keep receiving ordinary movement; their
unmodified executable cannot display the custom tracked head/hand extension.
The receive-only subscription requires the pending VR server and client changes
described in `REVIEW-2026-09-26-desktop-vr-observers.md`.

## Handoff fix

The regression reproduced a newer ordinary position correction being erased by
an older buffered VR pose. The actor then resumed at that outdated position after
tracking expired. `ApplyRemoteVRRoot` now preserves corrections received after the
sample being rendered. Ending tracked-root presentation also restores the actor's
ordinary movement/correction tick, including when the actor had gone to sleep.

This is shared desktop/PC VR/Quest code, with no viewer-mode-specific motion path.

## Validation

Artifacts: `Unreal/Saved/ReleaseValidation/sep27-cross-mode/`.

- `BeforeReport`: the new handoff regression failed before the fix, while the
  cross-mode running/animation checks passed.
- `Report`: movement review, observer negotiation, pose buffering, and rendered
  replication all passed after the fix. These cover sparse corrections, slopes,
  cliffs, tracked-root interpolation, hand/equipment rendering, and packet loss.
- `WireReport`: rendered replication additionally routes the handoff correction
  through the real F748 game-message decoder. VR fixtures go through F7B0/F7D1;
  desktop run/stop fixtures go through F74C. No direct pose-handler shortcut.
- The server suite passes 83 tests. Its relay fixture now checks both desktop and
  tracked senders against desktop, VR, and original retail observers, verifying
  identical ordinary position/motion payloads and excluding extension events
  from original retail clients.
- Windows editor/game and Quest Android compilation; shared-source mirror check.

These tests exercise network payloads and actual DAT avatar components without
two connected players or a worn headset. A live mixed-client playtest remains
necessary for subjective visual confirmation. No release packages were published.
