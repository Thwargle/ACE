#pragma once

#include "CoreMinimal.h"
class UMaterialInterface;
class UWidgetComponent;

namespace ACEVRPointerVisuals
{
	UMaterialInterface* GetMaterial(bool Overlay);
	bool IsInteractivePanel(const UWidgetComponent* Panel);
	inline FLinearColor ModeColor(int32 Mode)
	{
		return Mode == 1 ? FLinearColor(.08f,.8f,1.f) : Mode == 2 ? FLinearColor(1.f,.45f,.035f) : FLinearColor::White;
	}
	// Above personal UI (10000..19999), below the comfort curtain. Only use the
	// overlay material for a ray terminating at a visible, interactive menu.
	constexpr int32 OverlayLayer = 20000;
}
