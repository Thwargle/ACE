using System;
using System.IO;
using System.Numerics;

namespace ACE.Server.Entity
{
    // Cosmetic v1 pose. Positions use Unreal world axes in meters relative to
    // authoritative player feet. This stream never moves a collider or damages.
    public sealed class VRPose
    {
        public const int WireSize = 112;
        public uint Sequence, Cell, Teleport, Flags;
        public float EyeHeight, Draw;
        public Vector3[] Positions = new Vector3[3]; // head, physical left, physical right
        public Quaternion[] Rotations = new Quaternion[3];

        public static bool TryRead(BinaryReader reader, out VRPose pose)
        {
            pose = null;
            if (reader.BaseStream.Length - reader.BaseStream.Position != WireSize || reader.ReadUInt32() != 1) return false;
            var p = new VRPose { Sequence = reader.ReadUInt32(), Cell = reader.ReadUInt32(), Teleport = reader.ReadUInt32(),
                Flags = reader.ReadUInt32(), EyeHeight = reader.ReadSingle(), Draw = reader.ReadSingle() };
            if ((p.Flags & ~31u) != 0 || !float.IsFinite(p.EyeHeight) || p.EyeHeight < 1 || p.EyeHeight > 2.1f
                || !float.IsFinite(p.Draw) || p.Draw < 0 || p.Draw > 1) return false;
            for (var i = 0; i < 3; ++i)
            {
                p.Positions[i] = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
                var q = new Quaternion(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
                if (!VRCombatRequest.InReach(p.Positions[i], i == 0 ? 1.2f : 2.2f)
                    || !float.IsFinite(q.LengthSquared()) || q.LengthSquared() < .8f || q.LengthSquared() > 1.2f) return false;
                p.Rotations[i] = Quaternion.Normalize(q);
            }
            pose = p; return true;
        }

        public void Write(BinaryWriter writer)
        {
            writer.Write(1u); writer.Write(Sequence); writer.Write(Cell); writer.Write(Teleport); writer.Write(Flags);
            writer.Write(EyeHeight); writer.Write(Draw);
            for (var i = 0; i < 3; ++i)
            {
                var p = Positions[i]; var q = Rotations[i];
                foreach (var value in new[] { p.X, p.Y, p.Z, q.X, q.Y, q.Z, q.W }) writer.Write(value);
            }
        }
    }
}
