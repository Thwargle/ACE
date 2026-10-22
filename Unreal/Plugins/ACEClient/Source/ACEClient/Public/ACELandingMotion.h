#pragma once
#include "CoreMinimal.h"

namespace ACELandingMotion
{
// Authored position frames are incremental ACE-space transforms. Keep a small
// cumulative track so collision substeps consume each displacement exactly once,
// independently of the presentation component's tick/culling frequency.
struct FRootTrack
{
    struct FKey { float Time; FTransform Transform; };
    TArray<FKey> Keys;
    float Time = 0.f;
    FTransform Entry = FTransform::Identity;
    bool bApplyEntry = false;

    void Reset() { Keys.Reset(); Time = 0.f; Entry = FTransform::Identity; bApplyEntry = false; }
    bool IsActive() const { return Keys.Num() > 1 && Time < Keys.Last().Time; }
    FTransform Sample(float At) const
    {
        if (Keys.IsEmpty()) return FTransform::Identity;
        for (int32 I=1; I<Keys.Num(); ++I)
            if (At < Keys[I].Time)
            {
                FTransform Result;
                Result.Blend(Keys[I-1].Transform, Keys[I].Transform,
                    FMath::Clamp((At-Keys[I-1].Time)/(Keys[I].Time-Keys[I-1].Time),0.f,1.f));
                return Result;
            }
        return Keys.Last().Transform;
    }
    FTransform Advance(float Dt)
    {
        if (!IsActive()) return FTransform::Identity;
        const FTransform Before = Sample(Time);
        Time = FMath::Min(Time+FMath::Max(0.f,Dt),Keys.Last().Time);
        FTransform Delta = Sample(Time).GetRelativeTransform(Before);
        if (bApplyEntry && Dt > 0.f)
        {
            Delta.SetTranslation(Entry.GetTranslation()+Entry.GetRotation().RotateVector(Delta.GetTranslation()));
            Delta.SetRotation((Entry.GetRotation()*Delta.GetRotation()).GetNormalized());
            bApplyEntry = false;
        }
        return Delta;
    }
    FTransform AdvanceLooping(float Dt)
    {
        FTransform Result = FTransform::Identity;
        if (Keys.Num() < 2 || Keys.Last().Time <= SMALL_NUMBER) return Result;
        const float Duration = Keys.Last().Time;
        const bool Reverse = Dt < 0.f;
        float Remaining = FMath::Abs(Dt);
        if (bApplyEntry && Remaining > 0.f) { Result = Entry; bApplyEntry = false; }
        while (Remaining > SMALL_NUMBER)
        {
            if (Reverse ? Time <= SMALL_NUMBER : Time >= Duration-SMALL_NUMBER)
                Time = Reverse ? Duration : 0.f;
            const float Use = FMath::Min(Remaining, Reverse ? Time : Duration-Time);
            const FTransform Before = Sample(Time);
            Time += Reverse ? -Use : Use;
            const FTransform Delta = Sample(Time).GetRelativeTransform(Before);
            Result.SetTranslation(Result.GetTranslation()+Result.GetRotation().RotateVector(Delta.GetTranslation()));
            Result.SetRotation((Result.GetRotation()*Delta.GetRotation()).GetNormalized());
            Remaining -= Use;
        }
        return Result;
    }
};

// CPhysicsObj::calc_friction / UpdatePhysicsInternal. Physical velocity is
// separate from animation displacement and survives landing until friction
// brings it below SmallVelocity (0.25 AC units/sec).
inline FVector Step(FVector Velocity, float Friction, float DeltaSeconds)
{
    if (Velocity.SizeSquared() < .0625f + .0002f) return FVector::ZeroVector;
    return Velocity * FMath::Pow(1.f-FMath::Clamp(Friction,0.f,1.f),FMath::Max(0.f,DeltaSeconds));
}
}
