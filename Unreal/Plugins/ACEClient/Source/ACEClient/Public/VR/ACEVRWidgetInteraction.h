#pragma once
#include "CoreMinimal.h"
#include "Components/WidgetInteractionComponent.h"
#include "ACEVRWidgetInteraction.generated.h"

/** Personal VR menus use their rendered planes, independent of world collision. */
UCLASS()
class ACECLIENT_API UACEVRWidgetInteraction : public UWidgetInteractionComponent
{
	GENERATED_BODY()
public:
	void RefreshHit() { SimulatePointerMovement(); }
protected:
	virtual FWidgetTraceResult PerformTrace() const override;
};
