#pragma once

#include "CoreMinimal.h"

// OpenXR runtimes may expose an analog trigger, a converted click, or both.
// Merge them before action dispatch so one physical pull can never cast twice.
struct FACEVRTriggerState
{
	bool bDigital = false, bAnalog = false;
	bool IsPressed() const { return bDigital || bAnalog; }
	bool SetDigital(bool Pressed)
	{
		const bool Before = IsPressed(); bDigital = Pressed; return Before != IsPressed();
	}
	bool SetAnalog(float Value)
	{
		const bool Before = IsPressed();
		bAnalog = FMath::IsFinite(Value) && Value >= (bAnalog ? .35f : .55f);
		return Before != IsPressed();
	}
};
