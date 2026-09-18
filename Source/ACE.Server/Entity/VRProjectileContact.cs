using System;
using System.Numerics;
using ACE.Entity.Enum;
using ACE.Server.Physics;
using ACE.Server.Physics.Common;
using ACE.Server.WorldObjects;

namespace ACE.Server.Entity
{
    // Continuous, server-owned animated body envelopes. Unlike melee, projectiles
    // need the first entry fraction, not the closest point on a movement cylinder.
    public static class VRProjectileContact
    {
        public const float BodyPadding = .10f;

        public static bool Intersects(PhysicsObj body, Vector3 a, Vector3 b, float radius, out float contact)
        {
            contact = 1f; bool hit = false, hasGeometry = false;
            var parts = body.PartArray;
            var frame = parts?.Sequence?.GetCurrAnimFrame();
            var inverse = Quaternion.Conjugate(body.Position.Frame.Orientation);
            a = Vector3.Transform(a,inverse); b = Vector3.Transform(b,inverse);
            var padding = new Vector3(Math.Max(0,radius) + BodyPadding);
            if (parts?.Parts != null && frame != null)
                for (int i=0; i<parts.Parts.Count && i<frame.Frames.Count; ++i)
                {
                    var part = parts.Parts[i]; var bounds = part?.GfxObj?.GfxBoundBox;
                    if (bounds == null || part.NoDraw) continue;
                    hasGeometry = true;
                    var f = frame.Frames[i]; var inv = Quaternion.Conjugate(f.Orientation);
                    var origin = f.Origin * parts.Scale;
                    var scale = Vector3.Abs(part.GfxObjScale);
                    if (VRMeleeContact.SegmentBox(Vector3.Transform(a-origin,inv),Vector3.Transform(b-origin,inv),
                        bounds.Min*scale-padding,bounds.Max*scale+padding,out var t))
                    { hit = true; contact = Math.Min(contact,t); }
                }
            // Only models without animated geometry need a core fallback.
            if (!hasGeometry)
            {
                var r = body.GetRadius();
                hit = VRMeleeContact.SegmentBox(a,b,new Vector3(-r,-r,0)-padding,
                    new Vector3(r,r,body.GetHeight())+padding,out contact);
            }
            return hit;
        }

        public static PhysicsObj FirstContact(PhysicsObj projectile, Position end, out float fraction)
        {
            float nearest = 1f; PhysicsObj first = null;
            var source = projectile.WeenieObj?.WorldObject?.ProjectileSource?.PhysicsObj;
            void Check(Physics.Common.Landblock block)
            {
                if (block?.ServerObjects == null) return;
                foreach (var body in block.ServerObjects)
                {
                    if (body == source || body == projectile || body.Parent != null || body.CurCell == null
                        || body.WeenieObj?.WorldObject is not Creature creature || creature.IsDead
                        || (body.State & (PhysicsState.Ethereal | PhysicsState.IgnoreCollisions | PhysicsState.NoDraw)) != 0) continue;
                    // Position.GetOffset accounts for adjacent outdoor landblocks.
                    var a = body.Position.GetOffset(projectile.Position);
                    var b = body.Position.GetOffset(end);
                    if (Intersects(body,a,b,projectile.GetRadius(),out var t)
                        && (first == null || t<nearest || t==nearest && body.ID<first.ID))
                    { nearest=t; first=body; }
                }
            }
            Check(projectile.CurLandblock);
            var adjacent = projectile.CurLandblock?.get_adjacents();
            if (adjacent != null) foreach (var block in adjacent) Check(block);
            fraction=nearest; return first;
        }
    }
}
