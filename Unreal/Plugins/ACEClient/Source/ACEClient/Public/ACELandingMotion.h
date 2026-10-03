#pragma once
#include "CoreMinimal.h"

namespace ACELandingMotion
{
// CPhysicsObj::calc_friction / UpdatePhysicsInternal. Physical velocity is
// separate from animation displacement and survives landing until friction
// brings it below SmallVelocity (0.25 AC units/sec).
inline FVector Step(FVector Velocity, float Friction, float DeltaSeconds)
{
    if (Velocity.SizeSquared() < .0625f + .0002f) return FVector::ZeroVector;
    return Velocity * FMath::Pow(1.f-FMath::Clamp(Friction,0.f,1.f),FMath::Max(0.f,DeltaSeconds));
}
}
