using ACE.Entity.Enum;
using ACE.Server.Network.GameEvent.Events;
using ACE.Server.Network.GameMessages.Messages;

namespace ACE.Server.WorldObjects
{
    partial class Creature
    {
		internal bool IsNearbyVRFeedbackObserver(Player observer) => Location != null && observer.Location != null
			&& !observer.Teleporting && Location.SquaredDistanceTo(observer.Location) <= 192f * 192f;

        private void ReportVRHealthBar()
        {
            if (PhysicsObj?.ObjMaint == null) return;
            var fraction = Health.MaxValue > 0 ? (float)Health.Current/Health.MaxValue : 0f;
            foreach (var observer in PhysicsObj.ObjMaint.GetKnownPlayersValuesAsPlayer())
                if (observer != this && observer.Session != null && observer.VRHealthBarsSubscribed && IsNearbyVRFeedbackObserver(observer)
                    && (!Visibility || observer.Adminvision))
                    observer.Session.Network.EnqueueSend(new GameEventUpdateHealth(observer.Session, Guid.Full, fraction));
        }
        private void ReportHealthChange(int change, uint flags)
        {
            if (change == 0) return;
            void Send(Player observer)
            {
                if (observer.Session == null || !observer.VRHealthFeedbackSubscribed || !IsNearbyVRFeedbackObserver(observer)
                    || Visibility && !observer.Adminvision) return;
                observer.Session.Network.EnqueueSend(new GameEventVRHealthChange(observer.Session, Guid.Full, change, flags));
            }
            if (this is Player self) Send(self);
            if (PhysicsObj == null) return;
            foreach (var observer in PhysicsObj.ObjMaint.GetKnownPlayersValuesAsPlayer())
                if (observer != this) Send(observer);

            // Health damage from magic also has an impact, including the lethal
            // hit. Physical attacks already emit their directional splatter.
            if (change < 0 && (flags & 1u) != 0)
                EnqueueBroadcast(new GameMessageScript(Guid, PlayScript.SplatterMidRightFront));
        }
    }
}
