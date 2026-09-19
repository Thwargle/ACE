using System;
using System.IO;
using System.Numerics;
using ACE.DatLoader;
using ACE.Server.Physics.Animation;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ACE.Server.Tests.Physics;

[TestClass]
public class RunSpeedTests
{
    // Retail MovementSystem::GetRunRate, acclient.exe 0x006B1820:
    // fcom 800; fnstsw; test ah,44h; jp ordinary_curve => equality ONLY.
    // Values are unscaled forward metres/sec (human DAT cycle = 4 m/s).
    [TestMethod]
    [DataRow(593, 0f, 12.2257250946)]
    [DataRow(799, 0f, 12.7977977978)]
    [DataRow(800, 0f, 18.0)]
    [DataRow(801, 0f, 12.8021978022)]
    [DataRow(1000, 0f, 13.1666666667)]
    [DataRow(593, 1.5f, 8.1128625473)]
    [DataRow(593, 2f, 4.0)]
    [DataRow(0, 0f, 4.0)]
    public void RetailRunCurve(int skill, float burden, double metresPerSecond)
    {
        Assert.AreEqual(metresPerSecond, MovementSystem.GetRunRate(burden, skill, 1f) * 4, 0.00001);
    }

    [TestMethod]
    [DataRow(593, 1f, 12.2257250946)]
    [DataRow(593, 1.1f, 13.4482976041)]
    [DataRow(800, 1f, 18.0)]
    [DataRow(801, 1f, 12.8021978022)]
    public void RetailDatRunDisplacement(int skill, float scale, double metresPerSecond)
    {
        var directory = Environment.GetEnvironmentVariable("ACE_TEST_DAT");
        if (string.IsNullOrEmpty(directory))
            Assert.Inconclusive("Set ACE_TEST_DAT to measure the retail DAT run animation in server physics.");
        var dat = new DatDatabase(Path.Combine(directory, "client_portal.dat"));
        var animation = new Animation(dat.ReadFromDat<ACE.DatLoader.FileTypes.Animation>(0x03000002));
        var sequence = new Sequence();
        var node = sequence.AnimList.AddLast(new AnimSequenceNode
        {
            Anim = animation, LowFrame = 0, HighFrame = (int)animation.NumFrames - 1,
            Framerate = 30f * (float)MovementSystem.GetRunRate(0f, skill, 1f)
        });
        sequence.CurrAnim = sequence.FirstCyclic = node;
        var distance = Vector3.Zero;
        const int frames = 1200;
        for (int i = 0; i < frames; ++i)
        {
            var offset = new AFrame();
            sequence.Update(1f / 60f, ref offset);
            // PhysicsObj.UpdatePositionInternal applies Scale to supported
            // animation displacement, not to the run-rate skill calculation.
            distance += offset.Origin * scale;
        }
        // Sequence consumes whole DAT keyframes; allow one keyframe over 20s.
        Assert.AreEqual(metresPerSecond, distance.Length() / 20.0, 0.025);
    }
}
