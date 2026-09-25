using System;
using System.IO;
using System.Numerics;

namespace ACE.Server.Entity
{
    // Private ACE VR extension, v1. All floats are AC meters, in world axes relative to player feet.
    public sealed class VRCombatRequest
    {
        public const uint Version = 1;
        public const float MaxMissileMuzzleReach = 2.3f;
        public uint FeedbackFeatures;
        public uint Kind, Sequence, Cell, Teleport, Weapon, Subject, Target;
        public Vector3 Origin, Vector;
        public Vector3? ObservedBody;
        public float Amount, Duration;
        // Optional retail power/accuracy slider. Amount remains the physical
        // bow draw, so speed selection cannot bypass the bow's release checks.
        public float? RequestedPower;

        public string MissileReleaseRejection(ACE.Entity.Enum.CombatMode mode, ACE.Entity.Enum.CombatStyle? style)
        {
            if (mode != ACE.Entity.Enum.CombatMode.Missile) return "Enter missile stance before firing.";
            // Only a bow has a physical draw. Atlatls, crossbows and individual
            // thrown items fire on trigger-down with zero hold duration.
            if (style == ACE.Entity.Enum.CombatStyle.Bow && (Amount < .2f || Duration < .15f))
                return "Pull the arrow back before releasing.";
            return Amount < .2f ? "The shot was too weak. Try again." : null;
        }

        public static bool TryRead(BinaryReader reader, out VRCombatRequest request)
        {
            request = null;
            var remaining = reader.BaseStream.Length - reader.BaseStream.Position;
            if (remaining < 8 || reader.ReadUInt32() != Version) return false;
            var kind = reader.ReadUInt32();
            if (kind == 0 && remaining == 8) { request = new VRCombatRequest(); return true; }
            // A separate opt-in follows capabilities. Keep the original 8-byte hello
            // compatible with servers and clients that do not implement feedback.
            if (kind == 4 && remaining == 12)
            {
                request = new VRCombatRequest { Kind = 4, FeedbackFeatures = reader.ReadUInt32() & 15u };
                return true;
            }
            if (kind == 5 && (remaining == 12 || remaining == 16))
            {
                request = new VRCombatRequest { Kind = 5, Subject = reader.ReadUInt32() };
                if (remaining == 16) request.FeedbackFeatures = reader.ReadUInt32() & 1u;
                return true;
            }
            // Explicit inventory release: independent of the cosmetic pose stream.
            // Subject is zero for a whole item, otherwise the requested stack split.
            if (kind == 6 && remaining == 40)
            {
                var drop = new VRCombatRequest { Kind = kind, Sequence = reader.ReadUInt32(), Cell = reader.ReadUInt32(),
                    Teleport = reader.ReadUInt32(), Weapon = reader.ReadUInt32(), Subject = reader.ReadUInt32(),
                    Origin = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle()) };
                if (drop.Weapon == 0 || drop.Subject > int.MaxValue || !InReach(drop.Origin, 1.75f)
                    || drop.Origin.Z < .1f || drop.Origin.Z > 2.5f) return false;
                request = drop;
                return true;
            }
            // version/kind + 6 uint32 fields + 8 floats = 64 bytes, excluding the action envelope.
            var hasPower = (kind == 2 || kind == 3) && (remaining == 68 || (kind == 2 && remaining == 80));
            if ((remaining != 64 && !(kind == 2 && remaining == 76) && !hasPower) || (kind != 7 && (kind < 1 || kind > 3))) return false;
            var r = new VRCombatRequest { Kind = kind, Sequence = reader.ReadUInt32(), Cell = reader.ReadUInt32(),
                Teleport = reader.ReadUInt32(), Weapon = reader.ReadUInt32(), Subject = reader.ReadUInt32(),
                Target = reader.ReadUInt32() };
            r.Origin = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
            r.Vector = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
            r.Amount = reader.ReadSingle(); r.Duration = reader.ReadSingle();
            if (kind == 2 && (remaining == 76 || remaining == 80))
            {
                var body = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
                if (!IsFinite(body) || body.LengthSquared() > 36f) return false;
                r.ObservedBody = body;
            }
            if (hasPower)
            {
                var power = reader.ReadSingle();
                if (!float.IsFinite(power) || power < 0 || power > 1) return false;
                r.RequestedPower = power;
            }
            if (!IsFinite(r.Origin) || !IsFinite(r.Vector) || !float.IsFinite(r.Amount) || !float.IsFinite(r.Duration)) return false;
            if (r.Amount < 0 || r.Amount > 1 || r.Duration < 0 || r.Duration > 2) return false;
            // Crossbows originate at the barrel tip, beyond the supporting hand.
            // The combat handler applies the smaller bow-hand limit after resolving
            // the server's equipped weapon; a packet cannot claim a weapon style.
            if (!InReach(r.Origin, kind == 3 ? MaxMissileMuzzleReach : 2.8f)) return false;
            if (kind == 2)
            {
                if (!InReach(r.Vector, 2.8f) || r.Duration < .04f || r.Duration > .25f) return false;
                var distance = Vector3.Distance(r.Origin, r.Vector);
                var speed = distance / r.Duration;
                if (distance < .12f || speed < 1.4f || speed > 18f) return false;
            }
            else
            {
                var length = r.Vector.Length();
                if (length < .9f || length > 1.1f) return false;
                r.Vector /= length;
            }
            request = r;
            return true;
        }

        public static bool IsFinite(Vector3 v) => float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);

        public static bool NeighboringOutdoorCells(uint a, uint b)
        {
            var ac = a & 0xffff; var bc = b & 0xffff;
            if (ac < 1 || ac > 64 || bc < 1 || bc > 64) return false;
            var ax = (int)(a >> 24)*8 + (int)(ac-1)/8;
            var ay = (int)((a >> 16)&255)*8 + (int)(ac-1)%8;
            var bx = (int)(b >> 24)*8 + (int)(bc-1)/8;
            var by = (int)((b >> 16)&255)*8 + (int)(bc-1)%8;
            return Math.Abs(ax-bx)<=1 && Math.Abs(ay-by)<=1;
        }
        public static bool InReach(Vector3 v, float radius) => IsFinite(v) && v.Z >= -.25f && v.Z <= 3.25f && v.X * v.X + v.Y * v.Y <= radius * radius;

        public static float SegmentDistanceSquared(Vector3 a, Vector3 b, Vector3 point)
        {
            var ab = b - a;
            var t = ab.LengthSquared() > 1e-8f ? Math.Clamp(Vector3.Dot(point - a, ab) / ab.LengthSquared(), 0f, 1f) : 0f;
            return Vector3.DistanceSquared(a + ab * t, point);
        }

        // Closest points of two finite segments. A diagonal swing can hit the
        // bottom/top of a creature even when its midpoint is far from its body.
        public static float SegmentDistanceSquared(Vector3 a, Vector3 b, Vector3 c, Vector3 d, out float alongFirst)
        {
            var u = b - a; var v = d - c; var w = a - c;
            var aa = u.LengthSquared(); var bb = Vector3.Dot(u, v); var cc = v.LengthSquared();
            var dd = Vector3.Dot(u, w); var ee = Vector3.Dot(v, w);
            float s, t;
            if (aa < 1e-8f) { s = 0; t = cc > 1e-8f ? Math.Clamp(ee / cc, 0, 1) : 0; }
            else if (cc < 1e-8f) { t = 0; s = Math.Clamp(-dd / aa, 0, 1); }
            else
            {
                var det = aa * cc - bb * bb;
                s = det > 1e-8f ? Math.Clamp((bb * ee - cc * dd) / det, 0, 1) : 0;
                t = (bb * s + ee) / cc;
                if (t < 0) { t = 0; s = Math.Clamp(-dd / aa, 0, 1); }
                else if (t > 1) { t = 1; s = Math.Clamp((bb - dd) / aa, 0, 1); }
            }
            alongFirst = s;
            return Vector3.DistanceSquared(a + u * s, c + v * t);
        }

        public Quaternion AimRotation => Quaternion.Normalize(
            Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.Atan2(-Vector.X, Vector.Y)) *
            Quaternion.CreateFromAxisAngle(Vector3.UnitX, MathF.Atan2(Vector.Z, MathF.Sqrt(Vector.X * Vector.X + Vector.Y * Vector.Y))));
    }
}
