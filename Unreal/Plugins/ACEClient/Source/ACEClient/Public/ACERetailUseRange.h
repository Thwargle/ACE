#pragma once

#include "CoreMinimal.h"

namespace ACERetailUseRange
{
	// Position::cylinder_distance is deliberately based on the full origin
	// distance (even cylinder_distance_no_z uses XYZ), followed by vertical
	// cylinder separation. Flattening it to XY opens corpses too soon on slopes.
	inline float CylinderDistance(const FVector& SelfOrigin, float SelfRadius, float SelfHeight,
		const FVector& TargetOrigin, float TargetRadius, float TargetHeight)
	{
		const float Reach = FVector::Distance(SelfOrigin, TargetOrigin) - SelfRadius - TargetRadius;
		const float VerticalGap = SelfOrigin.Z <= TargetOrigin.Z
			? TargetOrigin.Z - (SelfOrigin.Z + SelfHeight)
			: SelfOrigin.Z - (TargetOrigin.Z + TargetHeight);
		if (VerticalGap > 0.f && Reach > 0.f) return FMath::Sqrt(VerticalGap * VerticalGap + Reach * Reach);
		if (VerticalGap < 0.f && Reach < 0.f) return -FMath::Sqrt(VerticalGap * VerticalGap + Reach * Reach);
		return Reach;
	}
}
