using System;
using System.Linq;
using ACE.Entity.Enum;
using ACE.Server.Entity;
using ACE.Server.Network.GameEvent.Events;

namespace ACE.Server.WorldObjects
{
    partial class Player
    {
        private void BeginVRCast(VRCombatRequest request, Spell spell, WorldObject item)
        {
            if (!vrCastingSubscribed) return;
            vrCast = request;
            float Length(MotionCommand motion) => Physics.Animation.MotionTable.GetAnimationLength(
                MotionTableId, MotionStance.Magic, motion, CastSpeed);
            var time = Length(MagicState.CastGesture);
            if (item == null && !spell.Flags.HasFlag(SpellFlags.FastCast))
                time += spell.Formula.WindupGestures.Where(g => g != MotionCommand.Invalid && (uint)g != 0x80000000u).Sum(Length);
            time = Math.Clamp(time, .01f, 60f);
            Session.Network.EnqueueSend(new GameEventVRCasting(Session, request.Sequence, request.Teleport, request.Subject, 1, time, time));
        }

        private void ReleaseVRCast()
        {
            if (vrCast == null || !vrCastingSubscribed) return;
            Session.Network.EnqueueSend(new GameEventVRCasting(Session, vrCast.Sequence, vrCast.Teleport, vrCast.Subject, 2, 0, 0));
        }

        private void EndVRCast()
        {
            if (vrCast == null) return;
            if (vrCastingSubscribed)
                Session.Network.EnqueueSend(new GameEventVRCasting(Session, vrCast.Sequence, vrCast.Teleport, vrCast.Subject, 0, 0, 0));
            vrCast = null;
        }
    }
}
