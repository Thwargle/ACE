using System.Numerics;
using ACE.Entity;
using ACE.Entity.Enum;
using ACE.Server.Physics;

namespace ACE.Server.WorldObjects
{
    partial class Player
    {
        private bool TryReleaseVRItem(WorldObject item, Vector3 hand)
        {
            // Start inside the player's known cell, then sweep the item's own
            // collision shape to the hand. A controller beyond a wall cannot
            // place inventory through it. Do this before broadcasting the spawn.
            item.Location = new Position(Location);
            item.Placement = ACE.Entity.Enum.Placement.Resting;
            if (item.PhysicsObj == null) item.InitPhysicsObj();
            var ethereal = item.Ethereal;
            item.Ethereal = true; // do not snag on the releasing player's cylinder
            try
            {
                if (!item.AddPhysicsObj()) return false;
                item.PhysicsObj.TransientState &= ~(TransientStateFlags.Contact | TransientStateFlags.OnWalkable);
                var destination = new Physics.Common.Position(Location);
                destination.Frame.Origin += hand;
                var transition = item.PhysicsObj.transition(item.PhysicsObj.Position, destination, false);
                if (transition?.SpherePath.CurCell != null)
                    item.PhysicsObj.SetPositionInternal(transition);
                item.SyncLocation();
            }
            finally { item.Ethereal = ethereal; }

            // A gentle outward release from the actual (wall-constrained) hand
            // position, in world axes. Do not add running speed or trust a client
            // throw velocity; this is a bounded drop, not a projectile attack.
            var outward = PhysicsObj.Position.GetOffset(item.PhysicsObj.Position);
            outward.Z = 0;
            if (outward.LengthSquared() < .0025f)
                outward = Vector3.Transform(Vector3.UnitY, Location.Rotation);
            outward.Z = 0;
            outward = Vector3.Normalize(outward);
            item.BeginVRDropFall(outward * .8f + Vector3.UnitZ * .55f);
            if (CurrentLandblock.AddWorldObject(item)) return true;
            item.PhysicsObj?.leave_world();
            item.IsVRFallingDrop = false;
            return false;
        }
    }
}
