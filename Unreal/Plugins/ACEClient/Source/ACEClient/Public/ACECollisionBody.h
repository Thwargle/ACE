#pragma once
#include "CoreMinimal.h"
#include "CollisionShape.h"

// SPHEREPATH uses at most the first two Setup spheres, preserving their
// individual centers/radii. The capsule is a coordinate envelope only.
struct FACECollisionBody
{
    FVector Centers[2];
    float Radii[2];
    int32 Count=0;
    float Radius=0,HalfHeight=0;

    FACECollisionBody(const FCollisionShape& Capsule)
        : Radius(Capsule.GetCapsuleRadius()),HalfHeight(Capsule.GetCapsuleHalfHeight())
    {
        const float Offset=FMath::Max(0.f,HalfHeight-Radius);
        Count=Offset>.001f?2:1;
        Centers[0]=FVector(0,0,-Offset);Centers[1]=FVector(0,0,Offset);
        Radii[0]=Radii[1]=Radius;
    }
    float GetCapsuleRadius() const { return Radius; }
    float GetCapsuleHalfHeight() const { return HalfHeight; }
    // Broad-phase queries must enclose offset/unequal authored spheres too.
    FCollisionShape BroadPhaseShape() const
    {
        float BoundRadius=0,Offset=0;
        for(int32 I=0;I<Count;++I)
        {
            BoundRadius=FMath::Max(BoundRadius,float(Centers[I].Size2D())+Radii[I]);
            Offset=FMath::Max(Offset,float(FMath::Abs(Centers[I].Z)));
        }
        return FCollisionShape::MakeCapsule(BoundRadius,BoundRadius+Offset);
    }
    FACECollisionBody WithClearance(float Floor,float Side) const
    {
        FACECollisionBody Result=*this;
        for(int32 I=0;I<Count;++I)Result.Radii[I]=FMath::Max(.001f,Radii[I]-Side);
        Result.Centers[0].Z+=Floor;
        Result.Radius=FMath::Max(.001f,Radius-Side);
        return Result;
    }
    FACECollisionBody Inflated(float Skin) const
    {
        FACECollisionBody Result=*this;
        for(int32 I=0;I<Count;++I)Result.Radii[I]+=Skin;
        Result.Radius+=Skin;Result.HalfHeight+=Skin;
        return Result;
    }
};
