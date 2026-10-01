#pragma once
#include "Components/WidgetComponent.h"
#include "ACEVRWidgetComponent.generated.h"

/** Personal VR UI uses filtered textures and alpha coverage above world effects. */
UCLASS()
class ACECLIENT_API UACEVRWidgetComponent : public UWidgetComponent
{
	GENERATED_BODY()
public:
	static UMaterialInterface* GetFilteredMaterial();
	/** Relative personal-UI order, below the comfort curtain and above world FX. */
	static constexpr int32 PersonalLayerBase = 10000;
	static void SetPersonalLayer(UWidgetComponent* Panel, int32 Order);
	virtual void OnRegister() override;
	virtual void UpdateRenderTarget(FIntPoint DesiredRenderTargetSize) override;
	virtual void DrawWidgetToRenderTarget(float DeltaTime) override;
};
