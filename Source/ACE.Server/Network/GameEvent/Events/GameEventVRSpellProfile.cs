namespace ACE.Server.Network.GameEvent.Events
{
    // Reply only to an explicit profile request. Retail and old VR clients
    // never receive this extension; damage/collision remain authoritative.
    public sealed class GameEventVRSpellProfile : GameEventMessage
    {
        public GameEventVRSpellProfile(Session session, uint spell, float speed, bool gravity, float? radius = null)
            : base(GameEventType.VRSpellProfile, GameMessageGroup.UIQueue, session, 24)
        {
            Writer.Write(spell);
            Writer.Write(speed);
            Writer.Write(gravity ? 1u : 0u);
            if (radius.HasValue) Writer.Write(radius.Value);
        }
    }
}
