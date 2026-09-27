# Desktop observers of VR players

The desktop AC:Unreal client did not negotiate VR extensions: the hello was
only sent by the local headset component. The server also treated transmitting
a tracked pose as the only way to opt into receiving poses. Consequently a
desktop client could have the remote-avatar renderer but never receive its data.

The client capability mask additionally discarded bits 32768 (equipment/root
poses) and 65536 (combat power), preventing the v2 stream and its faster position
reporting from being negotiated normally.

## Change

- Request capabilities once on world entry in desktop as well as VR.
- Advertise capability 131072 for explicit receive-only subscriptions. The
  existing kind-4 subscription keeps its exact 12-byte layout: bit 16 requests
  poses and bit 32 allows v2 equipment/root poses. Both ends retain older wire
  layouts; unsupported servers are never sent the new subscription bits.
- Keep desktop subscriptions limited to avatar playback. A local VR component
  explicitly opts into combat/UI feedback, including if it activates after the
  shared handshake. Receiving a pose never marks the observer's hands tracked.
- Preserve sender-based opt-in for old VR clients and downgrade relayed v2 poses
  to v1 for old observers. Unmodified retail clients receive no extension events.
- Retain all currently known capability bits and reset local tracking/subscription
  state when leaving a character or disconnecting.

Both the VR server and AC:Unreal clients need this change. This does not add
head/hand animation support to the original retail executable. No public server,
website package, or headset installation is changed by this review.

## Validation

Artifacts: `Unreal/Saved/ReleaseValidation/sep26-desktop-vr-observer/`.

- Server protocol and isolated live-DAT relay tests exercise desktop opt-in,
  negotiation gating, legacy headset opt-in, v1/v2 delivery, duplicate and stale
  teleport rejection, and exclusion of retail observers. Fixtures read local
  world templates but do not start a shard or save characters.
- Client loopback tests exercise desktop world entry, capability masks,
  subscriptions, late headset activation, legacy servers and actual v2 sends.
- Remote-avatar playback runs without a local VR rig and verifies head/hand,
  equipment and interpolated position updates, including a sleeping actor tick.
  Its older exact-position assertion was corrected to verify forward progress
  within the sampled segment: the production renderer intentionally eases its
  root, so equality with the instantaneous sample is not its contract.
- Windows editor/client module and Quest Android compilation; shared-source
  mirror check. This is automated validation, not a two-player headset playtest.
