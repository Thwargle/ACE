using System;
using System.Numerics;
using ACE.Entity.Enum;
using ACE.Server.Physics;
using ACE.Server.Physics.Common;

namespace ACE.Server.WorldObjects
{
    partial class WorldObject
    {
        // Ephemeral: ordinary placed loot and scenery retain their existing path.
        public bool IsVRFallingDrop { get; internal set; }
        private double vrDropStarted;

        public void BeginVRDropFall(Vector3 velocity)
        {
            GravityStatus = true;
            IsVRFallingDrop = true;
            vrDropStarted = PhysicsTimer.CurrentTime;
            PhysicsObj.TransientState &= ~(TransientStateFlags.Contact | TransientStateFlags.OnWalkable
                | TransientStateFlags.StationaryStop | TransientStateFlags.StationaryFall | TransientStateFlags.StationaryStuck);
            PhysicsObj.calc_acceleration();
            PhysicsObj.set_velocity(velocity, false);
        }

        private void UpdateVRDropFall()
        {
            if (!IsVRFallingDrop) return;
            if (PhysicsObj.Parent != null || (ContainerId ?? 0) != 0 || (WielderId ?? 0) != 0)
            { IsVRFallingDrop = false; return; }
            // Stop the short simulation after contact (or a bounded safety timeout).
            // Send a final zero-velocity F748 so every client stops predicting fall.
            var settled = !PhysicsObj.is_active() || PhysicsObj.IsGrounded
                || PhysicsTimer.CurrentTime - vrDropStarted > 10;
            if (settled)
            {
                PhysicsObj.Velocity = PhysicsObj.CachedVelocity = Vector3.Zero;
                PhysicsObj.set_active(false);
                IsVRFallingDrop = false;
            }
            if (settled || (DateTime.UtcNow - LastUpdatePosition).TotalSeconds >= .1)
                SendUpdatePosition();
        }
    }
}
