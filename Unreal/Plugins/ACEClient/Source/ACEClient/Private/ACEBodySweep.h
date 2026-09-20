#pragma once

#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"

namespace ACEBodySweep
{
    inline FVector RecoverCorner(UWorld& World, const FVector& From,
        const FCollisionShape& Capsule, const FCollisionQueryParams& Params)
    {
        TArray<FHitResult> Contacts;
        World.SweepMultiByChannel(Contacts,From,From+FVector(0,0,.001f),FQuat::Identity,ECC_Pawn,Capsule,Params);
        Contacts.RemoveAll([](const FHitResult& H) { return !H.bStartPenetrating || H.Normal.Z<-.15f || H.Normal.Z>=.6641741f; });
        if (Contacts.Num()<2) return From;
        FVector Push=FVector::ZeroVector;
        // Simultaneous wall constraints must be solved together. Resolving one
        // wall while rejecting the other initial overlap locks an inside corner.
        for (int32 Pass=0; Pass<8; ++Pass)
            for (const auto& Contact:Contacts)
            {
                // Door arches and stair corners need the complete separation
                // normal. Flattening it leaves the crown on a beveled triangle.
                const FVector N=Contact.Normal.GetSafeNormal();
                Push+=N*FMath::Max(0.,Contact.PenetrationDepth+.2-FVector::DotProduct(Push,N));
            }
        if (Push.Size()>Capsule.GetCapsuleRadius()*2.f) return From;
        for (const auto& Contact:Contacts)
            if (FVector::DotProduct(Push,Contact.Normal)<Contact.PenetrationDepth+.1f) return From;
        TArray<FHitResult> Escape;
        World.SweepMultiByChannel(Escape,From+Push,From,FQuat::Identity,ECC_Pawn,Capsule,Params);
        for (const auto& H:Escape)
        {
            if (!H.bBlockingHit || H.ImpactNormal.Z>=.6641741f) continue;
            const bool Existing=Contacts.ContainsByPredicate([&](const FHitResult& C) {
                // Initial overlap normals are capsule separation vectors, not
                // necessarily the face normal returned by the reverse sweep.
                return C.Component==H.Component && FVector::DotProduct(H.ImpactNormal,Push)>0.f;
            });
            if (!Existing || H.bStartPenetrating || FVector::DotProduct(H.ImpactNormal,Push)<=0) return From;
        }
        return From+Push;
    }

    inline FVector RecoverAlong(UWorld& World, const FVector& From, const FVector& Push,
        const FHitResult& Original, const FCollisionShape& Capsule, const FCollisionQueryParams& Params)
    {
        const FVector Delta = Push.GetClampedToMaxSize(Capsule.GetCapsuleRadius()*2.f);
        const FVector Candidate = From + Delta;
        TArray<FHitResult> Hits;
        World.SweepMultiByChannel(Hits,Candidate,From,FQuat::Identity,ECC_Pawn,Capsule,Params);
        for (const auto& Hit : Hits)
        {
            if (!Hit.bBlockingHit || Hit.ImpactNormal.Z >= .6641741f) continue;
            // Contact skin on a neighboring wall must not prevent recovery
            // parallel to that wall (for example, down a stair beside it).
            // Only waive a time-zero, shallow contact that is not worsening;
            // later hits and real overlaps still validate the whole escape.
            if (Hit.Time<=KINDA_SMALL_NUMBER && Hit.PenetrationDepth<=.05f
                && FVector::DotProduct(Hit.ImpactNormal,Delta)>=-.001f) continue;
            if (Hit.Component != Original.Component || FVector::DotProduct(Hit.ImpactNormal,Delta) <= 0.f)
                return From;
            // A deep initial contact may need several moves. Accept progress
            // out of that same solid, but never a new or worsening overlap.
            if (Hit.bStartPenetrating && Hit.PenetrationDepth > .05f
                && Hit.PenetrationDepth >= Original.PenetrationDepth-.01f)
                return From;
        }
        return Candidate;
    }

    inline FVector Recover(UWorld& World, const FVector& From, const FVector& Push,
        const FHitResult& Original, const FCollisionShape& Capsule, const FCollisionQueryParams& Params)
    {
        const FVector Direct=RecoverAlong(World,From,Push,Original,Capsule,Params);
        if (!Direct.Equals(From,.01f)) return Direct;
        // Beveled door/stair corners can require a small vertical clearance.
        // Flattening the contact normal leaves the capsule touching another
        // triangle, so horizontal recovery fails even while backing away.
        // Validate the actual separation vector against every solid as well.
        if (FMath::IsNearlyZero(Push.Z) && Original.Normal.Z > 0.f && Original.Normal.Z < .6641741f)
        {
            const FVector AlongContact=RecoverAlong(World,From,Original.Normal.GetSafeNormal()
                * FMath::Max(Push.Size(),double(Original.PenetrationDepth+.2f)),Original,Capsule,Params);
            if (!AlongContact.Equals(From,.01f)) return AlongContact;
        }
        const FVector Corner=RecoverCorner(World,From,Capsule,Params);
        if (!Corner.Equals(From,.01f)) return Corner;
        // Under a stairwell ceiling, moving out along a ramp's upward normal
        // can be blocked by the ceiling. Resolve the same penetration along
        // the ramp's horizontal downhill direction instead. Sweep that escape
        // too: it must not introduce another wall/ceiling intersection.
        const FVector Normal=Original.Normal.GetSafeNormal();
        const float HorizontalSq=Normal.SizeSquared2D();
        if (Normal.Z>.0871557f && Push.Z>0.f && HorizontalSq>.01f)
        {
            const float Clearance=FMath::Max(Original.PenetrationDepth+.5f,FVector::DotProduct(Push,Normal));
            return RecoverAlong(World,From,FVector(Normal.X,Normal.Y,0)*(Clearance/HorizontalSq),Original,Capsule,Params);
        }
        return From;
    }

    // CTransition::step_down tests the lower sphere, not just the center ray.
    // Preserve its contact while crossing the edge of a narrow seat/tread;
    // otherwise the ray sees the floor below and pushes the body into the seat.
    // A grounded body's contact must satisfy retail FloorZ. LandingZ is only
    // for airborne landings: using it here lets feet roll off a precipice.
    inline bool FindFootSupport(UWorld& World, const FVector& Feet, float Radius,
        float MaxDown, const FCollisionQueryParams& Params, float& SupportZ, float MaxUp = 0.f)
    {
        constexpr float Clearance=.5f;
        // A wall touching the body's side must not hide the floor from a
        // downward query. Remove only Chaos's sub-millimeter contact skin.
        const float SupportRadius=FMath::Max(1.f,Radius-.1f);
        FHitResult Hit;
        const FVector Top=Feet+FVector(0,0,SupportRadius+MaxUp+Clearance);
        if (!World.SweepSingleByChannel(Hit,Top,Top-FVector(0,0,MaxDown+MaxUp+Clearance),
            FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(SupportRadius),Params)
            || Hit.bStartPenetrating || Hit.Normal.Z<.6641741f) return false;
        const float Z=Hit.Location.Z-SupportRadius;
        if (Z>Feet.Z+MaxUp+Clearance || Z<Feet.Z-MaxDown) return false;
        SupportZ=Z;
        return true;
    }
    // Floor support is solved separately from lateral blocking. Chaos returns
    // a resting floor at time zero, hiding the wall later in a single sweep.
    // Remove only that bottom contact from the query, retaining the full radius
    // and crown. Never ignore the floor component: it can also contain walls.
    inline bool Sweep(UWorld& World, FHitResult& Hit, const FVector& From, const FVector& To,
        const FCollisionShape& Capsule, const FCollisionQueryParams& Params, bool bAllowFloorClearance=true)
    {
        float FloorClearance=0.f, SideClearance=0.f;
        int32 FloorRetries=0;
        const bool bMayClearFloor=bAllowFloorClearance && (FVector::DistSquared2D(From,To)>=KINDA_SMALL_NUMBER || To.Z>From.Z);
        for (;;)
        {
            const FVector Lift(0,0,FloorClearance*.5f);
            const float Radius=Capsule.GetCapsuleRadius()-SideClearance;
            const float Half=Capsule.GetCapsuleHalfHeight()-SideClearance-FloorClearance*.5f;
            const bool bHit=World.SweepSingleByChannel(Hit,From+Lift,To+Lift,FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeCapsule(Radius,Half),Params);
            // The caller resolves the original full-height capsule center.
            if (bHit) Hit.Location-=Lift;
            if (!bHit || Hit.Time>KINDA_SMALL_NUMBER) return bHit;

            // A floor and a wall can both touch at time zero. Either may win
            // the first query. Preserve BOTH clearances on subsequent retries:
            // restoring the radius after clearing the floor made a second wall
            // contact trap players even when they moved away from it.
            if (SideClearance==0.f && Hit.PenetrationDepth<=.05f && Hit.Normal.Z<.6641741f
                && FVector::DotProduct(To-From,Hit.Normal)>=-.001f && Radius>1.f)
            {
                SideClearance=.1f;
                continue;
            }
            if (bMayClearFloor && Hit.Normal.Z>=.6641741f && FloorRetries<3)
            {
                const float Next=FloorClearance+FMath::Max(0.f,Hit.PenetrationDepth)/Hit.Normal.Z+1.f;
                if (Capsule.GetCapsuleHalfHeight()-SideClearance-Next*.5f>=Radius)
                {
                    FloorClearance=Next;
                    ++FloorRetries;
                    continue;
                }
            }
            return true;
        }
    }

    // A contact at the start of a grounded move must preserve the unblocked
    // tangential displacement. Recovery alone spends the frame pushing out and
    // drops that input, producing sticky movement along triangulated interiors.
    inline FVector SlideFromPenetration(UWorld& World, const FVector& From, const FVector& To,
        const FHitResult& Contact, const FCollisionShape& Capsule, const FCollisionQueryParams& Params)
    {
        const FVector Normal=Contact.Normal.GetSafeNormal2D();
        if (Normal.IsNearlyZero()) return From;
        const FVector Start=Recover(World,From,Normal*(Contact.PenetrationDepth+2.f),Contact,Capsule,Params);
        FVector Remaining(To.X-From.X,To.Y-From.Y,0);
        const float Into=FVector::DotProduct(Remaining,Normal);
        if (Into<0.f) Remaining-=Normal*Into;
        if (Remaining.IsNearlyZero(.01f)) return Start;
        FHitResult Hit;
        if (!Sweep(World,Hit,Start,Start+Remaining,Capsule,Params)) return Start+Remaining;
        return Hit.bStartPenetrating ? Start : Hit.Location;
    }

    struct FAirborneMove
    {
        FVector Position;
        FVector ContactNormal = FVector::ZeroVector;
        bool bLanded = false;
    };

    // A jump collides with all faces, including a roof touched only by the lower
    // sphere. Center rays and 2D wall slides cannot validate this path.
    inline FAirborneMove MoveAirborne(UWorld& World, const FVector& From, const FVector& To,
        const FCollisionShape& Capsule, const FCollisionQueryParams& Params, bool bFalling, float LandingZ=.0871557f)
    {
        FAirborneMove Result{From};
        FVector Remaining=To-From;
        for (int32 Pass=0; Pass<4 && !Remaining.IsNearlyZero(.01f); ++Pass)
        {
            const FVector Start=Result.Position;
            FHitResult Hit;
            // A tangent wall/ceiling skin must not mask the floor while falling.
            // Retain every floor contact here, including diagonal descents.
            bool bHit=Sweep(World,Hit,Start,Start+Remaining,Capsule,Params,false);
            // A just-launched body may still touch its support. Only clear that
            // initial floor while leaving it, never while falling back into it.
            if (bHit && Hit.Time<=KINDA_SMALL_NUMBER && Hit.ImpactNormal.Z>=.6641741f
                && Hit.PenetrationDepth<.5f && FVector::DotProduct(Remaining,Hit.ImpactNormal)>0.f)
                bHit=Sweep(World,Hit,Start,Start+Remaining,Capsule,Params);
            if (!bHit) { Result.Position=Start+Remaining; break; }
            const FVector Normal=Hit.Normal.GetSafeNormal();
            Result.ContactNormal=Hit.ImpactNormal.GetSafeNormal();
            if (Hit.bStartPenetrating)
            {
                // A center-ray ground snap leaves the lower sphere slightly
                // inside a sloped roof. Holding that overlap forever also holds
                // the airborne flag, so keyboard movement can never resume.
                // Use the landing threshold here too: steep pitched roofs are
                // landable even when they cannot be walked up. Recover validates
                // the push against neighboring solids and caps its distance.
                // The same recovery is needed for an initial side/soffit
                // overlap at a ledge. Otherwise every falling frame repeats
                // the same time-zero hit and gravity never moves the body.
                if (!Normal.IsNearlyZero())
                {
                    const FVector Recovered = Recover(World, Start,
                        Normal * (Hit.PenetrationDepth + .5f), Hit, Capsule, Params);
                    if (!Recovered.Equals(Start, .01f))
                    {
                        Result.Position = Recovered;
                        // Sweep the remaining descent again. A capped push can
                        // still overlap; only a clear contact completes landing.
                        continue;
                    }
                }
                break;
            }
            Result.Position=Hit.Location+Normal*.1f;
            if (bFalling && Hit.ImpactNormal.Z>=LandingZ && Remaining.Z<0.f)
            { Result.bLanded=true; break; }
            Remaining*=1.f-Hit.Time;
            const float Into=FVector::DotProduct(Remaining,Normal);
            if (Into<0.f) Remaining-=Normal*Into;
            // Every adjusted segment is swept again, preserving roof/soffit Z.
        }
        if (bFalling && !Result.bLanded)
        {
            // A simultaneous side overlap can win every shape query, even
            // after the feet have reached a flat floor. Do not leave the
            // player airborne (and their input locked) at that support. This
            // is only a contact-skin check, not a snap from above a ledge.
            const FVector Feet=Result.Position-FVector(0,0,Capsule.GetCapsuleHalfHeight());
            FHitResult Floor;
            if (World.LineTraceSingleByChannel(Floor,Feet+FVector(0,0,.1f),Feet-FVector(0,0,.2f),ECC_Pawn,Params)
                && !Floor.bStartPenetrating && Floor.ImpactNormal.Z>=.6641741f)
            {
                Result.Position.Z=Floor.ImpactPoint.Z+Capsule.GetCapsuleHalfHeight();
                Result.ContactNormal=Floor.ImpactNormal;
                Result.bLanded=true;
            }
        }
        return Result;
    }

    // Sampling a tread beside the capsule does not prove the body can get there.
    // Validate the adjusted lateral reach and descent before committing a step.
    inline bool CanTraverseStep(UWorld& World, const FVector& Lifted, const FVector& Reach,
        const FVector& Landed, const FCollisionShape& Capsule, const FCollisionQueryParams& Params,
        float WalkableNormalZ, FVector* OutContact = nullptr)
    {
        FHitResult Hit;
        if (Sweep(World,Hit,Lifted,Reach,Capsule,Params)) return false;
        if (Sweep(World,Hit,Reach,Landed,Capsule,Params))
        {
            if (Hit.bStartPenetrating || Hit.Normal.Z<WalkableNormalZ
                || Hit.Location.Z>Landed.Z+Capsule.GetCapsuleRadius()+1.f) return false;
            // At a tread's edge the sphere touches before its center reaches
            // the ray's floor height. Keep that contact, never embed the foot.
            if (OutContact) *OutContact=Hit.Location;
        }
        return true;
    }
}
