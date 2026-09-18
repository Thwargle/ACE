#pragma once
#include "CoreMinimal.h"

namespace ACERetailPaperDoll
{
	// gmPaperDollUI::PostInit / UpdateHeritage, AC meters reflected on X.
	inline FVector CameraPositionCm(int32 Heritage)
	{
		float Distance = 240.f, Height = 88.f;
		if (Heritage == 6 || Heritage == 7) Distance = 300.f;
		if (Heritage == 8 || Heritage == 9 || Heritage == 12 || Heritage == 13) Distance = 340.f;
		if (Heritage == 8) Height = 100.f;
		return FVector(-12.f, -Distance, Height);
	}
	// Frame::set_heading uses (sin(heading),cos(heading)); reflecting X maps
	// that directly to Unreal yaw for art whose forward axis is +Y.
	constexpr float HeadingDegrees = 191.3679f;
	inline float HorizontalFov(float Aspect)
	{
		// CreatureMode's 45-degree FOV is vertical (Render::SetFOVInternal).
		return FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(PI / 8.f) * Aspect));
	}
}
