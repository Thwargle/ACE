using System;
using System.Linq;
using ACE.Server.Entity;
using ACE.Server.Network.GameEvent.Events;
using ACE.Server.Network.Sequence;

namespace ACE.Server.WorldObjects
{
    partial class Player
    {
        private bool vrPoseSubscribed;
        private uint vrPoseVersion = 1;
        private readonly System.Collections.Generic.Dictionary<uint,(uint Weapon,uint Ammo,DateTime Seen)> vrSeenEquipment = new();
        private uint vrPoseSequence;
        private DateTime vrNextPose;
        private DateTime vrLastTrackedPose;
        internal bool HasActiveVRHands => DateTime.UtcNow - vrLastTrackedPose < TimeSpan.FromSeconds(.75);

        public void HandleVRPose(VRPose pose)
        {
            if (!vrNegotiated || PhysicsObj == null || Teleporting || !IsAlive || Location == null
                || pose.Cell != Location.Cell || pose.Teleport != BitConverter.ToUInt16(Sequences.GetCurrentSequence(SequenceType.ObjectTeleport), 0)
                || unchecked((int)(pose.Sequence - vrPoseSequence)) <= 0) return;
            vrPoseSequence = pose.Sequence;
            var now = DateTime.UtcNow;
            if (now < vrNextPose) return;
            vrNextPose = now.AddMilliseconds(40); // max 25 Hz; client normally sends 20 Hz
            // AutoPos can already be queued when the tracked pose is dispatched,
            // but the landblock physics tick has not consumed it yet. Run the
            // normal validation/collision path before publishing the root. Never
            // substitute an unchecked client position or reorder cell crossings.
            if (RequestedLocation != null && RequestedLocation.Cell == Location.Cell)
            {
                var requested = RequestedLocation;
                RequestedLocation = null;
                var wasUpdating = InUpdate;
                try
                {
                    InUpdate = true; // Same cell, no landblock relocation.
                    UpdatePlayerPosition(requested);
                }
                finally { InUpdate = wasUpdating; }
                // Collision can trigger a portal/death; do not relay the old epoch.
                if (Teleporting || !IsAlive || pose.Cell != Location.Cell
                    || pose.Teleport != BitConverter.ToUInt16(Sequences.GetCurrentSequence(SequenceType.ObjectTeleport), 0)) return;
            }
            vrLastTrackedPose = (pose.Flags & 4u) != 0 ? now : DateTime.MinValue;
            // Preserve implicit opt-in for older VR clients. New desktop/VR
            // clients may instead subscribe without sending any tracked poses.
            vrPoseSubscribed = true;
            vrPoseVersion=Math.Max(vrPoseVersion,pose.Version);
            var weapon=GetEquippedMissileWeapon();
            var ammo=weapon?.IsAmmoLauncher==true ? GetEquippedAmmo() : null;
            if (pose.Weapon!=weapon?.Guid.Full) pose.Weapon=0;
            if (pose.Ammo!=ammo?.Guid.Full) pose.Ammo=0;
            var root=Location.ToGlobal(false);
            pose.Root=new System.Numerics.Vector3(-root.X,root.Y,root.Z);
            foreach (var observer in PhysicsObj.ObjMaint.GetKnownPlayersValuesAsPlayer())
            {
                if (observer == this || !observer.vrPoseSubscribed || observer.Session == null || observer.Teleporting
                    || Visibility && !observer.Adminvision) continue;
                var version=Math.Min(pose.Version,observer.vrPoseVersion);
                if (version==2)
                {
                    // The ammo can be invisible to retail observers until their
                    // first reload. VR observers need its model before that shot.
                    if (!observer.vrSeenEquipment.TryGetValue(Guid.Full,out var seen) || now-seen.Seen>TimeSpan.FromSeconds(1)
                        || seen.Weapon!=pose.Weapon || seen.Ammo!=pose.Ammo)
                    {
                        if (pose.Weapon!=0) observer.TrackEquippedObject(this,weapon);
                        if (pose.Ammo!=0)
                        {
                            observer.TrackEquippedObject(this,ammo);
                            observer.Session.Network.EnqueueSend(new ACE.Server.Network.GameMessages.Messages.GameMessageParentEvent(
                                this,ammo,ACE.Entity.Enum.ParentLocation.RightHand,ACE.Entity.Enum.Placement.RightHandCombat));
                        }
                    }
                    observer.vrSeenEquipment[Guid.Full]=(pose.Weapon,pose.Ammo,now);
                    if(observer.vrSeenEquipment.Count>128)
                        foreach(var key in observer.vrSeenEquipment.Where(e=>now-e.Value.Seen>TimeSpan.FromSeconds(2)).Select(e=>e.Key).ToArray())
                            observer.vrSeenEquipment.Remove(key);
                }
                observer.Session.Network.EnqueueSend(new GameEventVRPose(observer.Session, Guid.Full, pose,version));
            }
        }
    }
}
