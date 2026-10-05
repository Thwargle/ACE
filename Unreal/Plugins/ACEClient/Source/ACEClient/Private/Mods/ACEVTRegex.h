#pragma once
#include "CoreMinimal.h"

// ICU matching with explicit work/stack limits. Never run an unrestricted
// backtracking engine on imported profiles on the game thread.
namespace ACEVTRegex
{
bool Validate(const FString& Pattern, FString& Error);
bool Match(const FString& Pattern, const FString& Text, TMap<FString,FString>& Captures, FString& Error);
}
