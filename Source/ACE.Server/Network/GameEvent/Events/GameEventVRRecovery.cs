namespace ACE.Server.Network.GameEvent.Events
{
    // Explicit opt-in: retail and earlier VR clients never receive this event.
    public sealed class GameEventVRRecovery : GameEventMessage
    {
        public GameEventVRRecovery(Session session, uint sequence, uint teleport, float remaining, float duration)
            : base(GameEventType.VRRecovery, GameMessageGroup.UIQueue, session, 32)
        {
            Writer.Write(sequence);
            Writer.Write(teleport);
            Writer.Write(remaining);
            Writer.Write(duration);
        }
    }
}
