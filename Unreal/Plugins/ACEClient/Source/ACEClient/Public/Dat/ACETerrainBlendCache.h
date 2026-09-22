#pragma once
#include "CoreMinimal.h"

struct ACECLIENT_API FACETerrainBlend
{
    TArray<FColor> Pixels;
    int32 Width = 0;
    int32 Height = 0;

    // Prepared on the terrain worker, before publishing this immutable blend.
    // Lossless CPU backup only: the GPU still receives full-resolution BGRA8.
    bool PrepareUploadMips();
    bool IsValid() const;
    bool CopyMip(int32 Index, void* Destination, int64 Bytes) const;
    TArray<FColor> CopyBasePixels() const;
    uint64 GetAllocatedSize() const;
    struct FMip
    {
        TArray<uint8> Data;
        int32 RawBytes = 0;
        bool bPacked = false;
    };
    TArray<FMip> UploadMips;
};

/** Shared immutable pixels for the current DAT session, including background builders. */
class ACECLIENT_API FACETerrainBlendCache
{
public:
    using FBlendPtr = TSharedPtr<const FACETerrainBlend, ESPMode::ThreadSafe>;
    explicit FACETerrainBlendCache(uint64 InBudgetBytes = 128ull << 20) : BudgetBytes(InBudgetBytes) {}
    FBlendPtr GetOrBuild(uint32 PCode, uint64 Fingerprint, TFunctionRef<FBlendPtr()> Build);
    uint64 GetRetainedBytes() const;
private:
    struct FKey
    {
        uint32 Code;
        uint64 Fingerprint;
        bool operator==(const FKey& Other) const { return Code == Other.Code && Fingerprint == Other.Fingerprint; }
        friend uint32 GetTypeHash(const FKey& Key) { return HashCombine(GetTypeHash(Key.Code), GetTypeHash(Key.Fingerprint)); }
    };
    struct FEntry
    {
        TWeakPtr<const FACETerrainBlend, ESPMode::ThreadSafe> Live;
        FBlendPtr Retained;
        uint64 LastUse = 0;
    };
    FBlendPtr Find(const FKey& Key);
    mutable FCriticalSection Mutex;
    FCriticalSection BuildMutexes[16];
    TMap<FKey, FEntry> Entries;
    const uint64 BudgetBytes;
    uint64 RetainedBytes = 0;
    uint64 Clock = 0;
};
