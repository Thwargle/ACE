#pragma once
#include "Components/ComboBoxString.h"
#include "ACERetailComboBoxString.generated.h"

/** DAT dropdown art must retain its color instead of inheriting the editor's black tint. */
UCLASS()
class ACECLIENT_API UACERetailComboBoxString : public UComboBoxString
{
 GENERATED_BODY()
public:
 UACERetailComboBoxString(const FObjectInitializer& Initializer) : Super(Initializer)
 {
  InitForegroundColor(FLinearColor::White);
 }
};
