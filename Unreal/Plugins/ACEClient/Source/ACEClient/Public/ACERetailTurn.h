#pragma once
#include "CoreMinimal.h"

namespace ACERetailTurn
{
// MoveToManager::BeginTurnToHeading uses the DAT TurnRight modifier (1.5 rad/s),
// multiplied by MovementParameters.speed. It stops at the requested heading;
// exponential interpolation makes large turns faster and varies with frame rate.
inline FQuat Advance(const FQuat& Current, const FQuat& Desired, float Speed, float Seconds, float RadiansPerSecond = 1.5f)
{
    if (Seconds <= 0.f || Speed <= 0.f) return Current;
    const double Angle = Current.AngularDistance(Desired);
    return Angle <= SMALL_NUMBER ? Desired : FQuat::Slerp(Current, Desired,
        FMath::Min(1., FMath::Abs(double(RadiansPerSecond)) * Speed * Seconds / Angle)).GetNormalized();
}
}
