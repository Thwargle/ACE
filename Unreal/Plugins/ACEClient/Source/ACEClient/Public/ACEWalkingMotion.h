#pragma once
#include "CoreMinimal.h"
#include "ACELandingMotion.h"
#include "Dat/ACEDatFileTypes.h"

namespace ACEWalkingMotion
{
struct FLink
{
    TArray<FACEDatAnimData> Clips;
    ACELandingMotion::FRootTrack Root;
    float PreviousTime = 0.f;
};

// Retail removes the cyclic tail on key-up, preserving unfinished links.
// Entry + exit therefore complete a step even when the walk key was tapped.
struct FSequence
{
    TArray<FLink> Pending;
    FLink Display;
    int8 Direction = 0;
    bool HasPending() const { return !Pending.IsEmpty(); }
    void Reset() { Pending.Reset(); Display = {}; Direction = 0; }
    template<typename Builder> void SetDirection(int8 Next, Builder Build)
    {
        if (Next == Direction) return;
        auto Append = [&](int8 From, int8 To)
        {
            FLink Link;
            if (Pending.Num() < 32 && Build(From, To, Link)) Pending.Add(MoveTemp(Link));
        };
        // A sign change leaves through Ready, just as CMotionTable does.
        if (Direction) Append(Direction, 0);
        if (Next) Append(0, Next);
        Direction = Next;
    }
    FTransform Advance(float Dt, float& CycleSeconds)
    {
        Display = {};
        FTransform Delta = FTransform::Identity;
        CycleSeconds = FMath::Max(0.f, Dt);
        while (!Pending.IsEmpty() && CycleSeconds > 0.f)
        {
            FLink& Link = Pending[0];
            const float Use = FMath::Min(CycleSeconds, Link.Root.Keys.Last().Time - Link.Root.Time);
            Link.PreviousTime = Link.Root.Time;
            const FTransform Step = Link.Root.Advance(Use);
            Delta.SetTranslation(Delta.GetTranslation() + Delta.GetRotation().RotateVector(Step.GetTranslation()));
            Delta.SetRotation((Delta.GetRotation() * Step.GetRotation()).GetNormalized());
            Display.Clips = Link.Clips;
            Display.Root.Time = Link.Root.Time;
            Display.PreviousTime = Link.PreviousTime;
            CycleSeconds -= Use;
            if (Link.Root.IsActive()) break;
            Pending.RemoveAt(0, 1, EAllowShrinking::No);
        }
        // A completed link followed by a cycle must display the cycle.
        if (CycleSeconds > KINDA_SMALL_NUMBER) Display = {};
        return Delta;
    }
};
}
