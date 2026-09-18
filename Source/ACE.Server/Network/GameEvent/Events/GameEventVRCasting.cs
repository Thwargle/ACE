namespace ACE.Server.Network.GameEvent.Events
{
    // Sent only to clients that explicitly subscribe to live casting feedback.
    public sealed class GameEventVRCasting : GameEventMessage
    {
        public GameEventVRCasting(Session observer, uint sequence, uint epoch, uint spell, uint phase, float remaining, float duration)
            : base(GameEventType.VRCasting, GameMessageGroup.UIQueue, observer, 36)
        {
            Writer.Write(sequence); Writer.Write(epoch); Writer.Write(spell);
            Writer.Write(phase); // 0 ready/cancelled, 1 windup, 2 recoil
            Writer.Write(remaining); Writer.Write(duration);
        }
    }
}
