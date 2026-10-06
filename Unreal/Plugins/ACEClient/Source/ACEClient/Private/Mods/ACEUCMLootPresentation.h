#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// Presentation only: IDs and raw values remain unchanged in saved profiles.
namespace ACEUCMLootPresentation
{
    struct FProperty { int32 Id; const TCHAR* Name; };
    struct FCalculated { FString Name; TArray<FString> Arguments; };
    const TArray<FProperty>& Properties(const FString& Field);
    FString PropertyLabel(const FString& Field,int32 Id);
    const TMap<int32,FCalculated>& Calculated();
    FString ConditionSummary(const TSharedPtr<FJsonObject>& Condition);
    FString DataRequirement(const TSharedPtr<FJsonObject>& Condition);
    TSharedPtr<FJsonObject> Clone(const TSharedPtr<FJsonObject>& Object);
}
