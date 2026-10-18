using ACE.Server.Network.GameEvent.Events;

namespace ACE.Server.Network.GameAction.Actions
{
    public static class GameActionPingRequest
    {
        [GameAction(GameActionType.PingRequest)]
        public static void Handle(ClientMessage message, Session session)
        {
            session.Network.EnqueueSend(new GameEventPingResponse(session));
            // Optional discovery trailer. Stock ACE ignores this payload, so a
            // client need not send unknown extension actions to discover support.
            if (IsExtensionDiscovery(message.Payload))
                session.Player?.HandleVRCombat(new Entity.VRCombatRequest());
        }

        internal static bool IsExtensionDiscovery(System.IO.BinaryReader reader) =>
            reader.BaseStream.Length - reader.BaseStream.Position == 8 &&
            reader.ReadUInt32() == 0x45554341u && reader.ReadUInt32() == 1u; // ACUE, version 1
    }
}
