using System;
using ACE.Server.Entity;
using ACE.Server.Network.GameEvent.Events;
using ACE.Server.Network.Sequence;

namespace ACE.Server.WorldObjects
{
    partial class Player
    {
        private bool vrPoseSubscribed;
        private uint vrPoseSequence;
        private DateTime vrNextPose;
        private DateTime vrLastTrackedPose;
        private bool HasActiveVRHands => DateTime.UtcNow - vrLastTrackedPose < TimeSpan.FromSeconds(.75);

        public void HandleVRPose(VRPose pose)
        {
            if (!vrNegotiated || PhysicsObj == null || Teleporting || !IsAlive || Location == null
                || pose.Cell != Location.Cell || pose.Teleport != BitConverter.ToUInt16(Sequences.GetCurrentSequence(SequenceType.ObjectTeleport), 0)
                || unchecked((int)(pose.Sequence - vrPoseSequence)) <= 0) return;
            vrPoseSequence = pose.Sequence;
            var now = DateTime.UtcNow;
            if (now < vrNextPose) return;
            vrNextPose = now.AddMilliseconds(40); // max 25 Hz; client normally sends 20 Hz
            vrLastTrackedPose = (pose.Flags & 4u) != 0 ? now : DateTime.MinValue;
            // Sending this new, validated opcode is explicit opt-in. A retail
            // client or an older VR client never receives an unfamiliar event.
            vrPoseSubscribed = true;
            foreach (var observer in PhysicsObj.ObjMaint.GetKnownPlayersValuesAsPlayer())
            {
                if (observer == this || !observer.vrPoseSubscribed || observer.Session == null || observer.Teleporting
                    || Visibility && !observer.Adminvision) continue;
                observer.Session.Network.EnqueueSend(new GameEventVRPose(observer.Session, Guid.Full, pose));
            }
        }
    }
}
