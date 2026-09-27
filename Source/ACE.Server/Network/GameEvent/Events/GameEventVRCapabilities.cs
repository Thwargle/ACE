namespace ACE.Server.Network.GameEvent.Events
{
    public sealed class GameEventVRCapabilities : GameEventMessage
    {
        public GameEventVRCapabilities(Session session, ushort teleport = 0, System.Collections.Generic.IReadOnlyCollection<uint> objects = null)
            : base(GameEventType.VRCapabilities, GameMessageGroup.UIQueue, session, 24)
        {
            Writer.Write(1u); // version
            // 32768: equipment/root poses; 65536: retail combat slider;
            // 131072: receive-only pose subscription (including desktop observers).
            Writer.Write((objects == null ? 65527u : 65535u) | 65536u | Entity.VRCombatRequest.PoseObserverCapability);
            if (objects != null)
            {
                Writer.Write((uint)teleport);
                Writer.Write((uint)objects.Count);
                foreach (var guid in objects) Writer.Write(guid);
            }
            Writer.Write(session.Player?.GetEquippedMissileWeapon()?.Guid.Full ?? 0u);
            Writer.Write(session.Player?.GetProjectileSpeed() ?? 20.0f);
        }
    }
}
