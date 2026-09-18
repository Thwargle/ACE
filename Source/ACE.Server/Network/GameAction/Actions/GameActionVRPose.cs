using ACE.Server.Entity;

namespace ACE.Server.Network.GameAction.Actions
{
    public static class GameActionVRPose
    {
        [GameAction(GameActionType.VRPose)]
        public static void Handle(ClientMessage message, Session session)
        {
            if (session.Player != null && VRPose.TryRead(message.Payload, out var pose)) session.Player.HandleVRPose(pose);
        }
    }
}
