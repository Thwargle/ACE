using System;
using System.IO;
using System.Numerics;
using ACE.Entity.Enum;
using ACE.Server.Entity;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ACE.Server.Tests
{
    [TestClass]
    public class VRLocomotionTests
    {
        [TestMethod]
        public void EqualSpeedAtEveryAnglePreservesPartialStickAndAuthority()
        {
            foreach (bool running in new[] { false, true })
            foreach (float runRate in new[] { .2f, 1f, 2.6f, 4f })
            foreach (float magnitude in new[] { 0f, .25f, .7f, 1f })
            for (int degrees = 0; degrees < 360; degrees += 15)
            {
                float angle = degrees * MathF.PI / 180;
                var input = new Vector2(MathF.Sin(angle), MathF.Cos(angle)) * magnitude;
                var rates = VRLocomotion.Resolve(input, running, runRate);
                float expected = (running ? 4f * runRate : 3.12f) * magnitude;
                Assert.AreEqual(expected, rates.Velocity.Length(), .00001f);
                float forwardBase = rates.ForwardCommand == MotionCommand.RunForward ? 4f : 3.12f;
                var observed = new Vector2(1.25f * rates.Side, forwardBase * rates.Forward);
                Assert.IsTrue(Vector2.Distance(observed, rates.Velocity) < .00001f,
                    "Legacy interpreted rates must reproduce server velocity without a VR pose packet.");
                if (magnitude > 0) Assert.IsTrue(Vector2.Dot(input, observed) > 0, "Backward/sideways input retains body-relative direction.");
            }
            Assert.AreEqual(8f, VRLocomotion.Resolve(Vector2.One, true, 2).Velocity.Length(), .00001f);
        }

        [TestMethod]
        public void MoveExtensionRequiresNegotiationExactSizeAndBoundedRawInput()
        {
            var raw = new ACE.Server.Network.Structure.RawMotionState
            {
                ForwardCommand = MotionCommand.WalkBackwards, ForwardSpeed = .6f,
                SidestepCommand = MotionCommand.SideStepRight, SidestepSpeed = .8f
            };
            Vector2? Read(bool subscribed, uint marker = VRLocomotion.MoveMarker, bool extra = false)
            {
                using var bytes = new MemoryStream(); using var w = new BinaryWriter(bytes);
                w.Write(marker); if (extra) w.Write(0u); bytes.Position = 0;
                return VRLocomotion.ReadInput(new BinaryReader(bytes), subscribed, raw);
            }
            Assert.IsNull(Read(false)); Assert.IsNull(Read(true, 0)); Assert.IsNull(Read(true, extra: true));
            Assert.AreEqual(new Vector2(.8f, -.6f), Read(true).Value);
            foreach (float invalid in new[] { float.NaN, float.PositiveInfinity, -1f, 1.01f })
            {
                raw.ForwardSpeed = invalid; Assert.IsNull(Read(true));
            }
            raw.ForwardSpeed = 0; raw.SidestepSpeed = 0;
            Assert.AreEqual(Vector2.Zero, Read(true).Value, "Zero/missing magnitudes cannot become full speed.");
            raw.ForwardCommand = MotionCommand.Sleeping; Assert.IsNull(Read(true));
            raw.ForwardCommand = MotionCommand.Ready; raw.SidestepCommand = MotionCommand.Invalid;
            Assert.AreEqual(Vector2.Zero, Read(true).Value);
        }

        [TestMethod]
        public void BurdenedWalkingLeaveGroundUsesRetailRunSpeedCeiling()
        {
            var input = Vector2.Normalize(new Vector2(1, -1));
            var rates = VRLocomotion.Resolve(input, false, .2f);
            var interpreter = new ACE.Server.Physics.Animation.MotionInterp
            {
                MyRunRate = .2f,
                InterpretedState = new ACE.Server.Physics.Animation.InterpretedMotionState
                {
                    ForwardCommand = (uint)rates.ForwardCommand, ForwardSpeed = rates.Forward,
                    SideStepCommand = (uint)MotionCommand.SideStepRight, SideStepSpeed = rates.Side
                }
            };
            var leaveGround = interpreter.get_state_velocity();
            Assert.AreEqual(.8f, leaveGround.Length(), .00001f);
            Assert.AreEqual(input.X * .8f, leaveGround.X, .00001f);
            Assert.AreEqual(input.Y * .8f, leaveGround.Y, .00001f);
        }

        [TestMethod]
        public void StopClearsUniformMovementEvenWithoutPhysics()
        {
            var interpreter = new ACE.Server.Physics.Animation.MotionInterp { UniformVRInput = Vector2.UnitX };
            interpreter.StopCompletely();
            Assert.IsNull(interpreter.UniformVRInput);
        }
    }
}
