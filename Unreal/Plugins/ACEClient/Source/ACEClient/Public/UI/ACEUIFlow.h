#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/ACEUITypes.h"
#include "ACEUIFlow.generated.h"

class UACEUIElementManager;
class UACEUILayoutResolver;

/**
 * Retail UIFlow analogue — owns the active UI mode.
 */
UCLASS()
class ACECLIENT_API UACEUIFlow : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UACEUIElementManager* InManager, UACEUILayoutResolver* InLayoutResolver);
	void Shutdown();

	ACEUI::EACEUIFlowMode GetMode() const { return Mode; }
	void SetMode(ACEUI::EACEUIFlowMode NewMode);

	uint32 GetLayoutIdForMode(ACEUI::EACEUIFlowMode InMode) const;

private:
	UPROPERTY()
	TObjectPtr<UACEUIElementManager> Manager;

	UPROPERTY()
	TObjectPtr<UACEUILayoutResolver> LayoutResolver;

	ACEUI::EACEUIFlowMode Mode = ACEUI::EACEUIFlowMode::None;
};
