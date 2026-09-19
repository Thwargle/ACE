using System;
using System.IO;
using System.Numerics;
using ACE.Server.Entity;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ACE.Server.Tests
{
    [TestClass]
    public class VRCombatTests
    {
        [TestMethod]
        public void EquipmentPoseIsStrictlySizedAndCanBeDowngradedForLegacyObservers()
        {
            var p=new VRPose {Version=2,Sequence=1,Cell=0x7D64000C,Flags=7,EyeHeight=1.7f,Weapon=100,Ammo=200};
            for(int i=0;i<5;++i) {p.Positions[i]=new Vector3(.2f,0,1.2f);p.Rotations[i]=Quaternion.Identity;}
            byte[] Bytes(uint version) { using var s=new MemoryStream();using var w=new BinaryWriter(s);p.Write(w,version);return s.ToArray(); }
            var data=Bytes(2);Assert.AreEqual(VRPose.EquipmentWireSize,data.Length);
            Assert.IsTrue(VRPose.TryRead(new BinaryReader(new MemoryStream(data)),out var parsed));
            Assert.AreEqual(100u,parsed.Weapon);Assert.AreEqual(p.Positions[4],parsed.Positions[4]);
            var legacy=Bytes(1);Assert.AreEqual(VRPose.WireSize,legacy.Length);
            Assert.IsTrue(VRPose.TryRead(new BinaryReader(new MemoryStream(legacy)),out parsed));Assert.AreEqual(0u,parsed.Ammo);
            Array.Resize(ref data,data.Length-1);Assert.IsFalse(VRPose.TryRead(new BinaryReader(new MemoryStream(data)),out _));
            p.Positions[4]=new Vector3(100,0,1);Assert.IsFalse(VRPose.TryRead(new BinaryReader(new MemoryStream(Bytes(2))),out _));
            p.Positions[4]=new Vector3(0,0,1);p.Rotations[3]=new Quaternion(float.NaN,0,0,1);
            Assert.IsFalse(VRPose.TryRead(new BinaryReader(new MemoryStream(Bytes(2))),out _));
        }
        [TestMethod]
        public void OutdoorCombatCellsAcceptOnlyImmediatePhysicalNeighbors()
        {
            Assert.IsTrue(VRCombatRequest.NeighboringOutdoorCells(0x7D64000C,0x7D64000D));
            Assert.IsTrue(VRCombatRequest.NeighboringOutdoorCells(0x7D640040,0x7E640008));
            Assert.IsFalse(VRCombatRequest.NeighboringOutdoorCells(0x7D64000C,0x7D64003C));
            Assert.IsTrue(VRCombatRequest.NeighboringOutdoorCells(0x7D640040,0x7E650001));
            Assert.IsFalse(VRCombatRequest.NeighboringOutdoorCells(0x7D640040,0x7E660001));
            Assert.IsFalse(VRCombatRequest.NeighboringOutdoorCells(0x7D64000C,0x7D640100));
            Assert.IsFalse(VRCombatRequest.NeighboringOutdoorCells(0,0));
        }
        [TestMethod]
        public void MembershipSnapshotUsesTheNegotiatedFlagAndTeleportEpoch()
        {
            // Only the event envelope is exercised; constructing a real network
            // session would bind listeners and require a running shard.
            var session = (ACE.Server.Network.Session)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(typeof(ACE.Server.Network.Session));
            var legacy = new ACE.Server.Network.GameEvent.Events.GameEventVRCapabilities(session);
            using var oldWire = new BinaryReader(new MemoryStream(legacy.Data.ToArray()));
            oldWire.BaseStream.Position = 16;
            Assert.AreEqual(1u, oldWire.ReadUInt32()); Assert.AreEqual(65527u, oldWire.ReadUInt32());
            Assert.AreEqual(0u, oldWire.ReadUInt32()); Assert.AreEqual(20.0f, oldWire.ReadSingle());
            Assert.AreEqual(oldWire.BaseStream.Length, oldWire.BaseStream.Position);
            var snapshot = new ACE.Server.Network.GameEvent.Events.GameEventVRCapabilities(session, 12, new uint[] { 100, 300, 400 });
            using var wire = new BinaryReader(new MemoryStream(snapshot.Data.ToArray()));
            wire.BaseStream.Position = 16;
            Assert.AreEqual(1u, wire.ReadUInt32()); Assert.AreEqual(65535u, wire.ReadUInt32());
            Assert.AreEqual(12u, wire.ReadUInt32()); Assert.AreEqual(3u, wire.ReadUInt32());
            foreach (var guid in new uint[] { 100, 300, 400 }) Assert.AreEqual(guid, wire.ReadUInt32());
            Assert.AreEqual(0u, wire.ReadUInt32()); Assert.AreEqual(20.0f, wire.ReadSingle());
            Assert.AreEqual(wire.BaseStream.Length, wire.BaseStream.Position);
        }

        private static byte[] Wire(uint kind = 1, Vector3? origin = null, Vector3? vector = null, float amount = 1, float duration = .25f)
        {
            using var stream = new MemoryStream(); using var w = new BinaryWriter(stream);
            w.Write(1u); w.Write(kind); w.Write(7u); w.Write(0x12340001u); w.Write(2u);
            w.Write(0x80000001u); w.Write(42u); w.Write(0x80000002u);
            var a = origin ?? new Vector3(.2f, .3f, 1.5f); var b = vector ?? Vector3.UnitY;
            foreach (var value in new[] { a.X, a.Y, a.Z, b.X, b.Y, b.Z, amount, duration }) w.Write(value);
            return stream.ToArray();
        }
        private static bool Read(byte[] bytes, out VRCombatRequest r)
        {
            using var reader = new BinaryReader(new MemoryStream(bytes));
            return VRCombatRequest.TryRead(reader, out r);
        }

        [TestMethod]
        public void AimUpdatesAndObservedMeleeBodiesRetainStrictPacketValidation()
        {
            Assert.IsTrue(Read(Wire(7),out var aim));Assert.AreEqual(7u,aim.Kind);
            Assert.IsFalse(Read(Wire(7,vector:Vector3.Zero),out _));
            Assert.IsFalse(Read(Wire(7,origin:new Vector3(4,0,1)),out _));
            byte[] Swing(Vector3 body)
            {
                using var s=new MemoryStream();using var w=new BinaryWriter(s);
                w.Write(Wire(2,Vector3.UnitZ,new Vector3(.5f,0,1),duration:.1f));
                w.Write(body.X);w.Write(body.Y);w.Write(body.Z);return s.ToArray();
            }
            var bytes=Swing(new Vector3(0,1,0));Assert.AreEqual(76,bytes.Length);
            Assert.IsTrue(Read(bytes,out var hit));Assert.AreEqual(new Vector3(0,1,0),hit.ObservedBody);
            for(int n=65;n<76;++n)Assert.IsFalse(Read(bytes[..n],out _));
            Assert.IsFalse(Read(Swing(new Vector3(float.NaN,0,0)),out _));
            Assert.IsFalse(Read(Swing(new Vector3(7,0,0)),out _));
            Assert.IsTrue(Read(bytes[..64],out hit));Assert.IsNull(hit.ObservedBody,"Legacy swings remain compatible.");
        }

        [TestMethod]
        public void HealthFeedbackSubscriptionHasAnExactOptionalWireLayout()
        {
            var data = new byte[] { 1,0,0,0,4,0,0,0,1,0,0,0 };
            Assert.IsTrue(Read(data, out var request));
            Assert.AreEqual(4u,request.Kind); Assert.AreEqual(1u,request.FeedbackFeatures);
            data[8] = 255; Assert.IsTrue(Read(data, out request)); Assert.AreEqual(15u, request.FeedbackFeatures);
            for(int n=0;n<data.Length;++n) Assert.IsFalse(Read(data[..n],out _));
            Array.Resize(ref data,13); Assert.IsFalse(Read(data,out _));
        }

        [TestMethod]
        public void SpellProfileUsesExplicitRequestAndFiniteWireFields()
        {
            var data = new byte[] { 1,0,0,0,5,0,0,0,42,0,0,0 };
            Assert.IsTrue(Read(data,out var request));Assert.AreEqual(5u,request.Kind);Assert.AreEqual(42u,request.Subject);
            for(int n=0;n<data.Length;++n) Assert.IsFalse(Read(data[..n],out _));
            Array.Resize(ref data,13);Assert.IsFalse(Read(data,out _));
            var session=(ACE.Server.Network.Session)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(typeof(ACE.Server.Network.Session));
            var profile=new ACE.Server.Network.GameEvent.Events.GameEventVRSpellProfile(session,42,30,true);
            using var wire=new BinaryReader(new MemoryStream(profile.Data.ToArray()));wire.BaseStream.Position=16;
            Assert.AreEqual(42u,wire.ReadUInt32());Assert.AreEqual(30f,wire.ReadSingle());Assert.AreEqual(1u,wire.ReadUInt32());
            Assert.AreEqual(wire.BaseStream.Length,wire.BaseStream.Position);
            Array.Resize(ref data,16); data[12]=1;
            Assert.IsTrue(Read(data,out request)); Assert.AreEqual(1u,request.FeedbackFeatures);
            var extended=new ACE.Server.Network.GameEvent.Events.GameEventVRSpellProfile(session,42,30,true,.12f);
            using var extra=new BinaryReader(new MemoryStream(extended.Data.ToArray()));extra.BaseStream.Position=28;
            Assert.AreEqual(.12f,extra.ReadSingle()); Assert.AreEqual(extra.BaseStream.Length,extra.BaseStream.Position);
        }

        [TestMethod]
        public void InventoryReleaseWireValidatesOriginStackAndExactLength()
        {
            byte[] Drop(Vector3 origin, uint amount = 0)
            {
                using var stream = new MemoryStream(); using var w = new BinaryWriter(stream);
                foreach (var value in new[] { 1u, 6u, 9u, 0x12340001u, 2u, 0x80000001u, amount }) w.Write(value);
                w.Write(origin.X); w.Write(origin.Y); w.Write(origin.Z); return stream.ToArray();
            }
            var bytes = Drop(new Vector3(.3f,.4f,1.2f), 5);
            Assert.IsTrue(Read(bytes, out var r)); Assert.AreEqual(6u,r.Kind); Assert.AreEqual(5u,r.Subject);
            Assert.AreEqual(9u,r.Sequence); Assert.AreEqual(2u,r.Teleport); Assert.AreEqual(0x80000001u,r.Weapon);
            for (var n=0; n<40; ++n) Assert.IsFalse(Read(bytes[..n],out _));
            Array.Resize(ref bytes,41); Assert.IsFalse(Read(bytes,out _));
            foreach(var origin in new[] { new Vector3(float.NaN,0,1), new Vector3(5,0,1), new Vector3(0,0,-1), new Vector3(0,0,5) })
                Assert.IsFalse(Read(Drop(origin),out _));
            Assert.IsFalse(Read(Drop(Vector3.UnitZ,uint.MaxValue),out _));
            Assert.IsTrue(Read(Drop(Vector3.UnitZ),out _),"Zero means a whole-item release.");
        }

        [TestMethod]
        public void ExactWireLayoutAndNegotiation()
        {
            var bytes = Wire(); Assert.AreEqual(64, bytes.Length); Assert.IsTrue(Read(bytes, out var r));
            Assert.AreEqual(0x80000001u, r.Weapon); Assert.AreEqual(0x80000002u, r.Target);
            Assert.AreEqual(42u, r.Subject); Assert.AreEqual(0x12340001u, r.Cell); Assert.AreEqual(2u, r.Teleport);
            Assert.IsTrue(Read(new byte[] { 1, 0, 0, 0, 0, 0, 0, 0 }, out r)); Assert.AreEqual(0u, r.Kind);
            for (var length = 0; length < 64; ++length) Assert.IsFalse(Read(bytes[..length], out _), $"Truncated at {length}");
            Array.Resize(ref bytes, 65); Assert.IsFalse(Read(bytes, out _));
        }

        [TestMethod]
        public void InvalidVersionsKindsFloatsAndUnreachableHandsAreRejected()
        {
            var bytes = Wire(); bytes[0] = 2; Assert.IsFalse(Read(bytes, out _));
            Assert.IsFalse(Read(Wire(kind: 8), out _));
            foreach (var invalid in new[] { float.NaN, float.PositiveInfinity, float.NegativeInfinity })
            {
                Assert.IsFalse(Read(Wire(origin: new Vector3(invalid, 0, 1)), out _));
                Assert.IsFalse(Read(Wire(vector: new Vector3(0, invalid, 1)), out _));
                Assert.IsFalse(Read(Wire(amount: invalid), out _));
                Assert.IsFalse(Read(Wire(duration: invalid), out _));
            }
            Assert.IsFalse(Read(Wire(origin: new Vector3(5, 0, 1)), out _));
            Assert.IsFalse(Read(Wire(origin: new Vector3(0, 0, -1)), out _));
            Assert.IsFalse(Read(Wire(vector: Vector3.Zero), out _));
            Assert.IsFalse(Read(Wire(vector: Vector3.UnitY * 100), out _));
            Assert.IsFalse(Read(Wire(amount: 2), out _));
        }

        [TestMethod]
        public void SwingRejectsStationaryHandsTrackingJumpsAndInvalidDurations()
        {
            var a = new Vector3(0, 0, 1.5f); var b = new Vector3(.3f, 0, 1.5f);
            Assert.IsTrue(Read(Wire(2, a, b, duration: .1f), out _));
            Assert.IsFalse(Read(Wire(2, a, a, duration: .1f), out _));
            Assert.IsFalse(Read(Wire(2, a, b, duration: 0), out _));
            Assert.IsFalse(Read(Wire(2, a, b, duration: .01f), out _));
            Assert.IsFalse(Read(Wire(2, a, b, duration: .5f), out _));
            Assert.IsFalse(Read(Wire(2, a, new Vector3(3, 0, 1.5f), duration: .1f), out _));
            Assert.IsFalse(Read(Wire(2, a, a + new Vector3(.02f, 0, 0), duration: .01f), out _));
            Assert.IsFalse(Read(Wire(2, a, a + new Vector3(.11f, 0, 0), duration: .04f), out _));
        }

        [TestMethod]
        public void SpellTipCanExtendBeyondTheHandWithoutExtendingMissileReach()
        {
            var tip = new Vector3(2.4f, 0, 1.5f);
            Assert.IsTrue(Read(Wire(1, tip), out _));
            Assert.IsFalse(Read(Wire(3, tip), out _));
            Assert.IsFalse(Read(Wire(1, new Vector3(3f, 0, 1.5f)), out _));
        }

        [TestMethod]
        public void AimRotationPreservesHandDirectionIncludingVerticalShots()
        {
            foreach (var direction in new[] { Vector3.UnitY, Vector3.UnitX, -Vector3.UnitY, Vector3.UnitZ, -Vector3.UnitZ, Vector3.Normalize(new Vector3(-1, 2, 3)) })
            {
                Assert.IsTrue(Read(Wire(vector: direction), out var r));
                Assert.IsTrue(Vector3.Distance(Vector3.Transform(Vector3.UnitY, r.AimRotation), direction) < .00001f, direction.ToString());
            }
        }

        [TestMethod]
        public void CrossbowTriggerPacketAllowsZeroDrawTimeAndBarrelTipReach()
        {
            Assert.IsTrue(Read(Wire(3, new Vector3(2.2f, 0, 1.5f), duration: 0), out var request));
            Assert.AreEqual(0f, request.Duration);
            Assert.IsFalse(Read(Wire(3, new Vector3(2.4f, 0, 1.5f), duration: 0), out _));
            Assert.IsFalse(Read(Wire(3, duration: -.1f), out _));
        }

        [TestMethod]
        public void SweepChecksTheSegmentAndNotAnInfiniteLine()
        {
            Assert.AreEqual(0f, VRCombatRequest.SegmentDistanceSquared(Vector3.Zero, Vector3.UnitX, new Vector3(.5f, 0, 0)), .00001f);
            Assert.AreEqual(1f, VRCombatRequest.SegmentDistanceSquared(Vector3.Zero, Vector3.UnitX, new Vector3(2, 0, 0)), .00001f);
            Assert.AreEqual(1f, VRCombatRequest.SegmentDistanceSquared(Vector3.Zero, Vector3.Zero, Vector3.UnitY), .00001f);
        }

        [TestMethod]
        public void DiagonalAndVerticalSwingsCanHitAnywhereAlongTheCreatureBody()
        {
            var bottom = Vector3.Zero; var top = new Vector3(0, 0, 2);
            Assert.AreEqual(0f, VRCombatRequest.SegmentDistanceSquared(new Vector3(-.3f, 0, 1.8f), new Vector3(.1f, 0, .2f), bottom, top, out var at), .00001f);
            Assert.AreEqual(.75f, at, .00001f);
            Assert.AreEqual(0f, VRCombatRequest.SegmentDistanceSquared(new Vector3(0, 0, 3), new Vector3(0, 0, 1), bottom, top, out _), .00001f);
            Assert.AreEqual(1f, VRCombatRequest.SegmentDistanceSquared(new Vector3(-1, 0, 3), new Vector3(1, 0, 3), bottom, top, out _), .00001f);
            Assert.AreEqual(1f, VRCombatRequest.SegmentDistanceSquared(Vector3.UnitX, Vector3.UnitX, bottom, bottom, out _), .00001f);
        }

        [TestMethod]
        public void ReusedPhysicsTransitionClearsThePreviousProjectileTarget()
        {
            var info = new ACE.Server.Physics.Animation.ObjectInfo { TargetID = 42 };
            var projectile = new ACE.Server.Physics.PhysicsObj();
            info.Init(projectile, ACE.Server.Physics.Animation.ObjectInfoState.Default);
            Assert.AreEqual(0u, info.TargetID);
        }
    }
}
