#pragma once

#include "CoreMinimal.h"

enum class EACEUCMLogLevel : uint8 { Info, Warning, Error };

struct FACEUCMLogEntry
{
    FDateTime Time;
    EACEUCMLogLevel Level = EACEUCMLogLevel::Info;
    FString Message, Context;
    int32 Repeats = 1;
    FString ToText() const;
};

/** Bounded independently of window lifetime and Lua's memory budget. */
class ACECLIENT_API FACEUCMLog
{
public:
    static constexpr int32 Capacity = 1000;
    static constexpr int32 TextLimit = 1024;
    static constexpr int64 FileLimit = 16 * 1024 * 1024;
    const TArray<TSharedPtr<FACEUCMLogEntry>>& GetEntries() const { return Entries; }
    TSharedPtr<const FACEUCMLogEntry> GetLastProblem() const { return LastProblem; }
    uint64 Revision = 0;
    bool Dirty = false;
    void Add(const FString& Message, const FString& Context, EACEUCMLogLevel Level,
        FDateTime Time = FDateTime::Now());
    void Clear();
    bool Save(const FString& Path);
    bool Load(const FString& Path);
private:
    TArray<TSharedPtr<FACEUCMLogEntry>> Entries;
    TSharedPtr<FACEUCMLogEntry> LastProblem;
};
