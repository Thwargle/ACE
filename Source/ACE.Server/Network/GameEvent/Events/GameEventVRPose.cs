using ACE.Server.Entity;

namespace ACE.Server.Network.GameEvent.Events
{
    public sealed class GameEventVRPose : GameEventMessage
    {
        public GameEventVRPose(Session observer, uint source, VRPose pose)
            : base(GameEventType.VRPose, GameMessageGroup.SmartboxQueue, observer, 132)
        {
            Writer.Write(source); pose.Write(Writer);
        }
    }
}
