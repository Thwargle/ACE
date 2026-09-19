using ACE.Server.Entity;

namespace ACE.Server.Network.GameEvent.Events
{
    public sealed class GameEventVRPose : GameEventMessage
    {
        public GameEventVRPose(Session observer, uint source, VRPose pose, uint version = 1)
            : base(GameEventType.VRPose, GameMessageGroup.SmartboxQueue, observer, 132)
        {
            Writer.Write(source); pose.Write(Writer,version);
            if(version==2) { Writer.Write(pose.Root.X); Writer.Write(pose.Root.Y); Writer.Write(pose.Root.Z); }
        }
    }
}
