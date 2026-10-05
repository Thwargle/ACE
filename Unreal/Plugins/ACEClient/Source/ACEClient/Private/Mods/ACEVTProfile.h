#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// Data-only readers and bounded compilation to UCM instructions. Import itself
// never executes commands, expressions, embedded UI or third-party plugin code.
namespace ACEVTProfile
{
    inline constexpr int32 MaxLootConditions=64;
    TSharedPtr<FJsonObject> ConvertFile(const FString& Path,TArray<FString>& Issues);
    TSharedPtr<FJsonObject> CompileCommand(const FString& Text,TArray<FString>& Issues);
    FString NativeCommand(const FString& Text);
    // ICU-compatible patterns with explicit text, work and stack limits.
    bool IsSupportedPattern(const FString& Pattern);
    FString WriteLoot(const TSharedPtr<FJsonObject>& Document,FString& Error);
    TSharedPtr<FJsonObject> Read(const FString& Text,const FString& Extension,FString& Error);
    TSharedPtr<FJsonObject> Convert(const TSharedPtr<FJsonObject>& Document,TArray<FString>& Issues);
}
