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
        public const int EquipmentWireSize = 176;
        public uint Version = 1;
        public uint Weapon, Ammo;
        public Vector3 Root;
        public uint Sequence, Cell, Teleport, Flags;
        public float EyeHeight, Draw;
        public Vector3[] Positions = new Vector3[5]; // head, hands, displayed weapon and ammunition
        public Quaternion[] Rotations = new Quaternion[5];

        public static bool TryRead(BinaryReader reader, out VRPose pose)
        {
            pose = null;
            var size=reader.BaseStream.Length-reader.BaseStream.Position;
            if (size!=WireSize && size!=EquipmentWireSize) return false;
            var version=reader.ReadUInt32();
            if (version!=1 && version!=2 || size!=(version==2 ? EquipmentWireSize : WireSize)) return false;
            var p = new VRPose { Version=version, Sequence = reader.ReadUInt32(), Cell = reader.ReadUInt32(), Teleport = reader.ReadUInt32(),
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
            if (version==2)
            {
                p.Weapon=reader.ReadUInt32(); p.Ammo=reader.ReadUInt32();
                for (var i=3;i<5;++i)
                {
                    p.Positions[i]=new Vector3(reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle());
                    var q=new Quaternion(reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle());
                    if (!VRCombatRequest.InReach(p.Positions[i],3f) || !float.IsFinite(q.LengthSquared())
                        || q.LengthSquared()<.8f || q.LengthSquared()>1.2f) return false;
                    p.Rotations[i]=Quaternion.Normalize(q);
                }
            }
            pose = p; return true;
        }

        public void Write(BinaryWriter writer, uint? requestedVersion = null)
        {
            var version=requestedVersion ?? Version;
            writer.Write(version); writer.Write(Sequence); writer.Write(Cell); writer.Write(Teleport); writer.Write(Flags);
            writer.Write(EyeHeight); writer.Write(Draw);
            for (var i = 0; i < 3; ++i)
            {
                var p = Positions[i]; var q = Rotations[i];
                foreach (var value in new[] { p.X, p.Y, p.Z, q.X, q.Y, q.Z, q.W }) writer.Write(value);
            }
            if (version==2)
            {
                writer.Write(Weapon); writer.Write(Ammo);
                for(var i=3;i<5;++i)
                {
                    var p=Positions[i]; var q=Rotations[i];
                    foreach(var value in new[]{p.X,p.Y,p.Z,q.X,q.Y,q.Z,q.W}) writer.Write(value);
                }
            }
        }
    }
}
