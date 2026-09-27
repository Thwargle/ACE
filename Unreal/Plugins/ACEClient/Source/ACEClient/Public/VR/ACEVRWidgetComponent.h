#pragma once
#include "Components/WidgetComponent.h"
#include "ACEVRWidgetComponent.generated.h"

/** Filter world-space UI at its projected size instead of aliasing the top mip. */
UCLASS()
class ACECLIENT_API UACEVRWidgetComponent : public UWidgetComponent
{
	GENERATED_BODY()
public:
	static UMaterialInterface* GetFilteredMaterial();
	virtual void OnRegister() override;
	virtual void UpdateRenderTarget(FIntPoint DesiredRenderTargetSize) override;
	virtual void DrawWidgetToRenderTarget(float DeltaTime) override;
};
