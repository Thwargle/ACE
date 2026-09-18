#pragma once
#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

namespace ACERetailPortalAnimation
{
	// Client 1802 UIGlobals::Init / GetAnimLevel. Preserve integer quantization,
	// including the original pi approximation and truncation before accumulation.
	inline int32 GetAnimLevel(float Percentage)
	{
		static const TStaticArray<int32, 100> Levels = []
		{
			TStaticArray<int32, 100> Result;
			int32 Sum = 0;
			for (int32 I = 0; I < 100; ++I)
			{
				Result[I] = static_cast<int32>(FMath::Sin(I * 3.141592 * 0.0101010101010101) * 1024.0);
				Sum += Result[I];
			}
			int32 Cumulative = 0;
			for (int32& Level : Result)
			{
				Cumulative += Level;
				Level = (Cumulative << 10) / Sum;
			}
			return Result;
		}();
		return Levels[static_cast<int32>(static_cast<double>(FMath::Clamp(Percentage, 0.f, 1.f)) * 99.0)];
	}

	// SmartBox stores cot(vertical FOV/2), not an angle. gmSmartBoxUI tweens
	// this projection distance to .001 and back during the two exit stages.
	inline float StretchFov(float HorizontalFov, float Aspect, float Time, bool bIntoWorld)
	{
		Aspect = FMath::Max(Aspect, .2f);
		const float Distance = Aspect / FMath::Tan(FMath::DegreesToRadians(HorizontalFov) * .5f);
		const float T = GetAnimLevel(Time) / 1024.f;
		const float D = bIntoWorld ? FMath::Lerp(.001f, Distance, T) : FMath::Lerp(Distance, .001f, T);
		return FMath::Min(179.f, FMath::RadiansToDegrees(2.f * FMath::Atan(Aspect / FMath::Max(D, .001f))));
	}
}
