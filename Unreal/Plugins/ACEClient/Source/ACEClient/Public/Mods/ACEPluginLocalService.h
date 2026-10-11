#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class SWidget;

/** A compiled client capability, never loaded from a script/plugin directory.
 * Local Lua services receive only this adapter's snapshot and dispatch here,
 * not through the gameplay automation or console command paths. */
struct FACEPluginLocalService
{
    int32 Api = 1;
    TArray<FString> Permissions = {TEXT("local_cosmetics")};
    TFunction<TSharedPtr<FJsonObject>()> Snapshot;
    TFunction<bool(const TSharedPtr<FJsonObject>&, FString&)> Execute;
    TFunction<void()> Stop;
    TFunction<TSharedRef<SWidget>()> MakePanel;
};
