#pragma once
#include "CoreMinimal.h"
#include "Components/InputKeySelector.h"
#include "ACERetailKeySelector.generated.h"
UCLASS()
class ACECLIENT_API UACERetailKeySelector : public UInputKeySelector
{
 GENERATED_BODY()
public:
 FKey ActionKey; int32 BindingSlot=0;
 void InitializeBinding(FKey Key,int32 Slot);
 void RefreshBinding();
 bool bSyncing=false;
 UFUNCTION() void AcceptBinding(FInputChord Chord);
};
