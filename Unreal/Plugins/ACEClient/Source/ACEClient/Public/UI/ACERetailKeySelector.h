#pragma once
#include "CoreMinimal.h"
#include "Components/InputKeySelector.h"
#include "ACERetailKeySelector.generated.h"
class UTextBlock;
class UACEUICanvasWidget;
struct FACEUIElement;
UCLASS()
class ACECLIENT_API UACERetailKeySelector : public UInputKeySelector
{
 GENERATED_BODY()
public:
 FKey ActionKey; int32 BindingSlot=0;
 void InitializeBinding(FKey Key,int32 Slot);
 void RefreshBinding();
 void SetRetailButton(UACEUICanvasWidget* Canvas, const TSharedPtr<FACEUIElement>& Element, UTextBlock* Label);
 UPROPERTY(Transient) TObjectPtr<UTextBlock> RetailLabel;
 TSharedPtr<FACEUIElement> RetailElement;
 bool bSyncing=false;
 UFUNCTION() void AcceptBinding(FInputChord Chord);
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
};
