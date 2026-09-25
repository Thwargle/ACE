namespace ACE.Server.Network.GameEvent.Events
{
    public sealed class GameEventVRCapabilities : GameEventMessage
    {
        public GameEventVRCapabilities(Session session, ushort teleport = 0, System.Collections.Generic.IReadOnlyCollection<uint> objects = null)
            : base(GameEventType.VRCapabilities, GameMessageGroup.UIQueue, session, 24)
        {
            Writer.Write(1u); // version
            Writer.Write((objects == null ? 65527u : 65535u) | 65536u); // 32768: equipment pose; 65536: retail combat slider
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
