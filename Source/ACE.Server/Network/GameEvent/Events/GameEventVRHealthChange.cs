namespace ACE.Server.Network.GameEvent.Events
{
    // Only sent after explicit subscription; retail and older VR clients keep
    // receiving the original notifications, vital updates and chat messages.
    public sealed class GameEventVRHealthChange : GameEventMessage
    {
        public GameEventVRHealthChange(Session observer, uint target, int change, uint flags)
            : base(GameEventType.VRHealthChange, GameMessageGroup.UIQueue, observer, 24)
        {
            Writer.Write(target);
            Writer.Write(change); // actual applied health, including lethal / overheal clamping
            Writer.Write(flags); // 1 = magic, 2 = critical
        }
    }
}
