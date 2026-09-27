#pragma once

#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"

namespace ACEBodySweep
{
    inline bool IsUpperBodyContact(const FHitResult& Hit)
    {
        // These are explicit world shape queries, so the query-side bone name
        // is otherwise unused. Preserve Item/FaceIndex for instanced geometry.
        return Hit.MyBoneName==FName(TEXT("ACEUpperBodySphere"));
    }
    // Retail SPHEREPATH sweeps the lower and upper body spheres, not the
    // cylinder connecting them. Filling that waist blocked otherwise clear
    // passages beside creatures and caught the body on stair/pillar corners.
    // Keep results in body-center space so all contact/step solvers agree.
    inline bool SweepBody(UWorld& World, FHitResult& Hit, const FVector& From, const FVector& To,
        const FCollisionShape& Body, const FCollisionQueryParams& Params)
    {
        const float Radius=Body.GetCapsuleRadius();
        const double Offset=FMath::Max(0.f,Body.GetCapsuleHalfHeight()-Radius);
        bool Found=false;Hit=FHitResult();
        for(int32 I=0;I<(Offset>.001?2:1);++I)
        {
            const FVector Shift(0,0,I==0?-Offset:Offset);FHitResult Part;
            if(!World.SweepSingleByChannel(Part,From+Shift,To+Shift,FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeSphere(Radius),Params))continue;
            Part.Location-=Shift;Part.TraceStart=From;Part.TraceEnd=To;
            Part.MyBoneName=I==1?FName(TEXT("ACEUpperBodySphere")):NAME_None;
            if(!Found || (Part.bStartPenetrating && !Hit.bStartPenetrating)
                || (Part.bStartPenetrating==Hit.bStartPenetrating &&
                    (Part.Time<Hit.Time || (Part.Time==Hit.Time && Part.PenetrationDepth>Hit.PenetrationDepth))))
                Hit=Part;
            Found=true;
        }
        return Found;
    }
    inline void SweepBodyContacts(UWorld& World,TArray<FHitResult>& Hits,const FVector& From,const FVector& To,
        const FCollisionShape& Body,const FCollisionQueryParams& Params)
    {
        Hits.Reset();const float Radius=Body.GetCapsuleRadius();
        const double Offset=FMath::Max(0.f,Body.GetCapsuleHalfHeight()-Radius);
        for(int32 I=0;I<(Offset>.001?2:1);++I)
        {
            const FVector Shift(0,0,I==0?-Offset:Offset);TArray<FHitResult> Parts;
            World.SweepMultiByChannel(Parts,From+Shift,To+Shift,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(Radius),Params);
            for(auto& Part:Parts){Part.Location-=Shift;Part.TraceStart=From;Part.TraceEnd=To;
                Part.MyBoneName=I==1?FName(TEXT("ACEUpperBodySphere")):NAME_None;Hits.Add(Part);}
        }
    }
    inline bool OverlapsBody(UWorld& World,const FVector& Center,const FCollisionShape& Body,const FCollisionQueryParams& Params)
    {
        const float R=Body.GetCapsuleRadius(),Offset=FMath::Max(0.f,Body.GetCapsuleHalfHeight()-R);
        for(float Z:{-Offset,Offset})
            if(World.OverlapBlockingTestByChannel(Center+FVector(0,0,Z),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(R),Params))return true;
        return false;
    }
    inline bool IsCreatureBody(const FHitResult& Hit)
    {
        return Hit.Component.IsValid() && Hit.Component->ComponentTags.Contains(TEXT("ACECreatureBody"));
    }
    // Multi channel traces stop at the first blocking component. Discarding a
    // creature hit afterward does not reveal the floor behind it. Retry with
    // that body excluded, retaining every piece of architecture in the query.
    inline bool TraceGround(UWorld& World, TArray<FHitResult>& Hits, const FVector& From,
        const FVector& To, const FCollisionQueryParams& Params)
    {
        FCollisionQueryParams GroundParams=Params;
        TSet<const UPrimitiveComponent*> Ignored;
        for (;;)
        {
            Hits.Reset();
            if (!World.LineTraceMultiByChannel(Hits,From,To,ECC_Pawn,GroundParams)) return false;
            const FHitResult* Body=Hits.FindByPredicate([](const FHitResult& H){return H.bBlockingHit && IsCreatureBody(H);});
            if (!Body) return true;
            const auto* Component=Body->GetComponent();
            if (!Component || Ignored.Contains(Component)) return false;
            Ignored.Add(Component);GroundParams.AddIgnoredComponent(Component);
        }
    }
    inline FVector RecoverCorner(UWorld& World, const FVector& From,
        const FCollisionShape& Capsule, const FCollisionQueryParams& Params)
    {
        TArray<FHitResult> Contacts;
        // Include touching neighbours before solving the escape. At outdoor
        // coordinates Chaos can round a sub-centimetre contact out of the
        // overlap query even though it blocks the proposed recovery move.
        constexpr float Skin=1.f;
        const auto Expanded=FCollisionShape::MakeCapsule(Capsule.GetCapsuleRadius()+Skin,
            Capsule.GetCapsuleHalfHeight()+Skin);
        SweepBodyContacts(World,Contacts,From,From+FVector(0,0,.001f),Expanded,Params);
        Contacts.RemoveAll([](const FHitResult& H) { return !H.bBlockingHit
            || (!H.bStartPenetrating && H.Time>KINDA_SMALL_NUMBER); });
        if (Contacts.IsEmpty()) return From;
        bool bCreatureContact=false;
        for (auto& C:Contacts) if (IsCreatureBody(C))
        {
            bCreatureContact=true;
            const float Horizontal=C.Normal.Size2D();
            if (Horizontal<KINDA_SMALL_NUMBER) return From;
            C.Normal/=Horizontal;C.Normal.Z=0;
            C.PenetrationDepth/=Horizontal;
        }
        // The expanded probe also touches the supporting floor. Its artificial
        // skin must not lift a grounded body while resolving lateral contacts.
        Contacts.RemoveAll([](const FHitResult& C){return !IsCreatureBody(C) && !IsUpperBodyContact(C)
            && C.Normal.Z>=.6641741f && C.PenetrationDepth<=Skin+0.05f;});
        // The minimum separation in 3D lies on one, two or three contact planes.
        // Repeated projections converge too slowly between nearly opposing
        // curved bodies (Eiichi/lifestone); eight passes could never free them.
        FVector Push=FVector::ZeroVector;
        bool bClear=false;
        // Chaos returns only one separating plane per mesh, even when several
        // of its triangles touch the capsule. Probe each proposed escape and
        // add newly exposed planes in the ORIGINAL position's coordinates.
        // Solving just the first plane deadlocks compound posts/stairwells.
        for(int32 Probe=0;Probe<8;++Probe)
        {
            double Best=FMath::Square(Capsule.GetCapsuleRadius()*2.);
            bool Found=false;
            auto Try=[&](const FVector& Candidate) {
                if(Candidate.ContainsNaN() || Candidate.SizeSquared()>Best) return;
                // A crowd is lateral obstruction, never a stair or elevator.
                if(bCreatureContact && FMath::Abs(Candidate.Z)>.01) return;
                for(const auto& C:Contacts)
                    if(FVector::DotProduct(Candidate,C.Normal)<C.PenetrationDepth+.19) return;
                Push=Candidate;Best=Candidate.SizeSquared();Found=true;
            };
            // Bound the rare overlap solver; all contacts still validate candidates.
            const int32 Count=FMath::Min(Contacts.Num(),12);
            for(int32 I=0;I<Count;++I)
            {
                const FVector A=Contacts[I].Normal.GetSafeNormal();const double DA=Contacts[I].PenetrationDepth+.2;
                Try(A*DA);
                for(int32 J=I+1;J<Count;++J)
                {
                    const FVector B=Contacts[J].Normal.GetSafeNormal();const double DB=Contacts[J].PenetrationDepth+.2;
                    const double Dot=FVector::DotProduct(A,B),Det=1.-Dot*Dot;
                    if(Det>1.e-8) Try(A*((DA-Dot*DB)/Det)+B*((DB-Dot*DA)/Det));
                    for(int32 K=J+1;K<Count;++K)
                    {
                        const FVector C=Contacts[K].Normal.GetSafeNormal();const double DC=Contacts[K].PenetrationDepth+.2;
                        const FVector BC=FVector::CrossProduct(B,C);
                        const double Triple=FVector::DotProduct(A,BC);
                        if(FMath::Abs(Triple)>1.e-8)
                            Try((BC*DA+FVector::CrossProduct(C,A)*DB+FVector::CrossProduct(A,B)*DC)/Triple);
                    }
                }
            }
            if(!Found) return From;
            TArray<FHitResult> More;
            SweepBodyContacts(World,More,From+Push,From+Push+FVector(0,0,.001f),Expanded,Params);
            bool bAdded=false;
            for(auto C:More)
            {
                if(!C.bBlockingHit || (!C.bStartPenetrating && C.Time>KINDA_SMALL_NUMBER))continue;
                if(IsCreatureBody(C))
                {
                    bCreatureContact=true;
                    const float Horizontal=C.Normal.Size2D();
                    if(Horizontal<KINDA_SMALL_NUMBER)return From;
                    C.Normal/=Horizontal;C.Normal.Z=0;C.PenetrationDepth/=Horizontal;
                }
                if(!IsCreatureBody(C) && !IsUpperBodyContact(C) && C.Normal.Z>=.6641741f
                    && C.PenetrationDepth<=Skin+.05f)continue;
                C.PenetrationDepth+=FVector::DotProduct(Push,C.Normal);
                auto* Existing=Contacts.FindByPredicate([&](const FHitResult& H){return H.Component==C.Component
                    && FVector::DotProduct(H.Normal,C.Normal)>.9999f;});
                if(Existing)Existing->PenetrationDepth=FMath::Max(Existing->PenetrationDepth,C.PenetrationDepth);
                else Contacts.Add(C);
                bAdded=true;
            }
            if(!bAdded){bClear=true;break;}
            if(Contacts.Num()>12)return From;
        }
        if(!bClear)return From;
        TArray<FHitResult> Escape;
        SweepBodyContacts(World,Escape,From+Push,From,Capsule,Params);
        for (const auto& H:Escape)
        {
            if (!H.bBlockingHit || (!IsUpperBodyContact(H) && H.ImpactNormal.Z>=.6641741f)) continue;
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
        SweepBodyContacts(World,Hits,Candidate,From,Capsule,Params);
        for (const auto& Hit : Hits)
        {
            if (!Hit.bBlockingHit || (!IsUpperBodyContact(Hit) && Hit.ImpactNormal.Z >= .6641741f)) continue;
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

    // A body already inside a thin wall can overlap both opposing faces. Chaos
    // then supplies mutually incompatible per-triangle MTD planes. Retail's
    // placement insertion searches for a clear sphere position instead of
    // keeping an impossible contact set forever. Search along existing contact
    // directions, within one body diameter, and reverse-sweep each clear candidate
    // to reject any newly crossed solid. This is never used for ordinary travel.
    inline FVector RecoverEmbedded(UWorld& World, const FVector& From,
        const FCollisionShape& Body, const FCollisionQueryParams& Params)
    {
        TArray<FHitResult> Initial;
        SweepBodyContacts(World,Initial,From,From+FVector(0,0,.001),Body,Params);
        Initial.RemoveAll([](const FHitResult& H){return !H.bStartPenetrating || H.PenetrationDepth<=.05f;});
        if(Initial.IsEmpty() || Initial.ContainsByPredicate([](const FHitResult& H){return IsCreatureBody(H);}))return From;
        TArray<FVector,TInlineAllocator<8>> Directions;
        for(const auto& H:Initial)
        {
            const FVector N=H.Normal.GetSafeNormal();
            if(!N.IsNearlyZero())Directions.AddUnique(N);
            const FVector Horizontal=N.GetSafeNormal2D();
            if(!Horizontal.IsNearlyZero())Directions.AddUnique(Horizontal);
        }
        // Deep overlaps on both spheres can put their centers inside a wall;
        // its nearest triangle normal may point toward the opposite wall face.
        // Consider the other side only for that embedded state, never for the
        // shallow contact/precision skin encountered by an ordinary sweep.
        if(Initial.ContainsByPredicate([&](const FHitResult& H){return !IsUpperBodyContact(H) && H.PenetrationDepth>Body.GetCapsuleRadius()*.25f;})
            && Initial.ContainsByPredicate([&](const FHitResult& H){return IsUpperBodyContact(H) && H.PenetrationDepth>Body.GetCapsuleRadius()*.25f;}))
        {
            const int32 Count=Directions.Num();
            for(int32 I=0;I<Count;++I)Directions.AddUnique(-Directions[I]);
        }
        const int32 DirectionCount=Directions.Num();
        for(int32 I=0;I<DirectionCount;++I)for(int32 J=I+1;J<DirectionCount;++J)
        {
            const FVector Diagonal=(Directions[I]+Directions[J]).GetSafeNormal();
            if(!Diagonal.IsNearlyZero())Directions.AddUnique(Diagonal);
        }
        const double Radius=Body.GetCapsuleRadius();
        double Best=Radius*2.+1.;FVector Result=From;
        auto Clear=[&](const FVector& At)
        {
            TArray<FHitResult> Contacts;
            SweepBodyContacts(World,Contacts,At,At+FVector(0,0,.001),Body,Params);
            return !Contacts.ContainsByPredicate([](const FHitResult& H){return H.bBlockingHit && H.PenetrationDepth>.05f;});
        };
        for(const FVector& N:Directions)
        {
            double Previous=0.;
            for(double Distance=Radius*.25;Distance<=Radius*2. && Distance<Best+Radius*.25;Distance+=Radius*.25)
            {
                if(!Clear(From+N*Distance)){Previous=Distance;continue;}
                double Low=Previous,High=Distance;
                for(int32 I=0;I<8;++I){const double Mid=(Low+High)*.5;if(Clear(From+N*Mid))High=Mid;else Low=Mid;}
                const FVector Delta=N*(High+.2),Candidate=From+Delta;
                TArray<FHitResult> Reverse;SweepBodyContacts(World,Reverse,Candidate,From,Body,Params);
                bool Valid=Clear(Candidate);
                for(const auto& H:Reverse)
                {
                    if(!H.bBlockingHit)continue;
                    if(H.Time<=KINDA_SMALL_NUMBER && H.PenetrationDepth<=.05f
                        && FVector::DotProduct(H.Normal,Delta)>=-.001)continue;
                    if(H.bStartPenetrating || FVector::DotProduct(H.ImpactNormal,Delta)<=0.
                        || !Initial.ContainsByPredicate([&](const FHitResult& C){return C.Component==H.Component;}))Valid=false;
                }
                if(Valid && Delta.Size()<Best){Best=Delta.Size();Result=Candidate;}
                break;
            }
        }
        return Result;
    }

    inline FVector Recover(UWorld& World, const FVector& From, const FVector& Push,
        const FHitResult& Original, const FCollisionShape& Capsule, const FCollisionQueryParams& Params)
    {
        const bool bCreature=IsCreatureBody(Original);
        const FVector Separation=bCreature ? Original.Normal.GetSafeNormal2D()*Push.Size() : Push;
        const FVector Direct=RecoverAlong(World,From,Separation,Original,Capsule,Params);
        if (!Direct.Equals(From,.01f)) return Direct;
        // Beveled door/stair corners can require a small vertical clearance.
        // Flattening the contact normal leaves the capsule touching another
        // triangle, so horizontal recovery fails even while backing away.
        // Validate the actual separation vector against every solid as well.
        if (!bCreature && FMath::IsNearlyZero(Push.Z) && Original.Normal.Z > 0.f && Original.Normal.Z < .6641741f)
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
        if (!bCreature && Normal.Z>.0871557f && Push.Z>0.f && HorizontalSq>.01f)
        {
            const float Clearance=FMath::Max(Original.PenetrationDepth+.5f,FVector::DotProduct(Push,Normal));
            return RecoverAlong(World,From,FVector(Normal.X,Normal.Y,0)*(Clearance/HorizontalSq),Original,Capsule,Params);
        }
        return RecoverEmbedded(World,From,Capsule,Params);
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
        FCollisionQueryParams SupportParams=Params;
        TSet<const UPrimitiveComponent*> Ignored;
        for(;;)
        {
            if (!World.SweepSingleByChannel(Hit,Top,Top-FVector(0,0,MaxDown+MaxUp+Clearance),
                FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(SupportRadius),SupportParams))return false;
            if(!IsCreatureBody(Hit))break;
            const auto* Component=Hit.GetComponent();
            if (!Component || Ignored.Contains(Component)) return false;
            Ignored.Add(Component);SupportParams.AddIgnoredComponent(Component);
        }
        // Contact at a convex tread belongs to the lower sphere. Chaos can
        // report the riser's vertical triangle as ImpactNormal while the sphere
        // rests on its upper edge. Conversely a horizontal face can still be
        // too far behind the foot to support it. Sloped authored floor planes
        // retain support at their edges while the sphere still contacts above
        // the landing threshold; flat treads require a walkable sphere contact.
        const bool bWalkableContact=Hit.Normal.Z>=.6641741f
            || (Hit.ImpactNormal.Z>=.6641741f && Hit.ImpactNormal.Z<.99f && Hit.Normal.Z>.0871557f);
        if (IsCreatureBody(Hit) || Hit.bStartPenetrating || !bWalkableContact) return false;
        const float Z=Hit.Location.Z-SupportRadius;
        if (Z>Feet.Z+MaxUp+Clearance || Z<Feet.Z-MaxDown) return false;
        // Retain edge contact when descending and on sloped ramp planes, but
        // do not roll upward around a flat tread's vertical riser. That ascent
        // needs the full step/headroom checks instead of partial sphere lifts.
        if (Z>Feet.Z+Clearance && Hit.ImpactNormal.Z>.99f && Hit.Normal.Z<.6641741f) return false;
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
            const bool bHit=SweepBody(World,Hit,From+Lift,To+Lift,FCollisionShape::MakeCapsule(Radius,Half),Params);
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
            if (bMayClearFloor && !IsUpperBodyContact(Hit) && !IsCreatureBody(Hit) && Hit.Normal.Z>=.6641741f && FloorRetries<3)
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

    // Consume the remaining displacement across successive triangles. A single
    // second sweep stops at each small bend even though a tangent remains free.
    // Capsule separation normals round convex edges; face normals do not.
    inline FVector SlideGrounded(UWorld& World, const FVector& From, const FVector& To,
        const FHitResult& Contact, const FCollisionShape& Capsule, const FCollisionQueryParams& Params)
    {
        FVector Position=From,Remaining=To-From;
        FHitResult Hit=Contact;
        TArray<FVector,TInlineAllocator<6>> Planes;
        for(int32 Pass=0;Pass<6 && !Remaining.IsNearlyZero(.01f);++Pass)
        {
            if(Hit.bStartPenetrating)
            {
                const FVector N=IsCreatureBody(Hit) ? Hit.Normal.GetSafeNormal2D() : Hit.Normal.GetSafeNormal();
                Position=Recover(World,Position,N*(Hit.PenetrationDepth+.2f),Hit,Capsule,Params);
            }
            else
            {
                Position=Hit.Location;
                Remaining*=1.f-Hit.Time;
            }
            if(Hit.Normal.Z>=.6641741f && !IsUpperBodyContact(Hit) && !IsCreatureBody(Hit))
            {
                // Grounded ramp following is vertical, not an uphill speed boost.
                Remaining.Z=FMath::Max(Remaining.Z,
                    -(Hit.Normal.X*Remaining.X+Hit.Normal.Y*Remaining.Y)/Hit.Normal.Z);
            }
            else
            {
                const FVector N=Hit.Normal.Z<-.15f ? Hit.Normal.GetSafeNormal() : Hit.Normal.GetSafeNormal2D();
                if(N.IsNearlyZero())break;
                Planes.Add(N);
                // Keep earlier constraints too: projecting onto the second wall
                // alone can send the capsule back into the first in a concave bend.
                for(int32 Clip=0;Clip<6;++Clip)for(const FVector& Plane:Planes)
                    Remaining-=Plane*FMath::Min(0.,FVector::DotProduct(Remaining,Plane));
            }
            if(Remaining.IsNearlyZero(.01f))break;
            if(!Sweep(World,Hit,Position,Position+Remaining,Capsule,Params))
                return Position+Remaining;
        }
        return Position;
    }

    inline FVector SlideFromPenetration(UWorld& World, const FVector& From, const FVector& To,
        const FHitResult& Contact, const FCollisionShape& Capsule, const FCollisionQueryParams& Params)
    {
        return SlideGrounded(World,From,To,Contact,Capsule,Params);
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
        FCollisionQueryParams AirParams=Params;
        TSet<const UPrimitiveComponent*> IgnoredBodies;
        int32 SolidContacts=0;
        while (SolidContacts<16 && !Remaining.IsNearlyZero(.01f))
        {
            const FVector Start=Result.Position;
            FHitResult Hit;
            // A tangent wall/ceiling skin must not mask the floor while falling.
            // Retain every floor contact here, including diagonal descents.
            bool bHit=Sweep(World,Hit,Start,Start+Remaining,Capsule,AirParams,false);
            if (bHit && bFalling && IsCreatureBody(Hit))
            {
                // A moving creature can overlap a falling body at a wall. Its
                // rounded top is not a floor and must not lift/trap the player.
                // Stop horizontal travel, then sweep only the remaining fall
                // past that creature. All architecture still blocks this path.
                if(!Hit.bStartPenetrating){Result.Position=Hit.Location;Remaining*=1.f-Hit.Time;}
                Remaining.X=Remaining.Y=0;
                Result.ContactNormal=Hit.Normal.GetSafeNormal2D();
                const auto* Body=Hit.GetComponent();
                if(!Body || IgnoredBodies.Contains(Body)) break;
                IgnoredBodies.Add(Body);AirParams.AddIgnoredComponent(Body);
                continue;
            }
            ++SolidContacts;
            // A just-launched body may still touch its support. Only clear that
            // initial floor while leaving it, never while falling back into it.
            if (bHit && !IsUpperBodyContact(Hit) && Hit.Time<=KINDA_SMALL_NUMBER && Hit.ImpactNormal.Z>=.6641741f
                && Hit.PenetrationDepth<.5f && FVector::DotProduct(Remaining,Hit.ImpactNormal)>0.f)
                bHit=Sweep(World,Hit,Start,Start+Remaining,Capsule,AirParams);
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
                        Normal * (Hit.PenetrationDepth + .5f), Hit, Capsule, AirParams);
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
            if (bFalling && !IsUpperBodyContact(Hit) && Normal.Z>=LandingZ && Remaining.Z<0.f)
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
            if (World.LineTraceSingleByChannel(Floor,Feet+FVector(0,0,.1f),Feet-FVector(0,0,.2f),ECC_Pawn,AirParams)
                && !IsCreatureBody(Floor) && !Floor.bStartPenetrating && Floor.ImpactNormal.Z>=.6641741f)
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
            if (Hit.bStartPenetrating || IsUpperBodyContact(Hit) || Hit.Normal.Z<WalkableNormalZ
                || Hit.Location.Z>Landed.Z+Capsule.GetCapsuleRadius()+1.f) return false;
            // At a tread's edge the sphere touches before its center reaches
            // the ray's floor height. Keep that contact, never embed the foot.
            if (OutContact) *OutContact=Hit.Location;
        }
        return true;
    }
}
