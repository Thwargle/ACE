#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
struct lua_State;

/** One VM per running plugin; text scripts, 8 MiB heap, bounded work per callback. */
class FACEPluginVM
{
public:
    FACEPluginVM();
    ~FACEPluginVM();
    bool Load(const FString& Source, FString& Error);
    bool Step(const TSharedPtr<FJsonObject>& Snapshot, const TSharedPtr<FJsonObject>& Profile,
        TSharedPtr<FJsonObject>& Intent, FString& Error);
    SIZE_T GetMemoryUsage() const { return Bytes; }
    SIZE_T GetPeakMemoryUsage() const { return PeakBytes; }
private:
    lua_State* State = nullptr;
    SIZE_T Bytes = 0;
    SIZE_T PeakBytes = 0;
    int Function = 0;
};
