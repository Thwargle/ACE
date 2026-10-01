#pragma once
#include "CoreMinimal.h"

namespace ACEVRLocomotion
{
    constexpr uint32 Capability = 262144u;
    constexpr uint32 Subscription = 64u;
    constexpr uint32 MoveMarker = 0x314D5256u;

    inline FVector2D Input(float Forward, float Right)
    {
        if (!FMath::IsFinite(Forward) || !FMath::IsFinite(Right)) return FVector2D::ZeroVector;
        const FVector2D Stick(FMath::Clamp(Right, -1.f, 1.f), FMath::Clamp(Forward, -1.f, 1.f));
        return Stick.SizeSquared() > 1.f ? Stick.GetSafeNormal() : Stick;
    }

    inline FVector Velocity(float Forward, float Right, bool bRunning, float RunRate)
    {
        const FVector2D Stick = Input(Forward, Right);
        const float Speed = bRunning ? 4.f * FMath::Max(0.f, RunRate) : 3.12f;
        return FVector(Stick.X * Speed, Stick.Y * Speed, 0.f);
    }

    inline float AnimationRate(float Forward, float Right, bool bRunning, float RunRate)
    {
        const FVector V = Velocity(Forward, Right, bRunning, RunRate);
        if (FMath::Abs(Right) > FMath::Abs(Forward)) return FMath::Abs(V.X) / 1.25f;
        return FMath::Abs(V.Y) / (Forward > 0.f && bRunning ? 4.f : 3.12f);
    }

    inline FVector JumpVelocity(float Forward, float Right, bool bRunning, float RunRate)
    {
        // MotionInterp.get_state_velocity applies the server's run-speed ceiling
        // to leave-ground velocity, including walking while heavily burdened.
        return Velocity(Forward, Right, bRunning, RunRate).GetClampedToMaxSize2D(4.f * FMath::Max(0.f, RunRate));
    }
}
