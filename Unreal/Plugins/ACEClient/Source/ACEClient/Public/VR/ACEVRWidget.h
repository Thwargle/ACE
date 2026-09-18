#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ACEVRWidget.generated.h"
class UACEVRComponent;

/** Controller-sized world-space controls. Spell buttons select; only the trigger casts. */
UCLASS()
class ACECLIENT_API UACEVRWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient) TObjectPtr<UACEVRComponent> VR;
	bool bWrist = false;
	bool bKeyboard = false;
	bool bShift = false;
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
