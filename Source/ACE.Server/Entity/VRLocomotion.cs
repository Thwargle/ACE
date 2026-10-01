using System;
using System.IO;
using System.Numerics;
using ACE.Entity.Enum;

namespace ACE.Server.Entity
{
    // Optional locomotion contract. The client sends bounded stick input, never a speed.
    // Ordinary interpreted motion rates remain the wire format sent to all observers.
    public static class VRLocomotion
    {
        public const uint Capability = 262144u;
        public const uint Subscription = 64u;
        public const uint MoveMarker = 0x314D5256u; // VRM1, after the retail MoveToState body

        public static bool TryGetInput(MotionCommand forward, float forwardAmount,
            MotionCommand side, float sideAmount, out Vector2 input)
        {
            input = Vector2.Zero; // X=right, Y=forward, in body axes
            if (forward != MotionCommand.Invalid && forward != MotionCommand.Ready
                && forward != MotionCommand.WalkForward && forward != MotionCommand.WalkBackwards) return false;
            if (side != MotionCommand.Invalid && side != MotionCommand.SideStepLeft
                && side != MotionCommand.SideStepRight) return false;
            if (forward == MotionCommand.WalkForward || forward == MotionCommand.WalkBackwards)
            {
                if (!float.IsFinite(forwardAmount) || forwardAmount < 0 || forwardAmount > 1) return false;
                input.Y = forwardAmount * (forward == MotionCommand.WalkBackwards ? -1 : 1);
            }
            if (side != MotionCommand.Invalid)
            {
                if (!float.IsFinite(sideAmount) || sideAmount < 0 || sideAmount > 1) return false;
                input.X = sideAmount * (side == MotionCommand.SideStepLeft ? -1 : 1);
            }
            if (input.LengthSquared() > 1) input = Vector2.Normalize(input);
            return true;
        }

        public static Vector2? ReadInput(BinaryReader reader, bool subscribed, Network.Structure.RawMotionState raw)
        {
            if (!subscribed || reader.BaseStream.Length - reader.BaseStream.Position != 4
                || reader.ReadUInt32() != MoveMarker) return null;
            return TryGetInput(raw.ForwardCommand, raw.ForwardSpeed, raw.SidestepCommand, raw.SidestepSpeed,
                out var input) ? input : null;
        }

        public readonly struct Rates
        {
            public readonly MotionCommand ForwardCommand;
            public readonly float Forward, Side;
            public readonly Vector2 Velocity;
            public Rates(Vector2 input, bool running, float runRate)
            {
                // The run skill/encumbrance rate is supplied by the server's creature.
                float speed = running ? 4f * runRate : 3.12f;
                Velocity = input * speed;
                ForwardCommand = input.Y > 0 && running ? MotionCommand.RunForward : MotionCommand.WalkForward;
                Forward = Velocity.Y / (ForwardCommand == MotionCommand.RunForward ? 4f : 3.12f);
                Side = Velocity.X / 1.25f;
            }
        }

        public static Rates Resolve(Vector2 input, bool running, float runRate)
        {
            if (!float.IsFinite(input.X) || !float.IsFinite(input.Y)) input = Vector2.Zero;
            if (input.LengthSquared() > 1) input = Vector2.Normalize(input);
            if (!float.IsFinite(runRate) || runRate < 0) runRate = 0;
            return new Rates(input, running, runRate);
        }
    }
}
