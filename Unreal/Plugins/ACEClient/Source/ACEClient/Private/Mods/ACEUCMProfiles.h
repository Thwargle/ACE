#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// Disk representation is a run manifest plus independently reusable sections.
// Lua and existing editors continue to consume one resolved object.
namespace ACEUCMProfiles
{
    const TArray<FString>& Sections();
    FString SectionFor(const FString& Key);
    bool SafeName(const FString& Name);
    TSharedPtr<FJsonObject> Copy(const TSharedPtr<FJsonObject>& Object);
    TSharedPtr<FJsonObject> Extract(const TSharedPtr<FJsonObject>& Profile, const FString& Section);
    TArray<FString> List(const FString& Root, const FString& Section);
    FString UniqueName(const FString& Root, const FString& Section, const FString& Name);
    bool Load(const FString& Root, const FString& Name, TSharedPtr<FJsonObject>& Profile,
        TSharedPtr<FJsonObject>& Links, FString& Error);
    bool Save(const FString& Root, const FString& Name, TSharedPtr<FJsonObject>& Profile,
        TSharedPtr<FJsonObject>& Links, const TSharedPtr<FJsonObject>& Baseline, FString& Error,
        TFunction<bool(const TSharedPtr<FJsonObject>&)> Validate = {});
    bool Select(const FString& Root, const FString& Section, const FString& Name,
        TSharedPtr<FJsonObject>& Profile, TSharedPtr<FJsonObject>& Links, FString& Error);
    bool SaveSectionAs(const FString& Root, const FString& Section, const FString& Name,
        const TSharedPtr<FJsonObject>& Profile, FString& Error);
    bool EnsureDefaults(const FString& Root, const TSharedPtr<FJsonObject>& Defaults, FString& Error);
    // Retire a shared file and relink affected setups to Default, preserving a backup.
    bool DeleteSection(const FString& Root, const FString& Section, const FString& Name, FString& Error);
}
