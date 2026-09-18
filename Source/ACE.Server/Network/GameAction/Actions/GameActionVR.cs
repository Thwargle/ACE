using ACE.Server.Entity;

namespace ACE.Server.Network.GameAction.Actions
{
    public static class GameActionVR
    {
        [GameAction(GameActionType.VRCombat)]
        public static void Handle(ClientMessage message, Session session)
        {
            if (session.Player != null && VRCombatRequest.TryRead(message.Payload, out var request))
                session.Player.HandleVRCombat(request);
        }
    }
}
