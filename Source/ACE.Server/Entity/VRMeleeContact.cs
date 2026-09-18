using System;
using System.Numerics;
using ACE.Server.Physics;

namespace ACE.Server.Entity
{
    // Combat envelopes, not selection proxies or pixel-perfect triangles. The
    // server supplies every mesh bound and animation transform; clients only
    // submit the tracked weapon path. No new physics bodies or per-frame work.
    public static class VRMeleeContact
    {
        public static bool SegmentBox(Vector3 a, Vector3 b, Vector3 min, Vector3 max, out float contact)
        {
            contact = 0; var exit = 1f; var d = b-a;
            for (var axis = 0; axis < 3; axis++)
            {
                float p = a[axis], v = d[axis];
                if (Math.Abs(v) < 1e-7f) { if (p < min[axis] || p > max[axis]) return false; continue; }
                var first = (min[axis]-p)/v; var last = (max[axis]-p)/v;
                if (first > last) (first,last) = (last,first);
                contact = Math.Max(contact,first); exit = Math.Min(exit,last);
                if (contact > exit) return false;
            }
            return true;
        }

        public static bool Intersects(PhysicsObj body, Vector3 a, Vector3 b, float height, out float contact)
        {
            var radius = body.GetRadius() + .18f;
            var hit = VRCombatRequest.SegmentDistanceSquared(a,b,Vector3.Zero,Vector3.UnitZ*height,out contact) <= radius*radius;
            var parts = body.PartArray;
            var frame = parts?.Sequence?.GetCurrAnimFrame();
            if (parts?.Parts == null || frame == null) return hit;
            var inverse = Quaternion.Conjugate(body.Position.Frame.Orientation);
            a = Vector3.Transform(a,inverse); b = Vector3.Transform(b,inverse);
            // Extra 12cm beyond the client's 18cm combat envelope tolerates
            // animation interpolation/network phase without widening selection.
            var padding = new Vector3(.30f);
            for (var i = 0; i < parts.Parts.Count && i < frame.Frames.Count; i++)
            {
                var part = parts.Parts[i]; var bounds = part?.GfxObj?.GfxBoundBox;
                if (bounds == null || part.NoDraw) continue;
                var f = frame.Frames[i]; var inv = Quaternion.Conjugate(f.Orientation);
                var origin = f.Origin * parts.Scale;
                var scale = Vector3.Abs(part.GfxObjScale);
                if (SegmentBox(Vector3.Transform(a-origin,inv), Vector3.Transform(b-origin,inv),
                    bounds.Min*scale-padding,bounds.Max*scale+padding,out var along))
                { contact = hit ? Math.Min(contact,along) : along; hit = true; }
            }
            return hit;
        }
    }
}
